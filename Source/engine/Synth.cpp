// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// Synth.cpp – Märmel Engine

#include "Synth.h"
#include "VelocityResponse.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace maermel
{
namespace
{
constexpr float kHalfPi = 1.57079632679f;
constexpr float kSqrt2  = 1.41421356237f;


// 4-Punkt-Hermite-Interpolation (Catmull-Rom) zwischen y0 und y1
inline float hermite (float ym1, float y0, float y1, float y2, float t) noexcept
{
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}
} // namespace

double Synth::Voice::currentPitchNote() const noexcept
{
    return (double) note + 12.0 * std::log2 (glideFactor);
}

// ===========================================================================
// Vorbereitung und Einstellungen

void Synth::prepare (double hostSampleRate)
{
    crushAmountApplied = -1.0f;   // Lo-Fi-Rate hängt von der Samplerate ab: beim nächsten Block neu berechnen
    sampleRate = hostSampleRate;
    fadeStep = (float) (1.0 / (0.005 * sampleRate));   // Ausblenden in 5 ms
    for (auto& v : voices)
    {
        v = Voice();
        v.env.setSampleRate (sampleRate);
        v.env.setParams (envParams);
        v.filterEnv.setSampleRate (sampleRate);
        v.filterEnv.setParams (filterEnvParams);
        v.filterL.prepare (sampleRate);
        v.filterR.prepare (sampleRate);
    }
    smoothingCoeff = (float) std::exp (-16.0 / (0.02 * sampleRate));   // ca. 20 ms, pro 16-Sample-Block
    blockSmoothingCoeff = smoothingCoeff;
    chorus.prepare (sampleRate);          // alloziert den Delay-Puffer (nur hier, nie im Audio-Thread)
    chorusAmountSet = -1.0;
    smoothingPrimed = false;
    idleSamples = 0;
    monoLfo.restart();
    heldCount = 0;
    monoNote = -1;
    lastMonoPitch = -1.0;
    noteHadAttackVoices.fill (false);
}

void Synth::setEnvelope (const Envelope::Params& params)
{
    envParams = params;
    // Wirkt auch auf klingende Noten, ohne Sprung. Stille Plätze bekommen die Werte erst beim nächsten Anschlag
    // (startVoice) – spart beim Drehen eines Hüllkurvenreglers die Neuberechnung für bis zu 256 Plätze.
    for (auto& v : voices)
        if (v.active())
            v.env.setParams (params);
}

void Synth::setLfoRouting (float pitch, float filter, float volume, float pan) noexcept
{
    auto clamp = [] (float x) { return std::min (1.0f, std::max (-1.0f, x)); };
    routePitchTarget = clamp (pitch);
    routeFilterTarget = clamp (filter);
    routeVolumeTarget = clamp (volume);
    routePanTarget = clamp (pan);
}

void Synth::setFilterEnvelope (const Envelope::Params& params)
{
    filterEnvParams = params;
    for (auto& v : voices)
        if (v.active())                 // wie setEnvelope: stille Plätze erst beim Anschlag
            v.filterEnv.setParams (params);
}

void Synth::setVelocityRange (float decibels) noexcept
{
    velocityRangeDecibels = std::max (0.0f, decibels);
}

void Synth::setSampleStart (float start, float velocityAmount, bool random) noexcept
{
    startKnob = std::min (1.0f, std::max (0.0f, start));
    startVelKnob = std::min (1.0f, std::max (-1.0f, velocityAmount));
    randomStart = random;
}

void Synth::setFineTune (float cents) noexcept
{
    fineFactor = std::pow (2.0, (double) cents / 1200.0);
}

float Synth::crushBits (float amount) noexcept
{
    const float x = std::min (100.0f, std::max (0.0f, amount)) / 100.0f;
    // Die hohen Bittiefen braucht man selten: erste 30 % 24 → 15 Bit, restliche 70 % 15 → 11 Bit
    if (x <= 0.3f)
        return 24.0f - 9.0f * (x / 0.3f);
    return 15.0f - 4.0f * ((x - 0.3f) / 0.7f);
}

// Stufe der Ratenreduktion aus der Bittiefe: dieselbe Kennlinie wie bisher (bezogen auf 44,1 kHz), aber auf die
// nächste ganze oder halbe Haltelänge gerundet (1½ … 4). Damit gilt bei 44,1 und 48 kHz dasselbe Teilungs-
// verhältnis: 44,1 kHz → 29,4 / 22,05 / 17,64 / 14,7 / 12,6 / 11,025 kHz; 48 kHz → 32 / 24 / 19,2 / 16 / 13,7 / 12 kHz.
//
// Hohe Sampleraten (seit 19.11.2026): Bei 88,2/96 kHz wird die Haltelänge verdoppelt, bei 176,4/192 kHz
// vervierfacht. Vorher galt dort dieselbe Haltelänge wie bei 44,1 kHz – Lo-Fi 100 % ergab bei 96 kHz also 24 kHz
// statt 12 kHz und war kaum zu hören. Jetzt klingt Lo-Fi bei jeder Samplerate gleich; bei 44,1/48 kHz ändert
// sich nichts (bitgenau wie bisher). CrushChain zählt in halben Samples und verträgt jede Haltelänge.
int Synth::crushHoldStage (float bits, double sampleRate) noexcept
{
    const float hold = 44100.0f / Synth::crushRateHz (bits);
    const int half = (int) std::lround (hold * 2.0f);
    if (half < 3)
        return 0;                                         // unter 1½: keine Ratenreduktion (nur Bittiefe)

    int factor = 1;                                       // 44,1 / 48 kHz (und darunter)
    if (sampleRate >= 176000.0)
        factor = 4;                                       // 176,4 / 192 kHz
    else if (sampleRate >= 88000.0)
        factor = 2;                                       // 88,2 / 96 kHz

    return std::min (half, 8) * factor;                   // höchstens Haltelänge 4 (× Faktor)
}

float Synth::crushRateHz (float bits) noexcept
{
    // 11 Bit → 11 kHz, 16 Bit → 30 kHz; alle 3 Bit verdoppelt sich der Abstand zu 11 kHz (nach Gehör abgestimmt)
    return 11000.0f + 8740.0f * (std::pow (2.0f, (bits - 11.0f) / 3.0f) - 1.0f);
}

void Synth::setCrushDirect (float bits, float rateHz) noexcept
{
    crushDirect = true;
    crushAmountApplied = -1.0f;   // Messbetrieb: der zwischengespeicherte Wert gilt nicht mehr
    crushLevels = (bits > 0.0f && bits < 24.0f) ? std::pow (2.0f, bits - 1.0f) : 0.0f;
    crushInverse = (crushLevels > 0.0f) ? 1.0f / crushLevels : 0.0f;
    crushRate = (rateHz > 0.0f && rateHz < (float) sampleRate) ? rateHz : 0.0f;
    crushDirectRate = crushRate;
    crushHalfHold = 0;
}

void Synth::setCrush (float amount) noexcept
{
    fxAmount = std::max (0.0f, amount);   // Lo-Fi: geglättet in updateSmoothing; Chorus: eigene Glättung (20 ms)
}

void Synth::applyCrushAmount (float amount) noexcept
{
    // Wird alle 16 Samples aufgerufen; meist steht der Regler still. Dann sind Bittiefe und Rate dieselben wie
    // beim letzten Mal – das Ergebnis wäre bitgleich, also nichts neu rechnen (spart zwei pow() je Aufruf, im
    // Leerlauf mit Lo-Fi etwa die Hälfte der Rechenzeit der Engine; seit 04.11.2026).
    if (amount == crushAmountApplied)
        return;
    crushAmountApplied = amount;

    if (amount <= 0.0f)
    {
        crushLevels = crushInverse = crushRate = 0.0f;
        crushHalfHold = 0;
        return;
    }
    const float bits = crushBits (amount);
    crushLevels = std::pow (2.0f, bits - 1.0f);
    crushInverse = 1.0f / crushLevels;
    crushHalfHold = crushHoldStage (bits, sampleRate);
    crushRate = crushHalfHold > 0 ? (float) (sampleRate * 2.0 / crushHalfHold) : 0.0f;

}

int64_t Synth::computeStartFrame (const PlayableRegion& r, int velocity) noexcept
{
    // Nullpunkt ist der (geprüfte) offset aus der SFZ-Datei; davor wird nie gespielt
    const double sr = r.sample->sampleRate;
    const int64_t zero = r.startFrame;

    // Wirkbereich bis Loop-Ende bzw. Sample-Ende, abzüglich Platz für die Einblende
    const int64_t limit = (r.looping ? r.loopEnd : r.endFrame) - (int64_t) (0.003 * sr);
    if (limit <= zero || (startKnob <= 0.0f && startVelKnob == 0.0f))
        return zero;

    const double rangeMs = std::min (kSampleStartMaxMs, (double) (limit - zero) * 1000.0 / sr);

    // Kubische Kennlinie: der wichtige Bereich liegt bei den ersten Millisekunden
    double ms = rangeMs * (double) (startKnob * startKnob * startKnob);

    if (startVelKnob != 0.0f)
    {
        const double amount = (double) std::fabs (startVelKnob);
        const double shift = rangeMs * amount * amount * amount * (double) velocity / 127.0;
        ms += (startVelKnob > 0.0f) ? shift : -shift;   // rechts: nach hinten, links: nach vorn
    }

    ms = std::min (rangeMs, std::max (0.0, ms));
    if (randomStart)
        ms *= (double) nextRandom();                     // zufällig zwischen Nullpunkt und Startpunkt

    return zero + (int64_t) (ms * sr / 1000.0);
}

void Synth::setVoiceMode (VoiceMode mode, int numVoices) noexcept
{
    voiceMode = mode;
    // Mono/Legato: eine Note, aber mehrere Schichten (Velocity-Layer, Release-Samples) erlaubt
    maxVoices = (mode == VoiceMode::Poly) ? std::min (maxPolyphony, std::max (1, numVoices)) : monoNoteLimit;
}

// ===========================================================================
// Instrumentwechsel

std::shared_ptr<const Instrument> Synth::exchangeInstrument (std::shared_ptr<const Instrument> newInstrument) noexcept
{
    std::shared_ptr<const Instrument> toRetire;

    // Läuft noch ein früherer Wechsel, werden dessen Stimmen jetzt sofort beendet
    if (outgoing)
    {
        for (auto& v : voices)
            if (v.active() && v.owner == outgoing.get())
                v.region = nullptr;
        toRetire = std::move (outgoing);
        outgoing = nullptr;
    }

    // Stimmen des bisherigen Instruments ausblenden; es bleibt bis dahin erhalten
    if (instrument)
    {
        for (auto& v : voices)
            if (v.active() && v.owner == instrument.get())
                v.fading = true;
        outgoing = std::move (instrument);
    }

    instrument = std::move (newInstrument);
    keyswitch = instrument ? instrument->keyswitchDefault : -1;
    monoNote = -1;
    noteHadAttackVoices.fill (false);
    return toRetire;
}

std::shared_ptr<const Instrument> Synth::takeFinishedInstrument() noexcept
{
    if (! outgoing)
        return nullptr;
    for (const auto& v : voices)
        if (v.active() && v.owner == outgoing.get())
            return nullptr;   // noch nicht fertig ausgeblendet

    auto finished = std::move (outgoing);
    outgoing = nullptr;
    return finished;
}

// ===========================================================================
// Stimmenverwaltung

int Synth::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        if (v.active() && ! v.fading)
            ++n;
    return n;
}

