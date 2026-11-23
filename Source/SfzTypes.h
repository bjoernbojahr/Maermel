// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SfzTypes.h – Märmel Engine
// Reines C++17, keine JUCE-Abhängigkeit (kompilierbar mit Xcode 13).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace maermel
{

// Sample Start: größter Versatz ab dem Nullpunkt (Start) bzw. größte Verschiebung nach Anschlag (Vel, ±).
// Gilt für Engine, Anzeige und Eingabe gemeinsam. Bei kürzeren Samples wirkt der Regler über deren ganze Länge.
// (Bis 06.10.2026: 200 ms.)
inline constexpr double kSampleStartMaxMs = 1000.0;

enum class Trigger
{
    Attack,         // Standard: beim Anschlag
    Release,        // beim Loslassen (auch release_key)
    First,          // nur, wenn keine andere Taste gehalten wird
    Legato          // nur, wenn bereits eine andere Taste gehalten wird
};

enum class LoopMode
{
    Unset,          // nicht angegeben: wird beim Laden aufgelöst
    NoLoop,
    OneShot,
    Continuous,
    Sustain
};

// Eine Region, wie sie aus der SFZ-Datei gelesen wurde (Vererbung bereits aufgelöst).
// Sample-bezogene Werte (end, Loops) sind hier noch ungeprüft.
struct Region
{
    std::string samplePath;         // relativ zur SFZ-Datei, '/' als Trenner

    int loKey = 0, hiKey = 127;
    int loVel = 0, hiVel = 127;
    int pitchKeycenter = 60;
    bool pitchKeycenterFromSample = false;   // pitch_keycenter=sample: Grundton aus der Sample-Datei (smpl/INST)
    float pitchKeytrack = 100.0f;   // Cent pro Taste

    int transpose = 0;              // Halbtöne
    float tune = 0.0f;              // Cent
    float volume = 0.0f;            // dB
    float pan = 0.0f;               // -100 ... +100

    int64_t offset = 0;
    int64_t end = -1;               // -1 = bis zum Dateiende

    LoopMode loopMode = LoopMode::Unset;
    int64_t loopStart = -1;         // -1 = nicht angegeben
    int64_t loopEnd = -1;           // inklusiv, -1 = nicht angegeben

    Trigger trigger = Trigger::Attack;

    // Keyswitches: Region spielt nur, wenn zuletzt die Taste sw_last gedrückt wurde
    int swLast = -1;                // -1 = keine Bedingung
    int swLoKey = -1, swHiKey = -1; // Tastenbereich der Keyswitches (-1 = nicht angegeben)
    int swDefault = -1;             // Keyswitch beim Laden

    // Round Robin und Zufall
    int seqLength = 1;              // Anzahl der Stufen
    int seqPosition = 1;            // welche Stufe (1 ... seqLength)
    float loRand = 0.0f, hiRand = 1.0f;

    // Controller-Bedingungen (loccN/hiccN): Region spielt nur, wenn CC N beim Anschlag
    // im Bereich lo ... hi liegt, z. B. locc64=65 hicc64=127 = nur mit gedrücktem Pedal
    struct CcCondition { int cc = 0; int lo = 0; int hi = 127; };
    std::vector<CcCondition> ccConditions;

    // Überblendungen (SFZ 1, seit 19.10.2026): Velocity beim Anschlag, Controller laufend während die Note klingt.
    // xfin: unter lo stumm, ab hi voll; xfout: bis lo voll, ab hi stumm. Kurve: power (gleiche Leistung, Standard)
    // oder gain (linear). Standardwerte lassen die Region unverändert.
    int xfinLoVel = 0, xfinHiVel = 0, xfoutLoVel = 127, xfoutHiVel = 127;
    bool xfVelPower = true;
    struct CcCrossfade { int cc = 0; int inLo = 0, inHi = 0, outLo = 127, outHi = 127; };
    std::vector<CcCrossfade> ccCrossfades;
    bool xfCcPower = true;

    int polyphony = 0;              // 0 = unbegrenzt
    int polyphonyGroup = -1;        // Index des umgebenden <group>-Headers, oder aus group= (siehe Parser)
    // group= / off_by= (SFZ 1, seit 26.10.2026): Schlägt eine Region mit group = X an, verstummen alle klingenden
    // Regionen mit off_by = X (schnell ausgeblendet) – z. B. geschlossene Hi-Hat beendet die offene. 0 = keine Gruppe.
    int group = 0;
    int offBy = 0;

    int lineNumber = 0;             // Zeile des <region>-Headers, für Meldungen
};

struct ParsedInstrument
{
    std::vector<Region> regions;
    std::vector<std::string> warnings;
    std::vector<std::string> ignoredOpcodes;   // bekannte SFZ-Opcodes, die die Oberfläche übernimmt
};

} // namespace maermel
