// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// stucktest.cpp – Märmel Belastungstest gegen hängende Noten
// Spielt tausende zufällige Folgen aus Anschlägen, Loslassen, Pedal, Moduswechseln, C.Tune,
// KeyShift und Instrumentwechseln. Danach werden alle Tasten und das Pedal losgelassen;
// es darf keine Stimme mehr klingen.
// Aufruf: stucktest instrument1.sfz [instrument2.sfz ...]

#include "../Source/engine/Synth.h"
#include "../Source/engine/ExsImport.h"
#include <cstdio>
#include <random>
#include <set>
#include <vector>
using namespace maermel;
int main (int argc, char** argv)
{
    std::vector<std::shared_ptr<const Instrument>> insts;
    for (int a = 1; a < argc; ++a)
        insts.push_back (loadAnyInstrument (argv[a]));
    if (insts.empty())
    {
        std::printf ("Aufruf: stucktest instrument1.sfz [instrument2.sfz ...]\n");
        return 1;
    }
    std::vector<float> l (512), r (512);
    int failures = 0, runs = 5000;
    std::mt19937 rng (42);

    for (int run = 0; run < runs; ++run)
    {
        Synth s; s.prepare (48000);
        Envelope::Params e; e.releaseSeconds = 0.05f; s.setEnvelope (e);
        s.setInstrument (insts[rng() % insts.size()]);
        std::set<int> held; bool pedal = false;

        for (int step = 0; step < 200; ++step)
        {
            const int what = (int) (rng() % 100);
            const int note = 30 + (int) (rng() % 60);
            if (what < 35)       { s.noteOn (note, 1 + (int) (rng() % 127)); held.insert (note); }
            else if (what < 70)  { if (! held.empty()) { auto it = held.begin(); std::advance (it, rng() % held.size()); s.noteOff (*it); held.erase (it); } }
            else if (what < 76)  { pedal = ! pedal; s.sustainPedal (pedal); }
            else if (what < 82)  { const int m = (int) (rng() % 4); s.setVoiceMode (m == 0 ? Synth::VoiceMode::Legato : m == 1 ? Synth::VoiceMode::Mono : Synth::VoiceMode::Poly, m == 2 ? 2 : 32); }
            else if (what < 86)  s.setCoarseTune ((int) (rng() % 25) - 12);
            else if (what < 90)  s.setKeyShift ((int) (rng() % 13) - 6);
            else if (what < 92)  s.exchangeInstrument (insts[rng() % insts.size()]);
            else if (what < 94)  s.setGlideTime ((rng() % 2) ? 0.1f : 0.0f);
            s.render (l.data(), r.data(), 512);
            s.takeFinishedInstrument();
        }
        // Alles loslassen: jede Taste, dann das Pedal
        for (int n : held) s.noteOff (n);
        s.sustainPedal (false);
        for (int i = 0; i < 48000 * 4 / 512; ++i) s.render (l.data(), r.data(), 512);   // 4 s ausklingen
        if (s.activeVoiceCount() > 0) ++failures;
    }
    std::printf ("%d zufällige Folgen, davon mit hängenden Stimmen: %d\n", runs, failures);
    return failures == 0 ? 0 : 1;
}