void Synth::makeRoomForNote (int samplesNeeded)
{
    // Klingende Noten sammeln (nicht ausblendende Stimmen, gruppiert nach Note). Höchstens so viele Gruppen
    // wie Plätze; bei typischem Spiel sind es wenige, die Schleife bleibt kurz.
    struct Group { uint64_t id; bool released; };
    std::array<Group, voicePoolSize> groups;

    for (;;)
    {
        int numGroups = 0, free = 0, fadingCount = 0;
        for (const auto& v : voices)
        {
            if (! v.active()) { ++free; continue; }
            if (v.fading)     { ++fadingCount; continue; }
            const bool voiceReleased = ! v.held && ! v.sustained;
            int g = 0;
            while (g < numGroups && groups[(size_t) g].id != v.noteGroup)
                ++g;
            if (g == numGroups)
                groups[(size_t) numGroups++] = { v.noteGroup, voiceReleased };
            else if (! voiceReleased)
                groups[(size_t) g].released = false;   // ein gehaltenes Sample hält die ganze Note
        }

        const bool tooManyNotes = numGroups >= maxVoices;
        const bool poolTooSmall = free + fadingCount < samplesNeeded;
        if ((! tooManyNotes && ! poolTooSmall) || numGroups == 0)
            return;

        // Opfer: die älteste losgelassene Note, sonst die älteste (Kennungen steigen mit der Zeit)
        int victim = -1;
        for (int g = 0; g < numGroups; ++g)
        {
            const Group& c = groups[(size_t) g];
            if (victim < 0)
            {
                victim = g;
                continue;
            }
            const Group& best = groups[(size_t) victim];
            if ((c.released && ! best.released) || (c.released == best.released && c.id < best.id))
                victim = g;
        }
        const uint64_t id = groups[(size_t) victim].id;
        for (auto& v : voices)
            if (v.active() && ! v.fading && v.noteGroup == id)
                v.fading = true;                // ganze Note gemeinsam ausblenden (wenige Millisekunden)
    }
}

Synth::Voice* Synth::allocateVoice()
{
    // Die Notenzahl regelt makeRoomForNote; hier nur einen Platz finden.
    for (auto& v : voices)
        if (! v.active())
            return &v;

    // Pool voll: eine ausblendende Stimme sofort beenden – die leiseste (seit 23.11.2026; vorher die älteste).
    // Eine gerade erst zum Ausblenden markierte Stimme hat oft noch vollen Pegel; sie abzuschneiden knackt.
    // Bei gleichem Pegel gewinnt die ältere.
    Voice* quietest = nullptr;
    float quietestLevel = 0.0f;
    for (auto& v : voices)
    {
        if (! v.fading)
            continue;
        const float level = v.fadeGain * v.env.currentLevel();
        if (quietest == nullptr || level < quietestLevel
            || (level == quietestLevel && v.startOrder < quietest->startOrder))
        {
            quietest = &v;
            quietestLevel = level;
        }
    }

    if (quietest)
    {
        quietest->region = nullptr;
        return quietest;
    }
    return nullptr;
}

void Synth::chokeOffBy (const PlayableRegion& r, uint64_t currentNote) noexcept
{
    const int choking = r.sfz.group;
    if (choking == 0)
        return;
    for (auto& v : voices)
        if (v.active() && ! v.fading && v.owner == instrument.get() && v.noteGroup != currentNote
            && v.region->sfz.offBy == choking)
            v.fading = true;   // in wenigen Millisekunden ausblenden, nie hart abschneiden
}

void Synth::enforceGroupPolyphony (const PlayableRegion& r)
{
    const int limit = r.sfz.polyphony;
    if (limit <= 0)
        return;

    for (;;)
    {
        int count = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
        {
            if (! v.active() || v.fading || v.owner != instrument.get()
                || v.region->sfz.polyphonyGroup != r.sfz.polyphonyGroup)
                continue;
            ++count;
            if (! oldest || v.startOrder < oldest->startOrder)
                oldest = &v;
        }
        if (count < limit || ! oldest)
            return;
        oldest->fading = true;   // z. B. geschlossene Hi-Hat schneidet die offene ab
    }
}

double Synth::playbackStep (double cents, double fileSampleRate) const noexcept
{
    // Abspielschritt je Ausgabe-Sample: Tonhöhe (Cent) und Verhältnis der Sampleraten. Begrenzt auf ±10 Oktaven –
    // mehr ergibt keinen Klang mehr, und unsinnige Werte (z. B. aus beschädigten Dateien) bleiben endlich.
    const double limited = std::min (12000.0, std::max (-12000.0, cents));
    const double rateRatio = (fileSampleRate > 0.0 && sampleRate > 0.0) ? fileSampleRate / sampleRate : 1.0;
    return std::pow (2.0, limited / 1200.0) * rateRatio;
}

float Synth::nextRandom() noexcept
{
    // xorshift32: schnell, echtzeitfähig, für Round Robin völlig ausreichend
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return (float) (randomState >> 8) * (1.0f / 16777216.0f);   // 0 ... <1
}

bool Synth::isKeyswitchKey (int note) const noexcept
{
    return instrument && instrument->hasKeyswitches()
        && note >= instrument->keyswitchLow && note <= instrument->keyswitchHigh;
}

bool Synth::anyRegionOnKey (int note) const noexcept
{
    if (! instrument)
        return false;
    const int selection = selectionNote (note);   // mit C.Tune und KeyShift, wie beim Anschlag
    for (const auto& r : instrument->regions)
        if (selection >= r.sfz.loKey && selection <= r.sfz.hiKey)
            return true;
    return false;
}

