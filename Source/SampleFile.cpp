// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SampleFile.cpp – Märmel Engine

#include "SampleFile.h"
#include "FileUtil.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>

namespace maermel
{
namespace
{

using Bytes = std::vector<uint8_t>;

bool readWholeFile (const std::string& path, Bytes& out)
{
    std::ifstream f;
    openForReading (f, path);
    if (! f)
        return false;
    f.seekg (0, std::ios::end);
    const std::streamoff size = f.tellg();
    // WAV und AIFF können höchstens 4 GB groß sein (32-Bit-Längenangaben); alles darüber ist kein Sample
    if (size <= 0 || (uint64_t) size > 0xFFFFFFFFull + 16u)
        return false;
    out.resize ((size_t) size);
    f.seekg (0, std::ios::beg);
    f.read (reinterpret_cast<char*> (out.data()), size);
    return (bool) f;
}

// Die ersten 4 Bytes der Datei (Formatkennung), ohne die ganze Datei zu lesen
std::string fileTag (const std::string& path)
{
    std::ifstream f;
    openForReading (f, path);
    char tag[4] = {};
    if (! f || ! f.read (tag, 4))
        return {};
    return std::string (tag, 4);
}

struct Reader
{
    const Bytes& d;
    bool has (size_t pos, size_t len) const noexcept { return pos <= d.size() && len <= d.size() - pos; }

