// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// EXSParser.cpp

#include "EXSParser.h"

#include <algorithm>
#include <cstring>

namespace exs
{
namespace
{

// Sicherer Lesezugriff: Lesen außerhalb der Daten liefert 0 statt abzustürzen.
class ByteReader
{
public:
    ByteReader (const uint8_t* d, size_t s) noexcept : data (d), size (s) {}

    bool littleEndian = true;

    bool has (size_t pos, size_t len) const noexcept
    {
        return pos <= size && len <= size - pos;
    }

    int u8 (size_t pos) const noexcept
    {
        return has (pos, 1) ? data[pos] : 0;
    }

    // Echtes Int8 (Grenze 127) statt der früheren "> 101"-Regel
    int s8 (size_t pos) const noexcept
    {
        const int v = u8 (pos);
        return v > 127 ? v - 256 : v;
    }

    int32_t s32 (size_t pos) const noexcept
    {
        if (! has (pos, 4))
            return 0;

        const uint8_t* p = data + pos;
        const uint32_t v = littleEndian
            ? (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24)
            : (uint32_t) p[3] | ((uint32_t) p[2] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[0] << 24);
        return (int32_t) v;
    }

    // Leser, der auf einen Chunk begrenzt ist: Zugriffe über das Chunk-Ende
    // hinaus liefern 0, statt Bytes des nächsten Chunks zu lesen.
    ByteReader sub (size_t pos, size_t len) const noexcept
    {
        const size_t start = std::min (pos, size);
        ByteReader r (data + start, std::min (len, size - start));
        r.littleEndian = littleEndian;
        return r;
    }

    size_t length() const noexcept { return size; }

