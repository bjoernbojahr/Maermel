// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// Envelope.h – Märmel Engine
// ADSTR-Hüllkurve:
//   Attack  linear von 0 auf 1 in der Zeit A
//   Decay   exponentiell zum Sustain-Pegel, Abstand fällt nach D auf 10 % (−20 dB)
//   Sustain T = 0: Pegel bleibt; T < 0: sinkt zur Stille; T > 0: steigt, höchstens bis zum
//           Maximum (100 %). T verschiebt ab dem Ende des Attack das Sustain-Ziel selbst;
//           Decay folgt diesem wandernden Ziel. Gleichmäßig in dB:
//           Zeit für 20 dB = 2,3 s × ((64 − n) / n)^1,5, n = |T| in Schritten (−64 … +64)
//   Release exponentiell ab dem aktuellen Pegel, nach R auf 10 % (−20 dB)
//   Ende    bei −65 dB unter dem Pegel zu Beginn des Release
//
// Pro Stimme (beim Anschlag festgelegt):
//   Velocity: Attack- und Decay-Zeit liegen zwischen der Zeit bei Velocity 1 (Hauptregler) und
//             der Zeit bei Velocity 127 (VelA/VelD, nie länger). Interpoliert wird im Bereich
//             der Reglerstellung (Zeit^(1/4)), damit mittlere Anschläge nicht zu früh "kippen".
//   Key:      alle Zeiten × 2^(−Key × (Note − 60) / 12), also kürzer nach oben, länger nach unten.
//
// Zeitänderungen wirken sofort und ohne Sprung, weil nur die Geschwindigkeit wechselt.

#pragma once

#include "VelocityResponse.h"

#include <algorithm>
#include <cmath>

namespace maermel
{

class Envelope
{
public:
    struct Params
    {
        float attackSeconds = 0.0f;           // bei Velocity 127 (VelA, nie länger als bei Velocity 1)
        float attackSecondsVel1 = 0.0f;       // bei Velocity 1 (Hauptregler)
        float decaySeconds = 0.0f;
        float decaySecondsVel1 = 0.0f;
        float sustainLevel = 1.0f;            // 0 ... 1
        float sustainTimeSeconds = 0.0f;      // T: 0 = aus, < 0 sinkt, > 0 steigt (Betrag = Zeit)
        float releaseSeconds = 0.25f;
        float keyTrack = 0.0f;                // 0 ... 1

        // Einfache Zeiten ohne Velocity-Abhängigkeit setzen (z. B. für Tests)
        void setAttack (float s) noexcept { attackSeconds = attackSecondsVel1 = s; }
        void setDecay (float s) noexcept  { decaySeconds = decaySecondsVel1 = s; }
    };

    // Kennlinie der Zeitregler: Reglerstellung 0 ... 1 → 0 ... 10 s
    // Kennlinie der Zeitregler (Attack, Decay, Release, LFO-Delay; Glide mit 5 s): Zeit = Höchstwert × x^4,2.
    // Bis 20.10.2026 x^4 wie beim EXS24 (Mitte 625 ms); auf Wunsch von Björn etwas steiler, Mitte jetzt 544 ms
    // (10 % 0,63 ms, 30 % 64 ms, 70 % 2,24 s, 90 % 6,42 s). Bereich unverändert 0 … 10 s.
    static constexpr float kTimeCurveExponent = 4.2f;
    static float timeFromKnob (float x) noexcept
    {
        return x <= 0.0f ? 0.0f : 10.0f * std::pow (std::min (1.0f, x), kTimeCurveExponent);
    }

    // T-Regler −1 ... +1 (Schritte −64 ... +64) → Zeit für 20 dB, mit Vorzeichen.
    // Zeit für 20 dB: 2,3 s × ((64 − n) / n)^1,5 mit n = |T| × 64.
    // (Ein Versuch mit auf 20 s … 4 ms begrenztem Bereich lief beim Spielen zu schnell – verworfen.)
    static float sustainTimeFromKnob (float t) noexcept
    {
        const float n = std::fabs (t) * 64.0f;
        if (n < 0.5f)
            return 0.0f;                                   // Mitte: Pegel bleibt
        const float seconds = n >= 64.0f ? 0.005f
                            : std::max (0.005f, 2.3f * std::pow ((64.0f - n) / n, 1.5f));
        return t < 0.0f ? -seconds : seconds;
    }

