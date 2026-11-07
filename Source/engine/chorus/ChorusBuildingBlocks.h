// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//==============================================================================
//  ChorusBuildingBlocks.h
//  Kleine DSP-Bausteine für den Analog-Chorus (BBD-Stil)
//
//  Bewusst:
//   - reines C++17, KEINE JUCE-Abhängigkeit (1:1 in Märmel nutzbar)
//   - keine Speicher-Allokation im Audio-Thread (nur in prepare())
//   - einfache, gut lesbare Formeln statt "cleverer" Tricks
//   - kompiliert auch mit Xcode 13 und Visual Studio (kein M_PI!)
//
//  Inhalt:
//   - Konstanten und kleine Hilfsfunktionen
//   - OnePoleLowpass   : Tiefpass 1. Ordnung (6 dB/Oktave)
//   - LowpassCascade   : mehrere OnePoles hintereinander
//   - SmoothedValue    : weiches Nachführen von Werten (gegen Knacken)
//   - TriangleLfo      : Dreiecks-LFO, Phase 0..1
//   - DelayLine        : Ringpuffer mit 4-Punkt-Hermite-Interpolation (das "BBD")
//   - EnvelopeFollower : Hüllkurvenfolger mit Attack/Release
//   - Compressor       : 2:1-Kompressor nach Art klassischer Kompander-Chips (vor dem BBD)
//   - Expander         : 1:2-Expander nach Art klassischer Kompander-Chips (hinter dem BBD)
//==============================================================================
#pragma once

#include <cmath>
#include <vector>

namespace bjchorus
{
    //--------------------------------------------------------------------------
    // Konstanten
    //--------------------------------------------------------------------------
    // M_PI ist KEIN Standard-C++ (fehlt z. B. unter MSVC ohne _USE_MATH_DEFINES),
    // deshalb eine eigene Konstante.
    constexpr double kPi    = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;

    //--------------------------------------------------------------------------
    // Denormal-Schutz
    //--------------------------------------------------------------------------
    // Wenn ein Filter- oder Hüllkurvenzustand bei Stille langsam gegen 0 läuft,
    // landet er irgendwann im "Denormal"-Bereich (extrem kleine Zahlen). Intel-
    // CPUs (z. B. ältere MacBooks) rechnen damit um ein Vielfaches langsamer –
    // das kann bei Stille plötzlich hohe CPU-Last erzeugen.
    // Im Plugin schaltet juce::ScopedNoDenormals das zusätzlich per CPU-Flag ab;
    // diese Funktion schützt den Code aber auch ohne JUCE.
    inline float flushDenormal(float value)
    {
        if (std::fabs(value) < 1.0e-15f)
            return 0.0f;
        return value;
    }

