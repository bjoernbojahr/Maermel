// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// ParameterIds.h – Märmel
// Kurze interne IDs für die sechs Parameter, die GarageBand als Smart Controls zeigen soll.
//
// Hintergrund (festgestellt am 12.10.2026): Audio Units und VST3 kennen Parameter nur über Nummern. JUCE bildet
// die Nummer aus der internen ID als Prüfsumme (String::hashCode: jedes Zeichen × 31 aufaddiert). GarageBand
// zeigt bei Plugins ohne eigene Zuordnung die Parameter mit den KLEINSTEN Nummern, aufsteigend von links nach
// rechts. Kurze IDs ergeben sehr kleine Nummern: "g1" = 3242 … "g6" = 3247, "cutoff" dagegen über eine Milliarde.
// So erscheinen genau diese sechs, in dieser Reihenfolge: Cutoff, Resonance, Drive, Filter Env, Glide, Lo-Fi.
//
// Die Nummern sind ab der Veröffentlichung fest (Automation in DAW-Projekten hängt daran) – nicht mehr ändern.
// tools/paramidtest.cpp prüft, dass kein anderer Parameter eine kleinere Nummer hat.
//
// In SFZP-Dateien und kopierten Einstellungen stehen weiter die lesbaren Namen ("cutoff" …); presetName() und
// idFromPresetName() setzen beim Schreiben und Lesen um. Gelesen werden beide Formen.

#pragma once

#include <cstring>

namespace maermel::ids
{

inline constexpr const char* cutoff    = "g1";
inline constexpr const char* resonance = "g2";
inline constexpr const char* drive     = "g3";
inline constexpr const char* filterEnv = "g4";
inline constexpr const char* glide     = "g5";
inline constexpr const char* lofi      = "g6";

struct Alias { const char* name; const char* id; };   // lesbarer Name (SFZP, bis 12.10.2026 auch ID) → kurze ID
inline constexpr Alias kAliases[] = {
    { "cutoff",    cutoff },
    { "resonance", resonance },
    { "drive",     drive },
    { "filterenv", filterEnv },
    { "glide",     glide },
    { "crush",     lofi },
};

// Interne ID → Name in SFZP-Dateien (alle anderen Parameter: unverändert)
inline const char* presetName (const char* id) noexcept
{
    for (const auto& a : kAliases)
        if (std::strcmp (a.id, id) == 0)
            return a.name;
    return id;
}

// Name aus einer SFZP-Datei (oder einem älteren DAW-Projekt) → interne ID
inline const char* idFromPresetName (const char* name) noexcept
{
    for (const auto& a : kAliases)
        if (std::strcmp (a.name, name) == 0)
            return a.id;
    return name;
}

} // namespace maermel::ids
