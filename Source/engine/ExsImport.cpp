// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// ExsImport.cpp – Märmel Engine

#include "ExsImport.h"
#include "FileUtil.h"
#include "exs/EXSParser.h"
#include "exs/SFZWriter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>

namespace maermel
{
namespace
{
bool fileExists (const std::string& path)
{
    std::ifstream f;
    openForReading (f, path);
    return (bool) f;
}

std::string lowerCase (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

std::string lowerExtension (const std::string& path)
{
    const auto dot = path.find_last_of ('.');
    return dot == std::string::npos ? std::string() : lowerCase (path.substr (dot + 1));
}

std::string fileNameOf (const std::string& path)
{
    const auto slash = path.find_last_of (kPathSeparators);
    return slash == std::string::npos ? path : path.substr (slash + 1);
}

// "Name.wav" → "Name_2.wav"
std::string withSuffix (const std::string& name, int n)
{
    const auto dot = name.find_last_of ('.');
    const std::string suffix = "_" + std::to_string (n);
    return dot == std::string::npos ? name + suffix : name.substr (0, dot) + suffix + name.substr (dot);
}
} // namespace

ExsConversion convertExsToSfz (const std::string& exsPath, const std::string& samplesFolderName)
{
    ExsConversion result;

    std::ifstream in;
    openForReading (in, exsPath);
    const std::vector<uint8_t> data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());

    exs::Instrument exsInstrument;
    if (data.empty() || ! exs::Parser::parse (data.data(), data.size(), exsInstrument, result.error))
    {
        if (result.error.empty())
            result.error = "empty or unreadable";
        return result;
    }

    // Loop Tune (EXS-Eigenheit, in SFZ nicht vorgesehen): Märmel spielt es nicht, warnt aber in „Loading notes“
    // (seit 16.10.2026, wie Björns Sample Mapper). Im SFZ steht es zusätzlich als Kommentar (SFZWriter).
    {
        std::vector<std::string> names;
        for (const auto& z : exsInstrument.zones)
        {
            if (z.deleted || ! z.loopEnabled || z.loopTune == 0)
                continue;
            const std::string sampleName = (z.sampleIndex >= 0 && (size_t) z.sampleIndex < exsInstrument.samples.size())
                                               ? fileNameOf (exsInstrument.samples[(size_t) z.sampleIndex].name)
                                               : std::string ("zone ") + std::to_string (names.size() + 1);
            names.push_back (sampleName + " (" + (z.loopTune > 0 ? "+" : "") + std::to_string (z.loopTune) + " ct)");
        }
        if (! names.empty())
        {
            std::string list;
            const size_t shown = std::min<size_t> (names.size(), 6);
            for (size_t i = 0; i < shown; ++i)
                list += (i > 0 ? ", " : "") + names[i];
            if (names.size() > shown)
                list += ", ... (" + std::to_string (names.size() - shown) + " more)";
            result.warnings.push_back ("EXS: " + std::to_string (names.size()) + " zone(s) use loop tune, which SFZ does not "
                                       "support - loops may sound out of tune. Re-loop these samples: " + list);
        }
    }

    // Gruppe und Zone zusammenführen (seit 30.10.2026): Zonen, die im EXS24 nie erklingen (Bereiche ohne
    // Überschneidung, Pan von Gruppe und Zone heben sich auf) oder deren Lautstärke begrenzt werden musste,
    // in „Loading notes“ melden. Der SFZWriter lässt nie erklingende Zonen weg (nur ein Kommentar im SFZ).
    {
        std::vector<std::string> problems;
        for (const auto& z : exsInstrument.zones)
        {
            if (z.deleted || z.sampleIndex < 0 || (size_t) z.sampleIndex >= exsInstrument.samples.size()
                || exsInstrument.samples[(size_t) z.sampleIndex].deleted)
                continue;
            const exs::Group* group = (z.groupIndex >= 0 && (size_t) z.groupIndex < exsInstrument.groups.size()
                                       && ! exsInstrument.groups[(size_t) z.groupIndex].deleted)
                                          ? &exsInstrument.groups[(size_t) z.groupIndex] : nullptr;
            const exs::CombinedZone combined = exs::combineGroupAndZone (group, z);
            const std::string problem = exs::describeCombineProblem (group, z, combined);
            if (! problem.empty())
                problems.push_back (fileNameOf (exsInstrument.samples[(size_t) z.sampleIndex].name) + " (" + problem + ")");
        }
        const size_t shown = std::min<size_t> (problems.size(), 6);
        for (size_t i = 0; i < shown; ++i)
            result.warnings.push_back ("EXS: " + problems[i]);
        if (problems.size() > shown)
            result.warnings.push_back ("EXS: ... (" + std::to_string (problems.size() - shown) + " more zones like this)");
    }