    uint16_t le16 (size_t p) const noexcept { return has (p, 2) ? (uint16_t) (d[p] | (d[p + 1] << 8)) : 0; }
    uint32_t le32 (size_t p) const noexcept
    {
        return has (p, 4) ? (uint32_t) d[p] | ((uint32_t) d[p + 1] << 8) | ((uint32_t) d[p + 2] << 16) | ((uint32_t) d[p + 3] << 24) : 0;
    }
    uint16_t be16 (size_t p) const noexcept { return has (p, 2) ? (uint16_t) ((d[p] << 8) | d[p + 1]) : 0; }
    uint32_t be32 (size_t p) const noexcept
    {
        return has (p, 4) ? ((uint32_t) d[p] << 24) | ((uint32_t) d[p + 1] << 16) | ((uint32_t) d[p + 2] << 8) | (uint32_t) d[p + 3] : 0;
    }
    bool tag (size_t p, const char* t) const noexcept { return has (p, 4) && std::memcmp (&d[p], t, 4) == 0; }
};

// Plausibler Chunk-Kopf an Position p: Kennung nur aus Buchstaben, Ziffern, Leerzeichen und Unterstrich
// (z. B. "data", "smpl", "fmt ", "LIST", "_PMX") und eine Länge, die in die Datei passt
bool plausibleChunkAt (const Reader& r, size_t p, bool bigEndian) noexcept
{
    if (! r.has (p, 8))
        return false;
    for (size_t k = 0; k < 4; ++k)
    {
        const auto c = r.d[p + k];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '_';
        if (! ok)
            return false;
    }
    const uint32_t size = bigEndian ? r.be32 (p + 4) : r.le32 (p + 4);
    return size <= r.d.size() - (p + 8);
}

// Bekannte Kennungen in WAV- und AIFF-Dateien (für die Entscheidung bei fehlendem Füllbyte)
bool knownChunkId (const unsigned char* c) noexcept
{
    static const char* const ids[] = { "fmt ", "data", "smpl", "inst", "LIST", "cue ", "fact", "bext", "iXML", "JUNK",
                                       "junk", "PAD ", "acid", "id3 ", "ID3 ", "labl", "note", "ltxt", "plst", "DISP",
                                       "COMM", "SSND", "MARK", "INST", "APPL", "NAME", "AUTH", "(c) ", "ANNO", "COMT",
                                       "FVER", "AESD", "MIDI" };
    for (const char* id : ids)
        if (std::memcmp (c, id, 4) == 0)
            return true;
    return false;
}

// Entscheidung bei Chunks ungerader Länge (siehe nextChunkPosition): 1. nur eine Stelle plausibel → diese;
// 2. beide plausibel → die mit bekannter Kennung; 3. sonst entscheidet das Füllbyte: Es muss 0 sein – steht dort
// etwas anderes (z. B. das „d“ von „data“), fehlt es. Gültige Dateien (Füllbyte 0) werden nie anders gelesen.
bool readWithoutPadding (bool paddedOk, bool unpaddedOk, bool paddedKnown, bool unpaddedKnown,
                         unsigned char padByte) noexcept
{
    if (! unpaddedOk) return false;
    if (! paddedOk)   return true;
    if (paddedKnown != unpaddedKnown) return unpaddedKnown;
    return padByte != 0;
}

// Position des nächsten Chunks. Nach Vorschrift folgt auf einen Chunk ungerader Länge ein Füllbyte.
// Manche Programme vergessen es (bekannt beim 7 Byte langen WAV-„inst“-Chunk); dann stünde der nächste Chunk
// ein Byte früher, und ohne Korrektur gingen alle folgenden Chunks verloren – bis hin zu „data“.
// Regel: Steht an der korrekten Position (mit Füllbyte) ein plausibler Chunk-Kopf, gilt sie – gültige Dateien
// werden also nie anders gelesen. Nur wenn dort keiner steht, aber ohne Füllbyte einer, wird diese genommen.
size_t nextChunkPosition (const Reader& r, size_t body, uint32_t size, bool bigEndian) noexcept
{
    const size_t padded = body + size + (size & 1);
    if ((size & 1) == 0)
        return padded;
    const size_t unpadded = body + size;
    const bool paddedOk = plausibleChunkAt (r, padded, bigEndian);
    const bool unpaddedOk = plausibleChunkAt (r, unpadded, bigEndian);
    const bool paddedKnown = paddedOk && knownChunkId (&r.d[padded]);
    const bool unpaddedKnown = unpaddedOk && knownChunkId (&r.d[unpadded]);
    const unsigned char padByte = r.has (unpadded, 1) ? r.d[unpadded] : 0;
    // Beispiel (gefunden 08.10.2026): inst (7 Byte) ohne Füllbyte, direkt dahinter data mit kleiner Länge und stillem
    // Anfang – mit Füllbyte gelesen ergibt sich der Schein-Chunk „atab“ mit plausibler Länge.
    return readWithoutPadding (paddedOk, unpaddedOk, paddedKnown, unpaddedKnown, padByte) ? unpadded : padded;
}

// 80-Bit-Gleitkommazahl (AIFF-Samplerate)
double extended80 (const uint8_t* p)
{
    const int exponent = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i)
        mantissa = (mantissa << 8) | p[2 + i];
    if (exponent == 0 && mantissa == 0)
        return 0.0;
    const double v = std::ldexp ((double) mantissa, exponent - 16383 - 63);
    return (p[0] & 0x80) ? -v : v;
}

enum class Encoding { PcmInt, Float };

// WAV: Bytes je Sample in der Datei. Normalerweise (bits + 7) / 8. Manche Programme schreiben 24 Bit in
// 4-Byte-Containern, ohne das in wBitsPerSample anzugeben (dort steht 24); verraten wird es nur durch
// nBlockAlign = 4 × Kanäle. Mit 3 Byte Schrittweite gelesen käme Rauschen heraus (seit 23.11.2026).
// Gilt beim Laden, bei der Speicherschätzung und beim Lesen der Kopfdaten gleich.
int wavBytesPerSample (int bits, int channels, int blockAlign)
{
    if (bits == 24 && channels > 0 && blockAlign == 4 * channels)
        return 4;
    return (bits + 7) / 8;
}

// Wandelt Rohdaten in das Speicherformat um. bigEndian gilt für AIFF, signed8 ebenfalls.
bool convertPcm (const uint8_t* src, size_t srcBytes, int bits, Encoding enc, bool bigEndian, bool signed8,
                 SampleData& out, std::string& error)
{
    // Bytes je Sample: aufgerundet. AIFF erlaubt 1 … 32 Bit, linksbündig in ganzen Bytes gespeichert (z. B. 12 Bit
    // in 2 Bytes, 20 Bit in 3 Bytes). Bis 26.10.2026 stand hier bits / 8 – bei 12 Bit wurde dann mit 1 Byte
    // Schrittweite gezählt, aber 2 Bytes gelesen (beim Test mit beschädigten Dateien gefunden: Lesen hinter dem
    // Puffer). Verzweigt wird deshalb unten nach bytesPerSample, nicht nach bits.
    const int bytesPerSample = (bits + 7) / 8;
    if (bits < 1 || bytesPerSample > 8 || out.numChannels < 1)
    {
        error = "Unsupported bit depth: " + std::to_string (bits);
        return false;
    }

    const size_t count = srcBytes / (size_t) bytesPerSample;
    out.numFrames = (int64_t) (count / (size_t) out.numChannels);
    const size_t n = (size_t) out.numFrames * (size_t) out.numChannels;

    auto byteAt = [&] (size_t sample, int i) -> uint8_t
    {
        const size_t base = sample * (size_t) bytesPerSample;
        return src[base + (size_t) (bigEndian ? i : bytesPerSample - 1 - i)];   // i = 0: höchstwertiges Byte
    };

    if (enc == Encoding::Float)
    {
        out.format = SampleFormat::Float32;
        out.floatData.resize (n);
        for (size_t s = 0; s < n; ++s)
        {
            if (bits == 32)
            {
                uint32_t u = 0;
                for (int i = 0; i < 4; ++i) u = (u << 8) | byteAt (s, i);
                float f; std::memcpy (&f, &u, 4);
                out.floatData[s] = f;
            }
            else if (bits == 64)
            {
                uint64_t u = 0;
                for (int i = 0; i < 8; ++i) u = (u << 8) | byteAt (s, i);
                double f; std::memcpy (&f, &u, 8);
                out.floatData[s] = (float) f;
            }
            else
            {
                error = "Unsupported float bit depth: " + std::to_string (bits);
                return false;
            }
        }
        return true;
    }

    if (bytesPerSample <= 2)
    {
        out.format = SampleFormat::Int16;
        out.int16Data.resize (n);
        for (size_t s = 0; s < n; ++s)
        {
            if (bytesPerSample == 1)
            {
                const int v = signed8 ? (int) (int8_t) byteAt (s, 0) : (int) byteAt (s, 0) - 128;
                out.int16Data[s] = (int16_t) (v * 256);
            }
            else
            {
                out.int16Data[s] = (int16_t) ((byteAt (s, 0) << 8) | byteAt (s, 1));
            }
        }
        return true;
    }

    if (bytesPerSample == 3 || bytesPerSample == 4)
    {
        out.format = SampleFormat::Int32;
        out.int32Data.resize (n);
        for (size_t s = 0; s < n; ++s)
        {
            uint32_t u = ((uint32_t) byteAt (s, 0) << 24) | ((uint32_t) byteAt (s, 1) << 16) | ((uint32_t) byteAt (s, 2) << 8);
            if (bytesPerSample == 4)
                u |= byteAt (s, 3);
            out.int32Data[s] = (int32_t) u;   // linksbündig: 24 Bit liegen in den oberen Bits
        }
        return true;
    }

    error = "Unsupported bit depth: " + std::to_string (bits);
    return false;
}

bool loadWav (const Bytes& d, SampleData& out, std::string& error)
{
    Reader r { d };
    size_t pos = 12;
    int formatTag = 0, bits = 0, blockAlign = 0;
    size_t dataPos = 0, dataSize = 0;
    bool haveFmt = false, haveData = false, haveSmpl = false;
    int64_t smplStart = -1, smplEnd = -1;
    // Doppelte Chunks (entschieden 17.10.2026): jeweils der ERSTE gilt, spätere werden übersprungen – wie bei
    // üblicher Audiosoftware; verhindert z. B. Grundton aus dem zweiten, Loop aus dem ersten smpl-Chunk.

    while (r.has (pos, 8))
    {
        const uint32_t size = r.le32 (pos + 4);
        const size_t body = pos + 8;

        if (r.tag (pos, "fmt ") && ! haveFmt)
        {
            formatTag = r.le16 (body);
            out.numChannels = r.le16 (body + 2);
            out.sampleRate = r.le32 (body + 4);
            blockAlign = r.le16 (body + 12);
            bits = r.le16 (body + 14);
            if (formatTag == 0xFFFE && size >= 26)
                formatTag = r.le16 (body + 24);      // WAVE_FORMAT_EXTENSIBLE: Unterformat
            haveFmt = true;
        }
        else if (r.tag (pos, "data") && ! haveData)
        {
            dataPos = body;
            dataSize = std::min<size_t> (size, d.size() - body);
            haveData = true;
        }
        else if (r.tag (pos, "smpl") && size >= 36 && ! haveSmpl)
        {
            haveSmpl = true;
            out.rootKey = (int) r.le32 (body + 12);
            if (r.le32 (body + 28) > 0 && size >= 60)
            {
                smplStart = r.le32 (body + 36 + 8);
                smplEnd   = r.le32 (body + 36 + 12);
            }
        }

        pos = nextChunkPosition (r, body, size, false);   // fehlendes Füllbyte wird erkannt
    }

    if (! haveFmt || ! haveData)
    {
        error = "WAV file without fmt or data chunk";
        return false;
    }

    Encoding enc;
    if (formatTag == 1)      enc = Encoding::PcmInt;
    else if (formatTag == 3) enc = Encoding::Float;
    else
    {
        error = "Unsupported WAV format (type " + std::to_string (formatTag) + ")";
        return false;
    }

    // 24 Bit im 4-Byte-Container (siehe wavBytesPerSample): wie 32 Bit lesen, als 24 Bit anzeigen
    const bool padded24 = enc == Encoding::PcmInt && wavBytesPerSample (bits, out.numChannels, blockAlign) == 4 && bits == 24;
    out.bitsPerSample = bits;
    // d.data() + dataPos statt &d[dataPos]: Ein data-Chunk mit Länge 0 ganz am Dateiende ergäbe &d[d.size()],
    // einen Zugriff hinter das Vektor-Ende (Debug-Builds brechen dort ab). Gelesen werden dann 0 Bytes.
    if (! convertPcm (d.data() + dataPos, dataSize, padded24 ? 32 : bits, enc, false, false, out, error))
        return false;
    if (padded24)
    {
        // Üblich: die 24 Bit stehen oben, die Füllbits unten (dann stimmt das Ergebnis schon). Stehen sie unten
        // (Füllbits oben), ist das unterste Byte belegt – dann um 8 Bit nach oben schieben (linksbündig).
        bool lowByteUsed = false;
        for (size_t i = 0; i < out.int32Data.size() && i < 65536 && ! lowByteUsed; ++i)
            if ((out.int32Data[i] & 0xFF) != 0)
                lowByteUsed = true;
        if (lowByteUsed)
            for (auto& s : out.int32Data)
                s = (int32_t) ((uint32_t) s << 8);
    }

    if (smplStart >= 0 && smplEnd > smplStart && smplStart < out.numFrames)
    {
        out.hasLoop = true;
        out.loopStart = smplStart;
        // Manche Programme schreiben das Ende exklusiv (= Anzahl Frames): auf das letzte Sample begrenzen
        out.loopEnd = std::min<int64_t> (smplEnd, out.lastFrame());
    }
    return true;
}

bool loadAiff (const Bytes& d, SampleData& out, std::string& error)
{
    Reader r { d };
    const bool isAifc = r.tag (8, "AIFC");
    size_t pos = 12;
    int bits = 0;
    bool haveComm = false, haveSsnd = false, haveMark = false, haveInst = false;   // doppelte Chunks: der erste gilt
    bool littleEndianData = false, isFloat = false;
    size_t dataPos = 0, dataSize = 0;
    std::map<int, uint32_t> markers;
    int loopMode = 0, loopBeginId = -1, loopEndId = -1;

    while (r.has (pos, 8))
    {
        const uint32_t size = r.be32 (pos + 4);
        const size_t body = pos + 8;

        if (r.tag (pos, "COMM") && r.has (body, 18) && ! haveComm)
        {
            out.numChannels = (int16_t) r.be16 (body);
            bits = (int16_t) r.be16 (body + 6);
            out.sampleRate = extended80 (&d[body + 8]);
            if (isAifc && r.has (body + 18, 4))
            {
                if (r.tag (body + 18, "sowt"))                               littleEndianData = true;
                else if (r.tag (body + 18, "fl32") || r.tag (body + 18, "FL32")) isFloat = true;
                else if (r.tag (body + 18, "fl64") || r.tag (body + 18, "FL64")) isFloat = true;
                // in24/in32 (von einigen Apple-Programmen): unkomprimiert wie NONE (seit 23.11.2026)
                else if (! r.tag (body + 18, "NONE") && ! r.tag (body + 18, "twos")
                         && ! r.tag (body + 18, "in24") && ! r.tag (body + 18, "in32"))
                {
                    error = "Compressed AIFC files are not supported";
                    return false;
                }
            }
            haveComm = true;
        }
        else if (r.tag (pos, "SSND") && r.has (body, 8) && ! haveSsnd)
        {
            const uint32_t offset = r.be32 (body);
            dataPos = body + 8 + offset;
            dataSize = (dataPos < d.size()) ? std::min<size_t> (size - 8 - offset, d.size() - dataPos) : 0;
            haveSsnd = true;
        }
        else if (r.tag (pos, "MARK") && r.has (body, 2) && ! haveMark)
        {
            haveMark = true;
            const int count = r.be16 (body);
            size_t p = body + 2;
            // Nur innerhalb des MARK-Chunks lesen (wie readHeaderInfo): Eine beschädigte Marker-Anzahl würde sonst
            // Unsinn aus dem nächsten Chunk als Marker lesen (seit 23.11.2026)
            const size_t markEnd = std::min<size_t> (body + size, d.size());
            for (int i = 0; i < count && r.has (p, 7) && p + 7 <= markEnd; ++i)
            {
                const int id = (int16_t) r.be16 (p);
                markers[id] = r.be32 (p + 2);
                const size_t nameLen = d[p + 6];
                p += 6 + 1 + nameLen;
                if (((1 + nameLen) & 1) != 0)
                    ++p;                                      // Pascal-String auf gerade Länge aufgefüllt
            }
        }
        else if (r.tag (pos, "INST") && r.has (body, 14) && ! haveInst)
        {
            haveInst = true;
            out.rootKey = d[body];
            loopMode    = (int16_t) r.be16 (body + 8);
            loopBeginId = (int16_t) r.be16 (body + 10);
            loopEndId   = (int16_t) r.be16 (body + 12);
        }

        pos = nextChunkPosition (r, body, size, true);   // fehlendes Füllbyte wird erkannt
    }

    if (! haveComm || ! haveSsnd)
    {
        error = "AIFF file without COMM or SSND chunk";
        return false;
    }

    // Audiodaten müssen innerhalb der Datei beginnen (offset im SSND-Chunk kann bei beschädigten Dateien dahinter
    // zeigen). d.data() + dataPos statt &d[dataPos]: kein Zugriff hinter das Vektor-Ende.
    if (dataPos > d.size())
    {
        error = "Damaged file: audio data starts beyond the end of the file";
        return false;
    }
    out.bitsPerSample = bits;
    if (! convertPcm (d.data() + dataPos, dataSize, bits, isFloat ? Encoding::Float : Encoding::PcmInt,
                      ! littleEndianData, true, out, error))
        return false;

    if (loopMode != 0 && markers.count (loopBeginId) && markers.count (loopEndId))
    {
        const int64_t s = markers[loopBeginId];
        const int64_t e = (int64_t) markers[loopEndId] - 1;   // AIFF-Marker liegen zwischen Samples: Ende exklusiv
        if (e > s && s < out.numFrames)
        {
            out.hasLoop = true;
            out.loopStart = s;
            out.loopEnd = std::min<int64_t> (e, out.lastFrame());
        }
    }
    return true;
}

} // namespace