bool Synth::passesSequenceAndRandom (const PlayableRegion& r) noexcept
{
    const Region& s = r.sfz;
    if (s.seqLength > 1)
    {
        // Jede passende Region zählt bei jedem Anschlag weiter und spielt nur an ihrer Stelle
        const uint32_t step = r.seqCounter++ % (uint32_t) s.seqLength;
        if ((int) step != s.seqPosition - 1)
            return false;
    }
    return noteRandom >= s.loRand && (noteRandom < s.hiRand || s.hiRand >= 1.0f);
}

void Synth::controller (int number, int value) noexcept
{
    if (number >= 0 && number < 128)
        ccValues[(size_t) number] = (uint8_t) std::min (127, std::max (0, value));
}

bool Synth::ccConditionsMet (const Region& s) const noexcept
{
    for (const auto& c : s.ccConditions)
    {
        const int v = ccValues[(size_t) c.cc];
        if (v < c.lo || v > c.hi)
            return false;
    }
    return true;
}

namespace
{
// Überblendung nach SFZ 1: xfin (unter lo stumm, ab hi voll) und xfout (bis lo voll, ab hi stumm).
// power = gleiche Leistung (Wurzel), gain = linear. Bei lo == hi: harter Wechsel an dieser Stelle.
float crossfadeIn (int value, int lo, int hi) noexcept
{
    if (value >= hi) return 1.0f;
    if (value <= lo) return 0.0f;
    return (float) (value - lo) / (float) (hi - lo);
}
float crossfadeOut (int value, int lo, int hi) noexcept
{
    if (value <= lo) return 1.0f;
    if (value >= hi) return 0.0f;
    return (float) (hi - value) / (float) (hi - lo);
}
float crossfadeCurve (float x, bool power) noexcept { return power ? std::sqrt (x) : x; }
} // namespace

float Synth::velocityCrossfade (const Region& s, int velocity) noexcept
{
    if (s.xfinHiVel == 0 && s.xfoutLoVel == 127)
        return 1.0f;                                     // keine Velocity-Überblendung
    return crossfadeCurve (crossfadeIn (velocity, s.xfinLoVel, s.xfinHiVel), s.xfVelPower)
         * crossfadeCurve (crossfadeOut (velocity, s.xfoutLoVel, s.xfoutHiVel), s.xfVelPower);
}

float Synth::controllerCrossfade (const Region& s) const noexcept
{
    float gain = 1.0f;
    for (const auto& c : s.ccCrossfades)
    {
        const int v = ccValues[(size_t) c.cc];
        gain *= crossfadeCurve (crossfadeIn (v, c.inLo, c.inHi), s.xfCcPower)
              * crossfadeCurve (crossfadeOut (v, c.outLo, c.outHi), s.xfCcPower);
    }
    return gain;
}

bool Synth::regionTriggers (const PlayableRegion& r, int note, int velocity, bool otherKeysHeld) const noexcept
{
    const Region& s = r.sfz;
    if (note < s.loKey || note > s.hiKey || velocity < s.loVel || velocity > s.hiVel)
        return false;
    if (velocityCrossfade (s, velocity) <= 0.0f)
        return false;                                // Velocity-Überblendung: bei dieser Stärke stumm
    if (! ccConditionsMet (s))
        return false;                                // z. B. Pedal-Schicht ohne gedrücktes Pedal
    if (s.swLast >= 0 && s.swLast != keyswitch)
        return false;                                // andere Artikulation aktiv

    switch (s.trigger)
    {
        case Trigger::Attack:  return true;
        case Trigger::First:   return ! otherKeysHeld;
        case Trigger::Legato:  return otherKeysHeld;
        case Trigger::Release: return false;
    }
    return false;
}

void Synth::startVoice (Voice& v, const PlayableRegion& r, int note, int velocity)
{
    const SampleData& data = *r.sample;
    const Region& s = r.sfz;

    // Tonhöhe aus der wirksamen Note (gespielte Note + C.Tune); KeyShift ändert nur die Region
    const double cents = (double) (effectiveNote (note) - s.pitchKeycenter) * s.pitchKeytrack
                       + s.transpose * 100.0 + s.tune;

    v.region = &r;
    v.owner = instrument.get();
    v.note = note;
    v.channel = noteChannel[(size_t) note];
    v.pressure = -1.0f;
    v.startOrder = ++orderCounter;
    v.held = true;
    v.sustained = false;
    v.isReleaseVoice = false;
    v.fading = false;
    v.fadeGain = 1.0f;
    v.fadeInGain = 1.0f;
    v.fadeInStep = 0.0f;
    v.glideFactor = 1.0;
    v.glideRatio = 1.0;
    v.glideRemaining = 0;
    v.driveL.reset();
    v.driveR.reset();
    v.position = (double) computeStartFrame (r, velocity);
    v.loopWrapped = false;
    if (v.position > 0.0)
    {
        v.fadeInGain = 0.0f;                                   // nicht am Dateianfang: kurz einblenden
        v.fadeInStep = (float) (1.0 / (0.002 * sampleRate));   // 2 ms gegen Knackser
    }
    v.baseIncrement = playbackStep (cents, data.sampleRate);

    // Velocity-Lautstärke wird vor dem Drive angewendet (harte Anschläge verzerren stärker);
    // SFZ-volume und Amp Key (Lautstärke nach Note, fest ab dem Anschlag) danach
    // Lautstärke nach der Standard-Velocity-Kurve: Velocity 127 = 0 dB,
    // Velocity 1 = −Bereich (Vel in der Amp-Sektion)
    v.velocityGain = std::pow (10.0f, -velocityRangeDecibels * (1.0f - velocityResponse (velocity)) / 20.0f);
    // Amp Key wirkt laufend (pro Block, siehe renderVoice); hier nur die wirksame Note merken
    v.keyNote = effectiveNote (note);
    v.xfadeVel = velocityCrossfade (s, velocity);
    v.xfadeCc = -1.0f;                                   // erster Block: ohne Gleiten auf den aktuellen Stand
    const float gain = std::pow (10.0f, s.volume / 20.0f);
    const float pan = std::min (1.0f, std::max (-1.0f, s.pan / 100.0f));

    if (data.numChannels == 1)
    {
        // Mono: Panorama mit gleicher Leistung, Mitte = volle Lautstärke auf beiden Seiten
        const float angle = (pan + 1.0f) * 0.5f * kHalfPi;
        v.gainLeft  = gain * std::cos (angle) * kSqrt2;
        v.gainRight = gain * std::sin (angle) * kSqrt2;
    }
    else
    {
        // Stereo: Balance
        v.gainLeft  = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
        v.gainRight = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    }

    // Hüllkurven: Velocity und Note bestimmen die Zeiten dieser Stimme (Vel-Regler, Key)
    v.env.setParamsAndContext (envParams, velocity, effectiveNote (note));   // Koeffizienten nur einmal berechnen
    v.env.noteOn();

    // Filter: Hüllkurve, Keytracking (Oktaven relativ zu C4) und Velocity-Anteil beim Anschlag
    v.filterEnv.setParamsAndContext (filterEnvParams, velocity, effectiveNote (note));
    v.filterEnv.noteOn();
    v.filterL.start (filterType);
    v.filterR.start (filterType);
    v.keyOctaveOffset = (float) (effectiveNote (note) - 60) / 12.0f;   // × Key-Regler laufend in renderVoice
    v.polyLfo.restart();
    v.modGainLeft = v.modGainRight = -1.0f;
    v.sustainLift = -1.0f;
    // Anschlag und Velocity-Regler werden beim Anschlag festgehalten; Env wirkt laufend (geglättet)
    v.velocityNorm = velocityResponse (velocity);   // Standard-Velocity-Kurve
    v.velEnvAtOn = filterVelocity;
    v.velDirectAtOn = filterVelocityCutoff;
}

bool Synth::retargetMonoVoices (int newNote, double glideFromPitch)
{
    bool any = false;
    for (auto& v : voices)
    {
        if (! v.active() || v.fading || v.isReleaseVoice || v.note != monoNote || v.owner != instrument.get())
            continue;
        const PlayableRegion& r = *v.region;
        const Region& s = r.sfz;
        // Tonhöhe wie in startVoice, aber für die neue Note – gleiches Sample, gleiche Position
        const double cents = (double) (effectiveNote (newNote) - s.pitchKeycenter) * s.pitchKeytrack
                           + s.transpose * 100.0 + s.tune;
        v.baseIncrement = playbackStep (cents, r.sample->sampleRate);
        v.note = newNote;
        v.channel = noteChannel[(size_t) newNote];   // Legato: Aftertouch folgt der neuen Taste
        v.keyNote = effectiveNote (newNote);
        v.keyOctaveOffset = (float) (effectiveNote (newNote) - 60) / 12.0f;
        v.glideFactor = 1.0;                      // ein laufendes Glide endet hier; startGlide setzt neu an
        v.glideRatio = 1.0;
        v.glideRemaining = 0;
        startGlide (v, glideFromPitch);           // ohne Glide: Tonhöhe springt sofort
        any = true;
    }
    if (any)
        noteHadAttackVoices[(size_t) newNote] = true;
    return any;
}

