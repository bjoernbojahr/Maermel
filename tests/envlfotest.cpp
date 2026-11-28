// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// envlfotest.cpp – Märmel: prüft zwei Korrekturen aus 1.0.3
//   Hüllkurve: Decay 0 springt nach einem Attack nicht mehr in einem Sample auf den Sustain-Pegel (Knacken);
//              ohne Attack beginnt die Note bei Decay 0 gleich auf dem Sustain-Pegel (keine Spitze).
//   LFO:       Wechsel zwischen LFO und DC (Rate-Regler ganz links) gleitet statt zu springen; DC erreicht
//              danach genau ±1 (und keine Volume-Absenkung).
// Aufruf: g++ -std=c++17 -I Source/engine -o envlfotest tests/envlfotest.cpp
//         ./envlfotest

#include "../Source/engine/Envelope.h"
#include "../Source/engine/Lfo.h"
#include <cmath>
#include <cstdio>
using namespace maermel;

static int bad = 0;
static void check (bool ok, const char* what)
{
    if (! ok) ++bad;
    std::printf ("%s %s\n", ok ? "OK  " : "FEHL", what);
}

static Envelope makeEnvelope (float attack, float decay, float sustain)
{
    Envelope e;
    e.setSampleRate (48000.0);
    Envelope::Params p;
    p.setAttack (attack);
    p.setDecay (decay);
    p.sustainLevel = sustain;
    e.setParamsAndContext (p, 127, 60);
    e.noteOn();
    return e;
}

int main()
{
    // --- Hüllkurve ---------------------------------------------------------------
    {
        Envelope e = makeEnvelope (0.0f, 0.0f, 0.3f);
        const float first = e.next();
        float highest = first;
        for (int i = 0; i < 4800; ++i)
            highest = std::max (highest, e.next());
        check (std::fabs (first - 0.3f) < 1.0e-6f, "Attack 0, Decay 0: Note beginnt auf dem Sustain-Pegel");
        check (highest <= 0.3f + 1.0e-6f, "Attack 0, Decay 0: keine Spitze über dem Sustain");
    }
    {
        Envelope e = makeEnvelope (0.01f, 0.0f, 0.3f);
        float previous = 0.0f, biggestDrop = 0.0f, level = 0.0f;
        for (int i = 0; i < 4800; ++i)   // 100 ms
        {
            level = e.next();
            biggestDrop = std::max (biggestDrop, previous - level);
            previous = level;
        }
        // Mindest-Decay 4 ms: größter Schritt je Sample ≈ 0,7 × (1 − 10^(−1/192)) ≈ 0,008 (bis 1.0.2: 0,7 auf einmal)
        check (biggestDrop < 0.02f, "Attack 10 ms, Decay 0: kein Sprung nach dem Attack");
        check (std::fabs (level - 0.3f) < 1.0e-3f, "Attack 10 ms, Decay 0: erreicht den Sustain-Pegel");
    }
    {
        Envelope e = makeEnvelope (0.01f, 0.0f, 1.0f);
        float lowest = 2.0f;
        for (int i = 0; i < 480; ++i) e.next();             // Attack vorbei
        for (int i = 0; i < 4800; ++i) lowest = std::min (lowest, e.next());
        check (lowest == 1.0f, "Decay 0, Sustain 100 %: Pegel bleibt genau 1 (unverändert)");
    }

    // --- LFO ↔ DC ----------------------------------------------------------------
    const float block = 16.0f / 48000.0f;
    const float k = 1.0f - std::exp (-block / 0.002f);       // Glättung je Block (2 ms)
    {
        Lfo lfo;
        LfoParams p;
        p.wave = LfoWave::Square;
        p.rateHz = 0.1f;                                      // langsam: steht lange auf einem Wert
        lfo.restart();
        float v = 0.0f;
        for (int i = 0; i < 1500; ++i)                       // 0,5 s: Rechteck auf +1
            v = lfo.advance (p, block);
        p.invert = true;                                      // Invert: LFO-Wert −1 (geglättet)
        for (int i = 0; i < 1500; ++i) v = lfo.advance (p, block);
        p.invert = false;
        p.dc = true;                                          // Rate ganz links: DC = +1
        float biggestStep = 0.0f, previous = v;
        for (int i = 0; i < 3000; ++i)
        {
            v = lfo.advance (p, block);
            biggestStep = std::max (biggestStep, std::fabs (v - previous));
            previous = v;
        }
        check (biggestStep <= 2.0f * k + 1.0e-4f, "LFO → DC: gleitet statt zu springen");
        check (v == 1.0f, "DC: erreicht danach genau +1");
        check (lfo.volumeReduction() == 0.0f, "DC: keine Volume-Absenkung");

        p.invert = true;                                      // DC mit Invert: −1
        biggestStep = 0.0f;
        for (int i = 0; i < 3000; ++i)
        {
            v = lfo.advance (p, block);
            biggestStep = std::max (biggestStep, std::fabs (v - previous));
            previous = v;
        }
        check (biggestStep <= 2.0f * k + 1.0e-4f && v == -1.0f, "DC Invert umschalten: gleitet, endet genau bei −1");

        p.dc = false;                                         // zurück zum LFO
        p.invert = false;
        biggestStep = 0.0f;
        for (int i = 0; i < 3000; ++i)
        {
            v = lfo.advance (p, block);
            biggestStep = std::max (biggestStep, std::fabs (v - previous));
            previous = v;
        }
        check (biggestStep <= 2.0f * k + 1.0e-4f, "DC → LFO: gleitet statt zu springen");
    }
    {
        // DC ab Notenbeginn: sofort genau +1 (keine Glättung am Start)
        Lfo lfo;
        LfoParams p;
        p.dc = true;
        lfo.restart();
        check (lfo.advance (p, block) == 1.0f, "DC ab Notenbeginn: sofort genau +1");
    }

    std::printf (bad == 0 ? "Alle Prüfungen bestanden.\n" : "%d Prüfung(en) fehlgeschlagen.\n", bad);
    return bad == 0 ? 0 : 1;
}
