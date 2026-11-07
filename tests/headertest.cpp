// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// headertest.cpp – vergleicht sampleRootKeyFromHeader / sampleLoopFromHeader / sampleMemoryFromHeader
// (nur Kopfdaten) mit dem vollständigen Laden (loadSampleFile). Aufruf: headertest datei1.wav datei2.aif ...
#include "../Source/engine/SampleFile.h"
#include <cstdio>
using namespace maermel;

int main (int argc, char** argv)
{
    int errors = 0;
    for (int i = 1; i < argc; ++i)
    {
        SampleData d; std::string err;
        if (! loadSampleFile (argv[i], d, err)) { std::printf ("--    %s: %s\n", argv[i], err.c_str()); continue; }
        const int root = sampleRootKeyFromHeader (argv[i]);
        const SampleLoopPoints loop = sampleLoopFromHeader (argv[i]);
        const uint64_t mem = sampleMemoryFromHeader (argv[i]);
        const int64_t ls = d.hasLoop ? d.loopStart : -1, le = d.hasLoop ? d.loopEnd : -1;
        // WAV ohne smpl, aber mit inst: der Kopfdaten-Leser nimmt den Grundton aus inst, das Laden nicht (gewollt)
        const bool instOnly = root >= 0 && d.rootKey < 0;
        const bool ok = (root == d.rootKey || instOnly) && loop.start == ls && loop.end == le && mem == d.memoryBytes();
        if (! ok) ++errors;
        std::printf ("%s  %-40s Root %4d (Laden %4d)  Loop %lld-%lld (Laden %lld-%lld)  Speicher %s\n", ok ? "OK  " : "FEHLER",
                     argv[i], root, d.rootKey, (long long) loop.start, (long long) loop.end, (long long) ls, (long long) le,
                     mem == d.memoryBytes() ? "gleich" : "ANDERS");
        if (instOnly) std::printf ("      (Grundton aus dem WAV-inst-Chunk – Märmel selbst nutzt beim Laden nur smpl)\n");
    }
    std::printf ("%d Abweichung(en)\n", errors);
    return errors == 0 ? 0 : 1;
}