    const auto slash = exsPath.find_last_of (kPathSeparators);
    const std::string exsFolder = (slash == std::string::npos) ? std::string() : exsPath.substr (0, slash + 1);

    // Samples suchen: neben der EXS-Datei, im Unterordner mit dem Original-Ordnernamen, am Originalpfad.
    // Gewählt wird der Ort, an dem die meisten Samples gefunden werden.
    exs::SamplePathResult best;
    bool haveBest = false;
    for (auto strategy : { exs::SamplePathStrategy::NextToExs, exs::SamplePathStrategy::ExsFolderPlusSubfolder,
                           exs::SamplePathStrategy::OriginalPath })
    {
        auto candidate = exs::resolveSamplePaths (exsInstrument, exsFolder, strategy, {}, fileExists);
        if (! haveBest || candidate.found > best.found)
        {
            best = candidate;
            haveBest = true;
        }
        if (best.missing == 0)
            break;
    }
    result.found = best.found;
    result.missing = best.missing;

    // Jedes Sample bekommt seinen eigenen Pfad (Samples können in verschiedenen Ordnern liegen)
    const bool copy = ! samplesFolderName.empty();
    std::set<std::string> usedNames;   // Kleinschreibung, weil macOS/Windows nicht unterscheiden
    for (size_t i = 0; i < exsInstrument.samples.size() && i < best.fullPaths.size(); ++i)
    {
        auto& sample = exsInstrument.samples[i];
        if (sample.deleted)
            continue;

        const std::string& full = best.fullPaths[i];
        if (! best.exists[i])
        {
            result.warnings.push_back ("Sample not found: " + sample.name);
            continue;   // Name bleibt; die Engine meldet es beim Laden ebenfalls
        }

        if (copy)
        {
            std::string target = fileNameOf (full);
            for (int n = 2; usedNames.count (lowerCase (target)) > 0; ++n)
                target = withSuffix (fileNameOf (full), n);
            usedNames.insert (lowerCase (target));
            result.copies.push_back ({ full, target });
            sample.name = target;
        }
        else if (! exsFolder.empty() && full.compare (0, exsFolder.size(), exsFolder) == 0)
        {
            sample.name = full.substr (exsFolder.size());    // liegt im Instrument-Ordner: relativ
        }
        else
        {
            sample.name = full;                              // sonst absolut
        }
    }

    exs::SfzWriterOptions options;
    options.sourceName = fileNameOf (exsPath);
    options.generator = "Maermel EXS-Import";   // nur ASCII, landet als Kommentar im SFZ
    options.defaultPath = copy ? samplesFolderName + "/" : std::string();
    options.usePitchKeycenter = true;
    options.includeLoops = true;
    result.sfzText = exs::writeSfz (exsInstrument, options);

    for (const auto& w : exsInstrument.warnings)
        result.warnings.push_back ("EXS: " + w);
    result.ok = true;
    return result;
}

std::unique_ptr<Instrument> loadExsInstrument (const std::string& exsPath, const MemoryGuard& guard)
{
    // Im Speicher: Samples an ihrem Ort verwenden, nichts kopieren
    const ExsConversion conversion = convertExsToSfz (exsPath);
    if (! conversion.ok)
    {
        auto failed = std::make_unique<Instrument>();
        failed->sfzPath = exsPath;
        failed->warnings.push_back ("Cannot read EXS file: " + conversion.error);
        std::ifstream probe;
        openForReading (probe, exsPath);
        failed->fileMissing = ! probe;                 // Datei fehlt (nicht nur unlesbarer Inhalt)
        return failed;
    }

    auto instrument = loadInstrumentFromText (conversion.sfzText, exsPath, guard);
    for (auto it = conversion.warnings.rbegin(); it != conversion.warnings.rend(); ++it)
        if (it->rfind ("EXS: ", 0) == 0)
            instrument->warnings.insert (instrument->warnings.begin(), *it);
    return instrument;
}

std::unique_ptr<Instrument> loadAnyInstrument (const std::string& path, const MemoryGuard& guard)
{
    return lowerExtension (path) == "exs" ? loadExsInstrument (path, guard) : loadInstrument (path, guard);
}

} // namespace maermel
