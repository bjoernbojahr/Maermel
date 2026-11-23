// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SfzExport.cpp – Märmel Engine

#include "SfzExport.h"
#include "FileUtil.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace maermel
{

namespace
{
constexpr char kOpen = '\x01', kClose = '\x02';   // Platzhalter: \x01<Index>\x02

// Kommentare durch Leerzeichen gleicher Länge ersetzen (Zeilenumbrüche bleiben). So stimmen alle Positionen
// mit dem Originaltext überein, und beim Umschreiben bleiben Kommentare erhalten. '\r' gilt wie im Leser
// als Zeilenende.
std::string maskComments (const std::string& text)
{
    std::string out = text;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == '\r')
        {
            out[i] = '\n';
        }
        else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '*')
        {
            out[i] = out[i + 1] = ' ';
            i += 2;
            while (i + 1 < out.size() && ! (out[i] == '*' && out[i + 1] == '/'))
            {
                if (out[i] != '\n' && out[i] != '\r') out[i] = ' '; else out[i] = '\n';
                ++i;
            }
            if (i + 1 < out.size()) { out[i] = out[i + 1] = ' '; ++i; }
        }
        else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/')
        {
            while (i < out.size() && out[i] != '\n' && out[i] != '\r')
                out[i++] = ' ';
            if (i < out.size()) out[i] = '\n';
        }
    }
    return out;
}

bool isNameChar (char c) { return std::isalnum ((unsigned char) c) || c == '_'; }

// Wie im SFZ-Leser: Beginn des nächsten Opcodes („name=“ nach einem Leerzeichen) oder Headers („<“)
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

bool isAbsolute (const std::string& p)
{
    if (! p.empty() && p[0] == '/')
        return true;
    return p.size() > 2 && std::isalpha ((unsigned char) p[0]) && p[1] == ':' && p[2] == '/';
}

// "/a/./b/../c" → "/a/c" (für das Kopieren über JUCE eindeutige, absolute Pfade)
std::string normalise (const std::string& path)
{
    std::vector<std::string> parts;
    std::string prefix;
    size_t start = 0;
    if (! path.empty() && path[0] == '/')                         { prefix = "/"; start = 1; }
    else if (path.size() > 2 && path[1] == ':' && path[2] == '/') { prefix = path.substr (0, 3); start = 3; }
    while (start <= path.size())
    {
        size_t end = path.find ('/', start);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr (start, end - start);
        if (part == "..")
        {
            if (! parts.empty() && parts.back() != "..") parts.pop_back();
            else if (prefix.empty()) parts.push_back (part);
        }
        else if (! part.empty() && part != ".")
        {
            parts.push_back (part);
        }
        start = end + 1;
    }
    std::string out = prefix;
    for (size_t i = 0; i < parts.size(); ++i)
        out += (i ? "/" : "") + parts[i];
    return out;
}

std::string lower (std::string s)
{
    for (auto& c : s) c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string trimmed (const std::string& s, size_t& begin, size_t& end)
{
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t')) ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t')) --end;
    return s.substr (begin, end - begin);
}
} // namespace

std::string numberedFileName (const std::string& fileName, int number)
{
    if (number <= 1)
        return fileName;
    const size_t dot = fileName.find_last_of ('.');
    const std::string stem = dot == std::string::npos ? fileName : fileName.substr (0, dot);
    const std::string ext = dot == std::string::npos ? std::string() : fileName.substr (dot);
    return stem + " (" + std::to_string (number) + ")" + ext;
}

