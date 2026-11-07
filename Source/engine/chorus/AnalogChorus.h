// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//==============================================================================
//  AnalogChorus.h
//  Ein-Knopf-Chorus im Stil klassischer Analog-Synthesizer (BBD-Eimerkettenspeicher)
//
//  Bedienung: EIN Wert, der Effektanteil 0..100 %. Daraus ergeben sich über
//  eine nach Gehör ermittelte Kurve Rate, Filter und Pegel. Bei 0 % pausiert
//  der Chorus komplett und rechnet nichts (wichtig für ältere Bühnen-Rechner).
//
//  Signalweg:
//
//    L+R ─► Mono-Summe ─► Vorfilter ─► Kompressor ─► Delay-Leitung (BBD)
//                                                     │            │
//                                                 Stimme A     Stimme B
//                                                 (+LFO)       (-LFO, gegenphasig)
//                                                     │            │
//                                                 Expander     Expander
//                                                 Nachfilter   Nachfilter
//                                                     │            │
//    Ausgang L = Ausgleich * (trocken L + Anteil * Stimme A)   │       │
//    Ausgang R = Ausgleich * (trocken R + Anteil * Stimme B) ◄─┴───────┘
//
//  Weil beide Stimmen dasselbe Mono-Signal bekommen, reichen EIN Vorfilter,
//  EIN Kompressor und EINE Delay-Leitung mit zwei Leseköpfen. Das spart
//  Rechenzeit, klingt aber exakt gleich wie zwei getrennte Leitungen.
//
//  Reines C++17, keine JUCE-Abhängigkeit.
//
//  Thread-Regeln:
//   - prepare()    : NUR aus prepareToPlay() (alloziert Speicher)
//   - setAmount()  : im Audio-Thread, einmal pro Block VOR process()
//   - process()    : im Audio-Thread
//  Dadurch greift nie ein zweiter Thread gleichzeitig auf die Werte zu.
//==============================================================================
#pragma once

#include "ChorusBuildingBlocks.h"

namespace bjchorus
{
    //==========================================================================
    // Die Ein-Knopf-Kurve
    //==========================================================================
    // Stützpunkte nach Gehör ermittelt. Zwischen den Punkten wird linear
    // interpoliert – es gibt also KEINE Stufen, jede Knopfstellung hat ihren
    // eigenen Wert.
    //
    // Effektanteil: -100 dB steht für "aus". Interpoliert wird der lineare
    // Faktor (nicht die dB-Zahl), damit der Effekt von 0 % aus sauber aus der
    // Stille einblendet (bei 1 % ca. -25 dB, bei 5 % ca. -11 dB).
    //
    // Pegelausgleich (26.10.2026): Trocken + Effekt wurde mit dem Regler im Mittel bis ca. +3 dB (50 %) bzw.
    // +3,9 dB (100 %) lauter (Messreihe: K-gewichtete Lautheit nach ITU-R BS.1770, 7 Klänge – Sägezahn, Akkorde,
    // Sinus, Dreieck, E-Piano, Rauschfläche, Streicherakkord). Der Ausgleich senkt das Gesamtsignal so weit ab, dass
    // es nur noch leicht lauter wird (ca. +0,1 / +0,3 / +0,5 / +0,7 / +1,0 dB bei 10 / 25 / 50 / 70 / 100 %) – wie
    // beim Drive. Das Verhältnis von trockenem Signal und Effekt bleibt dabei unverändert. Bei 0 % gilt 0 dB, die
    // Pause bleibt also bitgenau trocken.
    //
    // Neue Werte? Einfach Zeilen ändern oder ergänzen. Die Knopfwerte müssen
    // aufsteigend sortiert sein, bei 0 beginnen und bei 100 enden.
    struct ChorusCurvePoint
    {
        double amountPercent;   // Knopfstellung 0..100
        double rateHz;          // LFO-Rate
        double cutoffHz;        // Vor- UND Nachfilter
        double wetDb;           // Effektanteil (-100 = aus)
        double outputDb;        // Pegelausgleich für das GESAMTE Signal (trocken + Effekt), 0 = keiner
    };