    // Wert auf einen Bereich begrenzen (einfach und für jeden Compiler eindeutig)
    inline double limitValue(double value, double minValue, double maxValue)
    {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    //==========================================================================
    // OnePoleLowpass – Tiefpass 1. Ordnung (6 dB/Oktave)
    //==========================================================================
    // Formel: y[n] = a0 * x[n] + b1 * y[n-1]
    // b1 = exp(-2*pi*fc/fs), a0 = 1 - b1  (Gleichanteil wird 1:1 durchgelassen)
    //
    // Hinweis: Bei hohen Grenzfrequenzen (über ca. 1/4 der Abtastrate) hängt
    // der Klang etwas von der Abtastrate ab. Beispiel 15 kHz, 2 Stufen, bei
    // 10 kHz gemessen: 44,1 kHz -2,0 dB, 48 kHz -2,15 dB, 96 kHz -2,9 dB.
    // Zwischen 44,1 und 48 kHz ist das vernachlässigbar. Eine "bilineare"
    // Formel löst das bei so hohen Frequenzen NICHT (geprüft) – dafür bräuchte
    // es Oversampling, was für einen dezenten Chorus übertrieben wäre.
    //
    // Standardwerte a0 = 1, b1 = 0 bedeuten "Durchleiten". So ist der Filter
    // auch ohne setCutoff()-Aufruf nicht stumm.
    class OnePoleLowpass
    {
    public:
        void setCutoff(double cutoffHz, double sampleRate)
        {
            // Grenzfrequenz begrenzen: nicht unter 10 Hz und nicht über 45 %
            // der Abtastrate (knapp unter Nyquist).
            cutoffHz = limitValue(cutoffHz, 10.0, sampleRate * 0.45);

            const double x = kTwoPi * cutoffHz / sampleRate;
            b1 = static_cast<float>(std::exp(-x));
            a0 = 1.0f - b1;
        }

        void reset()
        {
            z1 = 0.0f;
        }

        float process(float input)
        {
            z1 = input * a0 + z1 * b1;
            z1 = flushDenormal(z1);
            return z1;
        }

    private:
        float a0 = 1.0f;   // Eingangsfaktor
        float b1 = 0.0f;   // Rückkopplungsfaktor
        float z1 = 0.0f;   // letzter Ausgangswert (Filterzustand)
    };

    //==========================================================================
    // LowpassCascade – mehrere OnePoles hintereinander (je 6 dB/Oktave)
    //==========================================================================
    // Hinweis: Mehrere gleiche Stufen dämpfen an der Grenzfrequenz stärker
    // (bei 2 Stufen -6 dB statt -3 dB). Die Werte wurden nach Gehör mit
    // genau dieser Schaltung ermittelt – also so lassen.
    class LowpassCascade
    {
    public:
        static const int kMaxStages = 4;

        void setup(double cutoffHz, int numberOfStages, double sampleRate)
        {
            if (numberOfStages < 1)          numberOfStages = 1;
            if (numberOfStages > kMaxStages) numberOfStages = kMaxStages;
            stagesInUse = numberOfStages;

            for (int i = 0; i < kMaxStages; ++i)
                stages[i].setCutoff(cutoffHz, sampleRate);
        }

        void reset()
        {
            for (int i = 0; i < kMaxStages; ++i)
                stages[i].reset();
        }

        float process(float input)
        {
            float signal = input;
            for (int i = 0; i < stagesInUse; ++i)
                signal = stages[i].process(signal);
            return signal;
        }

    private:
        OnePoleLowpass stages[kMaxStages];
        int stagesInUse = 1;
    };

    //==========================================================================
    // SmoothedValue – Werte weich nachführen
    //==========================================================================
    // Springt ein Wert (z. B. der Effektanteil beim Drehen), knackt es. Diese
    // Klasse läuft exponentiell (wie ein RC-Glied) zum Zielwert.
    // smoothingTimeMs = Zeitkonstante (nach dieser Zeit ~63 % des Weges).
    class SmoothedValue
    {
    public:
        void prepare(double sampleRate, double smoothingTimeMs)
        {
            const double samples = smoothingTimeMs * 0.001 * sampleRate;
            if (samples < 1.0)
                coefficient = 0.0f;   // keine Glättung
            else
                coefficient = static_cast<float>(std::exp(-1.0 / samples));
        }

        // Neues Ziel setzen (wird dann langsam angefahren)
        void setTarget(float newTarget)
        {
            target = newTarget;
        }

        // Sofort auf einen Wert springen
        void setImmediately(float newValue)
        {
            target  = newValue;
            current = newValue;
        }

        // Einen Schritt weiter (einmal pro Sample aufrufen)
        float next()
        {
            current = target + coefficient * (current - target);

            // Am Ende "einrasten", damit der Wert nicht ewig minimal schwankt
            if (std::fabs(current - target) < 1.0e-6f)
                current = target;

            return current;
        }

        float getCurrent() const { return current; }
        float getTarget()  const { return target; }
        bool  isSettled()  const { return current == target; }

    private:
        float coefficient = 0.0f;
        float current     = 0.0f;
        float target      = 0.0f;
    };

    //==========================================================================
    // TriangleLfo – Dreiecks-LFO
    //==========================================================================
    // Die Phase läuft von 0.0 bis 1.0 (eine volle Schwingung) und beginnt dann
    // wieder bei 0. Die Ausgabe liegt immer zwischen -1 und +1.
    // Eine Änderung der Rate verändert nur die Schrittweite – die Phase läuft
    // ohne Sprung weiter, deshalb darf die Rate jederzeit geändert werden.
    class TriangleLfo
    {
    public:
        void prepare(double newSampleRate)
        {
            sampleRate = newSampleRate;
            setRate(rateHz);
        }

        void reset()
        {
            phase = 0.0;
        }

        void setRate(double newRateHz)
        {
            rateHz = newRateHz;
            phaseIncrement = rateHz / sampleRate;
        }

        // Liefert den aktuellen Wert (-1..+1) und rückt danach die Phase vor
        float next()
        {
            // Dreieck: Phase 0 -> -1, 0.25 -> 0, 0.5 -> +1, 0.75 -> 0, 1 -> -1
            // Erste Hälfte steigt, zweite Hälfte fällt.
            double value = 0.0;
            if (phase < 0.5)
                value = 4.0 * phase - 1.0;
            else
                value = 3.0 - 4.0 * phase;

            phase += phaseIncrement;
            if (phase >= 1.0)
                phase -= 1.0;

            return static_cast<float>(value);
        }

    private:
        double sampleRate     = 44100.0;
        double rateHz         = 0.5;
        double phase          = 0.0;
        double phaseIncrement = 0.5 / 44100.0;
    };

    //==========================================================================
    // DelayLine – Ringpuffer mit Hermite-Interpolation (das "BBD")
    //==========================================================================
    // Die Puffergröße ist eine Zweierpotenz. Dann kann man statt des (langsamen)
    // Modulo-Operators einfach "& mask" rechnen, um am Pufferende wieder vorne
    // anzufangen. Beispiel: Größe 512 -> mask = 511 (binär 1 1111 1111).
    //
    // Die Größe richtet sich nach der benötigten maximalen Delay-Zeit. Kleine
    // Puffer passen besser in den CPU-Cache – spürbar auf älteren Rechnern.
    //
    // Mehrere Lesezugriffe (readHermite) auf denselben Puffer sind erlaubt –
    // so teilen sich beide Chorus-Stimmen eine einzige Delay-Leitung.
    class DelayLine
    {
    public:
        // ACHTUNG: alloziert Speicher -> nur aus prepareToPlay() aufrufen,
        // niemals im Audio-Thread.
        void prepare(double sampleRate, double maxDelayMs)
        {
            // benötigte Länge + Reserve für die Interpolationspunkte
            const int needed = static_cast<int>(std::ceil(maxDelayMs * 0.001 * sampleRate)) + 8;

            int size = 16;
            while (size < needed)
                size = size * 2;

            buffer.assign(static_cast<size_t>(size), 0.0f);
            mask = size - 1;
            writeIndex = 0;

            // größte erlaubte Verzögerung (Platz für die Hermite-Nachbarn lassen)
            maxDelaySamples = static_cast<double>(size - 4);
        }

        void reset()
        {
            for (size_t i = 0; i < buffer.size(); ++i)
                buffer[i] = 0.0f;
            writeIndex = 0;
        }

        // Neues Sample in den Puffer schreiben
        void push(float sample)
        {
            buffer[static_cast<size_t>(writeIndex)] = sample;
            writeIndex = (writeIndex + 1) & mask;
        }

        // Liest das Signal "delaySamples" Samples in der Vergangenheit.
        // delaySamples = 0 wäre das gerade geschriebene Sample.
        float readHermite(double delaySamples) const
        {
            // Hermite braucht einen Punkt "nach" der Leseposition (y3).
            // Mit mindestens 2 Samples Verzögerung ist dieser Punkt immer
            // schon geschrieben. (Bei 44,1 kHz sind 2 Samples nur 0,045 ms.)
            if (delaySamples < 2.0)             delaySamples = 2.0;
            if (delaySamples > maxDelaySamples) delaySamples = maxDelaySamples;

            // Das neueste Sample liegt bei writeIndex - 1
            const double readPosition = static_cast<double>(writeIndex - 1) - delaySamples;

            // Ganzzahliger Teil (kann negativ sein!) und Nachkommaanteil 0..1
            const int   i1       = static_cast<int>(std::floor(readPosition));
            const float fraction = static_cast<float>(readPosition - static_cast<double>(i1));

            // Negative Indizes: "& mask" wandelt z. B. -1 in (size - 1) um.
            // Das funktioniert, weil int auf allen relevanten Plattformen im
            // Zweierkomplement gespeichert wird (ab C++20 sogar garantiert).
            const float y0 = buffer[static_cast<size_t>((i1 - 1) & mask)];
            const float y1 = buffer[static_cast<size_t>( i1      & mask)];
            const float y2 = buffer[static_cast<size_t>((i1 + 1) & mask)];
            const float y3 = buffer[static_cast<size_t>((i1 + 2) & mask)];

            // 4-Punkt-Hermite (Catmull-Rom), interpoliert zwischen y1 und y2.
            // Auswertung nach dem Horner-Schema: wenige Multiplikationen.
            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

            return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
        }

    private:
        std::vector<float> buffer = std::vector<float>(16, 0.0f);
        int    mask            = 15;
        int    writeIndex      = 0;
        double maxDelaySamples = 12.0;
    };

    //==========================================================================
    // EnvelopeFollower – Hüllkurve mit getrennter Attack-/Release-Zeit
    //==========================================================================
    class EnvelopeFollower
    {
    public:
        void prepare(double newSampleRate)
        {
            sampleRate = newSampleRate;
            setTimes(attackMs, releaseMs);
        }

        void setTimes(double newAttackMs, double newReleaseMs)
        {
            attackMs  = newAttackMs;
            releaseMs = newReleaseMs;
            attackCoefficient  = timeToCoefficient(attackMs);
            releaseCoefficient = timeToCoefficient(releaseMs);
        }

        void reset()
        {
            envelope = 0.0f;
        }

        float process(float input)
        {
            const float level = std::fabs(input);

            // Steigt der Pegel -> Attack-Zeit, fällt er -> Release-Zeit
            float coefficient = releaseCoefficient;
            if (level > envelope)
                coefficient = attackCoefficient;

            envelope = level + coefficient * (envelope - level);
            envelope = flushDenormal(envelope);
            return envelope;
        }

    private:
        float timeToCoefficient(double timeMs) const
        {
            const double samples = timeMs * 0.001 * sampleRate;
            if (samples < 1.0)
                return 0.0f;
            return static_cast<float>(std::exp(-1.0 / samples));
        }

        double sampleRate = 44100.0;
        double attackMs   = 5.0;
        double releaseMs  = 40.0;
        float  attackCoefficient  = 0.0f;
        float  releaseCoefficient = 0.0f;
        float  envelope = 0.0f;
    };

    //==========================================================================
    // Compressor / Expander – Kompander nach Art klassischer Kompander-Chips (Rauschminderung im BBD-Pfad)
    //==========================================================================
    // Idee: Vor dem BBD wird der Dynamikumfang halbiert (in dB), danach wieder
    // verdoppelt. Das gibt dem Chorus das typische leichte "Atmen".
    //
    // Mathematik (Pegel L linear, 1.0 = Bezugspegel 0 dBFS):
    //   Kompressor:  y = x / sqrt(env_x)    -> Ausgangspegel = sqrt(L)
    //   Expander:    z = y * env_y          -> env_y ~ sqrt(L), also z ~ L
    //
    // Jeder hat einen EIGENEN Hüllkurvenfolger. Der Expander folgt (wie beim
    // echten Chip) seinem eigenen Eingang, also dem Signal aus dem BBD.
    class Compressor
    {
    public:
        void prepare(double sampleRate)               { envelopeFollower.prepare(sampleRate); }
        void setTimes(double attackMs, double releaseMs) { envelopeFollower.setTimes(attackMs, releaseMs); }
        void reset()                                  { envelopeFollower.reset(); }

        float process(float input)
        {
            float envelope = envelopeFollower.process(input);

            // Untergrenze: höchstens 40 dB Verstärkung bei sehr leisen Signalen
            if (envelope < 0.0001f)
                envelope = 0.0001f;

            return input / std::sqrt(envelope);
        }

    private:
        EnvelopeFollower envelopeFollower;
    };

    class Expander
    {
    public:
        void prepare(double sampleRate)               { envelopeFollower.prepare(sampleRate); }
        void setTimes(double attackMs, double releaseMs) { envelopeFollower.setTimes(attackMs, releaseMs); }
        void reset()                                  { envelopeFollower.reset(); }

        float process(float input)
        {
            const float envelope = envelopeFollower.process(input);
            return input * envelope;
        }

    private:
        EnvelopeFollower envelopeFollower;
    };

} // namespace bjchorus
