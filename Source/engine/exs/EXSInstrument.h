// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// EXSInstrument.h
// Reines Datenmodell eines eingelesenen EXS24-Instruments.
// Keine Abhängigkeit von JUCE oder einer Oberfläche – nutzbar im Player,
// in Kommandozeilen-Tools und in den Web-Tools (z. B. über Emscripten).
//
// Herkunft des Formatwissens: ermittelt durch Analyse selbst erstellter EXS-Dateien (Björn Bojahrs eigene
// Library, ursprünglich vor über 20 Jahren); kein Code und keine Dokumentation von Apple verwendet, nichts
// dekompiliert. Zweck ist allein die Kompatibilität (Import des Sample-Mappings). EXS24 und Logic sind Marken
// von Apple Inc.; Märmel steht in keiner Verbindung zu Apple.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace exs
{

// Logic löscht Objekte nicht physisch aus der Datei, sondern markiert sie
// mit 0x0B an Offset 12 des Chunks. Sie bleiben als Platzhalter im Array,
// damit die Indizes (zone.groupIndex, zone.sampleIndex) gültig bleiben.

struct Zone
{
    std::string name;
    bool deleted = false;

    int rootKey = 60;
    int keyLow = 0, keyHigh = 127;

    bool velocityEnabled = false;          // Status-Bit 8: Velocity-Zone aktiv
    int velocityLow = 0, velocityHigh = 127;

    int transpose = 0;                     // Halbtöne (EXS "Coarse")
    int tuneCents = 0;                     // Cent (EXS "Fine")
    int volumeDb = 0;
    int pan = 0;                           // -100 … +100 wie SFZ (verifiziert an "AcKeyb St. Grandpiano 30.exs")

    bool oneShot = false;                  // Status-Bit 1
    bool pitchDisabled = false;            // Status-Bit 2 -> pitch_keytrack=0
    bool reverse = false;                  // Status-Bit 4 -> direction=reverse
    bool outputActive = false;             // Status-Bit 64: eigener EXS-Ausgang
    int output = 0;                        // Byte 150: 0 = Main, 1 = 3/4, 2 = 5/6 ...
    int volumeScale = 0;                   // Byte 73: EXS "Scale" (Einheit noch unklar)

    bool loopEnabled = false;
    bool loopEqualPower = false;           // Loop-Modus 3 = Loop + Equal Power
    int64_t loopCrossfade = 0;             // Byte 96: Auto Crossfade Length (Einheit noch unklar)
    int loopTune = 0;                      // EXS Loop Tune (in SFZ nicht standardisiert)

    int64_t startFrame = 0, endFrame = 0; // endFrame: je nach Ersteller inklusiv oder exklusiv
    int64_t loopStart = 0, loopEnd = 0;   // loopEnd exklusiv (wie im EXS gespeichert)

    int groupIndex = -1;                   // -1 = keine Gruppe
    int sampleIndex = -1;
};

enum class SelectMode
{
    None,
    KeySwitch,          // Rohwert 1
    Controller,         // Rohwert 3
    Other               // z. B. 2 = Select by Group, 4 = Pitch Bend (noch nicht umgesetzt)
};

struct Group
{
    std::string name;
    bool deleted = false;

    int volumeDb = 0;
    int pan = 0;                           // -100 … +100 wie SFZ
    int polyphony = 0;                     // EXS "Voices", 0 = unbegrenzt
    int velocityLow = 0, velocityHigh = 127;
    int keyLow = 0, keyHigh = 127;

    bool triggerOnRelease = false;

    // Relative Offsets der Gruppe (Rohwerte, Einheiten noch zu klären)
    int cutoffOffset = 0, resonanceOffset = 0;
    int attackOffset = 0, decayOffset = 0, sustainOffset = 0, releaseOffset = 0;

    SelectMode selectMode = SelectMode::None;
    int selectModeRaw = 0;
    int keySwitchNote = 0;
    int controllerNumber = 0;
    int controllerLow = 0, controllerHigh = 127;
};

struct Sample
{
    std::string name;
    bool deleted = false;
    std::string path;                      // Originalpfad vom Rechner des Erstellers
    int64_t lengthFrames = 0;
    int sampleRate = 0;
    int64_t fileSizeBytes = 0;
    std::string formatTag;                 // "WAVE" oder "AIFF"
    int bitDepth = 0;
    int channels = 0;
    int64_t dataOffset = 0;                // Beginn der Audiodaten in der Datei
};

struct Instrument
{
    std::string name;                      // interner Instrumentenname aus dem Header
    std::vector<Zone> zones;
    std::vector<Group> groups;
    std::vector<Sample> samples;

    bool bigEndian = false;
    bool usedSignatureTypes = false;       // true = Chunk-Typ aus Signatur, false = Positionslogik
    int declaredZones = 0, declaredGroups = 0, declaredSamples = 0;

    std::vector<std::string> warnings;
};

} // namespace exs
