// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SfzParser.cpp – Märmel Engine

#include "SfzParser.h"
#include "FileUtil.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <utility>

namespace maermel
{
namespace
{

// Ein Opcode mit seiner Zeile in der Datei (seit 23.11.2026: Meldungen nennen die Zeile des Opcodes selbst, nicht
// die der Region – ein fehlerhafter Wert in <group> wird so dort gefunden, wo er steht)
struct Opcode
{
    std::string name, value;
    int line = 0;
};
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
// Zeilenenden vereinheitlichen: \r\n (Windows) → \n, einzelnes \r (klassisches Mac OS) → \n.
// Seit 23.11.2026 als eigener Schritt vor dem Entfernen der Kommentare. Vorher wurde jedes \r zu \n, aus \r\n
// wurden also ZWEI Zeilenumbrüche – alle Zeilenangaben in „Loading notes“ lagen dann grob beim Doppelten. Bei
// reinen \r-Dateien hätte ein //-Kommentar außerdem bis zum nächsten \n gereicht, also bis zum Dateiende.
std::string unifyLineEnds (const std::string& text)
{
    std::string out;
    out.reserve (text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\r')
        {
            if (i + 1 < text.size() && text[i + 1] == '\n')
                continue;               // \r vor \n: weglassen, das \n folgt im nächsten Durchlauf
            out.push_back ('\n');      // einzelnes \r: als Zeilenende werten
            continue;
        }
        out.push_back (text[i]);
    }
    return out;
}

// Kommentare entfernen (/* … */ und // bis Zeilenende); Zeilenumbrüche bleiben erhalten, damit die Zeilennummern
// stimmen. Erwartet einheitliche Zeilenenden (unifyLineEnds).
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

        out.push_back (text[i]);
    }

    return out;
}

