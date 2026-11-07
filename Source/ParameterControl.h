// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// ParameterControl.h – Märmel
// Direkte Steuerung von Parametern per MIDI-Controller (vier Zeilen im PATH-Dialog).
// Ausdrücklich keine Modulationsmatrix: Ein Controller bewegt einen Knopf über seinen ganzen Weg,
// als würde man mit der Maus daran drehen (gleiche Kurve wie der Fader).
//
// Gemeinsam genutzt von Prozessor (Ausführung) und Einstellungsdialog (Anzeige), damit beide
// dieselbe Zielliste und dieselbe Konfliktregel verwenden.

#pragma once

#include <array>
#include <string>

#include "ParameterIds.h"

namespace maermel::control
{

constexpr int kSlots = 4;

struct Target
{
    const char* key;        // gespeicherter Name (stabil)
    const char* paramId;    // Parameter in Märmel ("" = kein Ziel)
    const char* section;    // Abschnitt im Menü
    const char* name;       // Eintrag im Menü
    const char* display;    // Name im Display (wie beim Drehen mit der Maus)
    int half;               // 0 = ganzer Weg; −1 = Poly-Hälfte, +1 = Mono-Hälfte des Routing-Faders
};

// Reihenfolge wie im Konzeptpapier: Cutoff, Resonance, Volume, Drive, Vel Env, Hüllkurven, LFO, zuletzt Lo-Fi/Chorus.
// Gespeichert wird der Schlüssel (key), nicht die Position – neue Einträge dürfen also dazwischen stehen.
// Menünamen mit Abschnitt („Amp Attack“), damit die geschlossene Auswahl zeigt, welche Hüllkurve gemeint ist.
// (Amp Pan entfiel am 07.10.2026; gespeicherte Zuordnungen auf „amppan“ werden zu „off“)
inline constexpr Target kTargets[] = {
    { "none",     "",             "",                "off",                "",                 0 },
    { "cutoff",   ids::cutoff,    "Filter",          "Cutoff",             "CUTOFF",           0 },
    { "resonance", ids::resonance,"Filter",          "Resonance",          "RESO",             0 },
    { "volume",   "volume",       "Amp",             "Volume",             "VOLUME",           0 },
    { "drive",    ids::drive,     "Filter",          "Drive",              "DRIVE",            0 },
    // Vel Env nur im positiven Bereich (26.10.2026, Wunsch eines Key-Users): Controller 0 = 0, 127 = Maximum – wie die
    // LFO-Hälften, weil sich ein Controller schlecht genau auf die Mitte stellen lässt
    { "fvelenv",  "filtervel",    "Filter",          "Vel Env (positive)", "VEL ENV",         +1 },
    { "attack",   "attack",       "Amp Envelope",    "Amp Attack",         "AMP ENV A",        0 },
    { "decay",    "decay",        "Amp Envelope",    "Amp Decay",          "AMP ENV D",        0 },
    { "sustain",  "sustain",      "Amp Envelope",    "Amp Sustain",        "AMP ENV S",        0 },
    { "release",  "release",      "Amp Envelope",    "Amp Release",        "AMP ENV R",        0 },
    { "fattack",  "fattack",      "Filter Envelope", "Filter Attack",      "F.ENV A",          0 },
    { "fdecay",   "fdecay",       "Filter Envelope", "Filter Decay",       "F.ENV D",          0 },
    { "fsustain", "fsustain",     "Filter Envelope", "Filter Sustain",     "F.ENV S",          0 },
    { "frelease", "frelease",     "Filter Envelope", "Filter Release",     "F.ENV R",          0 },
    { "plforate", "plforate",     "LFO",             "Poly LFO Rate",      "POLY LFO RATE",    0 },
    { "mlforate", "mlforate",     "LFO",             "Mono LFO Rate",      "MONO LFO RATE",    0 },
    { "ppitch",   "lfopitch",     "LFO",             "Poly LFO > Pitch",   "POLY LFO PITCH",  -1 },
    { "pfilter",  "lfofilter",    "LFO",             "Poly LFO > Filter",  "POLY LFO FILTER", -1 },
    { "pvolume",  "lfovolume",    "LFO",             "Poly LFO > Volume",  "POLY LFO VOLUME", -1 },
    { "ppan",     "lfopan",       "LFO",             "Poly LFO > Pan",     "POLY LFO PAN",    -1 },
    { "mpitch",   "lfopitch",     "LFO",             "Mono LFO > Pitch",   "MONO LFO PITCH",  +1 },
    { "mfilter",  "lfofilter",    "LFO",             "Mono LFO > Filter",  "MONO LFO FILTER", +1 },
    { "mvolume",  "lfovolume",    "LFO",             "Mono LFO > Volume",  "MONO LFO VOLUME", +1 },
    { "mpan",     "lfopan",       "LFO",             "Mono LFO > Pan",     "MONO LFO PAN",    +1 },
    { "crush",    ids::lofi,      "Amp",             "Lo-Fi / Chorus",     "LO-FI",            0 },   // Anzeige je nach Effekt (Editor)
};
constexpr int kNumTargets = (int) (sizeof (kTargets) / sizeof (kTargets[0]));

inline int targetIndex (const std::string& key)
{
    for (int i = 0; i < kNumTargets; ++i)
        if (key == kTargets[i].key)
            return i;
    return 0;
}

// Gespeicherte Einstellungen (global, pro Rechner)
inline std::string keyCc (int slot)     { return "paramCtrl" + std::to_string (slot + 1) + "Cc"; }
inline std::string keyTarget (int slot) { return "paramCtrl" + std::to_string (slot + 1) + "Target"; }
inline std::string keyJump (int slot)   { return "paramCtrl" + std::to_string (slot + 1) + "Jump"; }

// Controller, die nie zugewiesen werden: Modulationsrad, Sustain, Kanal-Modus-Meldungen (120–127)
inline bool isAssignable (int cc) noexcept
{
    return cc >= 0 && cc < 120 && cc != 1 && cc != 64;
}

// Konfliktregel (Konzeptpapier „Doppelte Belegungen“):
//  - ein Controller auf mehrere Ziele: erlaubt
//  - mehrere Zeilen auf dasselbe Ziel (auch beide Hälften desselben LFO-Faders): Konflikt, es gilt die obere
//  - Controller der Instrumentumschaltung: Konflikt
// active[i]   = Zeile wirkt;  conflict[i] = Zeile ist belegt, aber unwirksam (rot)
inline void evaluate (const std::array<int, kSlots>& cc, const std::array<int, kSlots>& target,
                      int switchNextCC, int switchPrevCC,
                      std::array<bool, kSlots>& active, std::array<bool, kSlots>& conflict)
{
    for (int i = 0; i < kSlots; ++i)
    {
        active[(size_t) i] = false;
        conflict[(size_t) i] = false;
        const int c = cc[(size_t) i], t = target[(size_t) i];
        if (c < 0 || t <= 0 || t >= kNumTargets)
            continue;                                           // nicht belegt
        bool bad = ! isAssignable (c) || c == switchNextCC || c == switchPrevCC;
        for (int j = 0; j < i && ! bad; ++j)
            if (active[(size_t) j] && std::string (kTargets[target[(size_t) j]].paramId) == kTargets[t].paramId)
                bad = true;                                     // Ziel schon von einer oberen Zeile gesteuert
        active[(size_t) i] = ! bad;
        conflict[(size_t) i] = bad;
    }
}

// Controllerwert 0 … 127 → normalisierte Parameterstellung 0 … 1 (wie die Faderstellung)
inline float normalisedFor (const Target& t, int value) noexcept
{
    const float v = (float) value / 127.0f;
    if (t.half < 0) return 0.5f - 0.5f * v;    // Poly-Hälfte: von der Mitte nach links
    if (t.half > 0) return 0.5f + 0.5f * v;    // Mono-Hälfte: von der Mitte nach rechts
    return v;
}

} // namespace maermel::control
