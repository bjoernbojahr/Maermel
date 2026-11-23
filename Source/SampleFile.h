// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SampleFile.h – Märmel Engine
// Lädt WAV- und AIFF-Dateien vollständig in den Speicher, in ihrer Originalauflösung:
// 8/16 Bit → int16, 24/32 Bit Ganzzahl → int32 (linksbündig), Float → float.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace maermel
{

enum class SampleFormat { Int16, Int32, Float32 };

struct SampleData
{
    int numChannels = 0;
    double sampleRate = 0.0;
    int64_t numFrames = 0;
    int bitsPerSample = 0;          // Bittiefe der Datei (nur zur Information)
    SampleFormat format = SampleFormat::Int16;

    // Nur einer der drei Puffer ist gefüllt, Kanäle verschachtelt (L R L R ...).
    std::vector<int16_t> int16Data;
    std::vector<int32_t> int32Data;
    std::vector<float>   floatData;

    bool hasLoop = false;
    int64_t loopStart = 0;          // inklusiv
    int64_t loopEnd = 0;            // inklusiv
    int rootKey = -1;               // aus smpl/INST, -1 = unbekannt (verwendet bei pitch_keycenter=sample)

    int64_t lastFrame() const noexcept { return numFrames - 1; }

    // Wert eines Frames als float im Bereich -1 ... +1
    float valueAt (int64_t frame, int channel) const noexcept
    {
        const size_t i = (size_t) (frame * numChannels + channel);
        switch (format)
        {
            case SampleFormat::Int16:   return int16Data[i] * (1.0f / 32768.0f);
            case SampleFormat::Int32:   return (float) int32Data[i] * (1.0f / 2147483648.0f);
            case SampleFormat::Float32: return floatData[i];
        }
        return 0.0f;
    }

    size_t memoryBytes() const noexcept
    {
        return int16Data.size() * 2 + int32Data.size() * 4 + floatData.size() * 4;
    }
};

// Gibt false zurück und füllt 'error', wenn die Datei nicht gelesen werden kann.
bool loadSampleFile (const std::string& path, SampleData& out, std::string& error);

// Genauer Speicherbedarf eines Samples nach dem Laden (wie memoryBytes()), ermittelt nur aus den Kopfdaten:
// springt von Chunk zu Chunk bis fmt/data (WAV) bzw. COMM/SSND (AIFF), ohne die Audiodaten zu lesen.
// Speicherablage: 8 und 16 Bit → 16 Bit, 24 und 32 Bit → 32 Bit, Float (32/64 Bit) → 32 Bit Float.
// 0 = unbekannt (Datei fehlt, unbekanntes Format) – dann schätzt der Aufrufer vorsichtig.
uint64_t sampleMemoryFromHeader (const std::string& path);

// ---------------------------------------------------------------------------------------------------------------
// Grundton und Loop nur aus den Kopfdaten lesen (seit 19.10.2026; in Märmel selbst nicht benutzt – als Beispiel
// für andere Projekte, z. B. wenn ein Importer wie JUCEs AudioFormatReader sonst die ganze Datei einlesen würde).
// Beide springen von Chunk zu Chunk (WAV: RIFF, AIFF/AIFC: FORM) und lesen nur die wenigen Bytes der
// betreffenden Chunks – auch wenn diese hinter den Audiodaten liegen. Bei doppelten Chunks gilt der erste
// (wie beim Laden). Fehlende Füllbytes werden erkannt wie beim Laden.

// Grundton (MIDI-Note 0 … 127) aus dem Sample-Header, sonst -1.
//   WAV:  smpl-Chunk (dwMIDIUnityNote); fehlt er, der inst-Chunk (Byte 0, unshifted note).
//         (Märmel selbst nutzt beim Laden nur smpl.)
//   AIFF: INST-Chunk (baseNote).
int sampleRootKeyFromHeader (const std::string& path);

// Loop aus dem Sample-Header.
//   start: erstes Sample im Loop (Frame-Index ab 0)
//   end:   letztes Sample im Loop, INKLUSIV (wie loop_end in SFZ)
//   Beide -1, wenn kein Loop vorhanden, der Loop ausgeschaltet oder unplausibel ist (Ende ≤ Start, Start hinter
//   dem Dateiende). Ein Ende hinter dem letzten Sample wird auf das letzte Sample begrenzt (wie beim Laden).
//   WAV:  erster Loop im smpl-Chunk (dwStart, dwEnd; dwEnd ist laut Spezifikation inklusiv).
//   AIFF: Sustain-Loop aus INST (playMode ≠ 0) mit den Positionen aus MARK; AIFF-Marker liegen zwischen den
//         Samples, das Ende wird daher um 1 verringert (inklusiv).
struct SampleLoopPoints
{
    int64_t start = -1;
    int64_t end = -1;
    bool valid() const noexcept { return start >= 0 && end > start; }
};
SampleLoopPoints sampleLoopFromHeader (const std::string& path);

} // namespace maermel