void Synth::startGlide (Voice& v, double fromPitch) noexcept
{
    if (glideSeconds <= 0.0f || fromPitch < 0.0)
        return;

    const double offset = fromPitch - (double) v.note;   // in Halbtönen
    if (std::fabs (offset) < 1.0e-6)
        return;

    const int steps = std::max (1, (int) (glideSeconds * sampleRate));
    v.glideFactor = std::pow (2.0, offset / 12.0);
    v.glideRatio = std::pow (2.0, -offset / 12.0 / (double) steps);
    v.glideRemaining = steps;
}

int Synth::startNote (int note, int velocity, double glideFromPitch)
{
    if (! instrument)
        return 0;

    const bool otherKeysHeld = heldCount > 1;
    noteVelocity[(size_t) note] = velocity;
    const int selection = selectionNote (note);   // C.Tune und KeyShift
    int started = 0;

    // Platz für die ganze Note schaffen (obere Grenze: Round Robin und Zufall wählen später ggf. weniger aus)
    int needed = 0;
    for (const auto& r : instrument->regions)
        if (regionTriggers (r, selection, velocity, otherKeysHeld))
            ++needed;
    if (needed == 0)
    {
        noteHadAttackVoices[(size_t) note] = false;
        return 0;
    }
    makeRoomForNote (needed);
    const uint64_t group = ++noteGroupCounter;

    for (const auto& r : instrument->regions)
    {
        if (! regionTriggers (r, selection, velocity, otherKeysHeld) || ! passesSequenceAndRandom (r))
            continue;

        chokeOffBy (r, group);
        enforceGroupPolyphony (r);
        Voice* v = allocateVoice();
        if (! v)
            continue;

        startVoice (*v, r, note, velocity);
        v->noteGroup = group;
        startGlide (*v, glideFromPitch);
        ++started;
    }

    noteHadAttackVoices[(size_t) note] = started > 0;
    return started;
}

Synth::Voice* Synth::firstSoundingVoice (int note)
{
    for (auto& v : voices)
        if (v.active() && ! v.fading && ! v.isReleaseVoice && v.note == note && v.owner == instrument.get())
            return &v;
    return nullptr;
}

void Synth::fadeNoteVoices (int note)
{
    for (auto& v : voices)
        if (v.active() && ! v.isReleaseVoice && v.note == note)
            v.fading = true;
}

void Synth::startReleaseSamples (int note)
{
    if (! instrument || note < 0)
        return;

    // Startpegel = aktueller Hüllkurvenpegel der auslösenden Note
    float level = 0.0f, filterLevel = 0.0f;
    bool found = false;
    for (const auto& v : voices)
    {
        if (v.active() && ! v.fading && ! v.isReleaseVoice && v.note == note && (v.held || v.sustained))
        {
            level = std::max (level, v.env.currentLevel());
            filterLevel = std::max (filterLevel, v.filterEnv.currentLevel());
            found = true;
        }
    }
    if (! found)
        level = noteHadAttackVoices[(size_t) note] ? 0.0f : 1.0f;   // reine Release-Instrumente: volle Stärke
    if (level < 1.0e-4f)
        return;

    const int velocity = noteVelocity[(size_t) note];
    const float fadeIn = (float) (1.0 / (0.002 * sampleRate));   // 2 ms gegen Knackser
    noteRandom = nextRandom();

    const int selection = selectionNote (note);

    auto releaseRegionMatches = [&] (const Region& s)
    {
        return s.trigger == Trigger::Release && selection >= s.loKey && selection <= s.hiKey
            && velocity >= s.loVel && velocity <= s.hiVel && velocityCrossfade (s, velocity) > 0.0f
            && (s.swLast < 0 || s.swLast == keyswitch) && ccConditionsMet (s);
    };

    // Release-Samples einer Note zählen zusammen als eine Note (bevorzugt verdrängt, sie sind ja losgelassen)
    int needed = 0;
    for (const auto& r : instrument->regions)
        if (releaseRegionMatches (r.sfz))
            ++needed;
    if (needed == 0)
        return;
    makeRoomForNote (needed);
    const uint64_t group = ++noteGroupCounter;

    for (const auto& r : instrument->regions)
    {
        const Region& s = r.sfz;
        if (! releaseRegionMatches (s))
            continue;
        if (! passesSequenceAndRandom (r))
            continue;

        chokeOffBy (r, group);
        enforceGroupPolyphony (r);
        Voice* v = allocateVoice();
        if (! v)
            continue;

        startVoice (*v, r, note, velocity);
        v->noteGroup = group;
        v->position = (double) r.startFrame;   // Release-Samples ohne Startpunkt-Verschiebung
        v->isReleaseVoice = true;
        v->held = false;
        v->env.startRelease (level);    // direkt in die R-Phase der Oberflächen-Hüllkurve
        v->filterEnv.startRelease (found ? filterLevel : 1.0f);   // Filterhüllkurve ebenso
        v->fadeInGain = 0.0f;
        v->fadeInStep = fadeIn;
    }
}

void Synth::releaseVoice (Voice& v)
{
    v.held = false;
    v.sustained = false;
    if (! v.region->oneShot && ! v.isReleaseVoice)   // One Shot läuft immer bis zum Ende
    {
        v.env.noteOff();
        v.filterEnv.noteOff();
    }
}

void Synth::releaseNoteVoices (int note)
{
    for (auto& v : voices)
        if (v.active() && ! v.isReleaseVoice && v.note == note && (v.held || v.sustained))
            releaseVoice (v);
}

// ===========================================================================
// Gehaltene Tasten

void Synth::pushHeld (int note) noexcept
{
    removeHeld (note);
    if (heldCount < (int) heldStack.size())
        heldStack[(size_t) heldCount++] = note;
}

void Synth::removeHeld (int note) noexcept
{
    for (int i = 0; i < heldCount; ++i)
    {
        if (heldStack[(size_t) i] == note)
        {
            for (int j = i; j < heldCount - 1; ++j)
                heldStack[(size_t) j] = heldStack[(size_t) j + 1];
            --heldCount;
            return;
        }
    }
}

// ===========================================================================
// MIDI

void Synth::noteOn (int note, int velocity, int channel)
{
    if (note < 0 || note > 127)
        return;
    if (velocity <= 0)
    {
        noteOff (note);
        return;
    }
    velocity = std::min (velocity, 127);   // Engine auch bei direktem Aufruf (Testprogramme) im MIDI-Bereich
    noteChannel[(size_t) note] = (uint8_t) std::min (15, std::max (0, channel));
    notePressure[(size_t) note] = 0.0f;   // neuer Anschlag: Poly-Aftertouch dieser Taste beginnt bei 0

    // Keyswitch: Artikulation wechseln. Liegt auf der Taste kein Klang, ist hier Schluss –
    // die Taste zählt dann auch nicht als gehaltene Note (wichtig für Mono/Legato).
    if (isKeyswitchKey (note))
    {
        keyswitch = note;
        if (! anyRegionOnKey (note))
            return;
    }

    // Nach einer Pause (keine Taste gehalten): Mono-LFO – Delay blendet neu ein, die Phase läuft frei weiter
    // (seit 15.10.2026, Wunsch von Björn: Modulation unabhängig vom Anschlag). Env Mode startet den Durchgang neu.
    if (heldCount == 0 && ! pedalDown)
    {
        if (monoLfoParams.envMode)
            monoLfo.restart();
        else
            monoLfo.restartDelay();
    }

    pushHeld (note);
    noteVelocity[(size_t) note] = velocity;
    noteRandom = nextRandom();

    if (voiceMode == VoiceMode::Poly)
    {
        startNote (note, velocity, -1.0);
        return;
    }

    // --- Mono und Legato: nur eine Note klingt, die zuletzt gespielte hat Vorrang ---
    Voice* current = (monoNote >= 0) ? firstSoundingVoice (monoNote) : nullptr;
    const double fromPitch = current ? current->currentPitchNote() : lastMonoPitch;

    // "Gebunden": die vorher klingende Taste ist noch gedrückt
    const bool tied = current != nullptr && heldCount > 1 && ! current->env.isReleasing();

    // Einstimmig: alle bisherigen Stimmen (auch ausklingende) ausblenden, Release-Samples ausgenommen
    auto fadeAllPlaying = [this]
    {
        for (auto& v : voices)
            if (v.active() && ! v.isReleaseVoice)
                v.fading = true;
    };

    if (voiceMode == VoiceMode::Legato && tied)
    {
        // Legato: kein neues Sample – die klingende Stimme wechselt nur die Tonhöhe (mit Glide);
        // Sample-Position, Hüllkurven, Filter und LFO laufen unverändert weiter
        retargetMonoVoices (note, fromPitch);
    }
    else
    {
        fadeAllPlaying();
        // Mono: Glide immer; Legato: nur bei gebundenen Noten (hier also nicht)
        startNote (note, velocity, voiceMode == VoiceMode::Mono ? fromPitch : -1.0);
    }

    monoNote = note;
    lastMonoPitch = (double) note;
}

