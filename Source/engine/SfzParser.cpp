// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SfzParser.cpp – Märmel Engine

#include "SfzParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>
#include <utility>

namespace maermel
{
namespace
{

using Opcode  = std::pair<std::string, std::string>;
using Opcodes = std::vector<Opcode>;

std::string trim (const std::string& s)
{
    const auto b = s.find_first_not_of (" \t\r\n");
    if (b == std::string::npos)
        return {};
    const auto e = s.find_last_not_of (" \t\r\n");
    return s.substr (b, e - b + 1);
}

std::string toLower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

bool isNameChar (char c)
{
    return std::isalnum ((unsigned char) c) || c == '_';
}

// Entfernt /* ... */ und // ... bis Zeilenende. Zeilenumbrüche bleiben erhalten,
// damit Zeilennummern in Meldungen stimmen.
std::string stripComments (const std::string& text)
{
    std::string out;
    out.reserve (text.size());

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < text.size() && ! (text[i] == '*' && text[i + 1] == '/'))
            {
                if (text[i] == '\n')
                    out.push_back ('\n');
                ++i;
            }
            ++i;
            continue;
        }

        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            while (i < text.size() && text[i] != '\n')
                ++i;
            if (i < text.size())
                out.push_back ('\n');
            continue;
        }

        out.push_back (text[i] == '\r' ? '\n' : text[i]);
    }

    return out;
}

// Findet ab 'from' den Beginn des nächsten Opcodes ("name=") oder Headers ("<").
// Ein Opcode beginnt nur nach einem Leerzeichen, damit '=' in Werten nicht stört.
size_t findNextToken (const std::string& line, size_t from)
{
    for (size_t i = from; i < line.size(); ++i)
    {
        if (line[i] == '<')
            return i;

        if ((line[i] == ' ' || line[i] == '\t') && i + 1 < line.size())
        {
            size_t j = i + 1;
            while (j < line.size() && (line[j] == ' ' || line[j] == '\t'))
                ++j;

            size_t k = j;
            while (k < line.size() && isNameChar (line[k]))
                ++k;

            if (k > j && k < line.size() && line[k] == '=')
                return j;
            if (j < line.size() && line[j] == '<')
                return j;
        }
    }
    return std::string::npos;
}

bool parseInt64 (const std::string& v, int64_t& out)
{
    char* endPtr = nullptr;
    const long long x = std::strtoll (v.c_str(), &endPtr, 10);
    if (endPtr == v.c_str())
        return false;
    out = (int64_t) x;
    return true;
}

bool parseFloat (const std::string& v, float& out)
{
    char* endPtr = nullptr;
    const double x = std::strtod (v.c_str(), &endPtr);
    if (endPtr == v.c_str())
        return false;
    // „nan“, „inf“ oder Werte jenseits von float (z. B. 1e300) sind kein sinnvoller Opcode-Wert – wie ein
    // unlesbarer Wert behandeln, damit nichts Unendliches in Lautstärke oder Stimmung landet
    if (! std::isfinite (x) || std::fabs (x) > 3.0e38)
        return false;
    out = (float) x;
    return true;
}

// Opcodes, die Märmel bewusst nicht aus der Datei übernimmt (Klangparameter,
// für die die Oberfläche zuständig ist). Sie werden gesammelt und gemeldet.
bool isKnownIgnoredOpcode (const std::string& name)
{
    static const char* prefixes[] = { "ampeg_", "fileg_", "pitcheg_", "amplfo_", "fillfo_", "pitchlfo_",
                                      "lfo", "eg", "fil_", "cutoff", "resonance", "effect", "amp_",
                                      "rt_decay", "xf", "fx",
                                      "off_mode" };   // off_by blendet immer schnell aus (entspricht off_mode=fast)
    for (auto* p : prefixes)
        if (name.rfind (p, 0) == 0)
            return true;
    return false;
}

enum class Header { None, Control, Global, Master, Group, Region, Other };

class ParserState
{
public:
    ParsedInstrument result;

    void header (const std::string& nameRaw, int line)
    {
        flushRegion();
        const std::string name = toLower (nameRaw);

        if      (name == "control") current = Header::Control;
        else if (name == "global")  { current = Header::Global; globalOps.clear(); masterOps.clear(); groupOps.clear(); }
        else if (name == "master")  { current = Header::Master; masterOps.clear(); groupOps.clear(); }
        else if (name == "group")   { current = Header::Group;  groupOps.clear(); ++groupIndex; }
        else if (name == "region")  { current = Header::Region; regionOps.clear(); regionLine = line; }
        else
        {
            current = Header::Other;
            if (reportedHeaders.insert (name).second)
                result.warnings.push_back ("Header <" + name + "> ignored (line " + std::to_string (line) + ")");
        }
    }