namespace
{
// Kleine Lesehilfe für die Kopfdaten: liest an einer Stelle, ohne die ganze Datei zu laden
struct HeaderReader
{
    std::ifstream& f;
    uint64_t size;
    bool read (uint64_t pos, unsigned char* buf, size_t n)
    {
        if (pos + n > size) return false;
        f.clear();
        f.seekg ((std::streamoff) pos, std::ios::beg);
        f.read (reinterpret_cast<char*> (buf), (std::streamsize) n);
        return (bool) f;
    }
};

bool plausibleId (const unsigned char* c)
{
    for (int k = 0; k < 4; ++k)
    {
        const unsigned char x = c[k];
        if (! ((x >= 'A' && x <= 'Z') || (x >= 'a' && x <= 'z') || (x >= '0' && x <= '9') || x == ' ' || x == '_'))
            return false;
    }
    return true;
}

uint64_t storedBytesPerSample (int bits, bool isFloat)
{
    if (isFloat) return 4;                    // 32 und 64 Bit Float → 32 Bit Float
    if (bits <= 16) return 2;                 // 8 und 16 Bit → 16 Bit
    return 4;                                 // 24 und 32 Bit → 32 Bit
}
} // namespace

uint64_t sampleMemoryFromHeader (const std::string& path)
{
    std::ifstream f;
    openForReading (f, path);
    if (! f)
        return 0;
    f.seekg (0, std::ios::end);
    const std::streamoff end = f.tellg();
    if (end < 12)
        return 0;
    HeaderReader r { f, (uint64_t) end };
    unsigned char h[12];
    if (! r.read (0, h, 12))
        return 0;
    const bool wav = std::memcmp (h, "RIFF", 4) == 0 && std::memcmp (h + 8, "WAVE", 4) == 0;
    const bool aiff = std::memcmp (h, "FORM", 4) == 0 && (std::memcmp (h + 8, "AIFF", 4) == 0 || std::memcmp (h + 8, "AIFC", 4) == 0);
    if (! wav && ! aiff)
        return 0;
    auto le32 = [] (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); };
    auto le16 = [] (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8); };
    auto be32 = [] (const unsigned char* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3]; };
    auto be16 = [] (const unsigned char* p) { return ((uint32_t) p[0] << 8) | (uint32_t) p[1]; };

    int channels = 0, bits = 0, blockAlign = 0; bool isFloat = false;
    uint64_t dataBytes = 0, aiffFrames = 0;
    bool haveFormat = false, haveData = false;

    uint64_t pos = 12;
    for (int guard = 0; guard < 1000 && pos + 8 <= r.size && ! (haveFormat && haveData); ++guard)
    {
        unsigned char c[8];
        if (! r.read (pos, c, 8) || ! plausibleId (c))
            break;
        const uint32_t size = wav ? le32 (c + 4) : be32 (c + 4);
        const uint64_t body = pos + 8;
        unsigned char b[26] = {};
        // Wie beim Laden gilt jeweils der erste fmt-Chunk (seit 23.11.2026; vorher hätte ein zweiter überschrieben)
        if (wav && std::memcmp (c, "fmt ", 4) == 0 && ! haveFormat && r.read (body, b, std::min<uint32_t> (size, 26)))
        {
            uint32_t tag = le16 (b);
            channels = (int) le16 (b + 2);
            blockAlign = (int) le16 (b + 12);
            bits = (int) le16 (b + 14);
            if (tag == 0xFFFE && size >= 26) tag = le16 (b + 24);   // WAVE_FORMAT_EXTENSIBLE
            isFloat = tag == 3;
            haveFormat = true;
        }
        else if (wav && std::memcmp (c, "data", 4) == 0)
        {
            dataBytes = std::min<uint64_t> (size, r.size - body);
            haveData = true;
        }
        else if (aiff && std::memcmp (c, "COMM", 4) == 0 && r.read (body, b, std::min<uint32_t> (size, 22)))
        {
            channels = (int) (int16_t) be16 (b);
            aiffFrames = be32 (b + 2);
            bits = (int) (int16_t) be16 (b + 6);
            if (size >= 22)
                isFloat = std::memcmp (b + 18, "fl32", 4) == 0 || std::memcmp (b + 18, "FL32", 4) == 0
                       || std::memcmp (b + 18, "fl64", 4) == 0 || std::memcmp (b + 18, "FL64", 4) == 0;
            haveFormat = true;
            haveData = true;                               // AIFF: Länge steht schon in COMM
        }
        // Nächster Chunk – wie beim Laden: fehlt ein Füllbyte, ohne Füllbyte weiterlesen
        uint64_t next = body + size + (size & 1);
        if ((size & 1) != 0)
        {
            auto plausibleAt = [&] (uint64_t p, unsigned char* id)
            {
                unsigned char hh[8];
                if (! r.read (p, hh, 8) || ! plausibleId (hh)) return false;
                std::memcpy (id, hh, 4);
                const uint32_t len = wav ? le32 (hh + 4) : be32 (hh + 4);
                return (uint64_t) len <= r.size - (p + 8);
            };
            unsigned char idP[4] = {}, idU[4] = {}, pad = 0;
            const bool paddedOk = plausibleAt (next, idP);
            const bool unpaddedOk = plausibleAt (body + size, idU);
            r.read (body + size, &pad, 1);
            if (readWithoutPadding (paddedOk, unpaddedOk, paddedOk && knownChunkId (idP), unpaddedOk && knownChunkId (idU), pad))
                next = body + size;
        }
        pos = next;
    }
    if (! haveFormat || channels <= 0 || bits <= 0)
        return 0;
    const uint64_t perSample = storedBytesPerSample (bits, isFloat);
    if (wav)
    {
        if (! haveData)
            return 0;
        const uint64_t fileBytesPerFrame = (uint64_t) channels * (uint64_t) wavBytesPerSample (bits, channels, blockAlign);
        return fileBytesPerFrame == 0 ? 0 : dataBytes / fileBytesPerFrame * (uint64_t) channels * perSample;
    }
    return aiffFrames * (uint64_t) channels * perSample;
}

