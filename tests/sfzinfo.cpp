// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// sfzinfo.cpp – Märmel Testwerkzeug
// Lädt SFZ-Dateien mit der Engine und zeigt die geprüfte Region-Tabelle.
// Aufruf: sfzinfo datei.sfz [weitere.sfz ...]

#include "../Source/engine/Instrument.h"
#include "../Source/engine/ExsImport.h"

#include <cstdio>

using namespace maermel;

static const char* formatName (SampleFormat f)
{
    switch (f)
    {
        case SampleFormat::Int16:   return "int16";
        case SampleFormat::Int32:   return "int32";
        case SampleFormat::Float32: return "float";
    }
    return "?";
}

int main (int argc, char** argv)
{
    for (int a = 1; a < argc; ++a)
    {
        auto inst = loadAnyInstrument (argv[a]);
        std::printf ("=== %s\n%s\n", argv[a], inst->summary().c_str());

        for (const auto& w : inst->warnings)
            std::printf ("  ! %s\n", w.c_str());

        std::printf ("  %-30s %7s %7s %4s %4s %8s %8s %-22s %s\n",
                     "Sample", "Tasten", "Vel", "Root", "Grp", "Ende", "Format", "Loop", "Sonstiges");

        for (const auto& r : inst->regions)
        {
            const auto& s = r.sfz;
            const auto& d = *r.sample;
            const std::string name = s.samplePath.substr (s.samplePath.find_last_of ('/') + 1);

            char keys[16], vels[16], loop[48], fmt[24], extra[64] = "";
            std::snprintf (keys, sizeof keys, "%d-%d", s.loKey, s.hiKey);
            std::snprintf (vels, sizeof vels, "%d-%d", s.loVel, s.hiVel);
            std::snprintf (fmt, sizeof fmt, "%s/%dk/%dch", formatName (d.format), (int) (d.sampleRate / 1000), d.numChannels);
            if (r.looping)
                std::snprintf (loop, sizeof loop, "%lld-%lld (%s)", (long long) r.loopStart, (long long) r.loopEnd, r.loopSource);
            else
                std::snprintf (loop, sizeof loop, "%s", r.oneShot ? "one shot" : "kein Loop");

            int len = 0;
            if (s.pitchKeytrack != 100.0f) len += std::snprintf (extra + len, sizeof extra - len, "keytrack=%g ", s.pitchKeytrack);
            if (s.polyphony > 0)           len += std::snprintf (extra + len, sizeof extra - len, "poly=%d ", s.polyphony);
            if (s.volume != 0.0f)          len += std::snprintf (extra + len, sizeof extra - len, "vol=%g ", s.volume);
            if (s.tune != 0.0f)            len += std::snprintf (extra + len, sizeof extra - len, "tune=%g ", s.tune);
            if (s.trigger == Trigger::Release) len += std::snprintf (extra + len, sizeof extra - len, "release ");
            if (s.trigger == Trigger::First)   len += std::snprintf (extra + len, sizeof extra - len, "first ");
            if (s.trigger == Trigger::Legato)  len += std::snprintf (extra + len, sizeof extra - len, "legato ");
            for (const auto& c : s.ccConditions)
                len += std::snprintf (extra + len, sizeof extra - len, "cc%d=%d-%d ", c.cc, c.lo, c.hi);
            if (s.swLast >= 0)                 len += std::snprintf (extra + len, sizeof extra - len, "sw=%d ", s.swLast);
            if (s.seqLength > 1)               len += std::snprintf (extra + len, sizeof extra - len, "seq=%d/%d ", s.seqPosition, s.seqLength);
            if (s.loRand > 0.0f || s.hiRand < 1.0f) len += std::snprintf (extra + len, sizeof extra - len, "rand=%.2f-%.2f ", s.loRand, s.hiRand);
            if (r.startFrame > 0)          len += std::snprintf (extra + len, sizeof extra - len, "start=%lld ", (long long) r.startFrame);

            std::printf ("  %-30s %7s %7s %4d %4d %8lld %8s %-22s %s\n", name.c_str(), keys, vels,
                         s.pitchKeycenter, s.polyphonyGroup, (long long) r.endFrame, fmt, loop, extra);
        }
        std::printf ("\n");
    }
    return 0;
}
