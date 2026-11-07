// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// veltest.cpp – Märmel: zeigt Velocity-Kurve und Offset als Tabelle
#include "../Source/VelocityTools.h"
#include <cstdio>
#include <initializer_list>
using namespace maermel::velocity;
int main()
{
    std::printf ("Kurve      ");
    for (int v : { 1, 10, 20, 40, 64, 90, 110, 127 }) std::printf ("%5d", v);
    std::printf ("   (eingehende Velocity)\n");
    for (float c : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        std::printf ("%+5.0f      ", c * 100);
        for (int v : { 1, 10, 20, 40, 64, 90, 110, 127 }) std::printf ("%5d", applyCurve (v, c));
        std::printf ("\n");
    }
    std::printf ("\nOffset: 126 + 1 = %d, 127 + 5 = %d, 20 - 32 = %d, 1 - 1 = %d, Velocity 0 (Note aus) + 10 = %d\n",
                 transform (126, 0, 1), transform (127, 0, 5), transform (20, 0, -32), transform (1, 0, -1), transform (0, 0, 10));
    std::printf ("Piano nur unterstes Layer (z. B. 1-60): Offset -67 nicht möglich, -32 → 127 wird %d\n", transform (127, 0, -32));
}