    void opcode (const std::string& nameRaw, const std::string& value, int line)
    {
        const std::string name = toLower (nameRaw);

        switch (current)
        {
            case Header::Control: controlOpcode (name, value, line); break;
            case Header::Global:  globalOps.emplace_back (name, value); break;
            case Header::Master:  masterOps.emplace_back (name, value); break;
            case Header::Group:   groupOps.emplace_back (name, value); break;
            case Header::Region:  regionOps.emplace_back (name, value); break;
            case Header::Other:   break;
            case Header::None:
                result.warnings.push_back ("Opcode '" + name + "' before the first header (line " + std::to_string (line) + ")");
                break;
        }
    }

    void finish()
    {
        flushRegion();
        result.ignoredOpcodes.assign (ignored.begin(), ignored.end());
    }

    int octaveOffset = 0;
    int noteOffset = 0;

private:
    Header current = Header::None;
    Opcodes globalOps, masterOps, groupOps, regionOps;
    std::string defaultPath;
    int groupIndex = -1;
    int regionLine = 0;
    std::set<std::string> ignored, reportedHeaders, reportedUnknown;

    void controlOpcode (const std::string& name, const std::string& value, int line)
    {
        if (name == "default_path")
        {
            defaultPath = value;
            std::replace (defaultPath.begin(), defaultPath.end(), '\\', '/');
            if (! defaultPath.empty() && defaultPath.back() != '/')
                defaultPath.push_back ('/');
        }
        else if (name == "octave_offset")
        {
            int64_t v = 0;
            if (parseInt64 (value, v)) octaveOffset = (int) std::min<int64_t> (10, std::max<int64_t> (-10, v));
        }
        else if (name == "note_offset")
        {
            int64_t v = 0;
            if (parseInt64 (value, v)) noteOffset = (int) std::min<int64_t> (127, std::max<int64_t> (-127, v));
        }
        else if (name.rfind ("set_cc", 0) == 0 || name.rfind ("label_cc", 0) == 0)
        {
            ignored.insert (name.substr (0, name.find_first_of ("0123456789")));
        }
        else
        {
            unknown (name, line);
        }
    }

    bool keyswitchNote (const std::string& value, int& out, const std::string& name)
    {
        if (SfzParser::parseNote (value, 0, 0, out) && out >= 0 && out <= 127)
            return true;
        result.warnings.push_back ("Invalid keyswitch note '" + value + "' in " + name);
        return false;
    }

    void unknown (const std::string& name, int line)
    {
        if (reportedUnknown.insert (name).second)
            result.warnings.push_back ("Unknown opcode '" + name + "' (line " + std::to_string (line) + ")");
    }

    // Liest eine Note ohne Bereichsprüfung. Nur ein unlesbarer Wert gilt als Fehler.
    bool note (const std::string& value, int& out, const std::string& name)
    {
        if (SfzParser::parseNote (value, octaveOffset, noteOffset, out))
            return true;

        result.warnings.push_back ("Invalid note '" + value + "' in " + name + " (region line "
                                   + std::to_string (regionLine) + ")");
        return false;
    }


    void apply (Region& r, const std::string& name, const std::string& value)
    {
        int n = 0;
        int64_t i = 0;
        float f = 0.0f;

        // Werte auf sinnvolle Bereiche begrenzen (SFZ-Spezifikation, großzügig): Unsinnswerte aus beschädigten
        // oder fremden Dateien dürfen weder überlaufen noch die Tonhöhe ins Unendliche treiben (gefunden 26.10.2026
        // beim Test mit zufällig veränderten Dateien: transpose=2327674 → Abspielposition unendlich).
        auto clampInt = [] (int64_t v, int64_t lo, int64_t hi) { return (int) std::min (hi, std::max (lo, v)); };
        auto clampFloat = [] (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); };

