// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// offbytest.cpp – prüft group= / off_by= (seit 26.10.2026) mit tests/offby/offby-test.sfz:
// Eine Region mit group = X beendet klingende Regionen mit off_by = X; Layer derselben Note bleiben.
// Aufruf: offbytest tests/offby/offby-test.sfz
#define private public
#include "../Source/engine/Synth.h"
#undef private
#include "../Source/engine/ExsImport.h"
#include <cstdio>
#include <vector>
using namespace maermel;

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "OK  " : "FEHLER", what); if (! ok) ++failures; }
static int sounding (const Synth& s, int note)
{
    int k = 0;
    for (const auto& v : s.voices) if (v.active() && ! v.fading && v.note == note) ++k;
    return k;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("Aufruf: offbytest offby-test.sfz\n"); return 2; }
    std::shared_ptr<const Instrument> inst = loadAnyInstrument (argv[1]);
    if (! inst || inst->regions.size() != 5) { std::printf ("Instrument nicht geladen\n"); return 2; }
    std::vector<float> L (256), R (256);
    {
        Synth s; s.prepare (44100); s.setInstrument (inst);
        s.noteOn (46, 100); s.render (L.data(), R.data(), 256);
        check (sounding (s, 46) == 1, "offene Hi-Hat klingt");
        s.noteOn (42, 100); s.render (L.data(), R.data(), 256);
        check (sounding (s, 46) == 0 && sounding (s, 42) == 1, "geschlossene Hi-Hat (group=1) beendet die offene (off_by=1)");
        s.noteOn (46, 100); s.render (L.data(), R.data(), 256);
        check (sounding (s, 42) == 1 && sounding (s, 46) == 1, "offene Hi-Hat (group=2) beendet die geschlossene nicht");
        s.noteOn (44, 100); s.render (L.data(), R.data(), 256);
        check (sounding (s, 46) == 0 && sounding (s, 44) == 2, "Pedal-Hi-Hat (group=1, 2 Layer) beendet die offene, beide Layer klingen");
    }
    {
        // Selbst-Abschneiden (group = off_by): neue Note beendet die alte, ihre eigenen Layer bleiben
        Synth s; s.prepare (44100); s.setInstrument (inst);
        s.noteOn (50, 100); s.render (L.data(), R.data(), 256);
        s.noteOff (50);     s.render (L.data(), R.data(), 256);
        s.noteOn (50, 100); s.render (L.data(), R.data(), 256);
        check (sounding (s, 50) == 1, "group=3 off_by=3: neuer Anschlag beendet den vorigen (monophon)");
    }
    std::printf ("%s\n", failures == 0 ? "Alle Prüfungen bestanden." : "Es gab Fehler.");
    return failures == 0 ? 0 : 1;
}