    static const ChorusCurvePoint kChorusCurve[] =
    {
        //  Knopf   Rate    Filter     Effekt   Ausgleich
        {     0.0,  0.38,  16000.0,  -100.0,     0.0 },
        {    10.0,  0.41,  15500.0,    -5.0,    -1.1 },
        {    25.0,  0.43,  15000.0,    -1.0,    -2.2 },
        {    50.0,  0.50,  14000.0,     0.2,    -2.5 },
        {    70.0,  0.64,  13000.0,     0.5,    -2.3 },
        {   100.0,  0.99,  12000.0,     2.0,    -2.9 },
    };

    static const int kChorusCurveSize = static_cast<int>(sizeof(kChorusCurve) / sizeof(kChorusCurve[0]));

    //--------------------------------------------------------------------------
    // Feste Werte (unabhängig von der Knopfstellung)
    //--------------------------------------------------------------------------
    constexpr double kDelayMinMs      = 1.66;   // kürzeste Verzögerung
    constexpr double kDelayMaxMs      = 5.35;   // längste Verzögerung
    constexpr int    kFilterStages    = 2;      // 2 x 6 dB/Oktave
    constexpr double kAttackMs        = 5.0;    // Kompander
    constexpr double kReleaseMs       = 40.0;   // Kompander
    constexpr double kWetSmoothingMs  = 20.0;   // Glättung des Effektanteils

    //==========================================================================
    class AnalogChorus
    {
    public:
        //----------------------------------------------------------------------
        // Ergebnis der Kurve für eine Knopfstellung
        //----------------------------------------------------------------------
        struct CurveValues
        {
            double rateHz;
            double cutoffHz;
            float  wetGain;    // linear, 0.0 = aus
            float  outputGain; // linear, Pegelausgleich für trocken + Effekt (1.0 = keiner)
        };

        static CurveValues getCurveValues(double amountPercent)
        {
            amountPercent = limitValue(amountPercent, 0.0, 100.0);

            // Passendes Teilstück suchen: Punkt "lower" <= Knopf <= Punkt "upper".
            // Einfache Suchschleife – bei 6 Punkten schneller und lesbarer als
            // alles andere.
            int upperIndex = 1;
            while (upperIndex < kChorusCurveSize - 1
                   && amountPercent > kChorusCurve[upperIndex].amountPercent)
            {
                ++upperIndex;
            }

            const ChorusCurvePoint& lower = kChorusCurve[upperIndex - 1];
            const ChorusCurvePoint& upper = kChorusCurve[upperIndex];

            // Position innerhalb des Teilstücks: 0.0 = lower, 1.0 = upper
            const double span = upper.amountPercent - lower.amountPercent;
            double t = 0.0;
            if (span > 0.0)
                t = (amountPercent - lower.amountPercent) / span;

            const double lowerGain = decibelsToGainOrOff(lower.wetDb);
            const double upperGain = decibelsToGainOrOff(upper.wetDb);

            CurveValues values;
            values.rateHz   = lower.rateHz   + t * (upper.rateHz   - lower.rateHz);
            values.cutoffHz = lower.cutoffHz + t * (upper.cutoffHz - lower.cutoffHz);
            values.wetGain  = static_cast<float>(lowerGain + t * (upperGain - lowerGain));
            // Ausgleich in dB interpolieren (gleichmäßig empfundene Übergänge)
            const double outputDb = lower.outputDb + t * (upper.outputDb - lower.outputDb);
            values.outputGain = static_cast<float>(std::pow(10.0, outputDb / 20.0));
            return values;
        }

