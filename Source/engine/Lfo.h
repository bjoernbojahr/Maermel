// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// Lfo.h – Märmel Engine
// Niederfrequenz-Oszillator mit Dreieck, Sinus, Sägezahn und Rechteck.
//
// Normalbetrieb: schwingt um die Mitte (-1 ... +1), geeignet für Vibrato, Tremolo usw.
// Env Mode:      läuft genau einen Durchgang wie eine einfache Hüllkurve und endet bei 0 (neutral),
//                wo er danach ruht. Dreieck/Sinus/Rechteck: 0 → 1 → 0, Sägezahn: −1 → 0 (steigt).
// Invert:        spiegelt die gewählte Wellenform (Sägezahn fällt statt zu steigen); im Env Mode
//                kehrt es nur die Richtung um, das Ende bleibt 0.
// Delay:         blendet die Wirkung nach dem Start linear ein.
// DC:            konstant +1 (Höchstwert), mit Invert −1 (Tiefstwert); sonst alles wirkungslos.
//
// Wird in Blöcken (16 Samples) weitergezählt; das genügt für LFO-Frequenzen bis ca. 30 Hz.

#pragma once

#include <algorithm>
#include <cmath>

namespace maermel
{

enum class LfoWave { Triangle = 0, Sine, Saw, Square };

struct LfoParams
{
    float rateHz = 4.0f;
    LfoWave wave = LfoWave::Triangle;
    bool invert = false;
    bool envMode = false;
    float delaySeconds = 0.0f;

    // DC (Linksstellung des Rate-Reglers): kein LFO, sondern ein fester Wert – der Höchstwert (+1),
    // mit Invert der Tiefstwert (−1). Wellenform, Sync, Env Mode und Delay sind dabei wirkungslos.
    // Für feste Zuweisungen, z. B. Modulationsrad → Cutoff über Wheel LFO (mit Invert: schließen).
    bool dc = false;
};

class Lfo
{
public:
    void restart() noexcept
    {
        phase = 0.0;
        finished = false;
        delayElapsed = 0.0f;
        smoothingPrimed = false;   // neuer Durchgang: am Startwert beginnen, nicht vom alten Wert gleiten
    }

    // Nur die Delay-Einblendung neu starten, die Phase läuft frei weiter (Mono-LFO außerhalb von Env Mode,
    // seit 15.10.2026). Keine Glättung zurücksetzen: der Wert gleitet in ≈ 2 ms auf die neue Einblendung.
    void restartDelay() noexcept
    {
        delayElapsed = 0.0f;
    }

    // Phase von außen setzen (Mono-LFO synchron zum Songtempo), 0 ... 1
    void setPhase (double p) noexcept { phase = p - std::floor (p); }

    // Um 'seconds' weiterzählen und den aktuellen Wert (inklusive Delay-Einblendung) liefern
    float advance (const LfoParams& p, float seconds) noexcept
    {
        // DC: fester Höchst- bzw. (Invert) Tiefstwert, die Phase ruht. Delay und Env Mode sind wirkungslos.
        // Der Wert läuft seit dem 25.11.2026 durch dieselbe Glättung wie im LFO-Betrieb – vorher kehrte die
        // Funktion ohne Glättung zurück, und beim Umschalten zwischen LFO und DC sprang der Ausgabewert hart
        // (hörbar am Filter und an der Lautstärke). volumeReduction gleitet dabei auf 0 (volle Lautstärke).
        const bool dc = p.dc;
        const float value = dc ? (p.invert ? -1.0f : 1.0f) : currentValue (p);
        const float fade = (! dc && p.delaySeconds > 0.0f)
                             ? std::min (1.0f, delayElapsed / p.delaySeconds) : 1.0f;

        if (! dc)
        {
            // Env Mode wurde ausgeschaltet, nachdem sein Durchgang schon beendet war: wieder frei schwingen.
            // Ohne diese Zeile blieb der LFO für immer am Ende des Durchgangs stehen (Phase 1, Rechteck = −1) –
            // beim Mono-LFO auf Cutoff saß der Filter dann dauerhaft zu, auch nach einem Instrumentwechsel, weil der
            // Mono-LFO außerhalb von Env Mode nie neu gestartet wird (Fehler gefunden von Björn, 30.10.2026).
            if (finished && ! p.envMode)
            {
                finished = false;
                phase -= std::floor (phase);   // Phase 1,0 → 0,0 (gleiche Stelle der Schwingung)
            }

            delayElapsed = std::min (delayElapsed + seconds, 1.0e6f);

            if (! finished)
            {
                phase += (double) (p.rateHz * seconds);
                if (phase >= 1.0)
                {
                    if (p.envMode)
                    {
                        phase = 1.0;          // Env Mode: ein Durchgang, dann stehen bleiben
                        finished = true;
                    }
                    else
                    {
                        phase -= std::floor (phase);
                    }
                }
            }
        }

        // Sehr kurze Glättung (Zeitkonstante kSmoothingSeconds): Sägezahn und Rechteck springen sonst von einem
        // Block zum nächsten – am Filter und an der Lautstärke hörbar als Knacken. Die Flanke ist nach ≈ 3 × 2 ms
        // vollständig; der Sprung-Charakter bleibt erhalten. Dreieck und Sinus ändern sich dadurch praktisch nicht.
        const float target = value * fade;
        const float volumeTarget = dc ? 0.0f : fade * (1.0f - envVolumeLevel (p, value));
        if (! smoothingPrimed)
        {
            smoothed = target;                 // nach dem Start ohne Gleiten beginnen
            smoothedVolume = volumeTarget;
            smoothingPrimed = true;
        }
        else
        {
            const float k = smoothingFactor (seconds);
            smoothed += (target - smoothed) * k;
            smoothedVolume += (volumeTarget - smoothedVolume) * k;
            // DC: am Ende genau auf ±1 bzw. 0 einrasten. Die Glättung allein bleibt knapp davor stehen (z. B. bei
            // 0,99999982), weil der Schritt kleiner als die Rechengenauigkeit von float wird – DC soll aber exakt
            // der Höchst- bzw. Tiefstwert sein. Nur bei DC, damit der LFO-Betrieb bitgleich bleibt.
            if (dc && std::fabs (target - smoothed) < 1.0e-6f)
                smoothed = target;
            if (dc && std::fabs (smoothedVolume) < 1.0e-6f)
                smoothedVolume = 0.0f;
        }
        return smoothed;
    }