namespace
{
// Was sampleRootKeyFromHeader und sampleLoopFromHeader brauchen – in einem Durchlauf über alle Chunks
struct HeaderInfo
{
    bool ok = false;
    int64_t frames = -1;                   // Anzahl Frames (WAV aus data/fmt, AIFF aus COMM)
    int smplRootKey = -1, instRootKey = -1;
    int64_t smplLoopStart = -1, smplLoopEnd = -1;
    int aiffLoopMode = 0, aiffLoopBeginId = -1, aiffLoopEndId = -1;
    std::map<int, uint32_t> markers;       // AIFF: Marker-Nummer → Position
    bool aiff = false;
};

HeaderInfo readHeaderInfo (const std::string& path)
{
    HeaderInfo info;
    std::ifstream f;
    openForReading (f, path);
    if (! f)
        return info;
    f.seekg (0, std::ios::end);
    const std::streamoff end = f.tellg();
    if (end < 12)
        return info;
    HeaderReader r { f, (uint64_t) end };
    unsigned char h[12];
    if (! r.read (0, h, 12))
        return info;
    const bool wav = std::memcmp (h, "RIFF", 4) == 0 && std::memcmp (h + 8, "WAVE", 4) == 0;
    const bool aiff = std::memcmp (h, "FORM", 4) == 0 && (std::memcmp (h + 8, "AIFF", 4) == 0 || std::memcmp (h + 8, "AIFC", 4) == 0);
    if (! wav && ! aiff)
        return info;
    info.aiff = aiff;
    auto le32 = [] (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); };
    auto le16 = [] (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8); };
    auto be32 = [] (const unsigned char* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3]; };
    auto be16 = [] (const unsigned char* p) { return ((uint32_t) p[0] << 8) | (uint32_t) p[1]; };

    int channels = 0, bits = 0, blockAlign = 0;
    uint64_t dataBytes = 0;
    bool haveFmt = false, haveData = false, haveComm = false, haveSmpl = false, haveInst = false, haveMark = false;

    uint64_t pos = 12;
    for (int guard = 0; guard < 1000 && pos + 8 <= r.size; ++guard)
    {
        unsigned char c[8];
        if (! r.read (pos, c, 8) || ! plausibleId (c))
            break;
        const uint32_t size = wav ? le32 (c + 4) : be32 (c + 4);
        const uint64_t body = pos + 8;
        unsigned char b[60] = {};

        if (wav && std::memcmp (c, "fmt ", 4) == 0 && ! haveFmt && r.read (body, b, std::min<uint32_t> (size, 16)))
        {
            channels = (int) le16 (b + 2);
            blockAlign = (int) le16 (b + 12);
            bits = (int) le16 (b + 14);
            haveFmt = true;
        }
        else if (wav && std::memcmp (c, "data", 4) == 0 && ! haveData)
        {
            dataBytes = std::min<uint64_t> (size, r.size - body);
            haveData = true;
        }
        else if (wav && std::memcmp (c, "smpl", 4) == 0 && ! haveSmpl && size >= 36
                 && r.read (body, b, std::min<uint32_t> (size, 60)))
        {
            haveSmpl = true;
            const uint32_t unity = le32 (b + 12);
            if (unity <= 127) info.smplRootKey = (int) unity;
            if (le32 (b + 28) > 0 && size >= 60)                       // Anzahl Loops > 0: ersten Loop nehmen
            {
                info.smplLoopStart = le32 (b + 36 + 8);
                info.smplLoopEnd   = le32 (b + 36 + 12);
            }
        }
        else if (wav && std::memcmp (c, "inst", 4) == 0 && ! haveInst && size >= 1 && r.read (body, b, 1))
        {
            haveInst = true;
            if (b[0] <= 127) info.instRootKey = b[0];
        }
        else if (aiff && std::memcmp (c, "COMM", 4) == 0 && ! haveComm && r.read (body, b, std::min<uint32_t> (size, 18)))
        {
            info.frames = be32 (b + 2);
            haveComm = true;
        }
        else if (aiff && std::memcmp (c, "INST", 4) == 0 && ! haveInst && size >= 14 && r.read (body, b, 14))
        {
            haveInst = true;
            if (b[0] <= 127) info.instRootKey = b[0];
            info.aiffLoopMode    = (int16_t) be16 (b + 8);
            info.aiffLoopBeginId = (int16_t) be16 (b + 10);
            info.aiffLoopEndId   = (int16_t) be16 (b + 12);
        }
        else if (aiff && std::memcmp (c, "MARK", 4) == 0 && ! haveMark && r.read (body, b, 2))
        {
            haveMark = true;
            const int count = (int) be16 (b);
            uint64_t p = body + 2;
            for (int i = 0; i < count && p + 7 <= body + size; ++i)
            {
                unsigned char m[7];
                if (! r.read (p, m, 7))
                    break;
                info.markers[(int) (int16_t) be16 (m)] = be32 (m + 2);
                const uint64_t nameLen = m[6];
                p += 6 + 1 + nameLen;
                if (((1 + nameLen) & 1) != 0)
                    ++p;                                               // Pascal-String auf gerade Länge aufgefüllt
            }
        }

        // Nächster Chunk – wie beim Laden: fehlt ein Füllbyte, ohne Füllbyte weiterlesen
        uint64_t next = body + size + (size & 1);
        if ((size & 1) != 0)
        {
            auto plausibleAt = [&] (uint64_t p, unsigned char* id)
            {
                unsigned char hh[8];
                if (! r.read (p, hh, 8) || ! plausibleId (hh)) return false;
                std::memcpy (id, hh, 4);
                const uint32_t len = wav ? le32 (hh + 4) : be32 (hh + 4);
                return (uint64_t) len <= r.size - (p + 8);
            };
            unsigned char idP[4] = {}, idU[4] = {}, pad = 0;
            const bool paddedOk = plausibleAt (next, idP);
            const bool unpaddedOk = plausibleAt (body + size, idU);
            r.read (body + size, &pad, 1);
            if (readWithoutPadding (paddedOk, unpaddedOk, paddedOk && knownChunkId (idP), unpaddedOk && knownChunkId (idU), pad))
                next = body + size;
        }
        pos = next;
    }

    if (wav && haveFmt && haveData && channels > 0 && bits > 0)
        info.frames = (int64_t) (dataBytes / ((uint64_t) channels * (uint64_t) wavBytesPerSample (bits, channels, blockAlign)));
    info.ok = true;
    return info;
}
} // namespace