void Synth::noteOff (int note)
{
    if (note < 0 || note > 127)
        return;
    // Das Loslassen wird nie verschluckt – auch nicht bei Keyswitch-Tasten. Gibt es keine
    // Stimmen dieser Taste, passiert einfach nichts; sonst könnten Stimmen hängen bleiben.
    removeHeld (note);

    if (voiceMode == VoiceMode::Poly)
    {
        releaseOrSustain (note);
        return;
    }

    // --- Mono und Legato ---
    if (note != monoNote)
    {
        // Diese Taste ist nicht die aktuelle Mono-Note. Klingen trotzdem Stimmen von ihr –
        // etwa weil der Stimmenmodus umgeschaltet wurde, während sie gehalten war –,
        // werden sie normal freigegeben, sonst würden sie hängen bleiben.
        releaseOrSustain (note);
        return;
    }

    if (heldCount > 0)
    {
        // Zurück zur zuletzt gespielten, noch gehaltenen Taste
        const int target = topHeld();
        Voice* current = firstSoundingVoice (monoNote);
        const double fromPitch = current ? current->currentPitchNote() : (double) monoNote;

        if (voiceMode == VoiceMode::Legato && current)
        {
            retargetMonoVoices (target, fromPitch);   // zurück zur gehaltenen Taste: nur die Tonhöhe ändern
        }
        else
        {
            fadeNoteVoices (monoNote);
            startNote (target, noteVelocity[(size_t) target], fromPitch);
        }
        monoNote = target;
        lastMonoPitch = (double) target;
        return;
    }

    releaseOrSustain (note);
}

void Synth::releaseOrSustain (int note)
{
    bool anyHeld = false;
    for (const auto& v : voices)
        if (v.active() && ! v.isReleaseVoice && v.note == note && v.held)
            anyHeld = true;
    if (! anyHeld)
        return;

    if (pedalDown)
    {
        for (auto& v : voices)
            if (v.active() && ! v.isReleaseVoice && v.note == note && v.held && ! v.region->oneShot)
            {
                v.held = false;
                v.sustained = true;
            }
        return;
    }
    startReleaseSamples (note);
    releaseNoteVoices (note);
}

void Synth::sustainPedal (bool down)
{
    pedalDown = down;                   // der exakte CC-64-Wert für locc64/hicc64 kommt über controller()
    if (down)
        return;

    // Pedal losgelassen: nur Stimmen freigeben, die allein vom Pedal gehalten werden (v.sustained).
    // Stimmen mit gedrückter Taste klingen weiter – auch wenn dieselbe Taste unter dem Pedal schon einmal
    // angeschlagen wurde (typischer Pedalwechsel beim Klavier: Akkord anschlagen, Pedal lüften und neu treten,
    // Tasten dabei gehalten). Früher wurden hier alle Stimmen der Taste freigegeben und der neue Anschlag
    // abgeschnitten.
    std::array<bool, 128> pedalOnly {};
    for (const auto& v : voices)
        if (v.active() && ! v.isReleaseVoice && v.sustained && v.note >= 0 && v.note < 128)
            pedalOnly[(size_t) v.note] = true;

    for (int note = 0; note < 128; ++note)
    {
        if (! pedalOnly[(size_t) note])
            continue;

        // Release-Samples einmal je Taste (nicht je Stimme) und nur, wenn die Taste nicht gerade gedrückt ist
        bool keyDown = false;
        for (const auto& v : voices)
            if (v.active() && ! v.isReleaseVoice && v.note == note && v.held)
                keyDown = true;
        if (! keyDown)
            startReleaseSamples (note);

        for (auto& v : voices)
            if (v.active() && ! v.isReleaseVoice && v.note == note && v.sustained)
                releaseVoice (v);
    }
}

void Synth::pitchBend (int value14Bit)
{
    bendNormalized = (float) (std::min (16383, std::max (0, value14Bit)) - 8192) / 8192.0f;
    bendFactor = std::pow (2.0, (double) (bendNormalized * bendRange) / 12.0);
}

void Synth::setPitchBendRange (float semitones) noexcept
{
    if (semitones == bendRange)
        return;
    bendRange = std::min (12.0f, std::max (0.0f, semitones));
    bendFactor = std::pow (2.0, (double) (bendNormalized * bendRange) / 12.0);
}

// Alle Noten loslassen (MIDI CC123/CC120 und Menü „All Notes Off“).
// Bewusst NICHT über sustainPedal (false): releaseVoice unten gibt ohnehin alle Stimmen frei, auch die nur vom
// Pedal gehaltenen. sustainPedal (false) würde zusätzlich Release-Samples (trigger=release) starten – also neue
// Klänge genau dann, wenn alles verstummen soll. (Anders bei CC121, resetControllers: Dort soll es wie ein normales
// Loslassen des Pedals klingen.)
void Synth::allNotesOff()
{
    pedalDown = false;
    ccValues[64] = 0;   // auch der gemerkte CC-64-Wert für locc64/hicc64 (seit 1.0.3; bis dahin nur im Menü-Weg)
    heldCount = 0;
    resetPressure();   // kein hängender Aftertouch (das Modulationsrad bleibt, wo es steht)
    for (auto& v : voices)
        if (v.active())
            releaseVoice (v);
}

// CC121 „Reset All Controllers“ nach der MIDI-Empfehlung RP-015: Spielhilfen zurück in die Ruhelage.
// Zurückgesetzt: Pitch Bend (Mitte), Modulationsrad, Kanal- und Poly-Aftertouch, Pedale (Sustain, Portamento,
// Sostenuto, Soft). Nicht zurückgesetzt (so verlangt es RP-015): Volume (CC7), Pan (CC10) und alle anderen
// Controller. Gedrückte Tasten klingen weiter; Noten, die nur noch das Sustain-Pedal hält, gehen in den Release
// – genau wie beim Loslassen des Pedals.
void Synth::resetControllers()
{
    pitchBend (8192);
    modWheel (0);
    resetPressure();
    for (int cc : { 1, 64, 65, 66, 67 })   // gemerkter Stand für loccN/hiccN
        controller (cc, 0);
    if (pedalDown)
        sustainPedal (false);
}

// ===========================================================================
// Wiedergabe