    // Nur Env Mode, Ziel Volume: Absenkung 0 (voll) … 1 (stumm) des zuletzt berechneten Blocks, inklusive
    // Delay-Einblendung (während des Delays volle Lautstärke). Lautstärke = 1 − Tiefe × volumeReduction().
    float volumeReduction() const noexcept { return smoothedVolume; }

    static float shape (LfoWave wave, float x) noexcept   // x = Phase 0 ... 1, Ergebnis -1 ... +1
    {
        switch (wave)
        {
            case LfoWave::Triangle: return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x;
            case LfoWave::Sine:     return -std::cos (6.28318530718f * x);    // beginnt unten wie das Dreieck
            case LfoWave::Saw:      return 2.0f * x - 1.0f;
            case LfoWave::Square:   return x < 0.5f ? 1.0f : -1.0f;
        }
        return 0.0f;
    }

private:
    float currentValue (const LfoParams& p) const noexcept
    {
        const float x = (float) std::min (1.0, phase);
        float v = shape (p.wave, x);
        if (p.envMode)
        {
            // Ein Durchgang, der bei 0 endet und danach dort ruht – mit und ohne Invert neutral.
            // Invert kehrt nur die Richtung der Auslenkung um.
            switch (p.wave)
            {
                case LfoWave::Triangle: v = x < 0.5f ? 2.0f * x : 2.0f - 2.0f * x; break;   // 0 → 1 → 0
                case LfoWave::Sine:     v = 0.5f * (1.0f - std::cos (6.28318530718f * x)); break;
                case LfoWave::Saw:      v = x - 1.0f; break;                             // −1 → 0 (steigt)
                case LfoWave::Square:   v = x < 0.5f && ! finished ? 1.0f : 0.0f; break; // an, dann aus
            }
        }
        return p.invert ? -v : v;
    }

    // Env Mode auf Volume wirkt wie eine Hüllkurve (Pegel 0 … 1, 1 = voll); Invert liefert die Gegenkurve.
    // Dreieck/Sinus: leise → voll → leise, Rechteck: an → aus, Sägezahn: blendet ein.
    // Mit Invert: voll → leise → voll, aus → an, blendet aus. Lauter als voll wird es nie.
    static float envVolumeLevel (const LfoParams& p, float value) noexcept
    {
        if (! p.envMode)
            return 1.0f;
        const bool fromBelow = (p.wave == LfoWave::Saw) != p.invert;   // Wert läuft im Bereich −1 … 0
        return std::min (1.0f, std::max (0.0f, fromBelow ? 1.0f + value : value));
    }

    static constexpr float kSmoothingSeconds = 0.002f;   // 2 ms

    double phase = 0.0;
    bool finished = false;
    float delayElapsed = 0.0f;
    float smoothed = 0.0f;
    float smoothedVolume = 0.0f;
    bool smoothingPrimed = false;
    // Glättungsfaktor je Blocklänge. Die Blöcke sind fast immer 16 Samples lang, werden aber an jedem MIDI-Ereignis
    // geteilt – die Länge wechselt dann ständig zwischen 16 und einem Rest. Zwei gemerkte Werte (der zuletzt benutzte
    // vorn) ersparen dabei das std::exp je Stimme und Block (seit 23.11.2026; vorher nur ein gemerkter Wert).
    float smoothingSeconds = -1.0f, smoothingK = 0.0f;            // zuletzt benutzt (-1 = noch nicht berechnet)
    float smoothingSeconds2 = -1.0f, smoothingK2 = 0.0f;          // davor benutzt

    float smoothingFactor (float seconds) noexcept
    {
        if (seconds == smoothingSeconds)
            return smoothingK;
        if (seconds == smoothingSeconds2)
        {
            std::swap (smoothingSeconds, smoothingSeconds2);      // nach vorn holen
            std::swap (smoothingK, smoothingK2);
            return smoothingK;
        }
        smoothingSeconds2 = smoothingSeconds;                      // bisherigen nach hinten, neuen vorn berechnen
        smoothingK2 = smoothingK;
        smoothingSeconds = seconds;
        smoothingK = 1.0f - std::exp (-seconds / kSmoothingSeconds);
        return smoothingK;
    }
};

} // namespace maermel
