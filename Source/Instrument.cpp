// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// Instrument.cpp – Märmel Engine

#include "Instrument.h"
#include "FileUtil.h"
#include "SfzParser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace maermel
{
namespace
{

std::string directoryOf (const std::string& path)
{
    const auto slash = path.find_last_of (kPathSeparators);
    return slash == std::string::npos ? std::string() : path.substr (0, slash + 1);
}

// isAbsolutePath: siehe FileUtil.h

std::string regionName (const PlayableRegion& r)
{
    return "'" + r.sfz.samplePath + "' (line " + std::to_string (r.sfz.lineNumber) + ")";
}

// Prüft eine Region gegen ihre Sample-Datei und korrigiert unplausible Werte.
// Gibt true zurück, wenn etwas korrigiert werden musste.
bool validateRegion (PlayableRegion& p, std::vector<std::string>& warnings)
{
    const Region& s = p.sfz;
    const SampleData& data = *p.sample;
    const int64_t last = data.lastFrame();
    bool corrected = false;

    auto note = [&] (const std::string& text)
    {
        warnings.push_back (regionName (p) + ": " + text);
        corrected = true;
    };

    // pitch_keycenter=sample: Grundton aus dem smpl- (WAV) bzw. INST-Chunk (AIFF). Fehlt er, bleibt der bisherige
    // Grundton der Region (aus key= oder einer äußeren Ebene, sonst 60).
    if (s.pitchKeycenterFromSample)
    {
        if (data.rootKey >= 0 && data.rootKey <= 127)
            p.sfz.pitchKeycenter = data.rootKey;
        else
            note ("pitch_keycenter=sample, but the sample file has no root key - using "
                  + std::to_string (s.pitchKeycenter));
    }

    // Ende
    p.endFrame = (s.end < 0) ? last : s.end;
    if (p.endFrame > last)
    {
        note ("end=" + std::to_string (s.end) + " beyond end of file, limited to " + std::to_string (last));
        p.endFrame = last;
    }

    // Start. offset=0 (Standard, auch wenn nichts angegeben ist) ist immer gültig – auch bei einer Datei mit nur
    // EINEM Sample, wo Start und Ende zusammenfallen. Vorher meldete Märmel dort „offset=0 implausible“, obwohl gar
    // kein Offset in der SFZ stand (kosmetisch, behoben 30.10.2026).
    p.startFrame = s.offset;
    if (s.offset != 0 && (p.startFrame < 0 || p.startFrame >= p.endFrame))
    {
        note ("offset=" + std::to_string (s.offset) + " implausible, set to 0");
        p.startFrame = 0;
    }

    // Loop-Modus: nicht angegeben → loopen, wenn SFZ oder Datei einen Loop enthalten
    const bool sfzHasLoop = s.loopStart >= 0 || s.loopEnd >= 0;
    LoopMode mode = s.loopMode;
    if (mode == LoopMode::Unset)
        mode = (sfzHasLoop || data.hasLoop) ? LoopMode::Continuous : LoopMode::NoLoop;

    p.oneShot = (mode == LoopMode::OneShot);
    p.looping = (mode == LoopMode::Continuous || mode == LoopMode::Sustain);
    p.loopOnlyWhileHeld = (mode == LoopMode::Sustain);

    if (! p.looping)
        return corrected;

    auto plausible = [&] (int64_t ls, int64_t le) { return ls >= 0 && le > ls && le <= p.endFrame; };

    if (sfzHasLoop)
    {
        const int64_t ls = (s.loopStart >= 0) ? s.loopStart : (data.hasLoop ? data.loopStart : 0);
        const int64_t le = (s.loopEnd >= 0)   ? s.loopEnd   : (data.hasLoop ? data.loopEnd : p.endFrame);

        if (plausible (ls, le))
        {
            p.loopStart = ls; p.loopEnd = le; p.loopSource = "SFZ";
            return corrected;
        }
        note ("Loop " + std::to_string (ls) + "-" + std::to_string (le) + " implausible");
    }

    if (data.hasLoop && plausible (data.loopStart, data.loopEnd))
    {
        p.loopStart = data.loopStart; p.loopEnd = data.loopEnd; p.loopSource = "file";
        return corrected;
    }

    if (! sfzHasLoop && ! data.hasLoop && s.loopMode != LoopMode::Unset)
    {
        // loop_continuous ohne jede Loop-Angabe: ganzes Sample loopen
        p.loopStart = p.startFrame; p.loopEnd = p.endFrame; p.loopSource = "whole sample";
        return corrected;
    }

    note ("no usable loop, playing without loop");
    p.looping = false;
    p.loopOnlyWhileHeld = false;
    return corrected;
}

// Größe einer Datei in Bytes (0, wenn sie sich nicht öffnen lässt); öffnet wie beim Laden, also auch mit
// der zweiten Schreibweise von Umlauten
uint64_t fileSizeOf (const std::string& path)
{
    std::ifstream f;
    openForReading (f, path);
    if (! f)
        return 0;
    f.seekg (0, std::ios::end);
    const std::streamoff size = f.tellg();
    return size > 0 ? (uint64_t) size : 0;
}

bool canOpen (const std::string& path)
{
    std::ifstream f;
    openForReading (f, path);
    return (bool) f;
}

// Sample-Pfade auflösen, mit eng begrenztem Rückgriff bei umbenannten Instrumenten (seit 22.10.2026):
// Wurden „altname.sfz“ und der Ordner „altname Samples“ umbenannt (z. B. in „neuname“), ohne die Pfade in
// der SFZ anzupassen, fehlen alle Samples. Nur dann wird der ERSTE Ordner des relativen Sample-Pfads ersetzt – erst
// durch „<Instrument> Samples“, dann durch „<Instrument>“; Unterordner und Dateiname bleiben, wie sie sind
// („altname Samples/Light/c4.wav“ → „neuname Samples/Light/c4.wav“). Wird das Sample unter dem eigentlichen
// Pfad gefunden, ändert sich nichts. Statt je Sample eine Meldung gibt es eine Zeile je umgeleitetem Ordner.
class SamplePathResolver
{
public:
    SamplePathResolver (const std::string& sfzPath)
        : baseDir (directoryOf (sfzPath))
    {
        std::string name = sfzPath.substr (baseDir.size());
        const auto dot = name.find_last_of ('.');
        stem = dot == std::string::npos || dot == 0 ? name : name.substr (0, dot);
    }

    // Vollständiger Pfad, unter dem das Sample geladen wird (bei Misserfolg der ursprüngliche)
    const std::string& resolve (const std::string& samplePath)
    {
        auto known = resolved.find (samplePath);
        if (known != resolved.end())
            return known->second;
        return resolved.emplace (samplePath, find (samplePath)).first->second;
    }

    // Eine Meldung je umgeleitetem Ordner (meist genau eine)
    void addNotes (std::vector<std::string>& warnings) const
    {
        std::vector<std::string> notes;
        for (const auto& [folders, count] : redirected)
            notes.push_back ("Sample paths do not match the instrument name: " + std::to_string (count)
                             + (count == 1 ? " sample" : " samples") + " loaded from folder '" + folders.second
                             + "' instead of '" + folders.first + "' - please update the paths in the SFZ file");
        warnings.insert (warnings.begin(), notes.begin(), notes.end());
    }

private:
    std::string find (const std::string& samplePath)
    {
        if (isAbsolutePath (samplePath))
            return samplePath;
        const std::string original = baseDir + samplePath;
        if (stem.empty() || canOpen (original))
            return original;

        // erster Ordner des Pfads („./“ am Anfang überspringen); ohne Ordner oder mit „..“ kein Rückgriff
        size_t start = 0;
        while (samplePath.compare (start, 2, "./") == 0)
            start += 2;
        const size_t slash = samplePath.find ('/', start);
        if (slash == std::string::npos || slash == start)
            return original;
        const std::string folder = samplePath.substr (start, slash - start);
        if (folder == "..")
            return original;
        const std::string rest = samplePath.substr (slash);          // „/Light/c4.wav“

        for (const std::string& candidate : { stem + " Samples", stem })
        {
            if (candidate == folder)
                continue;
            const std::string path = baseDir + candidate + rest;
            if (canOpen (path))
            {
                ++redirected[{ folder, candidate }];
                return path;
            }
        }
        return original;
    }

    std::string baseDir, stem;
    std::map<std::string, std::string> resolved;                     // Sample-Pfad aus der SFZ → vollständiger Pfad
    std::map<std::pair<std::string, std::string>, int> redirected;   // (alter Ordner, neuer Ordner) → Anzahl Samples
};

std::unique_ptr<Instrument> buildInstrument (const ParsedInstrument& parsed, const std::string& sfzPath,
                                             const MemoryGuard& guard)
{
    auto inst = std::make_unique<Instrument>();
    inst->sfzPath = sfzPath;
    inst->warnings = parsed.warnings;
    inst->ignoredOpcodes = parsed.ignoredOpcodes;

    SamplePathResolver paths (sfzPath);

    // Speicherschutz in zwei Stufen (siehe MemoryGuard): erst schnell über die Dateigrößen, genau nur wenn es knapp wird
    if (guard)
    {
        std::vector<std::string> files;
        std::set<std::string> seen;
        uint64_t fileBytes = 0;
        for (const Region& region : parsed.regions)
        {
            const std::string& fullPath = paths.resolve (region.samplePath);
            if (seen.insert (fullPath).second)
            {
                files.push_back (fullPath);
                fileBytes += fileSizeOf (fullPath);
            }
        }
        uint64_t need = fileBytes * 2;                                  // Stufe 1: sichere Obergrenze
        if (need + guard.currentBytes > guard.budgetBytes)
        {
            need = 0;                                                    // Stufe 2: genau aus den Kopfdaten
            for (const auto& file : files)
            {
                const uint64_t exact = sampleMemoryFromHeader (file);
                need += exact > 0 ? exact : fileSizeOf (file) * 2;       // unbekanntes Format: vorsichtig
            }
            if (need <= guard.budgetBytes && need + guard.currentBytes > guard.budgetBytes && guard.releaseCurrent)
                guard.releaseCurrent();                                  // passt nur ohne das aktuelle Instrument
        }
        inst->estimatedBytes = need;
        if (need > guard.budgetBytes)
        {
            char mb[32], file[32];
            auto megabytes = [] (char* out, size_t n, uint64_t bytes)   // ab 10 MB ohne, darunter mit Nachkommastelle
            {
                const double v = (double) bytes / (1024.0 * 1024.0);
                std::snprintf (out, n, v < 10.0 ? "%.1f" : "%.0f", v);
            };
            megabytes (mb, sizeof (mb), need);
            megabytes (file, sizeof (file), fileBytes);
            inst->notEnoughMemory = true;
            inst->warnings.insert (inst->warnings.begin(), std::string ("Not enough memory: the samples need ") + mb
                                   + " MB in memory (" + file + " MB on disk; 8-bit samples need twice, 24-bit samples "
                                   "4/3 of their size) - too large for this computer, instrument not loaded");
            return inst;
        }
    }
    std::map<std::string, std::shared_ptr<const SampleData>> cache;   // gleiches Sample nur einmal laden
    std::set<std::string> missing;                                     // Pfade fehlender Samples (je einmal)

    for (const Region& region : parsed.regions)
    {
        const std::string& fullPath = paths.resolve (region.samplePath);

        std::shared_ptr<const SampleData> sample;
        auto found = cache.find (fullPath);
        if (found != cache.end())
        {
            sample = found->second;
        }
        else if (missing.count (fullPath) == 0)
        {
            auto data = std::make_shared<SampleData>();
            std::string error;
            if (loadSampleFile (fullPath, *data, error))
            {
                sample = data;
                cache[fullPath] = data;
                inst->sampleMemoryBytes += data->memoryBytes();
            }
            else
            {
                missing.insert (fullPath);
                inst->warnings.push_back ("Sample '" + region.samplePath + "': " + error);
            }
        }

        if (! sample)
            continue;   // Region ohne ladbares Sample wird ausgelassen

        PlayableRegion p;
        p.sfz = region;
        p.sample = sample;
        if (validateRegion (p, inst->warnings))
            ++inst->correctedRegions;
        inst->regions.push_back (std::move (p));
    }

    // Keyswitch-Bereich: aus sw_lokey/sw_hikey, sonst aus den verwendeten sw_last-Werten
    int lo = 128, hi = -1, lastLo = 128, lastHi = -1;
    for (const auto& p : inst->regions)
    {
        const Region& s = p.sfz;
        if (s.swLoKey >= 0) lo = std::min (lo, s.swLoKey);
        if (s.swHiKey >= 0) hi = std::max (hi, s.swHiKey);
        if (s.swLast >= 0)  { lastLo = std::min (lastLo, s.swLast); lastHi = std::max (lastHi, s.swLast); }
        if (s.swDefault >= 0 && inst->keyswitchDefault < 0) inst->keyswitchDefault = s.swDefault;
    }
    if (hi >= 0 && lo <= hi)       { inst->keyswitchLow = lo; inst->keyswitchHigh = hi; }
    else if (lastHi >= 0)          { inst->keyswitchLow = lastLo; inst->keyswitchHigh = lastHi; }

    if (inst->hasKeyswitches() && inst->keyswitchDefault < 0)
        inst->warnings.push_back ("Keyswitches without sw_default: until the first keyswitch only regions without sw_last play");

    paths.addNotes (inst->warnings);                                  // Hinweis auf umbenannte Ordner zuerst

    inst->sampleCount = (int) cache.size();
    inst->missingSamples = (int) missing.size();
    return inst;
}

} // namespace

std::string Instrument::summary() const
{
    char mb[32];
    std::snprintf (mb, sizeof (mb), "%.1f", (double) sampleMemoryBytes / (1024.0 * 1024.0));

    std::ostringstream s;
    s << regions.size() << " regions, " << sampleCount << " samples (" << mb << " MB)";
    if (correctedRegions > 0) s << ", " << correctedRegions << " corrected";
    if (missingSamples > 0)   s << ", " << missingSamples << " missing";
    if (keyswitchLow >= 0)    s << ", Keyswitches " << keyswitchLow << "-" << keyswitchHigh;
    if (! ignoredOpcodes.empty())
    {
        s << ", ignored: ";
        for (size_t i = 0; i < ignoredOpcodes.size(); ++i)
            s << (i ? ", " : "") << ignoredOpcodes[i];
    }
    return s.str();
}

std::unique_ptr<Instrument> loadInstrumentFromText (const std::string& sfzText, const std::string& sfzPath,
                                                    const MemoryGuard& guard)
{
    return buildInstrument (SfzParser::parse (sfzText), sfzPath, guard);
}

std::unique_ptr<Instrument> loadInstrument (const std::string& sfzPath, const MemoryGuard& guard)
{
    std::ifstream f;
    openForReading (f, sfzPath);
    if (! f)
    {
        auto inst = std::make_unique<Instrument>();
        inst->sfzPath = sfzPath;
        inst->warnings.push_back ("Cannot read SFZ file: " + sfzPath);
        inst->fileMissing = true;
        return inst;
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    return loadInstrumentFromText (buffer.str(), sfzPath, guard);
}

} // namespace maermel