bool Synth::renderVoice (Voice& v, float* left, float* right, int numSamples)
{
    const PlayableRegion& r = *v.region;
    const SampleData& d = *r.sample;
    const int channels = d.numChannels;
    const int64_t endFrame = r.endFrame;
    const int64_t loopLength = r.loopEnd - r.loopStart + 1;

    // LFO-Modulation für diesen Block: Fader links = Poly-LFO, rechts = Mono-LFO, Tiefe = Auslenkung²
    const float polyLfoValue = v.polyLfo.advance (polyLfoParams, blockSeconds);
    auto modulation = [&] (float route) { return route * std::fabs (route) * (route < 0.0f ? -polyLfoValue : monoLfoValue); };
    // Modulationsrad bzw. Aftertouch: der größere Wert aus Rad (global) und Aftertouch dieser Stimme (Kanal-Aftertouch
    // ihres Kanals oder Poly-Aftertouch ihrer Taste), geglättet wie die Controller-Überblendung (ca. 20 ms)
    const float pressureTarget = std::max (chanPressure[(size_t) v.channel], notePressure[(size_t) v.note]);
    v.pressure = (v.pressure < 0.0f) ? pressureTarget
                                     : blockSmoothingCoeff * v.pressure + (1.0f - blockSmoothingCoeff) * pressureTarget;
    const float wheelNow = std::max (modWheelSmoothed, v.pressure);
    auto wheelFactor = [&] (float w) { return 1.0f - w + w * wheelNow; };

    const float lfoSemitones = 12.0f * modulation (routePitch) * wheelFactor (wheelPitch);
    const double lfoPitchFactor = (lfoSemitones != 0.0f) ? std::exp2 ((double) lfoSemitones / 12.0) : 1.0;
    const float lfoOctaves = 4.0f * modulation (routeFilter) * wheelFactor (wheelFilter);

    // Lautstärke zwischen voll und stumm, Panorama als Balance; innerhalb des Blocks gleitend
    // Wheel LFO (Amp) wirkt gemeinsam auf LFO-Volume und LFO-Pan, wie Wheel LFO bei Pitch und Filter
    const float ampWheel = wheelFactor (wheelAmp);
    const float volumeDepth = routeVolume * routeVolume * ampWheel;
    const float volumeSource = (routeVolume < 0.0f) ? polyLfoValue : monoLfoValue;
    const LfoParams& volumeLfo = (routeVolume < 0.0f) ? polyLfoParams : monoLfoParams;
    // Env Mode: wirkt wie eine Hüllkurve zwischen voll und (bei 100 %) stumm; Invert liefert die Gegenkurve
    const float lfoGain = (volumeLfo.envMode && ! volumeLfo.dc)
                            ? 1.0f - volumeDepth * ((routeVolume < 0.0f) ? v.polyLfo : monoLfo).volumeReduction()
                            : 1.0f - volumeDepth * 0.5f * (1.0f - volumeSource);
    // DC ohne Invert auf Volume hebt den Sustain der Amp-Hüllkurve an (in die Luft bis zum Höchstwert), statt
    // wirkungslos zu bleiben – z. B. Lautstärke per Aftertouch hochziehen. Gleitet im Block mit.
    const float sustainLiftTarget = (volumeLfo.dc && ! volumeLfo.invert) ? volumeDepth : 0.0f;
    if (v.sustainLift < 0.0f)
        v.sustainLift = sustainLiftTarget;
    const float sustainLiftStep = (sustainLiftTarget - v.sustainLift) / (float) numSamples;
    // Panorama: LFO als Balance (das Panorama der Region aus der SFZ-Datei steckt schon in gainLeft/Right)
    const float pan = std::min (1.0f, std::max (-1.0f, modulation (routePan) * ampWheel));
    // Amp Key laufend: Lautstärke nach Note mit dem aktuellen (geglätteten) Regler; gleitet im Block mit
    const float keyGain = (ampKeyScaleSmoothed != 0.0f)
                            ? std::pow (10.0f, ampKeyScaleDecibels (ampKeyScaleSmoothed, v.keyNote) / 20.0f) : 1.0f;
    // Überblendungen: Velocity fest, Controller laufend (geglättet wie die Regler, ca. 20 ms – ein Pedal oder
    // Modulationsrad blendet so ohne Knacken ein und aus)
    float xfade = v.xfadeVel;
    if (! r.sfz.ccCrossfades.empty())
    {
        const float target = controllerCrossfade (r.sfz);
        v.xfadeCc = (v.xfadeCc < 0.0f) ? target : blockSmoothingCoeff * v.xfadeCc + (1.0f - blockSmoothingCoeff) * target;
        xfade *= v.xfadeCc;
    }
    const float targetLeft  = keyGain * lfoGain * xfade * (1.0f - std::max (0.0f, pan));
    const float targetRight = keyGain * lfoGain * xfade * (1.0f - std::max (0.0f, -pan));
    if (v.modGainLeft < 0.0f)
    {
        v.modGainLeft = targetLeft;
        v.modGainRight = targetRight;
    }
    const float stepLeft  = (targetLeft  - v.modGainLeft)  / (float) numSamples;
    const float stepRight = (targetRight - v.modGainRight) / (float) numSamples;

    // Filter für diesen kurzen Block (16 Samples) einstellen
    v.filterL.setType (filterType);
    v.filterR.setType (filterType);
    const bool filterActive = ! v.filterL.isBypassed();
    if (filterActive)
    {
        // Cutoff = Ruhezustand; Vel, Vel Env und Env addieren sich (zusammen höchstens ±10 Oktaven)
        const float octaves = cutoffSmoothed + v.keyOctaveOffset * filterKeyTrackSmoothed + lfoOctaves
                            + filterModulationOctaves (envAmountSmoothed, v.velEnvAtOn, v.velDirectAtOn,
                                                       v.velocityNorm, v.filterEnv.currentLevel());
        const float hz = 20.0f * std::exp2 (octaves);
        v.filterL.setCutoff (hz, blockResonance);          // Resonanzwerte einmal je Block (updateSmoothing)
        if (channels > 1)
            v.filterR.copyCoefficientsFrom (v.filterL);     // gleiche Werte wie links, ohne sie neu zu rechnen
    }
    const float driveGain = blockDriveGain;
    const float driveMakeup = blockDriveMakeup;

    for (int i = 0; i < numSamples; ++i)
    {
        // loop_sustain loopt nur, solange die Note gehalten wird
        const bool loopActive = r.looping && (! r.loopOnlyWhileHeld || v.held || v.sustained);

        const int64_t idx = (int64_t) v.position;
        const float t = (float) (v.position - (double) idx);

        // Sample-Wert eines Frames für die Interpolation; im Loop werden die Nachbarn an den Loop-Grenzen umgeleitet.
        // Die Interpolation braucht auch den Nachbarn VOR der aktuellen Position (idx - 1). Steht die Position nach
        // einem Rücksprung auf loopStart, ist dieser Nachbar das Loop-Ende – nicht das Audio vor dem Loop.
        // (Seit 23.11.2026; vorher wurde loopStart - 1 gelesen. Hörbar bei Single-Cycle-Wellenformen, wo der Loop
        // nur wenige Samples lang ist: zusätzliche Obertöne in jeder Periode.) Beim ersten Hineinlaufen in den Loop
        // (noch kein Rücksprung) ist loopStart - 1 dagegen richtig.
        auto frameValue = [&] (int64_t f, int ch) -> float
        {
            if (loopActive)
            {
                if (f > r.loopEnd)
                    f = r.loopStart + (f - r.loopEnd - 1) % loopLength;   // hinter dem Ende: von vorn
                else if (f < r.loopStart && v.loopWrapped)
                    f += loopLength;                                      // vor dem Anfang, schon im Loop: Loop-Ende
            }
            if (f < 0)
                f = 0;
            if (f > endFrame)
                return 0.0f;
            return d.valueAt (f, ch);
        };

        float gain = v.env.next();
        v.sustainLift += sustainLiftStep;
        if (v.sustainLift > 0.0f)
            gain = v.env.boostedLevel (v.sustainLift);
        v.filterEnv.next();

        if (v.fadeInStep > 0.0f)
        {
            v.fadeInGain += v.fadeInStep;
            if (v.fadeInGain >= 1.0f)
            {
                v.fadeInGain = 1.0f;
                v.fadeInStep = 0.0f;
            }
            gain *= v.fadeInGain;
        }

        if (v.fading)
        {
            v.fadeGain -= fadeStep;
            if (v.fadeGain <= 0.0f)
                return false;
            gain *= v.fadeGain;
        }

        float outL, outR;
        const float s0 = hermite (frameValue (idx - 1, 0), frameValue (idx, 0),
                                  frameValue (idx + 1, 0), frameValue (idx + 2, 0), t);
        if (channels == 1)
        {
            outL = outR = s0;
        }
        else
        {
            outL = s0;
            outR = hermite (frameValue (idx - 1, 1), frameValue (idx, 1),
                            frameValue (idx + 1, 1), frameValue (idx + 2, 1), t);
        }

        // Pro Stimme: Velocity → Drive (2-fach überabgetastet) → Filter. Crush wirkt erst am Ende auf die Summe
        // aller Stimmen (wie ein Bitcrusher-Effekt hinter dem Sampler), siehe render().
        outL = v.driveL.process (outL * v.velocityGain, driveGain, driveMakeup);
        if (filterActive)
            outL = v.filterL.process (outL, Filter::kBassCompensation);
        if (channels == 1)
        {
            outR = outL;                   // Mono-Samples: nur einmal verzerren und filtern
        }
        else
        {
            outR = v.driveR.process (outR * v.velocityGain, driveGain, driveMakeup);
            if (filterActive)
                outR = v.filterR.process (outR, Filter::kBassCompensation);
        }

        v.modGainLeft += stepLeft;
        v.modGainRight += stepRight;
        left[i]  += outL * gain * v.gainLeft * v.modGainLeft;
        right[i] += outR * gain * v.gainRight * v.modGainRight;

        if (v.glideRemaining > 0)
        {
            v.glideFactor *= v.glideRatio;
            if (--v.glideRemaining == 0)
                v.glideFactor = 1.0;
        }

        v.position += v.baseIncrement * bendFactor * fineFactor * v.glideFactor * lfoPitchFactor;

        if (loopActive && v.position >= (double) (r.loopEnd + 1))
        {
            v.position -= (double) loopLength;
            v.loopWrapped = true;
            // Schritt größer als der Loop (sehr hoch gespielter, kurzer Loop): ganz in den Loop zurückholen,
            // sonst liefe die Position davon (26.10.2026, Test mit zufällig veränderten Dateien)
            if (v.position >= (double) (r.loopEnd + 1))
                v.position = (double) r.loopStart + std::fmod (v.position - (double) r.loopStart, (double) loopLength);
        }
        else if (! loopActive && v.position > (double) endFrame)
            return false;                  // Sample zu Ende

        if (! v.env.isActive())
            return false;                  // Hüllkurve ausgeklungen
    }

    // Nur noch vom Pedal gehaltene Stimme (Taste los), die nichts mehr hören lässt: Sustain 0 und ausgeklungen,
    // keine Anhebung durch DC → Volume. Sie würde sonst bei geloopten Samples bis zum Loslassen des Pedals still
    // weiterrechnen und Stimmen belegen (seit 23.11.2026). Bei gedrückter Taste bleibt sie, weil der Sustain-Regler
    // sie noch wieder anheben könnte.
    if (! v.held && v.sustained && v.sustainLift <= 0.0f && v.env.isSilentSustain())
        return false;
    return true;
}