        if (name == "sample")
        {
            r.samplePath = value;
            std::replace (r.samplePath.begin(), r.samplePath.end(), '\\', '/');
        }
        else if (name == "key")             { if (note (value, n, name)) { r.loKey = r.hiKey = r.pitchKeycenter = n; r.pitchKeycenterFromSample = false; } }
        else if (name == "lokey")           { if (note (value, n, name)) r.loKey = n; }
        else if (name == "hikey")           { if (note (value, n, name)) r.hiKey = n; }
        else if (name == "pitch_keycenter")
        {
            if (toLower (trim (value)) == "sample")
                r.pitchKeycenterFromSample = true;        // Grundton aus der Sample-Datei, beim Laden eingesetzt
            else if (note (value, n, name))
            {
                r.pitchKeycenter = n;                     // Grundton darf außerhalb liegen
                r.pitchKeycenterFromSample = false;
            }
        }
        else if (name == "lovel")           { if (parseInt64 (value, i)) r.loVel = clampInt (i, 0, 127); }
        else if (name == "hivel")           { if (parseInt64 (value, i)) r.hiVel = clampInt (i, 0, 127); }
        else if (name == "pitch_keytrack")  { if (parseFloat (value, f)) r.pitchKeytrack = clampFloat (f, -1200.0f, 1200.0f); }
        else if (name == "transpose")       { if (parseInt64 (value, i)) r.transpose = clampInt (i, -127, 127); }
        else if (name == "tune")            { if (parseFloat (value, f)) r.tune = clampFloat (f, -9600.0f, 9600.0f); }
        else if (name == "volume")          { if (parseFloat (value, f)) r.volume = clampFloat (f, -144.0f, 24.0f); }
        else if (name == "pan")             { if (parseFloat (value, f)) r.pan = clampFloat (f, -100.0f, 100.0f); }
        // Positionen in Frames: werden beim Laden gegen die Sample-Länge geprüft (Instrument.cpp, mit Hinweis)
        else if (name == "offset")          { if (parseInt64 (value, i)) r.offset = i; }
        else if (name == "end")             { if (parseInt64 (value, i)) r.end = i; }
        else if (name == "loop_start" || name == "loopstart") { if (parseInt64 (value, i)) r.loopStart = i; }
        else if (name == "loop_end"   || name == "loopend")   { if (parseInt64 (value, i)) r.loopEnd = i; }
        else if (name == "loop_mode"  || name == "loopmode")
        {
            const std::string v = toLower (value);
            if      (v == "no_loop")         r.loopMode = LoopMode::NoLoop;
            else if (v == "one_shot")        r.loopMode = LoopMode::OneShot;
            else if (v == "loop_continuous") r.loopMode = LoopMode::Continuous;
            else if (v == "loop_sustain")    r.loopMode = LoopMode::Sustain;
            else result.warnings.push_back ("Unknown loop_mode '" + value + "'");
        }
        else if (name == "trigger")
        {
            const std::string v = toLower (value);
            if      (v == "attack")                       r.trigger = Trigger::Attack;
            else if (v == "release" || v == "release_key") r.trigger = Trigger::Release;
            else if (v == "first")                        r.trigger = Trigger::First;
            else if (v == "legato")                       r.trigger = Trigger::Legato;
            else result.warnings.push_back ("Unknown trigger '" + value + "'");
        }
        // Keyswitch-Noten werden ohne octave_offset/note_offset gelesen (wie Sforzando)
        else if (name == "sw_last")         { if (keyswitchNote (value, n, name)) r.swLast = n; }
        else if (name == "sw_lokey")        { if (keyswitchNote (value, n, name)) r.swLoKey = n; }
        else if (name == "sw_hikey")        { if (keyswitchNote (value, n, name)) r.swHiKey = n; }
        else if (name == "sw_default")      { if (keyswitchNote (value, n, name)) r.swDefault = n; }
        else if (name == "seq_length")      { if (parseInt64 (value, i)) r.seqLength = clampInt (i, 1, 1000); }
        else if (name == "seq_position")    { if (parseInt64 (value, i)) r.seqPosition = clampInt (i, 1, 1000); }
        else if (name == "lorand")          { if (parseFloat (value, f)) r.loRand = f; }
        else if (name == "hirand")          { if (parseFloat (value, f)) r.hiRand = f; }
        else if (name == "sw_label" || name.rfind ("sw_label", 0) == 0) { /* nur Beschriftung */ }
        else if ((name.rfind ("locc", 0) == 0 || name.rfind ("hicc", 0) == 0) && name.size() > 4
                 && std::isdigit ((unsigned char) name[4]))
        {
            // loccN / hiccN: Controller-Bedingung
            int64_t ccNumber = 0, v = 0;
            if (parseInt64 (name.substr (4), ccNumber) && ccNumber >= 0 && ccNumber <= 127 && parseInt64 (value, v))
            {
                auto it = std::find_if (r.ccConditions.begin(), r.ccConditions.end(),
                                        [&] (const Region::CcCondition& c) { return c.cc == (int) ccNumber; });
                if (it == r.ccConditions.end())
                {
                    r.ccConditions.push_back ({ (int) ccNumber, 0, 127 });
                    it = r.ccConditions.end() - 1;
                }
                const int clamped = (int) std::min<int64_t> (127, std::max<int64_t> (0, v));
                if (name[0] == 'l') it->lo = clamped; else it->hi = clamped;
            }
            else
            {
                result.warnings.push_back ("Invalid controller condition '" + name + "=" + value + "'");
            }
        }
        else if (name == "polyphony")       { if (parseInt64 (value, i)) r.polyphony = clampInt (i, 0, 1000); }
        else if (name == "group")           { if (parseInt64 (value, i)) r.group = clampInt (i, -100000, 100000); }
        else if (name == "off_by")          { if (parseInt64 (value, i)) r.offBy = clampInt (i, -100000, 100000); }
        // Überblendungen per Velocity und Controller (andere xf-Opcodes, z. B. per Taste, bleiben unbeachtet)
        else if (name == "xfin_lovel" || name == "xfin_hivel" || name == "xfout_lovel" || name == "xfout_hivel")
        {
            if (parseInt64 (value, i))
            {
                const int v127 = (int) std::min<int64_t> (127, std::max<int64_t> (0, i));
                if      (name == "xfin_lovel")  r.xfinLoVel = v127;
                else if (name == "xfin_hivel")  r.xfinHiVel = v127;
                else if (name == "xfout_lovel") r.xfoutLoVel = v127;
                else                            r.xfoutHiVel = v127;
            }
        }
        else if (name == "xf_velcurve")     r.xfVelPower = toLower (value) != "gain";
        else if (name == "xf_cccurve")      r.xfCcPower = toLower (value) != "gain";
        else if ((name.rfind ("xfin_locc", 0) == 0 || name.rfind ("xfin_hicc", 0) == 0
                  || name.rfind ("xfout_locc", 0) == 0 || name.rfind ("xfout_hicc", 0) == 0)
                 && std::isdigit ((unsigned char) name.back()))
        {
            const bool isIn = name[2] == 'i';
            const size_t digits = isIn ? 9 : 10;              // Länge von "xfin_locc" bzw. "xfout_locc"
            const bool isLo = name[isIn ? 5 : 6] == 'l';
            int64_t ccNumber = 0, v = 0;
            if (parseInt64 (name.substr (digits), ccNumber) && ccNumber >= 0 && ccNumber <= 127 && parseInt64 (value, v))
            {
                auto it = std::find_if (r.ccCrossfades.begin(), r.ccCrossfades.end(),
                                        [&] (const Region::CcCrossfade& c) { return c.cc == (int) ccNumber; });
                if (it == r.ccCrossfades.end())
                {
                    r.ccCrossfades.push_back ({ (int) ccNumber, 0, 0, 127, 127 });
                    it = r.ccCrossfades.end() - 1;
                }
                const int clamped = (int) std::min<int64_t> (127, std::max<int64_t> (0, v));
                if (isIn) { if (isLo) it->inLo = clamped; else it->inHi = clamped; }
                else      { if (isLo) it->outLo = clamped; else it->outHi = clamped; }
            }
            else
            {
                result.warnings.push_back ("Invalid crossfade '" + name + "=" + value + "'");
            }
        }
        else if (isKnownIgnoredOpcode (name))
        {
            ignored.insert (name);
        }
        else
        {
            unknown (name, regionLine);
        }
    }

    void flushRegion()
    {
        if (current != Header::Region)
            return;

        Region r;
        r.lineNumber = regionLine;
        r.polyphonyGroup = groupIndex;   // ohne group=: je <group>-Header (Märmels großzügige Auslegung)

        for (const auto* ops : { &globalOps, &masterOps, &groupOps, &regionOps })
            for (const auto& op : *ops)
                apply (r, op.first, op.second);
        // Mit group= zählt die Gruppen-Polyphonie über diese Nummer (wie in der SFZ-Spezifikation), auch über
        // mehrere <group>-Header hinweg; eigener Zahlenbereich, damit sie nicht mit den Header-Indizes kollidiert
        if (r.group != 0)
            r.polyphonyGroup = 1000000 + r.group;

        // Ragt der Tastenbereich über 0 ... 127 hinaus (z. B. "lokey=c-2" je nach octave_offset
        // bzw. Notenzählung), wird er begrenzt. Liegt er komplett außerhalb, ist die Region
        // nicht spielbar und wird ausgelassen.
        if (r.hiKey < 0 || r.loKey > 127 || r.loKey > r.hiKey)
        {
            result.warnings.push_back ("Region outside key range 0-127, skipped (line "
                                       + std::to_string (regionLine) + ")");
        }
        else if (r.samplePath.empty())
        {
            result.warnings.push_back ("Region without sample (line " + std::to_string (regionLine) + ")");
        }
        else
        {
            r.loKey = std::max (0, r.loKey);
            r.hiKey = std::min (127, r.hiKey);
            r.samplePath = defaultPath + r.samplePath;
            result.regions.push_back (std::move (r));
        }

        regionOps.clear();
        current = Header::None;
    }
};

} // namespace