        //----------------------------------------------------------------------
        // ACHTUNG: alloziert Speicher (Delay-Puffer) -> nur aus prepareToPlay()
        //----------------------------------------------------------------------
        void prepare(double newSampleRate)
        {
            sampleRate = newSampleRate;

            // Delay-Mitte und Hub einmal in Samples umrechnen
            const double samplesPerMs = sampleRate / 1000.0;
            centreDelaySamples = 0.5 * (kDelayMinMs + kDelayMaxMs) * samplesPerMs;
            depthDelaySamples  = 0.5 * (kDelayMaxMs - kDelayMinMs) * samplesPerMs;

            // etwas Luft über der längsten Verzögerung lassen
            delayLine.prepare(sampleRate, kDelayMaxMs + 2.0);

            lfo.prepare(sampleRate);

            compressor.prepare(sampleRate);
            compressor.setTimes(kAttackMs, kReleaseMs);
            expanderA.prepare(sampleRate);
            expanderA.setTimes(kAttackMs, kReleaseMs);
            expanderB.prepare(sampleRate);
            expanderB.setTimes(kAttackMs, kReleaseMs);

            wetSmoother.prepare(sampleRate, kWetSmoothingMs);
            wetSmoother.setImmediately(0.0f);
            outputSmoother.prepare(sampleRate, kWetSmoothingMs);
            outputSmoother.setImmediately(1.0f);

            // Filter beim nächsten setAmount() sicher neu berechnen
            lastCutoffHz = -1.0;

            // Startzustand: pausiert. Wird ein Anteil > 0 gesetzt, blendet der
            // Chorus sauber aus der Stille ein.
            isPaused = true;
            resetState();
        }

        //----------------------------------------------------------------------
        // Knopfstellung setzen (0..100 %). Einmal pro Block vor process().
        //----------------------------------------------------------------------
        void setAmount(double amountPercent)
        {
            const CurveValues values = getCurveValues(amountPercent);

            lfo.setRate(values.rateHz);
            wetSmoother.setTarget(values.wetGain);
            outputSmoother.setTarget(values.outputGain);

            // Filter nur neu berechnen, wenn sich die Frequenz geändert hat.
            // Das kostet nur ein exp() pro Filterstufe – ein Drehen am Knopf
            // ist also auch auf alten Rechnern kein Problem. Der Filterzustand
            // bleibt dabei erhalten, deshalb knackt nichts.
            if (values.cutoffHz != lastCutoffHz)
            {
                preFilter.setup(values.cutoffHz, kFilterStages, sampleRate);
                postFilterA.setup(values.cutoffHz, kFilterStages, sampleRate);
                postFilterB.setup(values.cutoffHz, kFilterStages, sampleRate);
                lastCutoffHz = values.cutoffHz;
            }
        }

        // true = Chorus rechnet gerade nicht (0 % und ausgeblendet)
        bool isCurrentlyPaused() const { return isPaused; }

