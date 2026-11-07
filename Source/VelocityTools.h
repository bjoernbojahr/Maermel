// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// VelocityTools.h – Märmel
// Anpassung der eingehenden Velocity, bevor sie die Engine erreicht:
//   1. Kurve (global in den Einstellungen, für das eigene Masterkeyboard)
//   2. Offset (pro Instrument, auf der Oberfläche, im Preset gespeichert)
// Reines C++ ohne JUCE, damit es sich unabhängig testen lässt.

#pragma once

#include <algorithm>
#include <cmath>

namespace maermel::velocity
{

// Kurve: -1 ... +1 (Anzeige -100 ... +100). 0 = unverändert.
// Negativ: empfindlicher bei leisem Spiel (kleine Anschläge ergeben höhere Werte).
// Positiv: verlangt kräftigeres Spiel. Die Endpunkte 1 und 127 bleiben fest.
// Umsetzung: Potenzkurve mit Exponent 2^Kurve, also 0,5 … 2.
inline int applyCurve (int velocity, float curve) noexcept
{
    velocity = std::min (127, std::max (1, velocity));
    curve = std::min (1.0f, std::max (-1.0f, curve));
    if (curve == 0.0f)
        return velocity;
    const float exponent = std::exp2 (curve);
    const float x = (float) (velocity - 1) / 126.0f;
    return 1 + (int) std::lround (126.0f * std::pow (x, exponent));
}

// Offset: -32 ... +32, Ergebnis immer 1 ... 127
inline int applyOffset (int velocity, int offset) noexcept
{
    return std::min (127, std::max (1, velocity + offset));
}

// Beides in der richtigen Reihenfolge; Velocity 0 bleibt "Note aus" und wird nicht verändert
inline int transform (int velocity, float curve, int offset) noexcept
{
    if (velocity <= 0)
        return 0;
    return applyOffset (applyCurve (velocity, curve), offset);
}

} // namespace maermel::velocity