    void setSampleRate (double sr) noexcept
    {
        sampleRate = sr;
        sustainCoeff = coefficient (0.02f);   // Sustain-Änderungen gleiten in ~20 ms
        updateCoefficients();
    }

    void setParams (const Params& p) noexcept
    {
        params = p;
        updateCoefficients();
    }

    // Beim Anschlag: Velocity (1 ... 127) und Note (inkl. C.Tune) für Velocity- und Key-Zeiten
    static constexpr float kMinReleaseSeconds = 0.004f;

    void setVoiceContext (int velocity, int note) noexcept
    {
        velocityPosition = velocityResponse (velocity);   // Standard-Velocity-Kurve
        keyNote = note;
        updateCoefficients();
    }

    void noteOn() noexcept
    {
        level = 0.0f;
        sustainGain = 1.0f;
        stage = Stage::Attack;
        if (effectiveAttack <= 0.0f)
        {
            level = 1.0f;
            stage = Stage::Decay;
        }
    }

    void noteOff() noexcept
    {
        if (stage == Stage::Idle || stage == Stage::Release)
            return;
        startRelease (level);
    }

    // Für Release-Samples: direkt in die R-Phase ab einem Pegel
    void startRelease (float fromLevel) noexcept
    {
        level = fromLevel;
        releaseStart = fromLevel;
        releasedInAttack = (stage == Stage::Attack);
        releaseFloor = std::max (fromLevel * 0.000562341f, 1.0e-7f);   // −65 dB
        stage = Stage::Release;
    }

    float next() noexcept
    {
        switch (stage)
        {
            case Stage::Attack:
                level += attackIncrement;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::Decay;
                    sustainGain = 1.0f;                    // T beginnt ab hier zu wirken
                }
                break;

            case Stage::Decay:
            case Stage::Sustain:
            {
                // T verschiebt das Sustain-Ziel gleichmäßig in dB, nach oben höchstens bis 100 %
                if (params.sustainTimeSeconds < 0.0f)
                {
                    sustainGain *= sustainTimeCoeff;
                }
                else if (params.sustainTimeSeconds > 0.0f)
                {
                    const float base = std::max (params.sustainLevel, 0.001f);   // auch aus Sustain 0 heraus
                    sustainGain = std::min (sustainGain * sustainRiseFactor, 1.0f / base);
                }
                const float target = (params.sustainTimeSeconds > 0.0f)
                                       ? std::min (1.0f, std::max (params.sustainLevel, 0.001f) * sustainGain)
                                       : params.sustainLevel * sustainGain;

                // Decay folgt dem (ggf. wandernden) Ziel; im Sustain gleiten Änderungen in ~20 ms
                level = target + (level - target) * (stage == Stage::Decay ? decayCoeff : sustainCoeff);
                if (stage == Stage::Decay && std::fabs (level - target) < 1.0e-5f)
                    stage = Stage::Sustain;

                if (params.sustainTimeSeconds < 0.0f && level < 1.0e-6f && target < 1.0e-6f)
                {
                    level = 0.0f;
                    stage = Stage::Idle;                   // ausgeklungen: Stimme kann enden
                }
                break;
            }

            case Stage::Release:
                level *= releaseCoeff;
                if (level < releaseFloor)
                {
                    level = 0.0f;
                    stage = Stage::Idle;
                }
                break;

            case Stage::Idle:
                break;
        }
        return level;
    }

    // Sustain anheben (LFO auf DC ohne Invert → Volume, z. B. per Aftertouch): amount 0 … 1 hebt den Sustain-Pegel S
    // auf S + amount × (1 − S), also in die Luft, die die Hüllkurve nach oben noch hat – nie über den Höchstwert.
    // Umgerechnet wird der laufende Pegel (stückweise linear, 0 und 1 bleiben fest): Attack erreicht den vollen Pegel
    // früher, Decay fällt nur bis zum angehobenen Sustain, Release startet vom angehobenen Pegel und fällt ganz aus.
    // Bei S = 100 % keine Wirkung.
    float boostedLevel (float amount) const noexcept
    {
        if (amount <= 0.0f || stage == Stage::Idle)
            return level;
        const float s = std::min (1.0f, std::max (0.0f, params.sustainLevel));
        if (s >= 1.0f)
            return level;
        const float lifted = s + std::min (1.0f, amount) * (1.0f - s);
        if (stage == Stage::Release)
            return releaseStart > 0.0f
                     ? level * liftLevel (releaseStart, s, lifted, releasedInAttack) / releaseStart : level;
        return liftLevel (level, s, lifted, stage == Stage::Attack);
    }