void Synth::updateSmoothing (int numSamples) noexcept
{
    // Mono-LFO: einmal pro Block für alle Stimmen
    blockSeconds = (float) ((double) numSamples / sampleRate);
    monoLfoValue = monoLfo.advance (monoLfoParams, blockSeconds);

    // Kontinuierliche Regler gleiten in ca. 20 ms zum Ziel (gegen Zipper-Geräusche).
    // Der Faktor gilt für genau diese Blocklänge (seit 23.11.2026): Die Blöcke werden an jedem MIDI-Ereignis geteilt,
    // und ein Reststück von z. B. 3 Samples zählte vorher als voller 16er-Schritt. Bei dichtem Modulationsrad, Pitch
    // Bend oder Aftertouch glitten die Regler dadurch deutlich schneller als 20 ms (Zipper-Geräusche).
    // pow nur bei Teilblöcken; volle Blöcke nehmen den vorberechneten Wert (bitgenau wie bisher).
    blockSmoothingCoeff = (numSamples == 16) ? smoothingCoeff
                                             : std::pow (smoothingCoeff, (float) numSamples / 16.0f);
    const float a = smoothingPrimed ? blockSmoothingCoeff : 0.0f;   // beim ersten Mal ohne Gleiten
    const float b = 1.0f - a;
    smoothingPrimed = true;
    routePitch        = a * routePitch        + b * routePitchTarget;
    routeFilter       = a * routeFilter       + b * routeFilterTarget;
    routeVolume       = a * routeVolume       + b * routeVolumeTarget;
    routePan          = a * routePan          + b * routePanTarget;
    ampKeyScaleSmoothed    = a * ampKeyScaleSmoothed    + b * ampKeyScale;
    // Lo-Fi bekommt den Anteil nur, solange es der gewählte und aktive Effekt ist; beim Umschalten blendet es aus
    crushAmountTarget = (fxActive == FxMode::LoFi && fxRequested == FxMode::LoFi) ? fxAmount : 0.0f;
    crushAmountSmoothed    = a * crushAmountSmoothed    + b * crushAmountTarget;
    if (crushAmountTarget <= 0.0f && crushAmountSmoothed < 0.01f)
        crushAmountSmoothed = 0.0f;                  // ganz zurückgedreht: Lo-Fi wieder vollständig aus
    if (! crushDirect)
        applyCrushAmount (crushAmountSmoothed);
    filterKeyTrackSmoothed = a * filterKeyTrackSmoothed + b * filterKeyTrack;
    modWheelSmoothed  = a * modWheelSmoothed  + b * modWheelTarget;
    cutoffSmoothed    = a * cutoffSmoothed    + b * cutoffTarget;
    resonanceSmoothed = a * resonanceSmoothed + b * resonanceTarget;
    driveSmoothed     = a * driveSmoothed     + b * driveTarget;
    // Für alle Stimmen gleich: einmal hier statt je Stimme (siehe Synth.h), nur bei geändertem Wert
    if (resonanceSmoothed != blockResonanceFor)
    {
        blockResonance = Filter::resonanceInfo (resonanceSmoothed);
        blockResonanceFor = resonanceSmoothed;
    }
    if (driveSmoothed != blockDriveFor)
    {
        blockDriveGain = Drive::gain (driveSmoothed);
        blockDriveMakeup = Drive::makeup (blockDriveGain);
        blockDriveFor = driveSmoothed;
    }
    envAmountSmoothed = a * envAmountSmoothed + b * envAmountTarget;
}

void Synth::renderIdle (float* left, float* right, int numSamples) noexcept
{
    // Mono-LFO und Reglerglättung laufen exakt wie beim Spielen in 16-Sample-Schritten weiter (Mono-LFO bleibt damit
    // bitgenau wie bisher, Wunsch von Björn 18.10.2026); die Stimmen-Schleife entfällt.
    // Lo-Fi pausiert ebenfalls (seit 21.10.2026, spart ca. 0,12 % eines Kerns je Instanz mit Lo-Fi): Nach den 100 ms
    // ohne Stimme sind seine Filter ausgeklungen; beim nächsten Ton beginnt es frisch (crushRunning = false). Hörbar
    // ändert sich nichts – nur der Halte-Takt beginnt an einem anderen Sample.
    (void) left;
    (void) right;
    for (int pos = 0; pos < numSamples; pos += 16)
        updateSmoothing (std::min (16, numSamples - pos));
    crushRunning = false;
    // Chorus: im Leerlauf pausiert er nicht von selbst (der Anteil steht ja noch). Nur wenn er ausblenden soll
    // (Umschalten oder 0 %), läuft er auf der Stille weiter, bis er pausiert – sonst rechnet er hier nichts.
    if (fxActive == FxMode::Chorus && ! chorus.isCurrentlyPaused()
        && (fxRequested != FxMode::Chorus || fxAmount <= 0.0f))
        processChorus (left, right, numSamples);
    updateFxSwitch();
}

void Synth::processChorus (float* left, float* right, int numSamples) noexcept
{
    const double amount = (fxRequested == FxMode::Chorus) ? (double) fxAmount : 0.0;
    if (amount != chorusAmountSet)
    {
        chorus.setAmount (amount);   // Kurve → Rate, Filter, Anteil; Filter nur bei geänderter Frequenz neu
        chorusAmountSet = amount;
    }
    if (amount <= 0.0 && chorus.isCurrentlyPaused())
        return;                      // 0 % und ausgeblendet: nichts zu tun (Signal bleibt bitgenau trocken)
    chorus.process (left, right, left, right, numSamples);
}

void Synth::updateFxSwitch() noexcept
{
    if (fxRequested == fxActive)
        return;
    const bool quiet = (fxActive == FxMode::LoFi) ? (crushAmountSmoothed <= 0.0f && crushLevels <= 0.0f && crushRate <= 0.0f)
                                                  : chorus.isCurrentlyPaused();
    if (quiet)
    {
        fxActive = fxRequested;      // der neue Effekt beginnt bei 0 und blendet auf den Anteil ein
        crushRunning = false;
    }
}