int sampleRootKeyFromHeader (const std::string& path)
{
    const HeaderInfo info = readHeaderInfo (path);
    if (! info.ok)
        return -1;
    if (! info.aiff && info.smplRootKey >= 0)
        return info.smplRootKey;
    return info.instRootKey;                       // AIFF: INST; WAV: inst nur, wenn smpl fehlt
}

SampleLoopPoints sampleLoopFromHeader (const std::string& path)
{
    const HeaderInfo info = readHeaderInfo (path);
    SampleLoopPoints loop;
    if (! info.ok)
        return loop;

    int64_t start = -1, end = -1;
    if (! info.aiff)
    {
        start = info.smplLoopStart;
        end = info.smplLoopEnd;
    }
    else if (info.aiffLoopMode != 0 && info.markers.count (info.aiffLoopBeginId) && info.markers.count (info.aiffLoopEndId))
    {
        start = info.markers.at (info.aiffLoopBeginId);
        end = (int64_t) info.markers.at (info.aiffLoopEndId) - 1;   // Marker zwischen Samples: Ende inklusiv
    }

    if (start < 0 || end <= start || (info.frames >= 0 && start >= info.frames))
        return loop;                                                 // kein oder unplausibler Loop: -1 / -1
    if (info.frames > 0)
        end = std::min<int64_t> (end, info.frames - 1);              // auf das letzte Sample begrenzen
    if (end <= start)
        return loop;
    loop.start = start;
    loop.end = end;
    return loop;
}

