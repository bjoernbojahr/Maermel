// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// resettest.cpp – Märmel: prüft CC121 „Reset All Controllers“ (Synth::resetControllers, seit 1.0.3)
//   1. Noten, die nur noch das Sustain-Pedal hält, gehen in den Release und verklingen.
//   2. Gedrückte Tasten klingen weiter (CC121 ist kein „All Notes Off“).
//   3. Danach gilt das Pedal als oben: neue Noten klingen nach dem Loslassen aus.
// Aufruf: g++ -std=c++17 -I Source/engine -o resettest tests/resettest.cpp Source/engine/*.cpp Source/engine/exs/*.cpp
//         cd tests && ../resettest layer/layer-test.sfz

#include "../Source/engine/Synth.h"
#include "../Source/engine/ExsImport.h"
#include <cstdio>
#include <vector>
using namespace maermel;

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("Aufruf: resettest instrument.sfz\n");
        return 2;
    }
    std::shared_ptr<const Instrument> inst = loadAnyInstrument (argv[1]);
    std::vector<float> l (512), r (512);
    int bad = 0;
    auto check = [&] (bool ok, const char* what)
    {
        if (! ok) ++bad;
        std::printf ("%s %s\n", ok ? "OK  " : "FEHL", what);
    };
    // Rendert etwa die angegebene Zeit (Blöcke zu 512 Samples bei 48 kHz)
    auto renderSeconds = [&] (Synth& s, double seconds)
    {
        const int blocks = (int) (seconds * 48000.0 / 512.0) + 1;
        for (int i = 0; i < blocks; ++i)
        {
            s.render (l.data(), r.data(), 512);
            s.takeFinishedInstrument();
        }
    };

    Synth s;
    s.prepare (48000);
    Envelope::Params e;
    e.releaseSeconds = 0.05f;
    s.setEnvelope (e);
    s.setInstrument (inst);
    renderSeconds (s, 0.05);

    // Note 60 gedrückt halten, Note 64 nur über das Pedal halten
    s.sustainPedal (true);
    s.noteOn (60, 100);
    s.noteOn (64, 100);
    s.noteOff (64);
    renderSeconds (s, 0.1);
    const int before = s.activeVoiceCount();
    check (before > 0, "Stimmen klingen vor CC121");

    s.resetControllers();
    renderSeconds (s, 1.0);   // Release 50 ms – nach 1 s ist die Pedal-Note sicher verklungen
    const int heldOnly = s.activeVoiceCount();
    check (heldOnly > 0 && heldOnly < before, "nach CC121: Pedal-Note verklungen, gehaltene Taste klingt weiter");

    // Pedal gilt nun als oben: Taste loslassen → alles verklingt
    s.noteOff (60);
    renderSeconds (s, 1.0);
    check (s.activeVoiceCount() == 0, "nach dem Loslassen der Taste: keine Stimme mehr");

    // Neue Note ohne Pedal: klingt nach dem Loslassen aus (Pedal wurde wirklich zurückgesetzt)
    s.noteOn (67, 100);
    renderSeconds (s, 0.1);
    s.noteOff (67);
    renderSeconds (s, 1.0);
    check (s.activeVoiceCount() == 0, "neue Note nach CC121 verklingt ohne Pedal");

    std::printf (bad == 0 ? "Alle Prüfungen bestanden.\n" : "%d Prüfung(en) fehlgeschlagen.\n", bad);
    return bad == 0 ? 0 : 1;
}
