// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SfzParser.h – Märmel Engine
// Liest den Text einer SFZ-Datei und löst die Vererbung
// <global> → <master> → <group> → <region> auf.

#pragma once

#include "SfzTypes.h"
#include <string>

namespace maermel
{

class SfzParser
{
public:
    static ParsedInstrument parse (const std::string& text);

    // Notennummer (z. B. "60") oder Notenname (z. B. "c4", "f#1", "eb-1"),
    // gezählt nach SFZ-Spezifikation mit C4 = 60. octave_offset / note_offset werden wie bei sfizz und Sforzando
    // auf die Tastenwerte der Datei ADDIERT: Wert + 12 × octave_offset + note_offset (sfzformat.com: mit
    // octave_offset=-1 wird C4 zur MIDI-Note 48). Bis 13.10.2026 war das Vorzeichen vertauscht.
    // Keyswitch-Noten werden ohne Offsets gelesen (wie Sforzando).
    // Gibt false zurück, wenn der Wert nicht lesbar ist.
    static bool parseNote (const std::string& value, int octaveOffset, int noteOffset, int& result);
};

} // namespace maermel