bool SfzParser::parseNote (const std::string& valueRaw, int octaveOffset, int noteOffset, int& result)
{
    const std::string v = toLower (trim (valueRaw));
    if (v.empty())
        return false;

    // Reine Zahl
    if (std::isdigit ((unsigned char) v[0]) || (v[0] == '-' && v.size() > 1 && std::isdigit ((unsigned char) v[1])))
    {
        int64_t n = 0;
        if (! parseInt64 (v, n))
            return false;
        n = std::min<int64_t> (1000, std::max<int64_t> (-1000, n));   // Unsinnswerte begrenzen (kein Überlauf)
        result = (int) n + 12 * octaveOffset + noteOffset;
        return true;
    }

    static const int pitchClass[] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    if (v[0] < 'a' || v[0] > 'g')
        return false;

    int pc = pitchClass[v[0] - 'a'];
    size_t pos = 1;

    if (pos < v.size() && v[pos] == '#')
    {
        ++pc;
        ++pos;
    }
    else if (pos < v.size() && v[pos] == 'b' && pos + 1 < v.size()
             && (std::isdigit ((unsigned char) v[pos + 1]) || v[pos + 1] == '-'))
    {
        --pc;
        ++pos;
    }

    int64_t octave = 0;
    if (pos >= v.size() || ! parseInt64 (v.substr (pos), octave))
        return false;
    if (octave < -10 || octave > 20)
        return false;   // keine MIDI-Oktave (verhindert auch einen Zahlenüberlauf bei Unsinnswerten)

    // octave_offset / note_offset verschieben laut sfzformat.com die *eingehenden* MIDI-Noten.
    // Gleichwertig: das Mapping wird um denselben Betrag in die Gegenrichtung verschoben.
    result = (int) ((octave + 1) * 12 + pc) + 12 * octaveOffset + noteOffset;
    return true;
}

