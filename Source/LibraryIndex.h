// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// LibraryIndex.h – Märmel
// Verzeichnis aller SFZ-Instrumente unterhalb des Library-Ordners.
//
// - Läuft in einem eigenen Hintergrund-Thread, nie im Audio-Thread.
// - Wird von allen Märmel-Instanzen eines Programms gemeinsam genutzt
//   (juce::SharedResourcePointer<LibraryIndex>).
// - Ordner der Form "<Instrument> Samples" neben "<Instrument>.sfz" werden nicht durchsucht.
// - Aktualisierung pro Ordner: Hat sich das Änderungsdatum eines Ordners nicht geändert,
//   wird sein Inhalt aus dem Zwischenspeicher übernommen, ohne ihn neu aufzulisten.
// - Die Struktur wird in einer Cache-Datei gespeichert, damit das Menü beim Start sofort da ist.

#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

class LibraryIndex : public juce::ChangeBroadcaster,
                     private juce::Thread
{
public:
    struct Folder
    {
        juce::String name;
        juce::File directory;
        juce::int64 modified = 0;              // Änderungsdatum des Ordners (ms)
        std::vector<Folder> subfolders;        // natürlich sortiert, auch leere (für den Abgleich)
        juce::Array<juce::File> instruments;   // .sfz- und .exs-Dateien (EXS nur ohne gleichnamiges SFZ), natürlich sortiert

        int countInstruments() const;          // inklusive Unterordner
    };

    LibraryIndex();
    ~LibraryIndex() override;

    // Library-Ordner (wird in den Einstellungen gespeichert)
    juce::File getRootFolder() const;
    void setRootFolder (const juce::File& folder);

    // Neu einlesen anstoßen (läuft im Hintergrund, schnell bei unveränderten Ordnern)
    void requestRefresh();

    // Aktueller Stand; nullptr, solange noch nichts eingelesen wurde
    std::shared_ptr<const Folder> getSnapshot() const;
    bool isScanning() const noexcept { return scanning.load(); }

    // Hilfsfunktionen für Menü und Pfeiltasten -------------------------------
    // Gefilterte Kopie: nur Instrumente, deren Name 'text' enthält (Groß-/Kleinschreibung egal),
    // und nur Ordner mit Treffern. Leerer Text = alles.
    static std::shared_ptr<const Folder> filtered (const std::shared_ptr<const Folder>& root, const juce::String& text);

    // Alle Instrumente in Menü-Reihenfolge
    static juce::Array<juce::File> flatten (const Folder& root);

    // Ordner mit genau diesem Verzeichnis suchen (nullptr, wenn nicht vorhanden)
    static const Folder* findFolder (const Folder& root, const juce::File& directory);

    juce::PropertiesFile& getSettings() { return *settings; }

    // EXS-Instrumente anzeigen (Instrumenten-Menü, Blättern, LOAD)? Standard: aus. Der Index enthält sie immer;
    // ausgeblendet wird erst beim Anzeigen, damit das Umschalten sofort wirkt.
    static constexpr const char* kShowExsKey = "showExs";
    bool isShowingExs() { return settings->getBoolValue (kShowExsKey, false); }

    // Liste, wie sie der Nutzer sieht (ohne EXS, wenn ausgeschaltet)
    std::shared_ptr<const Folder> getVisibleSnapshot();

private:
    void run() override;
    Folder scanFolder (const juce::File& dir, const Folder* cached);
    bool isSampleFolderOf (const juce::File& dir) const;

    void loadCache();
    void saveCache (const Folder& root);

    std::unique_ptr<juce::PropertiesFile> settings;
    juce::File cacheFile;

    mutable juce::CriticalSection lock;
    std::shared_ptr<const Folder> snapshot;
    juce::File rootFolder;
    std::atomic<bool> scanning { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryIndex)
};
