// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// Instrument.h – Märmel Engine
// Lädt eine SFZ-Datei samt Samples und prüft jede Region gegen die tatsächliche Sample-Datei.
// Ergebnis ist eine flache, geprüfte Region-Tabelle für die Stimmen.

#pragma once

#include "SampleFile.h"
#include "SfzTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace maermel
{

struct PlayableRegion
{
    Region sfz;                                  // Werte aus der SFZ-Datei (für Info und Offsets)
    std::shared_ptr<const SampleData> sample;    // mehrfach genutzte Samples werden nur einmal geladen

    int64_t startFrame = 0;                      // geprüfter offset
    int64_t endFrame = 0;                        // geprüftes Ende, inklusiv
    bool oneShot = false;
    bool looping = false;                        // loop_continuous oder loop_sustain
    bool loopOnlyWhileHeld = false;              // loop_sustain
    int64_t loopStart = 0, loopEnd = 0;          // inklusiv, nur gültig wenn looping
    const char* loopSource = "";                 // "SFZ", "file", "whole sample" – für Info

    // Zähler für Round Robin (seq_length/seq_position). Wird nur vom Audio-Thread verändert.
    // Achtung: mutable in einem sonst unveränderlichen (const) Instrument. Das ist nur richtig, solange ausschließlich
    // der Synth des Audio-Threads ein geladenes Instrument abspielt. Käme ein zweiter Leser hinzu (z. B. Vorhören,
    // Export aus dem geladenen Instrument), müsste der Zähler in den Synth wandern (Array je Regionsnummer).
    mutable uint32_t seqCounter = 0;
};

struct Instrument
{
    std::string sfzPath;
    std::vector<PlayableRegion> regions;
    std::vector<std::string> warnings;
    std::vector<std::string> ignoredOpcodes;
    int sampleCount = 0;
    int missingSamples = 0;
    int correctedRegions = 0;
    size_t sampleMemoryBytes = 0;
    uint64_t estimatedBytes = 0;                 // vor dem Laden geschätzter Speicherbedarf der Samples
    bool notEnoughMemory = false;                // Laden abgelehnt (zu groß für diesen Rechner)
    bool fileMissing = false;                    // SFZ- bzw. EXS-Datei nicht lesbar (z. B. inzwischen gelöscht)

    // Keyswitches: Tastenbereich und Standard-Artikulation (-1 = keine)
    int keyswitchLow = -1, keyswitchHigh = -1;
    int keyswitchDefault = -1;
    bool hasKeyswitches() const noexcept { return keyswitchLow >= 0; }

    // Kurze Info-Zeile für die Oberfläche, z. B. "8 Regionen, 8 Samples (4,1 MB), 1 korrigiert"
    std::string summary() const;
};

// Speicherschutz (zweistufig, seit 08.10.2026), geprüft nach dem Lesen der SFZ-Datei und vor dem Laden der Samples:
//  1. schnell: Dateigrößen aller verwendeten Samples × 2 – eine sichere Obergrenze (8 Bit belegt im Speicher doppelt
//     so viel, 24 Bit 4/3, alles andere höchstens gleich viel). Passt das samt aktuellem Instrument, wird sofort geladen.
//  2. nur wenn es knapp wird: genauer Bedarf aus den Kopfdaten jeder Datei (sampleMemoryFromHeader). Danach: laden;
//     oder erst das aktuelle Instrument freigeben (releaseCurrent), wenn beide zusammen nicht passen; oder ablehnen,
//     wenn das neue allein schon zu groß ist (Instrument ohne Regionen, notEnoughMemory, Hinweis mit genauem Bedarf).
struct MemoryGuard
{
    uint64_t budgetBytes = 0;                 // höchstens so viel für die Samples eines Instruments (0 = keine Prüfung)
    uint64_t currentBytes = 0;                // belegt vom gerade geladenen Instrument
    std::function<void()> releaseCurrent;     // aktuelles Instrument freigeben und warten, bis es weg ist
    explicit operator bool() const noexcept { return budgetBytes > 0; }
};

// Pfade mit '/' als Trenner. Lesefehler der SFZ-Datei landen in warnings.
std::unique_ptr<Instrument> loadInstrument (const std::string& sfzPath, const MemoryGuard& guard = {});

// Wie oben, aber mit bereits gelesenem SFZ-Text (z. B. für Tests).
std::unique_ptr<Instrument> loadInstrumentFromText (const std::string& sfzText, const std::string& sfzPath,
                                                    const MemoryGuard& guard = {});

} // namespace maermel
