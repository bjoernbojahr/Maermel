// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// exporttest.cpp – Testwerkzeug für den Export eines Instruments (ohne Plugin)
// Aufruf: exporttest <instrument.sfz> <zielordner> <name>
// Erzeugt <zielordner>/<name>.sfz und <zielordner>/<name> Samples/, lädt danach Original und Export und
// vergleicht Regionen und Samples.
#include "../Source/engine/SfzExport.h"
#include "../Source/engine/Instrument.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace fs = std::filesystem;

int main (int argc, char** argv)
{
    if (argc < 4) { std::printf ("Aufruf: exporttest <instrument.sfz> <zielordner> <name>\n"); return 2; }
    const fs::path source = fs::absolute (argv[1]);
    std::ifstream in (source); std::stringstream ss; ss << in.rdbuf();
    auto plan = maermel::planSfzExport (ss.str(), source.parent_path().generic_string() + "/");
    if (! plan.ok) { std::printf ("Fehler: %s\n", plan.error.c_str()); return 1; }

    const std::string name = argv[3], folder = name + " Samples";
    const fs::path target = fs::path (argv[2]) / (name + ".sfz");
    fs::create_directories (fs::path (argv[2]) / folder);
    std::vector<std::string> finalNames;
    for (const auto& s : plan.samples)
    {
        fs::copy_file (s.source, fs::path (argv[2]) / folder / s.fileName, fs::copy_options::overwrite_existing);
        finalNames.push_back (s.fileName);
        std::printf ("  %-40s <- %s\n", s.fileName.c_str(), s.source.c_str());
    }
    std::ofstream (target) << maermel::fillExportText (plan, folder, finalNames);

    auto a = maermel::loadInstrument (source.generic_string());
    auto b = maermel::loadInstrument (target.generic_string());
    std::printf ("Original:   %s\nExportiert: %s\n", a->summary().c_str(), b->summary().c_str());
    const bool same = a->regions.size() == b->regions.size() && a->sampleCount == b->sampleCount && b->missingSamples == a->missingSamples;
    std::printf ("%s\n", same ? "OK: gleiche Regionen und Samples" : "ABWEICHUNG");
    return same ? 0 : 1;
}
