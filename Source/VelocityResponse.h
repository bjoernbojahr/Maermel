// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// VelocityResponse.h – Märmel Engine
// Standard-Velocity-Kurve als Formel:
//
//     Amplitude a(x) = 0,035 + 0,965 · x^1,75     mit x = Velocity / 127
//
// Velocity 1 liegt damit 29,1 dB unter Velocity 127.
//
// response (v) ist dieselbe Kurve, umgerechnet auf 0 (Velocity 1) … 1 (Velocity 127), gleichmäßig in dB:
//     response = 1 − log a(x) / log a(1/127)
// Sie gilt einheitlich für die Lautstärke (Pegel = −Vel-Bereich × (1 − response); bei 29,1 dB genau a(x)),
// die Filter-Velocity (Vel, Vel Env) und VelA/VelD. Die globale Velocity-Kurve in den Einstellungen
// (Masterkeyboard) wirkt davor: 0 = genau diese Kurve.

#pragma once

#include <algorithm>
#include <cmath>

namespace maermel
{

inline constexpr float kVelocityFloor    = 0.035f;   // Sockel für leise Anschläge
inline constexpr float kVelocityExponent = 1.75f;

inline float velocityAmplitude (int velocity) noexcept
{
    const float x = (float) std::min (127, std::max (1, velocity)) / 127.0f;
    return kVelocityFloor + (1.0f - kVelocityFloor) * std::pow (x, kVelocityExponent);
}

inline float velocityResponse (int velocity) noexcept
{
    if (velocity <= 1)
        return 0.0f;
    static const float logAtOne = std::log (velocityAmplitude (1));
    return 1.0f - std::log (velocityAmplitude (velocity)) / logAtOne;
}

} // namespace maermel