void Synth::render (float* left, float* right, int numSamples)
{
    // Leerlauf: keine Stimme aktiv (auch keine ausklingende Release-Stimme) – siehe renderIdle
    bool anyActive = false;
    for (const auto& v : voices)
        if (v.active()) { anyActive = true; break; }
    if (anyActive)
    {
        idleSamples = 0;
    }
    else
    {
        if ((double) idleSamples >= kIdleSeconds * sampleRate)
        {
            renderIdle (left, right, numSamples);
            return;
        }
        idleSamples += numSamples;
    }

    // In Blöcken von 16 Samples: Filterkoeffizienten und Glättung werden pro Block berechnet
    for (int pos = 0; pos < numSamples; pos += 16)
    {
        const int n = std::min (16, numSamples - pos);
        updateSmoothing (n);
        updateFxSwitch();   // vor den Effekten: ein gerade stiller Effekt wird sofort abgelöst (z. B. Preset mit Chorus)
        for (auto& v : voices)
            if (v.active() && ! renderVoice (v, left + pos, right + pos, n))
                v.region = nullptr;

        // Lo-Fi am Ende auf die Summe aller Stimmen (wie ein Bitcrusher-Effekt hinter dem Sampler):
        // nach Drive, Filter, Hüllkurve, Lautstärke und Pan; läuft nur einmal statt pro Stimme. Je Block von
        // 16 Samples, damit der geglättete Regler fein gleitend wirkt.
        // Hinweis: render() addiert in left/right; Lo-Fi wirkt auf den gesamten Pufferinhalt (der Prozessor
        // übergibt einen geleerten Puffer).
        if (crushRate > 0.0f || crushLevels > 0.0f)
            applyCrush (left + pos, right + pos, n);
        else
            crushRunning = false;
        // Chorus (statt Lo-Fi): ebenfalls auf der Summe, je Block von 16 Samples
        if (fxActive == FxMode::Chorus)
            processChorus (left + pos, right + pos, n);
    }
}

void Synth::CrushFilter::design (float cutoffHz, double sampleRate) noexcept
{
    // Tiefpass 2. Ordnung (Butterworth, Q = 0,707) nach dem Audio EQ Cookbook (R. Bristow-Johnson).
    // Hörtest 21.10.2026: mehr Resonanz oder eine tiefere Eckfrequenz (statt 0,45 × Crush-Rate) klangen nicht besser.
    const double w0 = 2.0 * 3.14159265358979323846 * std::min ((double) cutoffHz, 0.45 * sampleRate) / sampleRate;
    const double cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.70710678);
    const double a0 = 1.0 + alpha;
    b0 = (float) ((1.0 - cosw) * 0.5 / a0);
    b1 = (float) ((1.0 - cosw) / a0);
    b2 = b0;
    a1 = (float) (-2.0 * cosw / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void Synth::EmphasisFilter::design (double t1, double t2, double sampleRate) noexcept
{
    // H(s) = (1 + s·t1) / (1 + s·t2) per Bilineartransformation (s = K·(1 − z⁻¹)/(1 + z⁻¹), K = 2·fs)
    const double K = 2.0 * sampleRate;
    const double a0 = 1.0 + K * t2;
    b0 = (float) ((1.0 + K * t1) / a0);
    b1 = (float) ((1.0 - K * t1) / a0);
    a1 = (float) ((1.0 - K * t2) / a0);
}

void Synth::CrushChain::reset() noexcept
{
    pre = CrushFilter(); post = CrushFilter();
    emphasis = EmphasisFilter(); deemphasis = EmphasisFilter();
    accumulator = 0; phase = 1.0f;
    sumL = sumR = heldL = heldR = 0.0f;
    count = 0;
}

void Synth::CrushChain::configure (int newHalfHold, float directRateHz, double sampleRate) noexcept
{
    halfHold = newHalfHold;
    freeStep = (newHalfHold == 0 && directRateHz > 0.0f) ? (float) (directRateHz / sampleRate) : 0.0f;
    const float rate = halfHold > 0 ? (float) (sampleRate * 2.0 / halfHold) : directRateHz;
    if (rate <= 0.0f)
        return;
    pre.design (0.45f * rate, sampleRate);
    post.design (0.45f * rate, sampleRate);

    // Emphasis: klassisch 50/15 µs (Eckfrequenzen ≈ 3,2 / 10,6 kHz) oder mitwandernd (gleiches Verhältnis,
    // obere Ecke knapp unter der halben Crush-Rate, z. B. Crush 11 kHz: ≈ 1,6 / 5,3 kHz)
    double t1 = 50.0e-6, t2 = 15.0e-6;
    if (kCrushEmphasis == 2)
    {
        const double upper = 0.48 * (double) rate;                     // obere Ecke (Hz)
        t2 = 1.0 / (2.0 * 3.14159265358979323846 * upper);
        t1 = t2 * (50.0 / 15.0);
    }
    emphasis.design (t1, t2, sampleRate);
    deemphasis.design (t2, t1, sampleRate);                            // genau gespiegelt (Kehrwert)
}

void Synth::CrushChain::process (float& l, float& r, float levels, float inverse) noexcept
{
    const bool filterOn = rateOn();
    const bool emphasisOn = filterOn && kCrushEmphasis != 0;

    // Anti-Aliasing-Filter vor der Reduktion: dämpft die Höhen oberhalb der halben Crush-Rate
    // (sonst klappen sie als unharmonische Artefakte zurück)
    if (filterOn && kCrushInputFilter)
    {
        l = pre.process (l, 0);
        r = pre.process (r, 1);
    }
    // Emphasis: Höhen vor der Reduktion anheben
    if (emphasisOn)
    {
        l = emphasis.process (l, 0);
        r = emphasis.process (r, 1);
    }

    // Samplerate reduzieren: Die Eingangswerte eines Halteabschnitts werden gemittelt, gehalten wird der
    // Mittelwert. Bei ganzer Haltelänge immer gleich lang (z. B. 3-3-3), bei halber regelmäßig abwechselnd
    // (z. B. 2-3-2-3) – gezählt in halben Samples, daher exakt und ohne Drift.
    if (filterOn)
    {
        sumL += l;
        sumR += r;
        ++count;
        bool latch;
        if (halfHold > 0)
        {
            accumulator += 2;
            latch = accumulator >= halfHold;
            if (latch) accumulator -= halfHold;
        }
        else
        {
            phase += freeStep;
            latch = phase >= 1.0f;
            if (latch) phase -= std::floor (phase);
        }
        if (latch)
        {
            const float inv = 1.0f / (float) count;
            heldL = sumL * inv;
            heldR = sumR * inv;
            sumL = sumR = 0.0f;
            count = 0;
        }
        l = heldL;
        r = heldR;
    }
    // ... und Auflösung reduzieren (floor(x + 0.5) ist schneller als std::round)
    if (levels > 0.0f)
    {
        l = std::floor (l * levels + 0.5f) * inverse;
        r = std::floor (r * levels + 0.5f) * inverse;
    }
    // Emphasis: Höhen nach der Reduktion genau spiegelbildlich absenken (mit ihnen das Quantisierungsrauschen)
    if (emphasisOn)
    {
        l = deemphasis.process (l, 0);
        r = deemphasis.process (r, 1);
    }
    // Rekonstruktionsfilter nach der Reduktion (glättet die Stufen wie der Ausgang eines alten Wandlers)
    if (kCrushOutputFilter && filterOn)
    {
        l = post.process (l, 0);
        r = post.process (r, 1);
    }
}

void Synth::applyCrush (float* left, float* right, int numSamples) noexcept
{
    // Neue Raten-Stufe (oder freie Messrate): auf der zweiten Kette einrichten und in ca. 10 ms überblenden.
    // Während einer Überblendung wird kein weiterer Wechsel begonnen (der nächste Block holt ihn nach).
    const float directRate = crushDirect ? crushDirectRate : 0.0f;
    if (! crushRunning)
    {
        // Lo-Fi wird eingeschaltet: frisch beginnen, ohne Überblendung (vorher lief ja nichts)
        crushRunning = true;
        crushChains[crushActive].reset();
        crushChains[crushActive].configure (crushHalfHold, directRate, sampleRate);
        crushFade = 1.0f;
    }
    CrushChain& current = crushChains[crushActive];
    const bool changed = current.halfHold != crushHalfHold
                         || (crushDirect && current.freeStep != (directRate > 0.0f ? (float) (directRate / sampleRate) : 0.0f));
    if (changed && crushFade >= 1.0f)
    {
        CrushChain& next = crushChains[1 - crushActive];
        next.reset();
        next.configure (crushHalfHold, directRate, sampleRate);
        crushActive = 1 - crushActive;
        crushFade = 0.0f;
        crushFadeStep = (float) (1.0 / (0.01 * sampleRate));
    }

    CrushChain& now = crushChains[crushActive];
    CrushChain& before = crushChains[1 - crushActive];
    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i], r = right[i];
        if (crushFade < 1.0f)
        {
            float lo = l, ro = r;
            before.process (lo, ro, crushLevels, crushInverse);
            now.process (l, r, crushLevels, crushInverse);
            crushFade = std::min (1.0f, crushFade + crushFadeStep);
            l = lo + (l - lo) * crushFade;
            r = ro + (r - ro) * crushFade;
        }
        else
        {
            now.process (l, r, crushLevels, crushInverse);
        }
        left[i] = l;
        right[i] = r;
    }
}

} // namespace maermel
