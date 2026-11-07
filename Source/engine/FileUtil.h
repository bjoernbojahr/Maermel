// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// FileUtil.h – Märmel
// Kleine Hilfen für Pfade und Dateizugriff, damit die Engine (ohne JUCE) unter macOS, Windows und Linux gleich arbeitet.
//
// Windows: Pfade kommen dort mit "\" (z. B. "C:\Library\Piano.sfz"); beide Schrägstriche gelten als Trenner.
// Außerdem öffnet std::ifstream mit std::string unter Windows in der alten Zeichentabelle (nicht UTF-8) –
// Umlaute in Pfaden ("Flöte.wav") würden nicht gefunden. openForReading öffnet dort über einen UTF-8-Pfad.

#pragma once

#include <fstream>
#include <string>
#include <cstdint>
#include <cctype>
#ifndef _WIN32
 #include <dirent.h>
 #include <sys/stat.h>
#endif

#ifdef _WIN32
 #include <filesystem>
#endif

namespace maermel
{

#ifdef _WIN32
inline constexpr const char* kPathSeparators = "/\\";
inline bool isPathSeparator (char c) noexcept { return c == '/' || c == '\\'; }
#else
inline constexpr const char* kPathSeparators = "/";
inline bool isPathSeparator (char c) noexcept { return c == '/'; }
#endif

// --- Zwei Schreibweisen von Umlauten und Akzenten -------------------------------------------------------
// „ö“ kann in Dateinamen als ein Zeichen (U+00F6, zusammengesetzt) oder als „o“ + kombinierendes Trema
// (U+006F U+0308, zerlegt) gespeichert sein. macOS behandelt beide als gleich, Windows und Linux nicht.
// Wird eine Library auf dem Mac erstellt und auf Windows kopiert, passt die Schreibweise in der SFZ-Datei
// daher manchmal nicht zum Dateinamen auf der Platte. Abgedeckt sind die gängigen lateinischen Buchstaben
// mit Gravis, Akut, Zirkumflex, Tilde, Trema, Ring und Cedille.
namespace unicode
{
struct Composition { char32_t composed, base, mark; };
inline constexpr Composition kCompositions[] = {
    { 0xC0, 'A', 0x300 }, { 0xC1, 'A', 0x301 }, { 0xC2, 'A', 0x302 }, { 0xC3, 'A', 0x303 }, { 0xC4, 'A', 0x308 },
    { 0xC5, 'A', 0x30A }, { 0xC7, 'C', 0x327 }, { 0xC8, 'E', 0x300 }, { 0xC9, 'E', 0x301 }, { 0xCA, 'E', 0x302 },
    { 0xCB, 'E', 0x308 }, { 0xCC, 'I', 0x300 }, { 0xCD, 'I', 0x301 }, { 0xCE, 'I', 0x302 }, { 0xCF, 'I', 0x308 },
    { 0xD1, 'N', 0x303 }, { 0xD2, 'O', 0x300 }, { 0xD3, 'O', 0x301 }, { 0xD4, 'O', 0x302 }, { 0xD5, 'O', 0x303 },
    { 0xD6, 'O', 0x308 }, { 0xD9, 'U', 0x300 }, { 0xDA, 'U', 0x301 }, { 0xDB, 'U', 0x302 }, { 0xDC, 'U', 0x308 },
    { 0xDD, 'Y', 0x301 },
    { 0xE0, 'a', 0x300 }, { 0xE1, 'a', 0x301 }, { 0xE2, 'a', 0x302 }, { 0xE3, 'a', 0x303 }, { 0xE4, 'a', 0x308 },
    { 0xE5, 'a', 0x30A }, { 0xE7, 'c', 0x327 }, { 0xE8, 'e', 0x300 }, { 0xE9, 'e', 0x301 }, { 0xEA, 'e', 0x302 },
    { 0xEB, 'e', 0x308 }, { 0xEC, 'i', 0x300 }, { 0xED, 'i', 0x301 }, { 0xEE, 'i', 0x302 }, { 0xEF, 'i', 0x308 },
    { 0xF1, 'n', 0x303 }, { 0xF2, 'o', 0x300 }, { 0xF3, 'o', 0x301 }, { 0xF4, 'o', 0x302 }, { 0xF5, 'o', 0x303 },
    { 0xF6, 'o', 0x308 }, { 0xF9, 'u', 0x300 }, { 0xFA, 'u', 0x301 }, { 0xFB, 'u', 0x302 }, { 0xFC, 'u', 0x308 },
    { 0xFD, 'y', 0x301 }, { 0xFF, 'y', 0x308 },
};

inline std::u32string decode (const std::string& s)
{
    std::u32string out;
    for (size_t i = 0; i < s.size();)
    {
        const auto c = (unsigned char) s[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + (size_t) len > s.size())
            len = 1;
        char32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < len; ++k)
            cp = (cp << 6) | ((unsigned char) s[i + (size_t) k] & 0x3F);
        out.push_back (cp);
        i += (size_t) len;
    }
    return out;
}

inline std::string encode (const std::u32string& s)
{
    std::string out;
    for (char32_t cp : s)
    {
        if (cp < 0x80)        out += (char) cp;
        else if (cp < 0x800)  { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000){ out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
        else                  { out += (char) (0xF0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 0x3F));
                                out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
    }
    return out;
}

// Zusammengesetzte Schreibweise: „o“ + Trema → „ö“
inline std::string composed (const std::string& utf8)
{
    const auto in = decode (utf8);
    std::u32string out;
    for (size_t i = 0; i < in.size(); ++i)
    {
        bool done = false;
        if (i + 1 < in.size())
            for (const auto& c : kCompositions)
                if (c.base == in[i] && c.mark == in[i + 1]) { out.push_back (c.composed); ++i; done = true; break; }
        if (! done)
            out.push_back (in[i]);
    }
    return encode (out);
}

// Zerlegte Schreibweise: „ö“ → „o“ + Trema
inline std::string decomposed (const std::string& utf8)
{
    std::u32string out;
    for (char32_t cp : decode (utf8))
    {
        bool done = false;
        for (const auto& c : kCompositions)
            if (c.composed == cp) { out.push_back (c.base); out.push_back (c.mark); done = true; break; }
        if (! done)
            out.push_back (cp);
    }
    return encode (out);
}
} // namespace unicode

#ifndef _WIN32
// Rückfall ohne Rücksicht auf Groß/klein (seit 15.10.2026): Linux und Mac-Laufwerke mit Unterscheidung von Groß/klein
// finden "piano_c4.wav" sonst nicht, wenn die Datei "Piano_C4.WAV" heißt – häufig bei älteren SFZ-Dateien (z. B. von
// Cakewalk komplett klein geschrieben). Jeder Pfadteil, der so nicht existiert, wird im übergeordneten Ordner ohne
// Rücksicht auf Groß/klein (ASCII und westeuropäische Sonderzeichen wie Ö/ö) und Unicode-Form gesucht. Liefert den tatsächlichen Pfad oder "" (nicht gefunden).
// Gibt es mehrere Treffer, die sich nur in Groß/klein unterscheiden, gilt der erste. Unter Windows nicht nötig.
inline std::string findIgnoringCase (const std::string& utf8Path)
{
    if (utf8Path.empty())
        return {};
    auto key = [] (const std::string& s)
    {
        std::string k = unicode::composed (s);
        for (size_t i = 0; i < k.size(); ++i)
        {
            const auto c = (unsigned char) k[i];
            if (c < 0x80)
                k[i] = (char) std::tolower (c);
            else if (c == 0xC3 && i + 1 < k.size())          // UTF-8 À … Þ (außer ×) → à … þ, z. B. Ö → ö
            {
                const auto n = (unsigned char) k[i + 1];
                if (n >= 0x80 && n <= 0x9E && n != 0x97)
                    k[i + 1] = (char) (n + 0x20);
                ++i;
            }
        }
        return k;
    };
    const bool absolute = utf8Path[0] == '/';
    std::string built = absolute ? std::string() : std::string (".");
    size_t start = absolute ? 1 : 0;
    while (start <= utf8Path.size())
    {
        size_t end = utf8Path.find ('/', start);
        if (end == std::string::npos)
            end = utf8Path.size();
        const std::string part = utf8Path.substr (start, end - start);
        start = end + 1;
        if (part.empty() || part == ".")
            continue;
        if (part == "..")
        {
            built += "/..";
            continue;
        }
        const std::string direct = built + "/" + part;
        struct stat info;
        if (::stat (direct.c_str(), &info) == 0)
        {
            built = direct;
            continue;
        }
        DIR* dir = ::opendir (built.empty() ? "/" : built.c_str());
        if (dir == nullptr)
            return {};
        const std::string wanted = key (part);
        std::string match;
        while (const dirent* entry = ::readdir (dir))
            if (key (entry->d_name) == wanted)
            {
                match = entry->d_name;
                break;
            }
        ::closedir (dir);
        if (match.empty())
            return {};
        built += "/" + match;
    }
    return built;
}
#endif

// Datei zum Lesen öffnen; utf8Path ist UTF-8 (so kommt er aus JUCE: File::getFullPathName().toStdString()).
// Gelingt es nicht, wird der Pfad zusätzlich in der anderen Schreibweise der Umlaute versucht (siehe oben).
inline void openForReading (std::ifstream& file, const std::string& utf8Path)
{
    auto tryOpen = [&file] (const std::string& p)
    {
        file.clear();
       #ifdef _WIN32
        file.open (std::filesystem::u8path (p), std::ios::binary);
       #else
        // Nur normale Dateien: Unter macOS und Linux lässt sich auch ein ORDNER „öffnen“ (z. B. „sample=Piano
        // Samples/“ ohne Dateinamen); seine „Größe“ wäre dann ein Unsinnswert (gefunden 26.10.2026 beim Test mit
        // zufällig beschädigten SFZ-Dateien). Unter Windows schlägt das Öffnen eines Ordners ohnehin fehl.
        struct stat info {};
        if (::stat (p.c_str(), &info) == 0 && ! S_ISREG (info.st_mode))
            return false;
        file.open (p, std::ios::binary);
       #endif
        return file.is_open();
    };
    if (tryOpen (utf8Path))
        return;
    for (const std::string& alternative : { unicode::composed (utf8Path), unicode::decomposed (utf8Path) })
        if (alternative != utf8Path && tryOpen (alternative))
            return;
   #ifndef _WIN32
    const std::string found = findIgnoringCase (utf8Path);   // Groß/klein in Datei- oder Ordnernamen anders
    if (! found.empty() && found != utf8Path)
        tryOpen (found);
   #endif
}

} // namespace maermel