        //----------------------------------------------------------------------
        // Block verarbeiten.
        //  inLeft / inRight  : Eingang (bei Mono-Spur dürfen beide auf
        //                      denselben Kanal zeigen)
        //  outLeft / outRight: Ausgang. outRight = nullptr bedeutet Mono-
        //                      Ausgang: links bekommt dann die Mitte (L+R)/2.
        //  Ein- und Ausgang dürfen derselbe Speicher sein (In-Place), weil pro
        //  Sample erst gelesen und danach geschrieben wird.
        //----------------------------------------------------------------------
        void process(const float* inLeft, const float* inRight,
                     float* outLeft, float* outRight, int numSamples)
        {
            const bool wantsSilence = (wetSmoother.getTarget() <= 0.0f);

            //--- Pause-Logik ---------------------------------------------------
            if (isPaused)
            {
                if (wantsSilence)
                {
                    // Weiter pausieren: nur trockenes Signal durchreichen
                    copyDry(inLeft, inRight, outLeft, outRight, numSamples);
                    return;
                }

                // Wieder aufwachen: alte Reste in Puffern und Filtern löschen.
                // Der Effektanteil steht noch auf 0 und blendet jetzt über
                // kWetSmoothingMs ein – deshalb knackt nichts.
                resetState();
                isPaused = false;
            }

            //--- Chorus rechnen ----------------------------------------------------
            for (int i = 0; i < numSamples; ++i)
            {
                // 1. Eingang lesen (VOR dem Schreiben, wegen In-Place)
                const float dryLeft  = inLeft[i];
                const float dryRight = inRight[i];

                // 2. Mono-Summe -> Vorfilter -> Kompressor -> Delay-Leitung
                float bbdInput = 0.5f * (dryLeft + dryRight);
                bbdInput = preFilter.process(bbdInput);
                bbdInput = compressor.process(bbdInput);
                delayLine.push(bbdInput);

                // 3. Zwei Leseköpfe, gegenphasig moduliert (180°)
                const double lfoValue = static_cast<double>(lfo.next());
                float voiceA = delayLine.readHermite(centreDelaySamples + lfoValue * depthDelaySamples);
                float voiceB = delayLine.readHermite(centreDelaySamples - lfoValue * depthDelaySamples);

                // 4. Expander -> Nachfilter (je Stimme getrennt)
                voiceA = postFilterA.process(expanderA.process(voiceA));
                voiceB = postFilterB.process(expanderB.process(voiceB));

                // 5. Mischen: Ausgleich * (trocken + Anteil * Stimme)
                const float wetGain = wetSmoother.next();
                const float outputGain = outputSmoother.next();
                const float resultLeft  = outputGain * (dryLeft  + wetGain * voiceA);
                const float resultRight = outputGain * (dryRight + wetGain * voiceB);

                // 6. Schreiben
                if (outRight != nullptr)
                {
                    outLeft[i]  = resultLeft;
                    outRight[i] = resultRight;
                }
                else
                {
                    outLeft[i] = 0.5f * (resultLeft + resultRight);
                }
            }

            //--- Ganz ausgeblendet? Dann ab dem nächsten Block pausieren ---------
            if (wantsSilence && wetSmoother.isSettled() && outputSmoother.isSettled())
                isPaused = true;
        }

    private:
        static double decibelsToGainOrOff(double decibels)
        {
            if (decibels <= -99.0)
                return 0.0;   // "aus"
            return std::pow(10.0, decibels / 20.0);
        }

        // Alle Zustände löschen (Puffer, Filter, Hüllkurven, LFO-Phase)
        void resetState()
        {
            delayLine.reset();
            preFilter.reset();
            postFilterA.reset();
            postFilterB.reset();
            compressor.reset();
            expanderA.reset();
            expanderB.reset();
            lfo.reset();
        }

        // Im Pausenzustand: Eingang unverändert zum Ausgang
        static void copyDry(const float* inLeft, const float* inRight,
                            float* outLeft, float* outRight, int numSamples)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float dryLeft  = inLeft[i];
                const float dryRight = inRight[i];

                if (outRight != nullptr)
                {
                    outLeft[i]  = dryLeft;
                    outRight[i] = dryRight;
                }
                else
                {
                    outLeft[i] = 0.5f * (dryLeft + dryRight);
                }
            }
        }

        double sampleRate         = 44100.0;
        double centreDelaySamples = 0.0;
        double depthDelaySamples  = 0.0;
        double lastCutoffHz       = -1.0;
        bool   isPaused           = true;

        LowpassCascade preFilter;
        Compressor     compressor;
        DelayLine      delayLine;
        TriangleLfo    lfo;
        Expander       expanderA, expanderB;
        LowpassCascade postFilterA, postFilterB;
        SmoothedValue  wetSmoother;
        SmoothedValue  outputSmoother;   // Pegelausgleich, gleiche Glättung wie der Effektanteil
    };

} // namespace bjchorus