    std::string cstring (size_t pos, size_t maxLen) const
    {
        std::string s;
        for (size_t k = 0; k < maxLen && has (pos + k, 1); ++k)
        {
            const char c = (char) data[pos + k];
            if (c == 0)
                break;
            s.push_back (c);
        }
        return s;
    }

private:
    const uint8_t* data;
    size_t size;
};

// Die Feld-Offsets entsprechen den Zahlen im Xojo-Original:
// ManipulateMemory.Byte(o - 4 + i + 20)  ==  chunkStart + 16 + o
constexpr size_t fieldPos (size_t chunkStart, size_t o) noexcept { return chunkStart + 16 + o; }

enum class ChunkKind { Header, Zone, Group, Sample, Unknown };

// Typ im oberen Byte der Signatur (0x00000101 Header, 0x01000101 Zone ...).
// Bitte an echten Dateien verifizieren – bei Unstimmigkeit greift die Positionslogik.
// Märmel liest nur das Mapping; alle übrigen Chunks (auch die Instrument-Parameter) werden übersprungen.
ChunkKind kindFromSignature (int32_t sig) noexcept
{
    switch (((uint32_t) sig >> 24) & 0x0F)
    {
        case 0:  return ChunkKind::Header;
        case 1:  return ChunkKind::Zone;
        case 2:  return ChunkKind::Group;
        case 3:  return ChunkKind::Sample;
        default: return ChunkKind::Unknown;
    }
}

// Verhalten des Originals: erst N Zonen, dann Gruppen, dann Samples.
ChunkKind kindFromPosition (int index, const Instrument& inst) noexcept
{
    // Die Anzahlen stammen aus dem Dateikopf und sind nicht vertrauenswürdig (beschädigte Datei): negative Werte
    // als 0 behandeln und in 64 Bit addieren, sonst kann die Summe überlaufen (gefunden beim Fuzzing, 30.10.2026).
    const int64_t zones   = std::max (0, inst.declaredZones);
    const int64_t groups  = std::max (0, inst.declaredGroups);
    const int64_t samples = std::max (0, inst.declaredSamples);

    if (index == 0)                                return ChunkKind::Header;
    if (index <= zones)                            return ChunkKind::Zone;
    if (index <= zones + groups)                   return ChunkKind::Group;
    if (index <= zones + groups + samples)         return ChunkKind::Sample;
    return ChunkKind::Unknown;
}

void readZone (const ByteReader& r, Zone& z)
{
    auto f = [] (size_t o) { return fieldPos (0, o); };

    z.name = r.cstring (f (4), 64);

    const int status = r.u8 (f (68));
    z.oneShot         = (status & 1) != 0;
    z.pitchDisabled   = (status & 2) != 0;
    z.reverse         = (status & 4) != 0;
    z.velocityEnabled = (status & 8) != 0;
    z.outputActive    = (status & 64) != 0;

    z.rootKey      = r.u8 (f (69));
    z.tuneCents    = r.s8 (f (70));
    z.pan          = r.s8 (f (71));
    z.volumeDb     = r.s8 (f (72));
    z.volumeScale  = r.s8 (f (73));
    z.keyLow       = r.u8 (f (74));
    z.keyHigh      = r.u8 (f (75));
    z.velocityLow  = r.u8 (f (77));
    z.velocityHigh = r.u8 (f (78));

    z.startFrame = r.s32 (f (80));
    z.endFrame   = r.s32 (f (84));
    z.loopStart  = r.s32 (f (88));
    z.loopEnd    = r.s32 (f (92));

    z.loopCrossfade  = r.s32 (f (96));
    z.loopTune       = r.s8 (f (100));
    const int loopMode = r.u8 (f (101));
    z.loopEnabled    = loopMode > 0;
    z.loopEqualPower = (loopMode & 2) != 0;
    z.transpose      = r.s8 (f (148));
    z.output         = r.u8 (f (150));

    z.groupIndex  = r.s32 (f (156));
    z.sampleIndex = r.s32 (f (160));
}

void readGroup (const ByteReader& r, Group& g)
{
    auto f   = [] (size_t o) { return fieldPos (0, o); };
    auto has = [&r] (size_t o) { return r.has (fieldPos (0, o), 1); };

    g.name         = r.cstring (f (4), 64);
    g.volumeDb     = r.s8 (f (68));
    g.pan          = r.s8 (f (69));
    g.polyphony    = r.u8 (f (70));   // verifiziert an SC88pro_TR707_Kit.exs
    g.velocityLow  = r.u8 (f (73));
    g.velocityHigh = r.u8 (f (74));

    if (has (139))
    {
        g.cutoffOffset    = r.s8  (f (109));
        g.resonanceOffset = r.s8  (f (111));
        g.attackOffset    = r.s32 (f (124));
        g.decayOffset     = r.s32 (f (128));
        g.sustainOffset   = r.s32 (f (132));
        g.releaseOffset   = r.s32 (f (136));
    }

    // Ältere Gruppen-Chunks (z. B. 160 Bytes) enden vor diesen Feldern.
    // Das Original prüfte "Chunkgröße > 156" und las bei 160-Byte-Chunks
    // die Auswahlfelder aus dem folgenden Chunk.
    if (has (152))
    {
        const size_t chunkSize = r.length();

        g.triggerOnRelease = r.u8 (f (141)) != 0;
        g.selectModeRaw    = r.u8 (f (152));

        switch (g.selectModeRaw)
        {
            case 0:
                g.selectMode = SelectMode::None;
                break;

            case 1:
                g.selectMode    = SelectMode::KeySwitch;
                g.keySwitchNote = chunkSize > 170 ? r.u8 (f (143)) : r.u8 (f (153));
                break;

            case 3:
                g.selectMode       = SelectMode::Controller;
                g.controllerNumber = r.u8 (f (153));
                g.controllerLow    = r.u8 (f (154));
                g.controllerHigh   = r.u8 (f (155));
                break;

            default:
                g.selectMode = SelectMode::Other;
                break;
        }
    }

    if (has (157))
    {
        g.keyLow  = r.u8 (f (156));
        g.keyHigh = r.u8 (f (157));
    }
}

void readSample (const ByteReader& r, Sample& s)
{
    auto f = [] (size_t o) { return fieldPos (0, o); };
    const size_t chunkSize = r.length();

    s.name = r.cstring (f (4), 64);
    if (chunkSize > 660)                    // neuere Versionen: langer Dateiname
    {
        const std::string longName = r.cstring (f (404), 256);
        if (! longName.empty())             // leer -> kurzen Namen behalten
            s.name = longName;
    }

    s.path = r.cstring (f (148), 256);
    if (! r.littleEndian)                   // alte Mac-Pfade mit ':' als Trenner
        std::replace (s.path.begin(), s.path.end(), ':', '/');

    // Unter Windows erstellte Instrumente speichern "C:\Samples\...\".
    // Einheitlich '/' verwenden, sonst scheitern Ordnersuche und default_path.
    std::replace (s.path.begin(), s.path.end(), '\\', '/');

    s.dataOffset    = r.s32 (f (68));
    s.lengthFrames  = r.s32 (f (72));
    s.sampleRate    = r.s32 (f (76));
    s.bitDepth      = r.s32 (f (80));
    s.channels      = r.s32 (f (84));
    s.fileSizeBytes = r.s32 (f (100));

    // "WAVE" bzw. "AIFF" steht rückwärts ab Feld 96
    s.formatTag = { (char) r.u8 (f (99)), (char) r.u8 (f (98)),
                    (char) r.u8 (f (97)), (char) r.u8 (f (96)) };
}

} // namespace

bool Parser::parse (const uint8_t* data, size_t size, Instrument& out, std::string& error)
{
    out = Instrument();
    error.clear();

    ByteReader r (data, size);

    if (data == nullptr || ! r.has (16, 4))
    {
        error = "File too short for an EXS header.";
        return false;
    }

    if (std::memcmp (data + 16, "TBOS", 4) == 0)
        r.littleEndian = true;
    else if (std::memcmp (data + 16, "SOBT", 4) == 0)
        r.littleEndian = false;
    else
    {
        error = "Not an EXS24 file (signature at offset 16 missing).";
        return false;
    }

    out.bigEndian = ! r.littleEndian;

    const int32_t headerSig = r.s32 (0);
    out.usedSignatureTypes = (headerSig & 0xFFFF) == 0x0101
                          && kindFromSignature (headerSig) == ChunkKind::Header;

    size_t pos = 0;
    int index = 0;

    while (r.has (pos, 16))
    {
        const int32_t dataSize = r.s32 (pos + 4);
        if (dataSize < 0)
        {
            out.warnings.push_back ("Invalid chunk size at offset " + std::to_string (pos));
            break;
        }

        const size_t chunkSize = (size_t) dataSize + 84;

        // 0x0B an Offset 12 = von Logic als gelöscht markiertes Objekt.
        // Es wird trotzdem eingelesen und nur markiert, sonst verschieben sich
        // die Indizes aller folgenden Gruppen bzw. Samples.
        const bool deleted = r.s32 (pos + 12) == 0x0B;

        const ChunkKind kind = out.usedSignatureTypes ? kindFromSignature (r.s32 (pos))
                                                      : kindFromPosition (index, out);

        switch (kind)
        {
            case ChunkKind::Header:
                out.name            = r.cstring (fieldPos (pos, 4), 64);
                out.declaredZones   = r.s32 (pos + 88);
                out.declaredGroups  = r.s32 (pos + 92);
                out.declaredSamples = r.s32 (pos + 96);
                break;

            case ChunkKind::Zone:
                out.zones.emplace_back();
                readZone (r.sub (pos, chunkSize), out.zones.back());
                out.zones.back().deleted = deleted;
                break;

            case ChunkKind::Group:
                out.groups.emplace_back();
                readGroup (r.sub (pos, chunkSize), out.groups.back());
                out.groups.back().deleted = deleted;
                break;

            case ChunkKind::Sample:
                out.samples.emplace_back();
                readSample (r.sub (pos, chunkSize), out.samples.back());
                out.samples.back().deleted = deleted;
                break;

            case ChunkKind::Unknown:
                break;
        }

        pos += chunkSize;
        ++index;
    }

    // Plausibilitätsprüfungen
    if ((int) out.zones.size() != out.declaredZones)
        out.warnings.push_back ("Zones: " + std::to_string (out.zones.size()) + " read, "
                                + std::to_string (out.declaredZones) + " declared in header.");
    if ((int) out.groups.size() != out.declaredGroups)
        out.warnings.push_back ("Groups: " + std::to_string (out.groups.size()) + " read, "
                                + std::to_string (out.declaredGroups) + " declared in header.");
    if ((int) out.samples.size() != out.declaredSamples)
        out.warnings.push_back ("Samples: " + std::to_string (out.samples.size()) + " read, "
                                + std::to_string (out.declaredSamples) + " declared in header.");

    int badSample = 0, badGroup = 0;
    for (const auto& z : out.zones)
    {
        if (z.deleted)
            continue;
        if (z.sampleIndex < 0 || z.sampleIndex >= (int) out.samples.size()) ++badSample;
        if (z.groupIndex < -1 || z.groupIndex >= (int) out.groups.size())  ++badGroup;
    }
    if (badSample > 0)
        out.warnings.push_back (std::to_string (badSample) + " zone(s) without valid sample.");
    if (badGroup > 0)
        out.warnings.push_back (std::to_string (badGroup) + " zone(s) with invalid group (treated as ungrouped).");

    return true;
}

} // namespace exs
