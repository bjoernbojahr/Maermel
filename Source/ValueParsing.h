// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// ValueParsing.h – Märmel
// Rückübersetzung: eingetippter Text → Parameterwert (Gegenstück zu den Anzeigetexten).
// Genutzt von den Eingabefeldern unter den Reglern und von DAWs (z. B. Automationsspur in Logic).
//
// Großzügig: Einheiten dürfen fehlen, Dezimalkomma ist erlaubt, Groß-/Kleinschreibung egal.
// Ohne Einheit gilt die übliche Einheit des Parameters (Zeiten: ms, Cutoff: Hz, Start: ms).
// Reines C++ ohne JUCE, damit es sich unabhängig testen lässt.

#pragma once

#include "engine/SfzTypes.h"   // kSampleStartMaxMs

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace maermel::parse
{

inline std::string normalize (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    std::replace (s.begin(), s.end(), ',', '.');
    return s;
}

inline bool contains (const std::string& s, const char* word) { return s.find (word) != std::string::npos; }

// Erste Zahl im Text (mit Vorzeichen), z. B. "  -6,5 dB" → -6.5; ok = false, wenn keine Zahl.
// 'unit' erhält den Text direkt hinter der Zahl (ohne Leerzeichen), z. B. "db", "ms", "khz".
inline double number (const std::string& text, bool& ok, std::string* unit = nullptr)
{
    const std::string s = normalize (text);
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (std::isdigit ((unsigned char) c) || ((c == '-' || c == '+' || c == '.') && i + 1 < s.size()
                                                 && (std::isdigit ((unsigned char) s[i + 1]) || s[i + 1] == '.')))
        {
            char* end = nullptr;
            const double v = std::strtod (s.c_str() + i, &end);
            ok = end != s.c_str() + i;
            if (unit != nullptr)
            {
                size_t u = (size_t) (end - s.c_str());
                while (u < s.size() && std::isspace ((unsigned char) s[u])) ++u;
                *unit = s.substr (u);
            }
            return v;
        }
    }
    ok = false;
    return 0.0;
}

inline bool startsWith (const std::string& s, const char* prefix) { return s.rfind (prefix, 0) == 0; }

inline double clamp (double v, double lo, double hi) { return std::min (hi, std::max (lo, v)); }

// Zeit in Sekunden: "500" / "500 ms" → 0,5; "0,5 s" / "2s" / "2 sek" → Sekunden.
// Maßgeblich ist die Einheit direkt hinter der Zahl; ohne Einheit gelten Millisekunden.
inline double seconds (const std::string& text, bool& ok)
{
    std::string unit;
    const double v = number (text, ok, &unit);
    if (! ok) return 0.0;
    const bool isSeconds = ! startsWith (unit, "ms") && startsWith (unit, "s");
    return isSeconds ? v : v / 1000.0;
}

// --- Hüllkurvenzeiten (10 s × x^4,2) und Glide (5 s × x^4,2): Zeit → Reglerstellung (Umkehrung der Kennlinie) ---
inline constexpr double kTimeCurveExponent = 4.2;   // wie maermel::Envelope::kTimeCurveExponent
inline float timeKnob (const std::string& text, double maxSeconds, float fallback)
{
    const std::string s = normalize (text);
    if (contains (s, "aus") || contains (s, "off"))
        return 0.0f;
    bool ok = false;
    const double sec = seconds (s, ok);
    if (! ok) return fallback;
    return (float) std::pow (clamp (sec, 0.0, maxSeconds) / maxSeconds, 1.0 / kTimeCurveExponent);
}

// --- Prozent: "75 %" / "75" → 0,75 ---
inline float percent (const std::string& text, float fallback, double lo = 0.0, double hi = 1.0)
{
    bool ok = false;
    const double v = number (text, ok);
    return ok ? (float) clamp (v / 100.0, lo, hi) : fallback;
}

// --- LFO-Routing: "Poly 30 %" → -0,3; "Mono 30 %" → +0,3; "-30" → -0,3 ---
inline float routing (const std::string& text, float fallback)
{
    const std::string s = normalize (text);
    bool ok = false;
    double v = number (s, ok);
    if (! ok) return (contains (s, "aus") || contains (s, "off")) ? 0.0f : fallback;
    if (contains (s, "poly")) v = -std::fabs (v);
    if (contains (s, "mono")) v = std::fabs (v);
    return (float) clamp (v / 100.0, -1.0, 1.0);
}

// --- Dezibel: "-6 dB" / "-6" → -6; "-inf" → Minimum ---
inline float decibels (const std::string& text, float minimum, float maximum, float fallback)
{
    const std::string s = normalize (text);
    if (contains (s, "inf") || contains (s, "aus") || contains (s, "off"))
        return minimum;
    bool ok = false;
    const double v = number (s, ok);
    return ok ? (float) clamp (v, minimum, maximum) : fallback;
}