// Sieht ein Name wie ein SFZ-Opcode aus? Nur Kleinbuchstaben, Ziffern und Unterstrich, beginnend mit einem
// Buchstaben (alle Opcodes der SFZ-Beschreibung sind so geschrieben). Benutzt nur, wenn zwischen Name und '='
// Leerzeichen stehen – dann soll z. B. ein Wort aus einem Dateinamen nicht als Opcode gelten.
bool looksLikeOpcodeName (const std::string& line, size_t from, size_t to)
{
    if (to <= from || ! (line[from] >= 'a' && line[from] <= 'z'))
        return false;
    for (size_t i = from; i < to; ++i)
    {
        const char c = line[i];
        if (! ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

// Beginnt nach dem Leerraum ab Position 'i' (Leerzeichen/Tab) ein Header oder Opcode? Liefert dessen Anfang,
// sonst npos. Als Opcode gilt:
//   "name=" – Name direkt vor '=' (wie bisher, beliebige Groß-/Kleinschreibung), oder
//   "name =" / "name  =  wert" – Leerzeichen oder Tabs zwischen Name und '=' (seit 1.0.3; manche handgeschriebenen
//   Dateien, z. B. „key =66“). Dann nur mit einem Namen, der wie ein Opcode aussieht (looksLikeOpcodeName).
size_t tokenAfterSpace (const std::string& line, size_t i)
{
    size_t j = i;
    while (j < line.size() && (line[j] == ' ' || line[j] == '\t'))
        ++j;
    if (j == i || j >= line.size())
        return std::string::npos;          // kein Leerraum davor bzw. Zeilenende
    if (line[j] == '<')
        return j;

    size_t k = j;
    while (k < line.size() && isNameChar (line[k]))
        ++k;
    if (k == j)
        return std::string::npos;

    size_t m = k;
    while (m < line.size() && (line[m] == ' ' || line[m] == '\t'))
        ++m;
    if (m >= line.size() || line[m] != '=')
        return std::string::npos;
    if (m == k || looksLikeOpcodeName (line, j, k))
        return j;
    return std::string::npos;
}

// Findet ab 'from' den Beginn des nächsten Opcodes oder Headers ("<").
// Ein Opcode beginnt nur nach einem Leerzeichen, damit '=' in Werten nicht stört.
size_t findNextToken (const std::string& line, size_t from)
{
    for (size_t i = from; i < line.size(); ++i)
    {
        if (line[i] == '<')
            return i;
        if (line[i] == ' ' || line[i] == '\t')
        {
            const size_t token = tokenAfterSpace (line, i);
            if (token != std::string::npos)
                return token;
        }
    }
    return std::string::npos;
}

// Geschützter Anfang eines Pfad-Werts (seit 1.0.3): Bis zu dieser Position wird im Wert NICHT nach dem nächsten
// Opcode gesucht – so bleiben Dateinamen mit Leerzeichen und '=' ganz („Piano = Soft.wav“, „Mix a=b.wav“).
//   sample=       bis hinter die Audio-Endung (.wav, .aif, .aiff, .flac – Groß/klein egal), wenn danach das
//                 Zeilenende oder der nächste Opcode bzw. Header folgt. Die erste solche Stelle gilt, so dass auch
//                 ein Ordner wie „Alt.wav Dateien/“ nicht stört.
//   default_path= bis hinter den Ordner-Trenner ('/' oder '\\'), wenn danach dasselbe gilt.
// Ohne passende Stelle (z. B. "*sine" oder ein Pfad ohne Endung) gibt es keinen Schutz – Suche ab Wertbeginn wie bisher.
size_t protectedPathEnd (const std::string& line, size_t valueStart, const std::string& nameLower)
{
    const bool isSample = (nameLower == "sample");
    if (! isSample && nameLower != "default_path")
        return valueStart;

    const char* extensions[] = { ".wav", ".aiff", ".aif", ".flac" };
    for (size_t i = valueStart; i < line.size(); ++i)
    {
        size_t endOfPath = std::string::npos;
        if (isSample)
        {
            for (const char* ext : extensions)
            {
                const size_t n = std::strlen (ext);
                if (i + n <= line.size() && toLower (line.substr (i, n)) == ext)
                {
                    endOfPath = i + n;
                    break;
                }
            }
        }
        else if (line[i] == '/' || line[i] == '\\')
        {
            endOfPath = i + 1;
        }
        if (endOfPath == std::string::npos)
            continue;

        // Passt nur, wenn danach nichts mehr kommt (Leerraum bis Zeilenende) oder der nächste Opcode/Header
        size_t rest = endOfPath;
        while (rest < line.size() && (line[rest] == ' ' || line[rest] == '\t'))
            ++rest;
        if (rest >= line.size() || line[rest] == '<' || tokenAfterSpace (line, endOfPath) != std::string::npos)
            return endOfPath;
    }
    return valueStart;
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
    bool warnedPreprocessor = false;   // Warnung zu #define/#include schon ausgegeben (nur einmal je Datei)

    void header (const std::string& nameRaw, int line)
    {
        flushRegion();
        const std::string name = toLower (nameRaw);

        if      (name == "control") current = Header::Control;
        else if (name == "global")  { current = Header::Global; globalOps.clear(); masterOps.clear(); groupOps.clear(); templateValid = false; }
        else if (name == "master")  { current = Header::Master; masterOps.clear(); groupOps.clear(); templateValid = false; }
        else if (name == "group")   { current = Header::Group;  groupOps.clear(); ++groupIndex; templateValid = false; }
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
            case Header::Global:  globalOps.push_back ({ name, value, line }); templateValid = false; break;
            case Header::Master:  masterOps.push_back ({ name, value, line }); templateValid = false; break;
            case Header::Group:   groupOps.push_back ({ name, value, line });  templateValid = false; break;
            case Header::Region:  regionOps.push_back ({ name, value, line }); break;
            case Header::Other:   break;
            case Header::None:
                warnOnce ("Opcode '" + name + "' before the first header (line " + std::to_string (line) + ")");
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
    int opLine = 0;                       // Zeile des gerade angewendeten Opcodes (für Meldungen)
    std::set<std::string> ignored, reportedHeaders, reportedUnknown, reportedWarnings;

    // Fertig angewendete Werte aus <global>, <master> und <group> – gemeinsame Vorlage für alle Regionen dieser
    // Gruppe (seit 23.11.2026). Vorher wurden die oberen Ebenen für JEDE Region neu ausgewertet: bei großen
    // Bibliotheken viele tausend String-Vergleiche mehr, und jede Warnung aus <group> erschien einmal je Region.
    // Ungültig, sobald sich eine obere Ebene ändert (neuer Header, neuer Opcode dort, octave_offset/note_offset);
    // neu aufgebaut wird sie erst bei der nächsten Region. Das Ergebnis ist gleich, weil die Region-Opcodes wie
    // bisher zuletzt angewendet werden und damit gewinnen.
    Region groupTemplate;
    bool templateValid = false;


    // Eine Warnung nur einmal ausgeben (gleicher Text = gleiche Stelle in der Datei)
    void warnOnce (const std::string& text)
    {
        if (reportedWarnings.insert (text).second)
            result.warnings.push_back (text);
    }

    std::string atLine() const { return " (line " + std::to_string (opLine) + ")"; }

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
            templateValid = false;                // Noten in der Vorlage neu lesen
        }
        else if (name == "note_offset")
        {
            int64_t v = 0;
            if (parseInt64 (value, v)) noteOffset = (int) std::min<int64_t> (127, std::max<int64_t> (-127, v));
            templateValid = false;
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
        warnOnce ("Invalid keyswitch note '" + value + "' in " + name + atLine());
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

        warnOnce ("Invalid note '" + value + "' in " + name + atLine());
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
            else warnOnce ("Unknown loop_mode '" + value + "'" + atLine());
        }
        else if (name == "trigger")
        {
            const std::string v = toLower (value);
            if      (v == "attack")                       r.trigger = Trigger::Attack;
            else if (v == "release" || v == "release_key") r.trigger = Trigger::Release;
            else if (v == "first")                        r.trigger = Trigger::First;
            else if (v == "legato")                       r.trigger = Trigger::Legato;
            else warnOnce ("Unknown trigger '" + value + "'" + atLine());
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
                warnOnce ("Invalid controller condition '" + name + "=" + value + "'" + atLine());
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
                warnOnce ("Invalid crossfade '" + name + "=" + value + "'" + atLine());
            }
        }
        else if (isKnownIgnoredOpcode (name))
        {
            ignored.insert (name);
        }
        else
        {
            unknown (name, opLine);
        }
    }

    void flushRegion()
    {
        if (current != Header::Region)
            return;

        // Vorlage aus den oberen Ebenen (nur neu, wenn sich dort etwas geändert hat – siehe groupTemplate)
        if (! templateValid)
        {
            groupTemplate = Region();
            for (const auto* ops : { &globalOps, &masterOps, &groupOps })
                for (const auto& op : *ops)
                {
                    opLine = op.line;
                    apply (groupTemplate, op.name, op.value);
                }
            templateValid = true;
        }

        Region r = groupTemplate;        // Kopie: Werte aus <global>/<master>/<group> schon enthalten
        r.lineNumber = regionLine;
        r.polyphonyGroup = groupIndex;   // ohne group=: je <group>-Header (Märmels großzügige Auslegung)
        for (const auto& op : regionOps)
        {
            opLine = op.line;
            apply (r, op.name, op.value);
        }
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
        else if (r.samplePath[0] == '*')
        {
            // Eingebaute Generatoren der SFZ-Spezifikation (seit 23.11.2026): *silence ist verbreitet als Platzhalter,
            // z. B. für stumme Keyswitch- oder Release-Regionen – stillschweigend auslassen (klingt ja ohnehin nicht).
            // *sine, *saw, *square, *triangle, *noise … kann Märmel nicht erzeugen: einmal melden, aber nicht als
            // fehlendes Sample zählen (sonst erschiene „MISSING SAMPLES“, obwohl das Instrument in Ordnung ist).
            if (toLower (r.samplePath) != "*silence")
                warnOnce ("Built-in generator '" + r.samplePath + "' is not supported, region skipped");
        }
        else
        {
            if (r.seqLength > 1 && r.seqPosition > r.seqLength)   // bei seq_length 1 gilt seq_position nicht (spielt immer)
                warnOnce ("seq_position " + std::to_string (r.seqPosition) + " is greater than seq_length "
                          + std::to_string (r.seqLength) + " - region never plays (line " + std::to_string (regionLine) + ")");
            r.loKey = std::max (0, r.loKey);
            r.hiKey = std::min (127, r.hiKey);
            // default_path nur vor relative Pfade setzen (seit 23.11.2026): vor "/Users/…" oder "C:/…" ergäbe es
            // einen unsinnigen Pfad wie "Samples//Users/…"
            if (! isAbsolutePath (r.samplePath))
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
    // UTF-8-Kennung (BOM, Bytes EF BB BF) am Anfang überspringen (seit 23.11.2026): Sie ist unsichtbar, würde aber den
    // ersten Header verdecken – „<control>“ würde nicht erkannt, default_path ginge verloren und alle Samples
    // fehlten. Ältere Windows-Editoren (Notepad) schreiben sie.
    const bool hasBom = text.size() >= 3 && (unsigned char) text[0] == 0xEF
                        && (unsigned char) text[1] == 0xBB && (unsigned char) text[2] == 0xBF;
    const std::string clean = stripComments (unifyLineEnds (hasBom ? text.substr (3) : text));

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
                // Einmal je Datei statt je Zeile (seit 1.0.3): Bibliotheken mit vielen #define-Zeilen erzeugten sonst
                // eine lange Liste gleicher Warnungen. Unterstützung von #define/#include ist für später vorgemerkt.
                if (! state.warnedPreprocessor)
                    state.result.warnings.push_back ("#define/#include are not supported yet (first in line "
                                                     + std::to_string (lineNumber) + ")");
                state.warnedPreprocessor = true;
                break;
            }

            const size_t eq = line.find ('=', pos);
            if (eq == std::string::npos)
            {
                state.result.warnings.push_back ("Unrecognised text (line " + std::to_string (lineNumber) + ")");
                break;
            }

            const std::string name = trim (line.substr (pos, eq - pos));
            // Bei sample= und default_path= erst hinter dem Pfad nach dem nächsten Opcode suchen (protectedPathEnd)
            const size_t next = findNextToken (line, protectedPathEnd (line, eq + 1, toLower (name)));
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
