// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// InstrumentLocator.h – Märmel Engine
// Instrument eines DAW-Projekts wiederfinden, wenn der gespeicherte Pfad auf diesem Rechner nicht existiert
// (z. B. Library an anderer Stelle). Der Prozessor versucht zuerst den vollständigen Pfad, dann den Pfad relativ
// zur Library; erst danach sucht er in der Library nach Dateien mit gleichem Namen und wählt hier unter ihnen aus.
//
// Fingerabdruck: die Dateinamen der verwendeten Samples (ohne Ordner, klein geschrieben, als 32-Bit-Kennzahlen).
// Er übersteht Änderungen an Opcodes und einzelne ergänzte oder entfernte Regionen, unterscheidet aber ein
// gleichnamiges Instrument mit anderen Samples (z. B. „Piano.sfz“ aus einer fremden Library).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace maermel
{

// Sortierte, eindeutige Kennzahlen der Sample-Dateinamen eines Instruments (.sfz oder .exs); leer, wenn die Datei
// nicht lesbar ist oder keine Samples nennt. Liest nur den Text, lädt keine Samples.
std::vector<uint32_t> sampleFingerprint (const std::string& instrumentPath);
std::vector<uint32_t> sampleFingerprintFromText (const std::string& sfzText);

// Für das DAW-Projekt: Kennzahlen als Hex-Text, durch Leerzeichen getrennt (höchstens kMaxFingerprint Einträge)
constexpr size_t kMaxFingerprint = 512;
std::string fingerprintToString (const std::vector<uint32_t>& fingerprint);
std::vector<uint32_t> fingerprintFromString (const std::string& text);

// Anteil der gespeicherten Kennzahlen, die im Kandidaten vorkommen (0 … 1)
double fingerprintOverlap (const std::vector<uint32_t>& stored, const std::vector<uint32_t>& candidate);

// Mindestanteil gemeinsamer Samples für einen Treffer
constexpr double kMinOverlap = 0.5;

struct LocateChoice
{
    int index = -1;                  // gewählter Kandidat, -1 = keiner
    bool ambiguous = false;          // mehrere gleich gute Kandidaten – lieber nichts laden
    std::vector<double> overlaps;    // je Kandidat (-1 = ohne Fingerabdruck nicht geprüft)
};

// Auswahl unter Dateien gleichen Namens:
//  - mit Fingerabdruck: nur Kandidaten mit mindestens kMinOverlap gemeinsamen Samples; Vorrang hat der gleiche
//    übergeordnete Ordner, danach die größere Übereinstimmung
//  - ohne Fingerabdruck (Projekte von vor dem 11.10.2026): nur Kandidaten im gleichnamigen Ordner
//  - sind die besten zwei gleich gut, wird keiner gewählt (ambiguous)
// candidateFingerprints[i] gehört zu candidateParents[i] (Name des übergeordneten Ordners).
LocateChoice chooseCandidate (const std::vector<std::string>& candidateParents,
                              const std::vector<std::vector<uint32_t>>& candidateFingerprints,
                              const std::string& savedParent,
                              const std::vector<uint32_t>& storedFingerprint);

} // namespace maermel