    bool isActive() const noexcept   { return stage != Stage::Idle; }
    bool isReleasing() const noexcept { return stage == Stage::Release; }
    float currentLevel() const noexcept { return level; }

    // Für Tests: wirksame Zeiten dieser Stimme
    float getEffectiveAttack() const noexcept { return effectiveAttack; }
    float getEffectiveDecay() const noexcept  { return effectiveDecay; }

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    // Pegel e mit Sustain s → angehobenem Sustain lifted umrechnen.
    // Attack: anteilig angehoben (e + (lifted − s) × e, höchstens 1) – ohne Sprung am Notenanfang.
    // Decay/Sustain: s → lifted, 1 → 1, dazwischen linear; unter s (T lässt den Pegel sinken) im Verhältnis lifted/s.
    static float liftLevel (float e, float s, float lifted, bool attack) noexcept
    {
        if (attack)
            return std::min (1.0f, e * (1.0f + lifted - s));
        if (e >= s)
            return std::min (1.0f, lifted + (e - s) * (1.0f - lifted) / (1.0f - s));
        return e * lifted / s;                              // nur bei s > 0 erreichbar
    }

    // Faktor pro Sample, mit dem ein Abstand nach 'seconds' auf 10 % gefallen ist
    float coefficient (float seconds) const noexcept
    {
        if (seconds <= 0.0f || sampleRate <= 0.0)
            return 0.0f;
        return (float) std::pow (10.0, -1.0 / (seconds * sampleRate));
    }

    // Zwischen Zeit bei Velocity 1 und Velocity 127, interpoliert in der Reglerstellung (Zeit^(1/4)).
    // Im logarithmischen Zeitbereich würde ein VelA von 0 schon mittlere Anschläge fast auf 0 ziehen.
    float velocityTime (float atVel127, float atVel1) const noexcept
    {
        if (atVel127 == atVel1)
            return atVel127;
        const float a = std::sqrt (std::sqrt (std::max (0.0f, atVel1)));
        const float b = std::sqrt (std::sqrt (std::max (0.0f, atVel127)));
        const float k = a + (b - a) * velocityPosition;
        return k * k * k * k;
    }

    void updateCoefficients() noexcept
    {
        const float keyFactor = (params.keyTrack > 0.0f)
                                  ? std::exp2 (-params.keyTrack * (float) (keyNote - 60) / 12.0f) : 1.0f;

        effectiveAttack = velocityTime (params.attackSeconds, params.attackSecondsVel1) * keyFactor;
        effectiveDecay  = velocityTime (params.decaySeconds, params.decaySecondsVel1) * keyFactor;

        attackIncrement = (effectiveAttack > 0.0f && sampleRate > 0.0)
                              ? (float) (1.0 / (effectiveAttack * sampleRate)) : 1.0f;
        decayCoeff = coefficient (effectiveDecay);
        // Release 0 würde knacken: intern mindestens 4 ms (−20 dB nach 4 ms, −40 dB nach 8 ms).
        // Auf der Oberfläche darf Release bis 0 gehen.
        releaseCoeff = coefficient (std::max (params.releaseSeconds * keyFactor, kMinReleaseSeconds));
        sustainTimeCoeff = coefficient (std::fabs (params.sustainTimeSeconds) * keyFactor);
        sustainRiseFactor = (sustainTimeCoeff > 0.0f) ? 1.0f / sustainTimeCoeff : 1.0f;   // +20 dB in derselben Zeit
    }

    Params params;
    double sampleRate = 44100.0;
    Stage stage = Stage::Idle;
    float level = 0.0f;
    float velocityPosition = 1.0f;    // 0 = Velocity 1, 1 = Velocity 127
    int keyNote = 60;
    float effectiveAttack = 0.0f, effectiveDecay = 0.0f;
    float attackIncrement = 1.0f, decayCoeff = 0.0f, sustainCoeff = 0.0f, releaseCoeff = 0.0f;
    float sustainTimeCoeff = 1.0f, sustainRiseFactor = 1.0f;
    float sustainGain = 1.0f;          // Verschiebung des Sustain-Ziels durch T (multiplikativ, in dB gleichmäßig)
    float releaseFloor = 1.0e-7f;
    float releaseStart = 0.0f;          // Pegel beim Loslassen (für boostedLevel)
    bool releasedInAttack = false;
};

} // namespace maermel
