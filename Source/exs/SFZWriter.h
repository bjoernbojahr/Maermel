// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SFZWriter.h
// Erzeugt aus einem exs::Instrument den SFZ-Text und hilft bei der Suche
// nach den Sample-Dateien. Ohne Dateisystem-Abhängigkeit: Die Existenzprüfung
// übergibt der Aufrufer als Funktion (z. B. mit juce::File), damit der Code
// auch auf älteren macOS-Versionen ohne std::filesystem läuft.

#pragma once

#include "EXSInstrument.h"
#include <functional>
#include <string>
#include <vector>

namespace exs
{

struct SfzWriterOptions
{
    std::string sourceName;             // Originaldateiname für den Kopfkommentar
    std::string generator;              // z. B. App-Name und Version
    std::string defaultPath;            // leer = kein <control>-Block
    bool usePitchKeycenter = true;      // sonst key=
    bool includeLoops = true;           // false = Loop-Angaben auskommentiert
    bool emitLoopTune = false;          // true = eigener Opcode loop_tune, sonst nur Kommentar
    std::string newline = "\n";
};

std::string writeSfz (const Instrument& inst, const SfzWriterOptions& options);

// --- Gruppe und Zone zusammenführen (seit 30.10.2026) ---
//
// Im EXS24 wirken Gruppe und Zone ZUSAMMEN, in SFZ ERSETZT ein Wert an der Region den Wert der Gruppe.
// Darum schreibt der Writer an jede Region die wirksamen Werte und an die Gruppe keine dieser Werte mehr.
// Regeln (von Björn im EXS24, in Dimension Pro 1.5, sforzando und Märmel nachgeprüft, 29.10.2026):
//   - Tasten- und Velocity-Bereich: Schnittmenge aus Gruppe und Zone (die Gruppe schränkt ein)
//   - Volume: Gruppe + Zone (dB addieren)
//   - Pan: zwei Balance-Regler hintereinander. Je Regler: links = 1 bei Pan <= 0, sonst 1 - Pan/100;
//          rechts = 1 bei Pan >= 0, sonst 1 + Pan/100. Gruppe und Zone werden multipliziert.
//          Daraus EIN SFZ-Pan (Verhältnis von links und rechts) plus Lautstärke-Korrektur (lauterer Kanal).
// Dieselben Regeln gelten für Björns Sample Mapper, EXS2SFZ und das interne Kommandozeilen-Programm;
// die Testwerte stehen im Konzeptpapier (Offene Punkte) und in tests/exsmixtest.cpp.

struct CombinedZone
{
    int keyLow = 0, keyHigh = 127;
    int velocityLow = 0, velocityHigh = 127;
    double volumeDb = 0.0;          // Gruppe + Zone + Pan-Korrektur, begrenzt auf kMinVolumeDb … kMaxVolumeDb
    double pan = 0.0;               // -100 … +100

    bool keyRangeEmpty = false;     // Tastenbereiche von Gruppe und Zone überschneiden sich nicht
    bool velocityRangeEmpty = false;
    bool silentByPan = false;       // Gruppe und Zone schneiden zusammen beide Kanäle ab (z. B. +100 / -100)
    bool volumeClamped = false;     // Summe lag außerhalb des SFZ-Bereichs

    bool playable() const noexcept { return ! keyRangeEmpty && ! velocityRangeEmpty && ! silentByPan; }
};

constexpr double kMinVolumeDb = -144.0;   // wie Märmels SFZ-Leser (SfzParser.cpp)
constexpr double kMaxVolumeDb = 24.0;

// group darf nullptr sein (Zone ohne Gruppe)
CombinedZone combineGroupAndZone (const Group* group, const Zone& zone);

// Kurze Beschreibung, warum eine Zone nie erklingt oder begrenzt wurde (leer, wenn alles in Ordnung ist);
// für „Loading notes“ und als Kommentar im SFZ. Ohne '=' und ohne '<' '>' (Kompatibilität mit anderen Playern).
std::string describeCombineProblem (const Group* group, const Zone& zone, const CombinedZone& c);

// --- Suche nach den Sample-Dateien (entspricht den vier Radio-Buttons) ---

enum class SamplePathStrategy
{
    NextToExs,              // Samples liegen direkt neben der EXS-Datei
    ExsFolderPlusSubfolder, // EXS-Ordner + letzter Ordnername aus dem Originalpfad
    OriginalPath,           // absoluter Originalpfad aus der EXS-Datei
    CustomFolder            // vom Nutzer gewählter Ordner
};

struct SamplePathResult
{
    std::vector<std::string> fullPaths;  // pro Sample, gleiche Reihenfolge wie inst.samples
    std::vector<bool> exists;
    int found = 0, missing = 0;
    std::string suggestedDefaultPath;    // für SfzWriterOptions::defaultPath
};

// Pfade mit '/' als Trenner übergeben.
SamplePathResult resolveSamplePaths (const Instrument& inst,
                                     const std::string& exsFolder,
                                     SamplePathStrategy strategy,
                                     const std::string& customFolder,
                                     const std::function<bool (const std::string&)>& fileExists);

} // namespace exs