SfzExportPlan planSfzExport (const std::string& sfzTextIn, const std::string& sfzDirectory)
{
    SfzExportPlan plan;
    // Die Steuerzeichen der Platzhalter (kOpen/kClose) dürfen im Originaltext nicht vorkommen – sonst hielte
    // fillExportText sie für Platzhalter. In SFZ haben sie keine Bedeutung; sie werden zu Leerzeichen
    // (gefunden 26.10.2026 beim Test mit zufällig beschädigten Dateien).
    std::string sfzText = sfzTextIn;
    for (char& c : sfzText)
        if (c == kOpen || c == kClose)
            c = ' ';
    const std::string masked = maskComments (sfzText);

    struct Replacement { size_t begin, end; std::string with; };
    std::vector<Replacement> replacements;
    std::map<std::string, size_t> indexOfSource;       // gleiche Datei nur einmal
    std::map<std::string, int> usedNames;              // Dateinamen im Plan (ohne Groß/klein, wie auf dem Mac)

    bool inControl = false;
    std::string defaultPath;

    size_t lineStart = 0;
    while (lineStart <= masked.size())
    {
        size_t lineEnd = masked.find ('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = masked.size();
        const std::string line = masked.substr (lineStart, lineEnd - lineStart);

        size_t pos = 0;
        while (pos < line.size())
        {
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
            if (pos >= line.size()) break;

            if (line[pos] == '<')
            {
                const size_t close = line.find ('>', pos);
                if (close == std::string::npos) break;
                size_t b = pos + 1, e = close;
                inControl = lower (trimmed (line, b, e)) == "control";
                pos = close + 1;
                continue;
            }
            if (line[pos] == '#')
            {
                plan.error = "#define and #include are not supported - cannot export";
                return plan;
            }
            const size_t eq = line.find ('=', pos);
            if (eq == std::string::npos) break;

            size_t nb = pos, ne = eq;
            const std::string name = lower (trimmed (line, nb, ne));
            const size_t next = findNextToken (line, eq + 1);
            size_t vb = eq + 1, ve = (next == std::string::npos) ? line.size() : next;
            std::string value = trimmed (line, vb, ve);

            if (inControl && name == "default_path")
            {
                // Wie im Leser merken; im neuen Text entfällt die Angabe (alle Pfade zeigen in den Samples-Ordner)
                defaultPath = value;
                std::replace (defaultPath.begin(), defaultPath.end(), '\\', '/');
                if (! defaultPath.empty() && defaultPath.back() != '/') defaultPath.push_back ('/');
                // Steht die Angabe allein in ihrer Zeile, verschwindet die ganze Zeile (keine Leerzeile im Export)
                const std::string original = sfzText.substr (lineStart, lineEnd - lineStart);   // mit Kommentaren
                const bool aloneInLine = original.find_first_not_of (" \t\r", ve) == std::string::npos
                                         && original.find_first_not_of (" \t") == nb;
                if (aloneInLine)
                {
                    size_t end = lineEnd;                                   // Zeilenumbruch mitnehmen (\n oder \r\n)
                    if (end < sfzText.size() && sfzText[end] == '\r') ++end;
                    if (end < sfzText.size() && sfzText[end] == '\n') ++end;
                    replacements.push_back ({ lineStart, end, std::string() });
                }
                else
                {
                    replacements.push_back ({ lineStart + nb, lineStart + ve, std::string() });
                }
            }
            else if (name == "sample" && ! value.empty() && value[0] != '*')   // *sine usw.: eingebaute Klänge
            {
                std::replace (value.begin(), value.end(), '\\', '/');
                const std::string full = isAbsolute (value) ? value : defaultPath + value;
                const std::string source = normalise (isAbsolute (full) ? full : sfzDirectory + full);

                auto found = indexOfSource.find (source);
                size_t index;
                if (found != indexOfSource.end())
                {
                    index = found->second;
                }
                else
                {
                    const size_t slash = source.find_last_of ('/');
                    const std::string base = slash == std::string::npos ? source : source.substr (slash + 1);
                    int number = 1;
                    while (usedNames.count (lower (numberedFileName (base, number))) != 0) ++number;
                    const std::string fileName = numberedFileName (base, number);
                    usedNames[lower (fileName)] = 1;
                    index = plan.samples.size();
                    plan.samples.push_back ({ source, fileName });
                    indexOfSource[source] = index;
                }
                replacements.push_back ({ lineStart + vb, lineStart + ve,
                                          std::string (1, kOpen) + std::to_string (index) + std::string (1, kClose) });
            }
            pos = (next == std::string::npos) ? line.size() : next;
        }

        if (lineEnd >= masked.size()) break;
        lineStart = lineEnd + 1;
    }

    // Ersetzungen von hinten nach vorn in den Originaltext übernehmen (Positionen bleiben gültig)
    std::string out = sfzText;
    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it)
        out.replace (it->begin, it->end - it->begin, it->with);

    plan.text = out;
    plan.ok = true;
    return plan;
}

std::string fillExportText (const SfzExportPlan& plan, const std::string& samplesFolder,
                            const std::vector<std::string>& finalNames)
{
    // Seit 15.10.2026 (Wunsch von Björn): Alle Samples liegen im Samples-Ordner – der gemeinsame Teil steht einmal
    // als default_path im <control>-Bereich, die Regionen nennen nur noch den Dateinamen (besser lesbar).
    std::string out;
    out.reserve (plan.text.size() + 32 * finalNames.size() + samplesFolder.size() + 32);
    for (size_t i = 0; i < plan.text.size(); ++i)
    {
        if (plan.text[i] == kOpen)
        {
            // Platzhalter \x01<Index>\x02 – Ziffern von Hand lesen (keine Ausnahme bei unerwartetem Inhalt)
            size_t j = i + 1, index = 0;
            bool digits = false;
            while (j < plan.text.size() && plan.text[j] >= '0' && plan.text[j] <= '9' && index < 1000000)
            {
                index = index * 10 + (size_t) (plan.text[j] - '0');
                digits = true;
                ++j;
            }
            if (digits && j < plan.text.size() && plan.text[j] == kClose && index < plan.samples.size())
            {
                out += (index < finalNames.size() ? finalNames[index] : plan.samples[index].fileName);
                i = j;
            }
            // sonst: kein gültiger Platzhalter (kann nach planSfzExport nicht vorkommen) – Zeichen weglassen
        }
        else
        {
            out += plan.text[i];
        }
    }
    if (plan.samples.empty())
        return out;

    // default_path einsetzen: direkt hinter den ersten Header, wenn er <control> ist, sonst als eigener
    // <control>-Block vor den ersten Header (Kopfkommentare bleiben oben). Die alte Angabe hat planSfzExport entfernt.
    const std::string nl = out.find ("\r\n") != std::string::npos ? "\r\n" : "\n";   // Zeilenende wie in der Datei
    const std::string line = "default_path=" + samplesFolder + "/";
    const std::string masked = maskComments (out);
    const size_t open = masked.find ('<');
    if (open == std::string::npos)
        return line + nl + out;
    const size_t close = masked.find ('>', open);
    std::string header = close == std::string::npos ? std::string() : masked.substr (open + 1, close - open - 1);
    header = lower (header);
    header.erase (std::remove_if (header.begin(), header.end(), [] (char c) { return std::isspace ((unsigned char) c); }), header.end());
    if (header == "control")
    {
        size_t rest = close + 1;                       // Leerzeichen hinter <control> (z. B. nach entferntem default_path)
        while (rest < out.size() && (out[rest] == ' ' || out[rest] == '\t')) ++rest;
        const bool lineEnds = rest >= out.size() || out[rest] == '\n' || out[rest] == '\r';
        if (lineEnds)
            out.erase (close + 1, rest - close - 1);
        out.insert (close + 1, nl + line + (lineEnds ? "" : nl));
    }
    else
    {
        out.insert (open, "<control>" + nl + line + nl + nl);
    }
    return out;
}

} // namespace maermel
