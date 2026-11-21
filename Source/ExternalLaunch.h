// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// ExternalLaunch.h – andere Programme starten: Library-Ordner öffnen, Webseite öffnen, SFZ im Editor öffnen
// (seit 20.11.2026).
//
// macOS und Windows: wie bisher über JUCE (Finder/Explorer, Standard-Browser, ChildProcess).
//
// Linux (und BSD): eigener Start, aus demselben Grund wie bei den Datei-Dialogen (siehe FileDialogs.h):
//   1. Ardour (offizielle Fassung) setzt LD_LIBRARY_PATH auf seine mitgelieferten Bibliotheken. Ein gestartetes
//      Programm (xdg-open → Dateimanager/Browser, oder der SFZ-Editor) erbt das und startet dann oft nicht.
//      → Wir geben dem neuen Programm die Umgebung ohne LD_LIBRARY_PATH und LD_PRELOAD mit.
//   2. JUCE startet Programme mit fork(); das kopiert die Seitentabellen des ganzen Host-Prozesses und kann den
//      Audio-Thread kurz anhalten (Hinweis Robin Gareus, Ardour).
//      → Wir benutzen posix_spawn(): glibc erzeugt den Prozess dabei ohne Kopie (wie vfork) und startet das
//        Programm sofort. Offene Dateien des Hosts (Audio-Gerät, MIDI …) werden im neuen Prozess geschlossen.
//
// Alle Funktionen werden nur vom Message-Thread (Oberfläche) aus aufgerufen, nie aus dem Audio-Thread.

#pragma once

#include <JuceHeader.h>

namespace maermel
{
namespace external
{
    // Ordner im Dateimanager öffnen (Finder, Explorer, unter Linux über xdg-open)
    bool openFolder (const juce::File& folder);

    // Webseite im Standard-Browser öffnen
    bool openUrl (const juce::String& url);

   #if JUCE_LINUX || JUCE_BSD
    // Ein Programm mit Argumenten starten, z. B. { "/usr/bin/gedit", "/pfad/datei.sfz" }.
    // Das Programm läuft unabhängig von Märmel weiter. false, wenn es nicht gestartet werden konnte.
    bool startProgram (const juce::StringArray& args);
   #endif
}
}
