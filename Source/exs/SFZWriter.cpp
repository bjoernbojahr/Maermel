// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SFZWriter.cpp

#include "SFZWriter.h"
#include "../FileUtil.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace exs
{
namespace
{

std::string withTrailingSlash (std::string s)
{
    if (! s.empty() && s.back() != '/')
        s.push_back ('/');
    return s;
}

std::string lastFolderName (const std::string& path)
{
    std::string p = path;
    while (! p.empty() && maermel::isPathSeparator (p.back()))
        p.pop_back();

    const auto slash = p.find_last_of (maermel::kPathSeparators);
    return slash == std::string::npos ? p : p.substr (slash + 1);
}

std::string trim (const std::string& s)
{
    const auto b = s.find_first_not_of (" \t\r\n");
    if (b == std::string::npos)
        return {};
    const auto e = s.find_last_not_of (" \t\r\n");
    return s.substr (b, e - b + 1);
}

// Zahl mit höchstens 'decimals' Nachkommastellen, ohne überflüssige Nullen, immer mit Punkt.
// Bewusst von Hand statt mit printf: printf richtet sich nach der Spracheinstellung des Systems und
// würde in einer deutschen Umgebung ein Komma schreiben, das kein SFZ-Player versteht.
std::string formatNumber (double value, int decimals)
{
    long long scale = 1;
    for (int i = 0; i < decimals; ++i)
        scale *= 10;

    long long scaled = std::llround (value * (double) scale);
    if (scaled == 0)
        return "0";                                       // auch -0,001 → "0", nie "-0"

    std::string text;
    if (scaled < 0)
    {
        text = "-";
        scaled = -scaled;
    }
    text += std::to_string (scaled / scale);

    long long fraction = scaled % scale;
    if (fraction != 0)
    {
        std::string digits = std::to_string (fraction);
        while ((int) digits.size() < decimals)
            digits.insert (digits.begin(), '0');          // z. B. 0,05 → "05"
        while (! digits.empty() && digits.back() == '0')
            digits.pop_back();                            // 2,50 → 2,5
        text += "." + digits;
    }
    return text;
}

// Balance-Regler des EXS24 (siehe SFZWriter.h): Faktor für den linken bzw. rechten Kanal
double balanceLeft (int pan)  { return pan <= 0 ? 1.0 : 1.0 - pan / 100.0; }
double balanceRight (int pan) { return pan >= 0 ? 1.0 : 1.0 + pan / 100.0; }

void writeGroupOpcodes (std::ostream& o, const Group& g)
{
    // Tasten- und Velocity-Bereich, Volume und Pan der Gruppe stehen NICHT mehr hier, sondern sind in jede
    // Region eingerechnet (seit 30.10.2026, siehe combineGroupAndZone). In SFZ würde eine Region mit eigenen
    // Werten die Gruppe sonst einfach überschreiben – anders als im EXS24, wo beide zusammenwirken.

    if (g.polyphony > 0)
        o << " polyphony=" << g.polyphony;

    if (g.triggerOnRelease)
        o << " trigger=release";

    if (g.selectMode == SelectMode::KeySwitch)
        o << " sw_last=" << g.keySwitchNote;

    if (g.selectMode == SelectMode::Controller && g.controllerNumber > 0)
        o << " locc" << g.controllerNumber << "=" << g.controllerLow
          << " hicc" << g.controllerNumber << "=" << g.controllerHigh;

    const std::string name = trim (g.name);
    if (! name.empty())
        o << "        // " << name;
}

// Kommentarzeile unter <group>: welche Werte der EXS-Gruppe in die Regionen eingerechnet sind.
// Ohne '=' und ohne spitze Klammern (manche Player stolpern darüber, siehe Konzept „Kompatibilität“).
void writeGroupInfo (std::ostream& o, const Group& g, const std::string& nl)
{
    std::string parts;
    auto add = [&parts] (const std::string& s) { parts += (parts.empty() ? "" : ", ") + s; };

    if (g.volumeDb != 0)
        add ("volume " + std::to_string (g.volumeDb) + " dB");
    if (g.pan != 0)
        add ("pan " + std::to_string (g.pan));
    if (g.velocityLow > 1 || g.velocityHigh < 127)
        add ("velocity " + std::to_string (g.velocityLow) + "-" + std::to_string (g.velocityHigh));
    if (g.keyLow > 0 || g.keyHigh < 127)
        add ("keys " + std::to_string (g.keyLow) + "-" + std::to_string (g.keyHigh));

    if (! parts.empty())
        o << "// EXS group: " << parts << " (included in each region)" << nl;
}

void writeRegion (std::ostream& o, const Zone& z, const Group* g, const Sample& s, const SfzWriterOptions& opt)
{
    const std::string& nl = opt.newline;

    // Wirksame Werte aus Gruppe und Zone (siehe combineGroupAndZone)
    const CombinedZone c = combineGroupAndZone (g, z);
    if (! c.playable())
    {
        // Erklingt im EXS24 nie – nicht als Region schreiben, nur als Hinweis (ohne '=' und spitze Klammern)
        o << "// Skipped " << s.name << ": " << describeCombineProblem (g, z, c) << nl;
        return;
    }

    o << "<region> sample=" << s.name;
    o << (opt.usePitchKeycenter ? " pitch_keycenter=" : " key=") << z.rootKey;
    o << " lokey=" << c.keyLow << " hikey=" << c.keyHigh;

    // Velocity 0 ist nie ein Note-On, daher sind lovel=0 und lovel=1 gleichwertig:
    // geschrieben wird nur, wenn der Bereich tatsächlich eingeschränkt ist.
    if (c.velocityLow > 1 || c.velocityHigh < 127)
        o << " lovel=" << c.velocityLow << " hivel=" << c.velocityHigh;

    if (z.oneShot)        o << " loop_mode=one_shot";
    if (z.pitchDisabled)  o << " pitch_keytrack=0";
    if (z.reverse)        o << " direction=reverse";

    const std::string volume = formatNumber (c.volumeDb, 2);
    const std::string pan = formatNumber (c.pan, 1);
    if (volume != "0")    o << " volume=" << volume;
    if (pan != "0")       o << " pan=" << pan;
    if (z.tuneCents != 0) o << " tune=" << z.tuneCents;
    if (z.transpose != 0) o << " transpose=" << z.transpose;
    if (z.startFrame > 0) o << " offset=" << z.startFrame;

    // SFZ "end" ist das letzte abgespielte Sample (inklusiv). EXS-Dateien
    // speichern das volle Sample mal als Länge, mal als Länge-1 – beides
    // bedeutet "bis zum Ende" und braucht keinen end-Opcode.
    if (z.endFrame > 0 && z.endFrame < s.lengthFrames - 1)
        o << " end=" << z.endFrame;

    if (z.loopEnabled)
    {
        if (! opt.includeLoops || z.oneShot)
            o << nl << " //       ";

        // EXS speichert das Loop-Ende exklusiv (erstes Sample NACH dem Loop),
        // SFZ erwartet es inklusiv (letztes Sample IM Loop). Belegt an
        // "Oasys ... Vel128_d#6_99.wav": EXS-Loop-Ende = Anzahl der Frames.
        const int64_t sfzLoopEnd = std::max (z.loopStart, z.loopEnd - 1);

        o << " loop_mode=loop_continuous loop_start=" << z.loopStart
          << " loop_end=" << sfzLoopEnd;

        if (z.loopTune != 0)
        {
            if (opt.emitLoopTune)
                o << " loop_tune=" << z.loopTune;
            else
                o << nl << "// (unsupported) loop_tune=" << z.loopTune;
        }
    }
    else if (! z.oneShot)
    {
        o << " loop_mode=no_loop";
    }

    o << nl;
}

} // namespace

std::string writeSfz (const Instrument& inst, const SfzWriterOptions& opt)
{
    const std::string& nl = opt.newline;
    std::ostringstream o;

    o << "/////////////////////////////////////////////////////////////////////////////" << nl
      << "// sfz definition file - " << opt.sourceName;
    if (! opt.generator.empty())
        o << "    by " << trim (opt.generator);
    o << nl;

    const bool hasLoopTune = std::any_of (inst.zones.begin(), inst.zones.end(),
                                          [] (const Zone& z) { return ! z.deleted && z.loopTune != 0; });
    if (hasLoopTune && ! opt.emitLoopTune)
        o << nl << "//" << nl
          << "// Attention!! The original file uses loop_tune, which is not part of the sfz standard -"
          << " some samples might be out of tune!" << nl
          << "//" << nl;

    if (! opt.defaultPath.empty())
        o << nl << "<control>" << nl << "default_path=" << opt.defaultPath << nl;

    // Keyswitch-Bereich für sw_last (von der SFZ-Spezifikation verlangt)
    int swLow = 128, swHigh = -1;
    for (const auto& g : inst.groups)
        if (! g.deleted && g.selectMode == SelectMode::KeySwitch)
        {
            swLow  = std::min (swLow,  g.keySwitchNote);
            swHigh = std::max (swHigh, g.keySwitchNote);
        }
    if (swHigh >= 0)
        o << nl << "<global> sw_lokey=" << swLow << " sw_hikey=" << swHigh << nl;

    // Zonen nach Gruppen sortieren: Eimer 0 = ohne Gruppe, Eimer n = Gruppe n-1
    std::vector<std::vector<size_t>> buckets (inst.groups.size() + 1);
    for (size_t k = 0; k < inst.zones.size(); ++k)
    {
        if (inst.zones[k].deleted)
            continue;

        // Zonen einer gelöschten Gruppe gelten als ungruppiert
        const int gi = inst.zones[k].groupIndex;
        const bool valid = gi >= 0 && gi < (int) inst.groups.size()
                        && ! inst.groups[(size_t) gi].deleted;
        buckets[valid ? (size_t) gi + 1 : 0].push_back (k);
    }

    for (size_t b = 0; b < buckets.size(); ++b)
    {
        std::vector<size_t> zonesWithSample;
        for (size_t k : buckets[b])
        {
            const int si = inst.zones[k].sampleIndex;
            if (si >= 0 && si < (int) inst.samples.size() && ! inst.samples[(size_t) si].deleted)
                zonesWithSample.push_back (k);
        }
        if (zonesWithSample.empty())
            continue;

        const Group* group = (b > 0) ? &inst.groups[b - 1] : nullptr;

        o << nl << "<group>";
        if (group != nullptr)
            writeGroupOpcodes (o, *group);
        o << nl;
        if (group != nullptr)
            writeGroupInfo (o, *group, nl);

        for (size_t k : zonesWithSample)
        {
            const Zone& z = inst.zones[k];
            writeRegion (o, z, group, inst.samples[(size_t) z.sampleIndex], opt);
        }
    }

    return o.str();
}

CombinedZone combineGroupAndZone (const Group* group, const Zone& zone)
{
    CombinedZone c;

    // Werte der Gruppe; ohne Gruppe neutral (voller Bereich, 0 dB, Mitte)
    const int gKeyLow  = group ? group->keyLow : 0,       gKeyHigh = group ? group->keyHigh : 127;
    const int gVelLow  = group ? group->velocityLow : 0,  gVelHigh = group ? group->velocityHigh : 127;
    const int gVolume  = group ? group->volumeDb : 0;
    const int gPan     = group ? std::clamp (group->pan, -100, 100) : 0;
    const int zPan     = std::clamp (zone.pan, -100, 100);

    // 1. Tastenbereich: Schnittmenge (die Gruppe schränkt ein)
    c.keyLow  = std::max (zone.keyLow,  gKeyLow);
    c.keyHigh = std::min (zone.keyHigh, gKeyHigh);
    c.keyRangeEmpty = c.keyLow > c.keyHigh;

    // 2. Velocity: Zone ohne eigene Velocity-Zone deckt alles ab; dann Schnittmenge mit der Gruppe
    const int zVelLow  = zone.velocityEnabled ? zone.velocityLow  : 0;
    const int zVelHigh = zone.velocityEnabled ? zone.velocityHigh : 127;
    c.velocityLow  = std::max (zVelLow,  gVelLow);
    c.velocityHigh = std::min (zVelHigh, gVelHigh);
    // Velocity 0 ist kein Note-On: ein Bereich, der nur 0 enthält, erklingt ebenfalls nie
    c.velocityRangeEmpty = c.velocityLow > c.velocityHigh || c.velocityHigh < 1;

    // 3. Pan: zwei Balance-Regler hintereinander (Gruppe, dann Zone)
    const double left  = balanceLeft (gPan)  * balanceLeft (zPan);
    const double right = balanceRight (gPan) * balanceRight (zPan);
    const double louder = std::max (left, right);

    double panCorrectionDb = 0.0;
    if (louder <= 0.0)
    {
        c.silentByPan = true;                    // z. B. Gruppe +100, Zone -100: beide Kanäle zu
    }
    else
    {
        // Ein SFZ-Pan mit demselben Verhältnis von links und rechts; der lautere Kanal bestimmt die Korrektur.
        // Beispiel: Gruppe -50, Zone +100 → links 0, rechts 0,5 → pan=100 und -6,02 dB.
        if (left >= right)
            c.pan = -(1.0 - right / left) * 100.0;
        else
            c.pan = (1.0 - left / right) * 100.0;
        panCorrectionDb = 20.0 * std::log10 (louder);
    }

    // 4. Volume: Gruppe + Zone (+ Pan-Korrektur), begrenzt auf den SFZ-Bereich von Märmel
    const double volume = (double) gVolume + (double) zone.volumeDb + panCorrectionDb;
    c.volumeDb = std::clamp (volume, kMinVolumeDb, kMaxVolumeDb);
    c.volumeClamped = c.volumeDb != volume;

    return c;
}

std::string describeCombineProblem (const Group* group, const Zone& zone, const CombinedZone& c)
{
    auto range = [] (int lo, int hi) { return std::to_string (lo) + "-" + std::to_string (hi); };
    const std::string groupName = (group && ! trim (group->name).empty()) ? " '" + trim (group->name) + "'" : "";

    if (c.keyRangeEmpty)
        return "never plays - key range " + range (zone.keyLow, zone.keyHigh)
             + " outside group" + groupName + " key range " + range (group ? group->keyLow : 0, group ? group->keyHigh : 127);
    if (c.velocityRangeEmpty)
        return "never plays - velocity " + range (zone.velocityEnabled ? zone.velocityLow : 0,
                                                  zone.velocityEnabled ? zone.velocityHigh : 127)
             + " outside group" + groupName + " velocity " + range (group ? group->velocityLow : 0, group ? group->velocityHigh : 127);
    if (c.silentByPan)
        return "never plays - group" + groupName + " pan " + std::to_string (group ? group->pan : 0)
             + " and zone pan " + std::to_string (zone.pan) + " cancel each other out";
    if (c.volumeClamped)
        return "volume of group and zone together is " + formatNumber ((group ? group->volumeDb : 0) + zone.volumeDb, 2)
             + " dB, limited to " + formatNumber (c.volumeDb, 2) + " dB";
    return {};
}

SamplePathResult resolveSamplePaths (const Instrument& inst,
                                     const std::string& exsFolder,
                                     SamplePathStrategy strategy,
                                     const std::string& customFolder,
                                     const std::function<bool (const std::string&)>& fileExists)
{
    SamplePathResult result;

    if (inst.samples.empty())
        return result;

    // Wie im Original: Referenz ist der Pfad des ersten Samples
    const std::string originalPath = withTrailingSlash (inst.samples.front().path);
    const std::string subfolder    = withTrailingSlash (lastFolderName (originalPath));
    const std::string base         = withTrailingSlash (exsFolder);

    std::string prefix;
    switch (strategy)
    {
        case SamplePathStrategy::NextToExs:
            prefix = base;
            break;
        case SamplePathStrategy::ExsFolderPlusSubfolder:
            prefix = base + subfolder;
            result.suggestedDefaultPath = subfolder;
            break;
        case SamplePathStrategy::OriginalPath:
            prefix = originalPath;
            result.suggestedDefaultPath = originalPath;
            break;
        case SamplePathStrategy::CustomFolder:
            prefix = withTrailingSlash (customFolder);
            result.suggestedDefaultPath = trim (customFolder).empty() ? std::string() : prefix;
            break;
    }

    for (const auto& s : inst.samples)
    {
        const std::string full = prefix + s.name;
        const bool ok = fileExists && fileExists (full);

        result.fullPaths.push_back (full);
        result.exists.push_back (ok);
        ok ? ++result.found : ++result.missing;
    }

    return result;
}

} // namespace exs
