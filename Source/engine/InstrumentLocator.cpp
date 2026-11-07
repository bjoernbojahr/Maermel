// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// InstrumentLocator.cpp – Märmel Engine

#include "InstrumentLocator.h"
#include "ExsImport.h"
#include "FileUtil.h"
#include "SfzParser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace maermel
{

namespace
{
std::string lowerAscii (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

uint32_t fnv1a (const std::string& s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s)
    {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

bool hasExtension (const std::string& path, const char* ext)
{
    const size_t dot = path.find_last_of ('.');
    return dot != std::string::npos && lowerAscii (path.substr (dot + 1)) == ext;
}
} // namespace

std::vector<uint32_t> sampleFingerprintFromText (const std::string& sfzText)
{
    std::vector<uint32_t> out;
    const ParsedInstrument parsed = SfzParser::parse (sfzText);
    for (const auto& region : parsed.regions)
    {
        std::string path = region.samplePath;
        std::replace (path.begin(), path.end(), '\\', '/');
        const size_t slash = path.find_last_of ('/');
        const std::string name = slash == std::string::npos ? path : path.substr (slash + 1);
        if (! name.empty())
            out.push_back (fnv1a (lowerAscii (unicode::composed (name))));   // NFC: Mac (NFD) und Windows gleich
    }
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

std::vector<uint32_t> sampleFingerprint (const std::string& instrumentPath)
{
    if (hasExtension (instrumentPath, "exs"))
    {
        const ExsConversion conversion = convertExsToSfz (instrumentPath);
        return conversion.ok ? sampleFingerprintFromText (conversion.sfzText) : std::vector<uint32_t>();
    }
    std::ifstream f;
    openForReading (f, instrumentPath);
    if (! f)
        return {};
    std::stringstream buffer;
    buffer << f.rdbuf();
    return sampleFingerprintFromText (buffer.str());
}

std::string fingerprintToString (const std::vector<uint32_t>& fingerprint)
{
    std::string out;
    char hex[12];
    for (size_t i = 0; i < fingerprint.size() && i < kMaxFingerprint; ++i)
    {
        std::snprintf (hex, sizeof (hex), "%08x", (unsigned) fingerprint[i]);
        if (i > 0) out += ' ';
        out += hex;
    }
    return out;
}

std::vector<uint32_t> fingerprintFromString (const std::string& text)
{
    std::vector<uint32_t> out;
    std::istringstream in (text);
    std::string token;
    while (in >> token && out.size() < kMaxFingerprint)
    {
        if (token.size() > 8 || token.find_first_not_of ("0123456789abcdefABCDEF") != std::string::npos)
            continue;
        out.push_back ((uint32_t) std::stoul (token, nullptr, 16));
    }
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

double fingerprintOverlap (const std::vector<uint32_t>& stored, const std::vector<uint32_t>& candidate)
{
    if (stored.empty())
        return 0.0;
    size_t common = 0;
    for (uint32_t h : stored)
        if (std::binary_search (candidate.begin(), candidate.end(), h))
            ++common;
    return (double) common / (double) stored.size();
}

LocateChoice chooseCandidate (const std::vector<std::string>& candidateParents,
                              const std::vector<std::vector<uint32_t>>& candidateFingerprints,
                              const std::string& savedParent,
                              const std::vector<uint32_t>& storedFingerprint)
{
    LocateChoice choice;
    const bool haveFingerprint = ! storedFingerprint.empty();
    const std::string parent = lowerAscii (unicode::composed (savedParent));

    // Rangfolge: gleicher Ordner vor anderem Ordner, dann größere Übereinstimmung
    int best = -1, second = -1;
    double bestKey = -1.0, secondKey = -1.0;
    for (size_t i = 0; i < candidateParents.size(); ++i)
    {
        const bool sameParent = lowerAscii (unicode::composed (candidateParents[i])) == parent;
        double overlap = -1.0;
        bool eligible;
        if (haveFingerprint)
        {
            overlap = i < candidateFingerprints.size() ? fingerprintOverlap (storedFingerprint, candidateFingerprints[i]) : 0.0;
            eligible = overlap >= kMinOverlap;
        }
        else
        {
            eligible = sameParent;
        }
        choice.overlaps.push_back (overlap);
        if (! eligible)
            continue;

        const double key = (sameParent ? 2.0 : 0.0) + std::max (0.0, overlap);   // Übereinstimmung höchstens 1
        if (key > bestKey)
        {
            second = best; secondKey = bestKey;
            best = (int) i; bestKey = key;
        }
        else if (key > secondKey)
        {
            second = (int) i; secondKey = key;
        }
    }

    if (best >= 0 && second >= 0 && secondKey >= bestKey - 1.0e-9)
    {
        choice.ambiguous = true;   // zwei gleich gute Kandidaten
        return choice;
    }
    choice.index = best;
    return choice;
}

} // namespace maermel
