// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// Filter.h – Märmel Engine
// Resonanzfilter:
// vier Tiefpass-Stufen hintereinander mit Rückkopplung vom Ausgang auf den Eingang.
//
// Bauweise: "Zero-Delay-Feedback" (TPT, nach V. Zavalishin, "The Art of VA Filter Design").
// Bleibt auch bei schneller Modulation stabil und klickfrei. Eine weiche Sättigung am
// Eingang der Rückkopplung gibt den analogen Charakter und begrenzt die Resonanz.
//
// Filtertypen als Mischung der Stufenausgänge, dadurch klingen
// alle Typen nach demselben Filter. Beim Umschalten wird in 5 ms übergeblendet.

#pragma once

#include <iterator>

#include <algorithm>
#include <cmath>

namespace maermel
{

enum class FilterType { None = 0, LowPass12, LowPass24, HighPass, BandPass };

// Schnelle, weiche Sättigung (rationale Näherung von tanh, exakt begrenzt auf ±1)
inline float softClip (float x) noexcept
{
    if (x <= -3.0f) return -1.0f;
    if (x >=  3.0f) return  1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

class Filter
{
public:
    // Mischung aus trockenem Signal, Eingang nach Rückkopplung und den vier Stufenausgängen
    struct Mix { float dry, input, s1, s2, s3, s4; };

    static Mix mixFor (FilterType type) noexcept
    {
        switch (type)
        {
            case FilterType::None:      return { 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 0.0f };   // ganz ohne Filter
            case FilterType::LowPass12: return { 0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f };
            case FilterType::LowPass24: return { 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f };
            case FilterType::HighPass:  return { 0.0f, 1.0f, -2.0f, 1.0f, 0.0f, 0.0f };   // 12 dB/Oktave
            case FilterType::BandPass:  return { 0.0f, 0.0f, 2.0f, -2.0f, 0.0f, 0.0f };   // 6 dB/Oktave je Flanke
        }
        return { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    }

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        fadeStep = (float) (1.0 / (0.005 * sr));
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : state) s = 0.0f;
    }

    // Neuer Anschlag: Zustand löschen und Typ ohne Überblendung setzen
    void start (FilterType type) noexcept
    {
        reset();
        current = previous = mixFor (type);
        currentType = type;
        fade = 1.0f;
    }

    // Ohne Filter und ohne laufende Überblendung: Rechnen kann entfallen
    bool isBypassed() const noexcept { return currentType == FilterType::None && fade >= 1.0f; }

    void setType (FilterType type) noexcept
    {
        const Mix target = mixFor (type);
        if (type == currentType && fade >= 1.0f)
            return;
        // Aus „kein Filter“ heraus (seit 23.11.2026): Im Bypass wurde der Zustand nicht weitergerechnet und stammt
        // noch aus der Zeit vor dem Abschalten. Löschen, damit beim Einblenden kein alter Rest der Resonanz hörbar wird.
        if (isBypassed())
            reset();
        previous = blendedMix();       // von der aktuellen Mischung aus überblenden
        current = target;
        currentType = type;
        fade = 0.0f;
    }

    // Stärkste Rückkopplung bei Resonanz 100 %: knapp unter der Selbstoszillation (die bei 4 beginnt).
    // Nach Gehör und Messungen abgestimmt (Dreieck, LP24).
    static constexpr float kMaxFeedback = 3.95f;

    // Kennlinie des Resonanzreglers: steigt früh an (bei 1/3 Regelweg ca. 0,38 des
    // Maximums, bei 2/3 ca. 0,82), oben flacher. k = kMaxFeedback × sin(π/2 · r)^1,4
    static float feedbackForResonance (float resonance) noexcept
    {
        const float r = std::min (1.0f, std::max (0.0f, resonance));
        return kMaxFeedback * std::pow (std::sin (1.57079632679f * r), 1.4f);
    }

    // Verschiebung der Grenzfrequenz mit der Resonanz in Oktaven (ca. +0,3, kurz vor der
    // Selbstoszillation ca. +0,2)
    static float resonanceCutoffShift (float feedback) noexcept
    {
        return 0.28f * (1.0f - std::exp (-2.0f * feedback)) - 0.1f * std::max (0.0f, feedback - 3.4f);
    }

    // Stärke der Verschiebung abhängig von der Reglerstellung (nach Gehör von Björn, 17.10.2026, mit fast der ganzen
    // Factory Library geprüft): bis Resonanz 50 % voll wie gemessen, darüber weich (ohne Knick) bis 0,55 bei 100 %.
    // Ergibt: Reso bis 50 % ca. +18 … +21 %, 70 % +18 %, 80 % +13 %, 90 % +10 %, 100 % +9 %.
    // Frühere Stände: bis 16.10.2026 durchgehend 1 (ohne Bassausgleich); Zwischenstand 17.10.: 0,91 durchgehend.
    static constexpr float kShiftLow = 1.0f, kShiftHigh = 0.55f, kShiftStart = 0.5f;

    static float resonanceShiftScale (float resonance) noexcept
    {
        if (resonance <= kShiftStart)
            return kShiftLow;
        const float t = std::min (1.0f, (resonance - kShiftStart) / (1.0f - kShiftStart));
        return kShiftLow + (kShiftHigh - kShiftLow) * t * t * (3.0f - 2.0f * t);   // „smoothstep“
    }

    // Bassausgleich bei Resonanz im Tiefpass (0 = keiner, 1 = voller Ausgleich des Verlusts 1/(1+k)).
    // 0,1: hält etwas Körper unter dem Cutoff (Björn, 17.10.2026; vorher 0).
    static constexpr float kBassCompensation = 0.1f;

    // Pegelanhebung mit der Resonanz in dB (ca. +4 dB, kurz vor der Selbstoszillation weniger)
    static float resonanceGainDecibels (float feedback) noexcept
    {
        return 4.3f * (1.0f - std::exp (-2.0f * feedback)) - 7.0f * std::max (0.0f, feedback - 3.6f);
    }

    // Alles, was nur von der Resonanz abhängt – für alle Stimmen gleich, daher einmal je Block statt je Stimme
    // berechnet (seit 23.11.2026; vorher je Stimme und bei Stereo zweimal: sin, 2 × pow, exp2, 2 × exp).
    struct ResonanceInfo
    {
        float k = 0.0f;              // Rückkopplung
        float shiftFactor = 1.0f;    // Verschiebung der Grenzfrequenz als Faktor
        float gain = 1.0f;           // Pegelanhebung (linear)
    };

    static ResonanceInfo resonanceInfo (float resonance) noexcept
    {
        ResonanceInfo info;
        info.k = feedbackForResonance (resonance);
        // Mit Resonanz rückt die Grenzfrequenz etwas nach oben (bei hoher Resonanz weniger, siehe
        // resonanceShiftScale), und der Pegel wird angehoben (aus den Messungen an Dreieck/LP24 abgeleitet)
        info.shiftFactor = std::exp2 (resonanceShiftScale (resonance) * resonanceCutoffShift (info.k));
        info.gain = std::pow (10.0f, resonanceGainDecibels (info.k) / 20.0f);
        return info;
    }

    // Grenzfrequenz in Hz und Resonanz 0 ... 1 (bei 1 kräftige Resonanz, aber keine Selbstoszillation).
    // Wird in kleinen Blöcken aufgerufen, nicht zwingend pro Sample.
    void setCutoffAndResonance (float hz, float resonance) noexcept
    {
        setCutoff (hz, resonanceInfo (resonance));
    }

    // Wie setCutoffAndResonance, aber mit fertig berechneten Resonanzwerten (siehe resonanceInfo).
    // Gleiche Rechenschritte in gleicher Reihenfolge – das Ergebnis ist bitgenau dasselbe.
    void setCutoff (float hz, const ResonanceInfo& res) noexcept
    {
        k = res.k;
        hz *= res.shiftFactor;
        resonanceGain = res.gain;

        const float maxHz = (float) (sampleRate * 0.45);
        hz = std::min (maxHz, std::max (10.0f, hz));
        const float g = (float) std::tan (3.14159265358979 * hz / sampleRate);
        G = g / (1.0f + g);

        // Alles, was nur von G und k abhängt, einmal pro Block statt pro Sample
        oneMinusG = 1.0f - G;
        const float G2 = G * G, G3 = G2 * G;
        G4 = G3 * G;
        c0 = G3 * oneMinusG;
        c1 = G2 * oneMinusG;
        c2 = G * oneMinusG;
        c3 = oneMinusG;
        inverseDenominator = 1.0f / (1.0f + k * G4);
    }

    // Stereo: Das rechte Filter bekommt dieselben Koeffizienten wie das linke (nicht Zustand, Typ oder Überblendung)
    void copyCoefficientsFrom (const Filter& o) noexcept
    {
        k = o.k;
        resonanceGain = o.resonanceGain;
        G = o.G;
        oneMinusG = o.oneMinusG;
        G4 = o.G4;
        c0 = o.c0; c1 = o.c1; c2 = o.c2; c3 = o.c3;
        inverseDenominator = o.inverseDenominator;
    }

    // compensation 0 ... 1: gleicht den Pegelverlust bei hoher Resonanz aus (Tiefpass)
    float process (float x, float compensation) noexcept
    {
        // Lineare Lösung der Rückkopplung ohne Verzögerung (ZDF)
        const float S = c0 * state[0] + c1 * state[1] + c2 * state[2] + c3 * state[3];
        const float y4estimate = (G4 * x + S) * inverseDenominator;

        // Eingang mit Rückkopplung, weich gesättigt
        const float u = softClip (x - k * y4estimate);

        // Vier Stufen: y = G·x + (1−G)·s, neuer Zustand s = 2y − s
        float in = u;
        float y[4];
        for (int i = 0; i < 4; ++i)
        {
            y[i] = G * in + oneMinusG * state[i];
            state[i] = 2.0f * y[i] - state[i];
            in = y[i];
        }

        // Typ-Mischung (mit Überblendung beim Umschalten)
        if (fade < 1.0f)
            fade = std::min (1.0f, fade + fadeStep);
        // Ohne laufende Überblendung die Mischung direkt nehmen (spart 12 Multiplikationen je Sample; Überblendungen
        // gibt es nur 5 ms lang beim Umschalten). Ergebnis gleich: bei fade = 1 ist blendedMix() == current.
        const Mix m = (fade < 1.0f) ? blendedMix() : current;
        const float out = m.dry * x + m.input * u + m.s1 * y[0] + m.s2 * y[1] + m.s3 * y[2] + m.s4 * y[3];

        // Pegelausgleich bei Resonanz (die Rückkopplung senkt die Bässe um 1/(1+k))
        return out * resonanceGain * (1.0f + compensation * k * lowPassShare (m));
    }

private:
    Mix blendedMix() const noexcept
    {
        const float a = fade, b = 1.0f - fade;
        return { a * current.dry + b * previous.dry,     a * current.input + b * previous.input,
                 a * current.s1 + b * previous.s1,       a * current.s2 + b * previous.s2,
                 a * current.s3 + b * previous.s3,       a * current.s4 + b * previous.s4 };
    }

    // Wie stark die Mischung ein Tiefpass ist (nur dort wird der Bassverlust ausgeglichen)
    static float lowPassShare (const Mix& m) noexcept
    {
        return std::min (1.0f, std::max (0.0f, m.s2 + m.s4));
    }

    double sampleRate = 44100.0;
    float state[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float G = 0.5f, k = 0.0f, resonanceGain = 1.0f;
    float oneMinusG = 0.5f, G4 = 0.0625f, c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.5f, inverseDenominator = 1.0f;
    Mix current { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, previous { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    FilterType currentType = FilterType::None;
    float fade = 1.0f, fadeStep = 0.01f;
};

// Drive vor dem Filter, pro Stimme (Akkorde bleiben sauber).
// Zunehmend weiche Sättigung über den ganzen Regelweg (kubische Kennlinie, beginnt behutsam),
// bis zur 16-fachen Aussteuerung (vorher 8-fach; mit der 2-fachen Überabtastung darf es kräftiger sein).
// Die Velocity wirkt vor dem Drive: harte Anschläge verzerren stärker.
// Fester Pegelausgleich g^-0,55 (aus einer Messreihe mit Sägezahn und Dreieck): ein kräftiger Anschlag
// bleibt etwa gleich laut, leisere werden etwas lauter, sehr heiße etwas leiser – wie ein Verstärker,
// der beim Aufdrehen "zusammendrückt". Ohne Messen und Nachregeln, daher keine Sprünge beim Drehen.
struct Drive
{
    static constexpr float kMakeupExponent = 0.55f;

    static float fromKnob (float knob) noexcept { return std::min (1.0f, std::max (0.0f, knob)); }

    static float gain (float drive) noexcept
    {
        const float d = std::min (1.0f, std::max (0.0f, drive));
        return 1.0f + 15.0f * d * d * d;  // bis 16-fache Aussteuerung (+24 dB; seit der 2-fachen Überabtastung)
    }

    // Einmal pro Block berechnen, nicht pro Sample
    static float makeup (float g) noexcept { return std::pow (g, -kMakeupExponent); }

    static float process (float x, float g, float makeupGain) noexcept
    {
        return g <= 1.0f ? x : softClip (g * x) * makeupGain;
    }
};

// Drive mit 2-facher Überabtastung (pro Stimme und Kanal).
// Die Verzerrung erzeugt Obertöne oberhalb der halben Abtastrate, die sonst als unharmonisches Aliasing
// zurückklappen. Deshalb läuft nur diese eine Stufe mit doppelter Rate: hochtasten, verzerren, runtertasten.
// Halbbandfilter mit 39 Koeffizienten (Remez): Durchlass bis 0,41·fs linear (±0,01 dB), Sperrdämpfung ≈ 64 dB;
// jeder zweite Koeffizient ist 0, daher nur 10 Koeffizientenpaare je Richtung.
// Gesamtverzögerung genau kDelay Samples. Ist Drive aus, wird das Signal nur um dieselbe Zeit verzögert –
// so knackt es nicht, wenn Drive während einer Note auf- oder zugedreht wird. Kosten bei Drive aus: praktisch null.
//
// Ringpuffer-Zugriffe (seit 1.0.3): Alle Indizes werden als (p + kSize − Abstand) & kMask gerechnet. So ist der Wert
// vor dem Maskieren nie negativ – "& kMask" auf negativen int-Werten setzt das Zweierkomplement voraus, das C++17 noch
// nicht garantiert (erst C++20). Funktioniert hat es überall, so ist es aber auch formal sauber und kostet nichts;
// das Ergebnis ist bitgleich.
class DriveStage
{
public:
    static constexpr int kDelay = 19;   // Samples bei der Grundrate (0,43 ms bei 44,1 kHz)

    void reset() noexcept
    {
        std::fill (std::begin (x), std::end (x), 0.0f);
        std::fill (std::begin (even), std::end (even), 0.0f);
        std::fill (std::begin (odd), std::end (odd), 0.0f);
        pos = 0;
        active = false;
    }

    float process (float in, float g, float makeupGain) noexcept
    {
        pos = (pos + 1) & kMask;
        x[pos] = in;

        if (g <= 1.0f)
        {
            active = false;
            return x[(pos + kSize - kDelay) & kMask];   // Drive aus: nur gleich verzögern
        }
        if (! active)
        {
            prime();                                    // Verlauf aus dem bisherigen Signal vorbereiten
            active = true;
        }

        // Hochtasten: zwei Werte je Eingangssample (gerade = Original, ungerade = Halbbandinterpolation)
        even[pos] = Drive::process (x[(pos + kSize - 10) & kMask], g, makeupGain);
        odd[pos]  = Drive::process (interpolate (pos), g, makeupGain);

        // Runtertasten (Halbband auf den verzerrten Werten, nur jeden zweiten Ausgang berechnen)
        float z = 0.5f * even[(pos + kSize - 9) & kMask];
        for (int k = 0; k < kPairs; ++k)
            z += kCoeffs[k] * (odd[(pos + kSize - 10 - k) & kMask] + odd[(pos + kSize - 9 + k) & kMask]);
        return z;
    }

private:
    static constexpr int kPairs = 10;
    static constexpr int kSize = 64, kMask = kSize - 1;

    // Der weiteste Blick zurück ist 10 + (kPairs − 1) = 19 Samples (bzw. kDelay). Der Puffer muss größer sein,
    // sonst würden sich alte und neue Werte überschneiden (seit 1.0.3 vom Compiler geprüft).
    static_assert (10 + kPairs - 1 < kSize, "Ringpuffer zu klein fuer die Halbbandfilter");
    static_assert (kDelay < kSize, "Ringpuffer zu klein fuer die Verzoegerung");
    static_assert ((kSize & kMask) == 0, "kSize muss eine Zweierpotenz sein (wegen & kMask)");
    static constexpr float kCoeffs[kPairs] = {
        3.1670276539e-01f, -1.0041591320e-01f, 5.4444960931e-02f, -3.3293785157e-02f, 2.0909774399e-02f,
        -1.2933442895e-02f, 7.6598010009e-03f, -4.2182400787e-03f, 2.0744360849e-03f, -9.3035647952e-04f };

    // Zwischenwert in der Mitte zwischen x[p−10] und x[p−9] (Halbbandinterpolation, Verstärkung 2 durch Nullen)
    float interpolate (int p) const noexcept
    {
        float sum = 0.0f;
        for (int k = 0; k < kPairs; ++k)
            sum += kCoeffs[k] * (x[(p + kSize - 10 - k) & kMask] + x[(p + kSize - 9 + k) & kMask]);
        return 2.0f * sum;
    }

    // Beim Einschalten: die letzten 20 Werte so füllen, als wäre das Signal unverzerrt hochgetastet worden.
    // Drive setzt beim Aufdrehen nahezu linear ein, daher geht der Übergang nahtlos.
    void prime() noexcept
    {
        for (int i = 0; i < 2 * kPairs; ++i)
        {
            const int p = (pos + kSize - i) & kMask;
            even[p] = x[(p + kSize - 10) & kMask];
            odd[p] = interpolate (p);
        }
    }

    float x[kSize] {}, even[kSize] {}, odd[kSize] {};
    int pos = 0;
    bool active = false;
};

} // namespace maermel