bool loadSampleFile (const std::string& path, SampleData& out, std::string& error)
{
    out = SampleData();
    Bytes d;
    // RF64/BW64 (WAV über 4 GB, z. B. lange Aufnahmen): eigener Hinweis statt „not found“ – und vor dem Lesen
    // geprüft, damit eine riesige Datei gar nicht erst in den Speicher kommt (seit 23.11.2026)
    const std::string tag = fileTag (path);
    if (tag == "RF64" || tag == "BW64")
    {
        error = "RF64/BW64 WAV files (over 4 GB) are not supported";
        return false;
    }
    if (! readWholeFile (path, d))
    {
        error = "File not found or unreadable";
        return false;
    }

    Reader r { d };
    bool ok = false;
    if (r.tag (0, "RIFF") && r.tag (8, "WAVE"))
        ok = loadWav (d, out, error);
    else if (r.tag (0, "FORM") && (r.tag (8, "AIFF") || r.tag (8, "AIFC")))
        ok = loadAiff (d, out, error);
    else
        error = "Not a WAV or AIFF file (only WAV and AIFF are supported)";

    if (ok && (out.numFrames <= 0 || out.sampleRate <= 0))
    {
        error = "Empty sample file";
        ok = false;
    }
    return ok;
}

} // namespace maermel
