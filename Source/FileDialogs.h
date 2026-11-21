// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// FileDialogs.h – welcher Datei-Dialog für Laden, Exportieren und die Einstellungen benutzt wird (seit 20.11.2026).
//
// macOS und Windows: der Dialog des Betriebssystems (wie bisher) – vertraut und mit allen Favoriten des Nutzers.
//
// Linux (und BSD): JUCEs eigener Dateibrowser, im Plugin-Fenster gezeichnet. Ein "nativer" Dialog ist unter Linux
// kein Teil des Systems, sondern ein eigenes Programm (zenity oder kdialog), das JUCE als Kindprozess startet.
// Das hat in DAWs zwei Probleme (Diskussion im Ardour-Forum, November 2026, u. a. Paul Davis und Robin Gareus):
//   1. Ardour bringt eigene Bibliotheken mit und setzt LD_LIBRARY_PATH. Das gestartete zenity/kdialog erbt diese
//      Umgebung, findet die falschen Bibliotheken und startet nicht – der Dialog erscheint einfach nicht.
//   2. Ein Kindprozess wird mit fork() erzeugt; dabei wird der ganze Speicher des Host-Prozesses verwaltet,
//      was die Echtzeit-Audioverarbeitung kurz anhalten kann (Aussetzer).
// JUCEs eigener Dateibrowser startet kein anderes Programm und hat beide Probleme nicht.

#pragma once

#include <JuceHeader.h>

namespace maermel
{
#if JUCE_LINUX || JUCE_BSD
    constexpr bool kUseNativeFileDialogs = false;
#else
    constexpr bool kUseNativeFileDialogs = true;
#endif
}
