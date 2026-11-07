// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// ErrorLog.h – Märmel
// Schreibt Fehler und Hinweise (z. B. fehlende Samples, Ladefehler) in eine Textdatei,
// statt sie nur auf der Oberfläche zu zeigen. Der Pfad steht in den Einstellungen (global, gespeichert);
// der Schalter "Fehler ins Logfile schreiben" gilt wie Live-Edit nur für die jeweilige Instanz, solange
// sie offen ist (wird nicht gespeichert). Jede Meldung wird mit Datum, Uhrzeit und Instrumentnamen angehängt;
// die Datei wird nie überschrieben. Aufrufbar vom Message-Thread und vom Loader-Thread.

#pragma once

#include <JuceHeader.h>

struct ErrorLog
{
    static constexpr const char* kPathKey    = "logFilePath";
    static constexpr const char* kEnabledKey = "logErrors";   // früher gespeichert; wird nur noch entfernt

    // Gültig und beschreibbar: vorhandene Datei mit Schreibrecht, oder neue Datei in einem
    // beschreibbaren Ordner
    static bool isWritable (const juce::File& file)
    {
        if (file == juce::File() || file.isDirectory())
            return false;
        if (file.existsAsFile())
            return file.hasWriteAccess();
        const juce::File folder = file.getParentDirectory();
        return folder.isDirectory() && folder.hasWriteAccess();
    }

    static juce::File getFile (juce::PropertiesFile& settings)
    {
        const juce::String path = settings.getValue (kPathKey);
        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }

    static bool isEnabled (juce::PropertiesFile& settings, bool active)
    {
        return active && isWritable (getFile (settings));
    }

    // Eine Meldung anhängen (mehrzeilige Meldungen werden eingerückt)
    // active: Schalter der Instanz (MaermelAudioProcessor::isLogErrorsActive)
    // header: Version, Build-Zeitpunkt, Plattform und Plug-in-Format (wie in der About-Box). Wird als Kopfzeile
    // geschrieben, sobald in dieser Sitzung zum ersten Mal oder von einer anderen Fassung/einem anderen Format
    // ins Logfile geschrieben wird – so verrät jedes Logfile, welche Fassung auf welchem System lief.
    static void write (juce::PropertiesFile& settings, bool active, const juce::String& header,
                       const juce::String& instrument, const juce::String& text)
    {
        if (text.isEmpty() || ! isEnabled (settings, active))
            return;

        const juce::File file = getFile (settings);
        const juce::String time = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
        juce::String entry = time + "  " + (instrument.isNotEmpty() ? instrument : juce::String ("-")) + "  "
                           + text.trim().replace ("\n", "\n                     ") + "\n";

        static juce::CriticalSection lock;   // gleichzeitige Meldungen nicht vermischen
        const juce::ScopedLock sl (lock);

        static juce::String lastHeader;       // je Programmsitzung (DAW-Prozess)
        if (header.isNotEmpty() && header != lastHeader)
        {
            entry = "\n=== " + header + " ===\n" + entry;
            lastHeader = header;
        }
        juce::FileOutputStream out (file);    // hängt an das Ende an, legt die Datei bei Bedarf an
        if (out.openedOk())
        {
            out.writeText (entry, false, false, "\n");
            out.flush();
        }
    }
};