ParsedInstrument SfzParser::parse (const std::string& text)
{
    ParserState state;
    const std::string clean = stripComments (text);

    size_t lineStart = 0;
    int lineNumber = 0;

    while (lineStart <= clean.size())
    {
        size_t lineEnd = clean.find ('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = clean.size();

        const std::string line = clean.substr (lineStart, lineEnd - lineStart);
        ++lineNumber;

        size_t pos = 0;
        while (pos < line.size())
        {
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                ++pos;
            if (pos >= line.size())
                break;

            if (line[pos] == '<')
            {
                const size_t close = line.find ('>', pos);
                if (close == std::string::npos)
                {
                    state.result.warnings.push_back ("Incomplete header (line " + std::to_string (lineNumber) + ")");
                    break;
                }
                state.header (trim (line.substr (pos + 1, close - pos - 1)), lineNumber);
                pos = close + 1;
                continue;
            }

            if (line[pos] == '#')
            {
                state.result.warnings.push_back ("#define/#include are not supported yet (line "
                                                 + std::to_string (lineNumber) + ")");
                break;
            }

            const size_t eq = line.find ('=', pos);
            if (eq == std::string::npos)
            {
                state.result.warnings.push_back ("Unrecognised text (line " + std::to_string (lineNumber) + ")");
                break;
            }

            const std::string name = trim (line.substr (pos, eq - pos));
            const size_t next = findNextToken (line, eq + 1);
            const std::string value = trim (line.substr (eq + 1, next == std::string::npos ? std::string::npos
                                                                                           : next - eq - 1));
            state.opcode (name, value, lineNumber);

            pos = (next == std::string::npos) ? line.size() : next;
        }

        if (lineEnd >= clean.size())
            break;
        lineStart = lineEnd + 1;
    }

    state.finish();
    return std::move (state.result);
}

} // namespace maermel