// --- Cutoff: "440" / "440 Hz" / "1,5 kHz" → Reglerstellung (20 Hz × 1000^x) ---
inline float cutoffKnob (const std::string& text, float fallback)
{
    bool ok = false;
    std::string unit;
    double hz = number (text, ok, &unit);
    if (! ok) return fallback;
    if (startsWith (unit, "k")) hz *= 1000.0;
    return (float) clamp (std::log (clamp (hz, 20.0, 20000.0) / 20.0) / std::log (1000.0), 0.0, 1.0);
}

// --- Filter-Env: "+4,5 Okt" / "-2" → Oktaven / 10 ---
inline float envOctaves (const std::string& text, float fallback)
{
    bool ok = false;
    const double v = number (text, ok);
    return ok ? (float) clamp (v / 10.0, -1.0, 1.0) : fallback;
}

// --- Filter Vel Env: "+5 Okt" / "-5" → ±0,5 (Oktaven / 10, wie Env) ---
inline float velEnv (const std::string& text, float fallback)
{
    bool ok = false;
    const double v = number (text, ok);
    return ok ? (float) clamp (v / 10.0, -1.0, 1.0) : fallback;
}

// --- Sample Start: "37,5 ms" → Reglerstellung (kSampleStartMaxMs × x³); Vel: "+50 ms" / "-50 ms" ---
inline float startKnob (const std::string& text, float fallback)
{
    bool ok = false;
    const double sec = seconds (text, ok);
    if (! ok) return fallback;
    return (float) std::cbrt (clamp (sec * 1000.0, 0.0, kSampleStartMaxMs) / kSampleStartMaxMs);
}

inline float startVelocity (const std::string& text, float fallback)
{
    bool ok = false;
    const double sec = seconds (text, ok);
    if (! ok) return fallback;
    const double ms = clamp (sec * 1000.0, -kSampleStartMaxMs, kSampleStartMaxMs);
    return (float) ((ms < 0 ? -1.0 : 1.0) * std::cbrt (std::fabs (ms) / kSampleStartMaxMs));
}

// --- T (Sustain Time) in Schritten −64 … +64: "-30" → -30/64; "+10" → +10/64; "0" → 0 ---
// "sinkt"/"fällt" oder negatives Vorzeichen = nach unten, sonst nach oben.
inline float sustainTime (const std::string& text, float fallback)
{
    const std::string s = normalize (text);
    bool ok = false;
    const double v = number (s, ok);
    if (! ok) return fallback;
    if (v == 0.0) return 0.0f;
    const bool down = contains (s, "sinkt") || contains (s, "fall") || contains (s, "down") || v < 0.0;
    return (float) ((down ? -1.0 : 1.0) * clamp (std::round (std::fabs (v)) / 64.0, 0.0, 1.0));
}

// --- Cent / Halbtöne: "+25 ct" → 25; "-12" → -12 ---
inline float plainNumber (const std::string& text, double lo, double hi, float fallback)
{
    bool ok = false;
    const double v = number (text, ok);
    return ok ? (float) clamp (v, lo, hi) : fallback;
}

// --- Crush: "13 Bit" / "13" → Reglerstellung (Umkehrung der Bit-Kennlinie); "aus" / "0" → 0 ---
// Kennlinie: 0 … 30 % → 24 … 15 Bit, 30 … 100 % → 15 … 11 Bit
inline float crushAmount (const std::string& text, float fallback)
{
    const std::string s = normalize (text);
    if (contains (s, "aus") || contains (s, "off"))
        return 0.0f;
    bool ok = false;
    const double v = number (s, ok);
    if (! ok) return fallback;
    if (v <= 0.0) return 0.0f;
    const double bits = clamp (v, 11.0, 24.0);
    const double x = bits >= 15.0 ? (24.0 - bits) / 9.0 * 0.3 : 0.3 + (15.0 - bits) / 4.0 * 0.7;
    return (float) (x * 100.0);
}

// --- Amp Pan: "L 50" → -0,5; "R 30" → +0,3; "C" / "Mitte" / "0" → 0; "-50" → -0,5 ---
inline float pan (const std::string& text, float fallback)
{
    const std::string s = normalize (text);
    bool ok = false;
    double v = number (s, ok);
    if (! ok)
        return (contains (s, "c") || contains (s, "mitte")) ? 0.0f : fallback;
    const size_t firstLetter = s.find_first_not_of (" \t+-.0123456789");
    if (firstLetter != std::string::npos && (s[firstLetter] == 'l')) v = -std::fabs (v);
    if (firstLetter != std::string::npos && (s[firstLetter] == 'r')) v = std::fabs (v);
    return (float) clamp (v / 100.0, -1.0, 1.0);
}

// --- LFO-Rate frei: "4 Hz" / "4" → Reglerstellung (0,05 Hz × 600^x) ---
inline float lfoRateKnob (const std::string& text, float fallback)
{
    bool ok = false;
    const double hz = number (text, ok);
    if (! ok) return fallback;
    return (float) clamp (std::log (clamp (hz, 0.05, 30.0) / 0.05) / std::log (600.0), 0.0, 1.0);
}

} // namespace maermel::parse
