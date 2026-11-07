// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// layertest.cpp – prüft die Noten-Zählung (seit 19.10.2026) mit tests/layer/layer-test.sfz:
// Stimmenzahl = Noten; eine Note mit 6 Layern belegt eine Stimme; verdrängt werden immer ganze Noten.
// Aufruf: layertest tests/layer/layer-test.sfz
#define private public
#include "../Source/engine/Synth.h"
#undef private
#include "../Source/engine/ExsImport.h"
#include <cstdio>
#include <map>
#include <vector>
using namespace maermel;

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "OK  " : "FEHLER", what); if (! ok) ++failures; }

struct Census { int notes = 0; bool complete = true; int samples = 0; };
static Census census (const Synth& s)
{
    std::map<uint64_t, int> groups;
    std::map<uint64_t, int> notes;
    for (const auto& v : s.voices)
        if (v.active() && ! v.fading) { ++groups[v.noteGroup]; notes[v.noteGroup] = v.note; }
    Census c;
    c.notes = (int) groups.size();
    for (auto& g : groups)
    {
        c.samples += g.second;
        const int n = notes[g.first];
        const int expected = (n >= 48 && n <= 59) ? 6 : 1;
        if (g.second != expected) c.complete = false;
    }
    return c;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("Aufruf: layertest layer-test.sfz\n"); return 2; }
    std::shared_ptr<const Instrument> inst = loadAnyInstrument (argv[1]);
    std::vector<float> L (256), R (256);

    {
        Synth s; s.prepare (44100); s.setInstrument (inst); s.setVoiceMode (Synth::VoiceMode::Poly, 6);
        s.render (L.data(), R.data(), 256);
        s.noteOn (64, 100);                                  // ein Stereo-Sample
        for (int n = 48; n < 53; ++n) s.noteOn (n, 100);     // fünf Noten mit je 6 Layern
        s.render (L.data(), R.data(), 256);
        Census c = census (s);
        check (c.notes == 6 && c.samples == 31 && c.complete, "6 Stimmen: 1 Einzel-Sample + 5 Layer-Noten klingen vollständig (31 Samples)");
        s.noteOn (53, 100);                                  // 7. Note: die älteste (Einzel-Sample) muss weichen
        s.render (L.data(), R.data(), 256);
        c = census (s);
        bool singleGone = true;
        for (const auto& v : s.voices) if (v.active() && ! v.fading && v.note == 64) singleGone = false;
        check (c.notes == 6 && c.complete && singleGone, "7. Note: älteste Note ganz verdrängt, alle Layer der neuen Note klingen");
    }
    {
        Synth s; s.prepare (44100); s.setInstrument (inst); s.setVoiceMode (Synth::VoiceMode::Poly, 2);
        s.noteOn (48, 100);
        s.render (L.data(), R.data(), 256);
        Census c = census (s);
        check (c.notes == 1 && c.samples == 6 && c.complete, "2 Stimmen: eine Note mit 6 Layern klingt vollständig (früher verdrängte sie ihre eigenen Layer)");
    }
    {
        // Losgelassene Noten werden vor gehaltenen verdrängt, als Ganzes
        Synth s; s.prepare (44100); s.setInstrument (inst); s.setVoiceMode (Synth::VoiceMode::Poly, 3);
        s.setEnvelope ([] { Envelope::Params p; p.releaseSeconds = 2.0f; return p; }());
        s.noteOn (48, 100); s.noteOn (49, 100); s.noteOn (50, 100);
        s.noteOff (49);                                      // 49 ausklingend (Release)
        s.render (L.data(), R.data(), 256);
        s.noteOn (51, 100);
        s.render (L.data(), R.data(), 256);
        bool releasedGone = true, heldKept = true;
        for (const auto& v : s.voices)
        {
            if (! v.active() || v.fading) continue;
            if (v.note == 49) releasedGone = false;
        }
        for (int n : { 48, 50, 51 })
        {
            int k = 0; for (const auto& v : s.voices) if (v.active() && ! v.fading && v.note == n) ++k;
            if (k != 6) heldKept = false;
        }
        check (releasedGone && heldKept, "Losgelassene Note wird zuerst und ganz verdrängt, gehaltene bleiben vollständig");
    }
    {
        // Release-Samples zählen als eigene Note
        Synth s; s.prepare (44100); s.setInstrument (inst); s.setVoiceMode (Synth::VoiceMode::Poly, 2);
        s.noteOn (64, 100); s.render (L.data(), R.data(), 256);
        s.noteOff (64);     s.render (L.data(), R.data(), 256);   // Release-Sample startet
        int rel = 0; for (const auto& v : s.voices) if (v.active() && ! v.fading && v.isReleaseVoice) ++rel;
        check (rel == 1, "Release-Sample startet beim Loslassen");
        s.noteOn (48, 100); s.render (L.data(), R.data(), 256);
        Census c = census (s);
        check (c.notes <= 2 && c.complete, "Release-Note zählt mit, neue Layer-Note klingt vollständig");
    }
    {
        // Pool-Grenze: 64 Stimmen × 6 Layer = 384 > 192 Plätze -> ganze Noten weichen, nie halbe
        Synth s; s.prepare (44100); s.setInstrument (inst); s.setVoiceMode (Synth::VoiceMode::Poly, 64);
        bool ok = true;
        for (int i = 0; i < 200; ++i)
        {
            s.noteOn (48 + (i % 12), 100);
            s.render (L.data(), R.data(), 64);
            Census c = census (s);
            if (! c.complete || c.samples > Synth::voicePoolSize) ok = false;
        }
        check (ok, "64 Stimmen mit 6-Layer-Noten: Pool (192) reicht nie für halbe Noten – es weichen immer ganze");
    }
    std::printf ("%s\n", failures == 0 ? "Alle Prüfungen bestanden." : "Es gab Fehler.");
    return failures == 0 ? 0 : 1;
}
