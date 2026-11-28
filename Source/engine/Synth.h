// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// Synth.h – Märmel Engine
// Stimmenverwaltung und Wiedergabe – das Herz der Engine (reines C++17, keine JUCE-Abhängigkeit).
//
// Was hier passiert:
//   - Region-Auswahl pro Note: Tasten- und Velocity-Bereich, Keyswitches, Round Robin (seq_*),
//     Zufall (lorand/hirand), Pedal-/Controller-Schichten (loccN/hiccN), Überblendungen (xfin/xfout),
//     Release-Samples (trigger=release)
//   - Wiedergabe: 4-Punkt-Hermite-Interpolation, Loops, Sample Start (Start/Vel/Rnd), C.Tune, KeyShift,
//     Fine, Pitch Bend, Glide; Samplerate-Umrechnung über den Abspielschritt
//   - Stimmenmodi: Legato, Mono, 2 … 64 Stimmen. Die Stimmenzahl zählt NOTEN, nicht Samples:
//     Eine Note mit mehreren Layern (oder Split-Stereo) belegt nur eine Stimme, und beim Voice Stealing
//     weicht immer die ganze älteste Note. Intern gibt es voicePoolSize Sample-Plätze (siehe unten);
//     MON/LEG erlauben bis zu monoNoteLimit Noten einschließlich ausklingender Release-Noten.
//   - Gruppen aus der SFZ: polyphony (Stimmen je Gruppe) und group/off_by (z. B. geschlossene Hi-Hat
//     beendet die offene). Beendet wird immer mit kurzem Ausblenden, nie hart abgeschnitten.
//   - Modulation: Amp- und Filterhüllkurve (Envelope.h), Poly-LFO je Stimme und ein gemeinsamer Mono-LFO
//     (Lfo.h) mit Routing auf Pitch, Filter, Volume und Pan, Modulationsrad (Wheel LFO), Aftertouch
//     (Kanal-Aftertouch je MIDI-Kanal, polyphoner Aftertouch je Taste)
//   - Sustain-Pedal, Ausblenden beim Instrumentwechsel
//
// Signalweg:
//   pro Stimme:   Sample → Drive (2-fach überabgetastet) → Filter (Filter.h) → Amp-Hüllkurve, Vol → Pan
//   auf der Summe: Lo-Fi ODER Chorus (chorus/AnalogChorus.h) – es rechnet immer höchstens einer;
//                  beim Umschalten blendet der alte aus, danach der neue von 0 ein.
//
// Rechenzeit:
//   - Filterkoeffizienten, Reglerglättung, Mono-LFO und Effekt-Regler werden in Blöcken von 16 Samples
//     berechnet, nicht für jedes Sample.
//   - Leerlauf: Klingt seit 100 ms keine Stimme mehr, entfällt die Schleife über die Stimmen. Mono-LFO und
//     Glättung laufen bitgenau weiter; Lo-Fi pausiert, der Chorus rechnet nur, solange er noch ausblendet.
//   - Bei 0 % pausieren Lo-Fi und Chorus ganz (das Signal bleibt bitgenau trocken).
//
// Thread-Regeln:
//   - prepare() nur aus prepareToPlay() (reserviert den Speicher für alle Stimmen und Effekte)
//   - exchangeInstrument() / takeFinishedInstrument() im Audio-Thread: Das neue Instrument wird nur
//     übernommen, das alte erst nach dem Ausblenden zurückgegeben. Freigegeben wird es NIE im
//     Audio-Thread, sondern vom Aufrufer an einen anderen Thread weitergereicht (siehe PluginProcessor,
//     takePendingInstrument / collectFinishedInstrument). setInstrument() ist nur für die Testwerkzeuge.
//   - alle übrigen Methoden (Noten, Controller, set...-Regler, render) im Audio-Thread
//
// Echtzeit-Regel: Nach prepare() reservieren und freigeben alle Methoden keinen Speicher, lesen keine
// Dateien und warten auf keine Sperren.

#pragma once

#include "Envelope.h"
#include "Filter.h"
#include "Lfo.h"
#include "Instrument.h"
#include "chorus/AnalogChorus.h"

#include <array>
#include <cstdint>
#include <memory>

namespace maermel
{

class Synth
{
public:
    enum class VoiceMode { Legato, Mono, Poly };

    // Stimmenzahl = Anzahl gleichzeitig klingender NOTEN (seit 19.10.2026; vorher Samples). Eine Note kann aus
    // mehreren Samples bestehen (Layer, Split-Stereo); Release-Samples einer Note zählen zusammen als eine Note.
    static constexpr int maxPolyphony  = 64;    // höchste wählbare Stimmenzahl (Noten)
    static constexpr int monoNoteLimit = 16;    // MON/LEG: Noten inklusive ausklingender Release-Samples
    static constexpr int voicePoolSize = 192;   // Sample-Plätze: 64 Noten × 2 Stereo-Layer + 64 Reserve (Release, Ausblenden)

    void prepare (double hostSampleRate);

    // --- Instrument ---------------------------------------------------------
    // Tauscht das Instrument aus. Klingende Stimmen des bisherigen Instruments werden in
    // wenigen Millisekunden ausgeblendet. Zurückgegeben wird ein älteres Instrument, das
    // nicht mehr gebraucht wird (oder nullptr) – es darf nicht im Audio-Thread freigegeben
    // werden, sondern wird an einen anderen Thread weitergereicht.
    std::shared_ptr<const Instrument> exchangeInstrument (std::shared_ptr<const Instrument> newInstrument) noexcept;

    // Liefert das ausgetauschte Instrument, sobald keine Stimme es mehr benutzt (sonst nullptr).
    std::shared_ptr<const Instrument> takeFinishedInstrument() noexcept;

    // Bequeme Variante für Tests außerhalb des Audio-Threads.
    void setInstrument (std::shared_ptr<const Instrument> newInstrument) { exchangeInstrument (std::move (newInstrument)); }

    // --- Einstellungen -------------------------------------------------------
    void setEnvelope (const Envelope::Params& params);
    // Pitch-Bend-Bereich in Halbtönen (0 = ohne Wirkung ... 12); wirkt sofort auch auf ein gehaltenes Rad
    void setPitchBendRange (float semitones) noexcept;

    // Amp Key: Lautstärke nach Note, -1 ... +1. Bezug Note 60; bei voller Auslenkung 3 dB je Oktave,
    // höchstens ±12 dB. Wirkt laufend (geglättet) – auch gehaltene Töne folgen dem Regler.
    void setAmpKeyScale (float amount) noexcept { ampKeyScale = std::min (1.0f, std::max (-1.0f, amount)); }
    static float ampKeyScaleDecibels (float amount, int note) noexcept
    {
        const float db = amount * 3.0f * (float) (note - 60) / 12.0f;
        return std::min (12.0f, std::max (-12.0f, db));
    }

    // Dynamikbereich der Anschlagsstärke in dB (0 = keine Dynamik, 84 = SFZ-Standard).
    void setVelocityRange (float decibels) noexcept;

    // Stimmenmodus. Bei Poly gibt 'voices' die Stimmenzahl an (1 ... 64).
    void setVoiceMode (VoiceMode mode, int voices = 32) noexcept;

    // Glide-Zeit in Sekunden (wirkt in Legato und Mono)
    void setGlideTime (float seconds) noexcept { glideSeconds = seconds; }

    // --- Sample Start and Tune -------------------------------------------------
    // Startpunkt (gilt ab dem Anschlag): Start 0 ... 1, Vel -1 ... +1 (bipolar), Rnd an/aus.
    // Wirkbereich: das Kleinere von kSampleStartMaxMs (1 s) und dem Rest des Samples bis Loop- bzw. Sample-Ende.
    void setSampleStart (float startKnob, float velocityKnob, bool randomStart) noexcept;

    // C.Tune: verschiebt die eingehende Note (-24 ... +24), Region und Tonhöhe folgen gemeinsam
    void setCoarseTune (int semitones) noexcept { coarseTune = semitones; }

    // KeyShift: Region wird mit (Note - KeyShift) gewählt, die Tonhöhe bleibt (-12 ... +12)
    void setKeyShift (int semitones) noexcept   { keyShift = semitones; }

    // Fine: Feinstimmung in Cent, wirkt sofort auch auf klingende Noten
    void setFineTune (float cents) noexcept;

    // Crush: ein Knopf für Bittiefe und Samplerate. 0 = aus, 100 = 11 Bit / 11 kHz.
    // Wirkt sofort auch auf klingende Noten.
    //   Bittiefe:   erste 30 % des Regelwegs 24 → 15 Bit, übrige 70 % 15 → 11 Bit
    //   Samplerate: 11 kHz + 8,74 kHz × (2^((Bit − 11) / 3) − 1), nach Gehör abgestimmt;
    //               liegt sie über der Samplerate der DAW, ist die Reduzierung aus (ab ca. 17,8 Bit)
    //   Samplerate-Reduzierung: Mittelwert der Eingangswerte eines Halteabschnitts wird gehalten;
    //   dazu Filter vor und nach der Reduzierung sowie Emphasis (siehe applyCrush)
    void setCrush (float amount) noexcept;   // Anteil 0 … 100 des gewählten Effekts (Lo-Fi oder Chorus), s. setFxMode

    // Effekt hinter der Stimmensumme: Lo-Fi ODER Chorus (seit 26.10.2026). Es rechnet immer höchstens einer.
    // Beim Umschalten wird der laufende Effekt erst ausgeblendet (Lo-Fi über seine Glättung, Chorus über seinen
    // Effektanteil), danach gilt der neue – er beginnt bei 0 und blendet auf den Anteil ein. Bei 0 % pausieren beide.
    enum class FxMode { LoFi = 0, Chorus = 1 };
    void setFxMode (FxMode mode) noexcept          { fxRequested = mode; }
    FxMode activeFxMode() const noexcept           { return fxActive; }
    bool chorusPaused() const noexcept             { return chorus.isCurrentlyPaused(); }   // für Tests
    static float crushBits (float amount) noexcept;
    static float crushRateHz (float bits) noexcept;
    // Raten-Stufe als Haltelänge in halben Samples (0 = keine Ratenreduktion, 3 … 8 = 1½ … 4); Rate = Samplerate × 2 / Stufe.
    // Bei 88,2/96 kHz doppelt, bei 176,4/192 kHz vierfach so lang (gleicher Klang bei jeder Samplerate).
    static int crushHoldStage (float bits, double sampleRate) noexcept;

    // Nur für Tests: Bittiefe und Samplerate direkt setzen (statt über den Crush-Knopf); 0 = aus
    void setCrushDirect (float bits, float rateHz) noexcept;   // nur für Messungen (ohne Glättung)

    // --- Filter ----------------------------------------------------------------
    // Cutoff, Resonanz, Drive und Env-Menge wirken sofort (geglättet, ca. 20 ms);
    // Keytracking und Velocity gelten ab dem Anschlag.
    void setFilterType (FilterType type) noexcept      { filterType = type; }
    void setFilterCutoff (float knob) noexcept         { cutoffTarget = cutoffOctaves (knob); }
    void setFilterResonance (float amount) noexcept    { resonanceTarget = amount; }
    void setDrive (float amount) noexcept              { driveTarget = Drive::fromKnob (amount); }
    void setFilterKeyTrack (float amount) noexcept     { filterKeyTrack = amount; }        // 0 ... 1
    void setFilterEnvAmount (float amount) noexcept    { envAmountTarget = amount; }       // -1 ... +1 (±10 Oktaven)
    // Vel Env: -1 ... +1 = ±10 Oktaven zusätzliche Hüllkurventiefe, gewichtet mit der Anschlagstärke.
    // Addiert sich wie Env: bei härtestem Anschlag wirkt Vel Env -10 genau wie Env -10.
    void setFilterVelocity (float amount) noexcept     { filterVelocity = amount; }

    // Vel (direkt): verschiebt den Cutoff je nach Anschlag, ohne Hüllkurve; -1 ... +1 = ±7 Oktaven
    // bei härtestem Anschlag (leisester Anschlag = keine Verschiebung).
    void setFilterVelocityCutoff (float amount) noexcept { filterVelocityCutoff = amount; }

    static constexpr float kEnvOctaves = 10.0f;            // Env und Vel Env: je ±10 Oktaven
    static constexpr float kVelocityCutoffOctaves = 7.0f;  // Vel (direkt)
    static constexpr float kMaxModulationOctaves = 10.0f;  // Summe aus Vel, Vel Env und Env: höchstens ±10

    // Gesamte Verschiebung des Cutoffs in Oktaven durch Vel, Vel Env und Env.
    // env, velEnv, velDirect: Reglerstellungen -1 ... +1; velocity: 0 (leisester) ... 1 (härtester Anschlag);
    // envLevel: aktueller Pegel der Filterhüllkurve 0 ... 1.
    static float filterModulationOctaves (float env, float velEnv, float velDirect, float velocity, float envLevel) noexcept
    {
        // Hüllkurventiefe: Env fest plus Vel Env je nach Anschlag (beide in Oktaven, beide Vorzeichen)
        float depth = env * kEnvOctaves + velEnv * kEnvOctaves * velocity;
        depth = std::min (kMaxModulationOctaves, std::max (-kMaxModulationOctaves, depth));
        const float total = velDirect * kVelocityCutoffOctaves * velocity + depth * envLevel;
        return std::min (kMaxModulationOctaves, std::max (-kMaxModulationOctaves, total));
    }
    void setFilterEnvelope (const Envelope::Params& params);

    // Cutoff-Regler 0 ... 1 → 20 Hz ... 20 kHz, gleichmäßig in Oktaven
    static float cutoffHz (float knob) noexcept { return 20.0f * std::pow (1000.0f, knob); }

    // --- LFOs ------------------------------------------------------------------
    // Poly-LFO: eigene Phase pro Stimme, startet mit jeder Note (bei Legato läuft er weiter).
    // Mono-LFO: eine Phase für alle Stimmen; Delay und Env Mode starten neu, wenn nach einer
    //           Pause (keine Taste gehalten) wieder gespielt wird.
    void setPolyLfo (const LfoParams& p) noexcept { polyLfoParams = p; }
    void setMonoLfo (const LfoParams& p) noexcept { monoLfoParams = p; }

    // Mono-LFO-Phase von außen setzen (Sync zum Songtempo, wenn die DAW läuft), 0 ... 1
    void alignMonoLfo (double phase) noexcept     { monoLfo.setPhase (phase); }

    // Routing-Fader, je -1 (Poly-LFO) ... 0 (aus) ... +1 (Mono-LFO); Tiefe = Auslenkung²
    void setLfoRouting (float pitch, float filter, float volume, float pan) noexcept;

    // Wheel LFO je Ziel 0 … 1: 0 = LFO wirkt direkt, 1 = nur über Modulationsrad (bzw. Aftertouch), dazwischen anteilig.
    // amp gilt gemeinsam für LFO-Volume und LFO-Pan (das feste Panorama aus der SFZ-Datei bleibt unberührt).
    void setWheelAmounts (float pitch, float filter, float amp = 0.0f) noexcept { wheelPitch = pitch; wheelFilter = filter; wheelAmp = amp; }
    void modWheel (int value) noexcept            { modWheelTarget = toUnit (value); }

    // Aftertouch wirkt wie das Modulationsrad, aber je Stimme (seit 25.10.2026): Jede Stimme nimmt den größten Wert
    // aus Modulationsrad, Kanal-Aftertouch ihres MIDI-Kanals und polyphonem Aftertouch ihrer Taste. So stören sich
    // Rad, Kanal- und Poly-Aftertouch nicht; bei MPE (eine Note je Kanal) wirkt der Kanal-Druck nur auf diese Note.
    // Ein Keyboard auf einem Kanal mit Kanal-Aftertouch verhält sich wie bisher (alle Noten gemeinsam).
    void channelPressure (int channel, int value) noexcept
    {
        if (channel >= 0 && channel < 16) chanPressure[(size_t) channel] = toUnit (value);
    }
    void polyPressure (int note, int value) noexcept
    {
        if (note >= 0 && note < 128) notePressure[(size_t) note] = toUnit (value);
    }
    void aftertouch (int value) noexcept           { chanPressure.fill (toUnit (value)); }   // alle Kanäle (Testwerkzeuge)
    void resetPressure() noexcept                  { chanPressure.fill (0.0f); notePressure.fill (0.0f); }

    // --- MIDI ------------------------------------------------------------------
    void noteOn (int note, int velocity, int channel = 0);   // channel 0 … 15 (für Kanal-Aftertouch/MPE)
    void noteOff (int note);
    void sustainPedal (bool down);
    void controller (int number, int value) noexcept;   // Stand merken für loccN/hiccN
    void pitchBend (int value14Bit);                // 0 ... 16383, Mitte 8192
    void allNotesOff();
    void resetControllers();                        // CC121 „Reset All Controllers“ (seit 1.0.3)

    // Addiert das Signal in die beiden Puffer.
    void render (float* left, float* right, int numSamples);
    void applyCrush (float* left, float* right, int numSamples) noexcept;   // Crush auf die Summe (am Ende)

    int activeVoiceCount() const noexcept;

    // Zuletzt gedrückter Keyswitch (-1 = keiner), z. B. für die Anzeige
    int currentKeyswitch() const noexcept { return keyswitch; }

private:
    struct Voice
    {
        const PlayableRegion* region = nullptr;
        const Instrument* owner = nullptr;   // zu welchem Instrument die Region gehört
        int note = -1;
        uint64_t startOrder = 0;
        uint64_t noteGroup = 0;        // alle Samples einer Note (bzw. ihrer Release-Samples) tragen dieselbe Kennung
        bool held = false;             // Taste gedrückt
        bool sustained = false;        // Taste los, aber Pedal hält
        bool isReleaseVoice = false;   // Release-Sample
        bool fading = false;           // wird ausgeblendet (Voice Stealing, Mono, Instrumentwechsel)
        float fadeGain = 1.0f;
        float fadeInGain = 1.0f, fadeInStep = 0.0f;   // Einblenden (Release-Samples, Legato)

        double position = 0.0;
        bool loopWrapped = false;      // mindestens einmal ans Loop-Ende zurückgesprungen (linker Nachbar = Loop-Ende)
        double baseIncrement = 1.0;
        double glideFactor = 1.0, glideRatio = 1.0;   // Tonhöhenverlauf beim Glide
        int glideRemaining = 0;

        float velocityGain = 1.0f;     // Velocity-Lautstärke, wird vor dem Drive angewendet
        float xfadeVel = 1.0f;         // Velocity-Überblendung (xfin/xfout_lovel/hivel), fest ab dem Anschlag
        float xfadeCc = -1.0f;         // Controller-Überblendung, geglättet; -1 = noch nicht gesetzt
        int channel = 0;               // MIDI-Kanal der Note (Kanal-Aftertouch)
        float pressure = -1.0f;        // Aftertouch dieser Stimme, geglättet; -1 = noch nicht gesetzt

        float gainLeft = 1.0f, gainRight = 1.0f;
        Envelope env;

        // Filter pro Stimme (Stereo-Samples: zwei Filter)
        Envelope filterEnv;
        Filter filterL, filterR;
        DriveStage driveL, driveR;   // Drive mit 2-facher Überabtastung (bei Drive aus: nur gleiche Verzögerung)
        float keyOctaveOffset = 0.0f;   // (Note − 60) / 12: Filter-Keytracking wird laufend mit dem Key-Regler multipliziert
        int keyNote = 60;               // wirksame Note für Amp-Keytracking (laufend ausgewertet)
        float velocityNorm = 1.0f;    // Standard-Velocity-Kurve: 0 (Velocity 1) ... 1 (Velocity 127), beim Anschlag
        float velEnvAtOn = 0.0f, velDirectAtOn = 0.0f;   // Vel Env und Vel beim Anschlag

        // Poly-LFO und die daraus folgende Modulation (Lautstärke/Panorama gleitend pro Block)
        Lfo polyLfo;
        float modGainLeft = -1.0f, modGainRight = -1.0f;   // -1 = noch nicht gesetzt
        float sustainLift = -1.0f;                          // DC → Volume: Anhebung des Sustains, -1 = noch nicht gesetzt

        bool active() const noexcept { return region != nullptr; }
        double currentPitchNote() const noexcept;     // Note inklusive laufendem Glide
    };

    // Tastennummer für die Region-Auswahl und für die Tonhöhe (C.Tune, KeyShift)
    int effectiveNote (int played) const noexcept   { return played + coarseTune; }
    int selectionNote (int played) const noexcept   { return played + coarseTune - keyShift; }
    int64_t computeStartFrame (const PlayableRegion& r, int velocity) noexcept;

    // Regionen einer Note starten; gibt die Anzahl gestarteter Stimmen zurück.
    // glideFromPitch: Tonhöhe, von der aus geglitten wird, oder < 0 für kein Glide.
    // (Legato läuft über retargetMonoVoices: gleiche Stimmen, neue Tonhöhe. Der frühere Legato-Zweig hier mit
    // übernommener Hüllkurve wurde nicht mehr benutzt und am 23.11.2026 entfernt.)
    int startNote (int note, int velocity, double glideFromPitch);
    void startVoice (Voice& v, const PlayableRegion& r, int note, int velocity);
    // Legato: die klingenden Stimmen der Mono-Note auf eine neue Note umstellen – gleiches Sample an gleicher
    // Stelle, Hüllkurven und Filter laufen weiter, nur die Tonhöhe ändert sich (mit Glide). false = nichts klang.
    bool retargetMonoVoices (int newNote, double glideFromPitch);
    void startGlide (Voice& v, double fromPitch) noexcept;
    void startReleaseSamples (int note);
    Voice* allocateVoice();
    // Vor dem Start einer Note: Platz schaffen. Ist die Notenzahl erreicht, wird die älteste Note (bevorzugt
    // eine losgelassene) als Ganzes ausgeblendet; ebenso, wenn der Pool für 'samplesNeeded' nicht reicht.
    void makeRoomForNote (int samplesNeeded);
    uint64_t noteGroupCounter = 0;
    void enforceGroupPolyphony (const PlayableRegion& r);
    // off_by: Schlägt Region r mit group != 0 an, alle klingenden Stimmen mit off_by = group ausblenden. Stimmen
    // derselben Note (noteGroup) bleiben – sonst würden sich Layer einer Note mit group = off_by selbst abschneiden.
    void chokeOffBy (const PlayableRegion& r, uint64_t currentNote) noexcept;
    void releaseVoice (Voice& v);
    void releaseNoteVoices (int note);
    void releaseOrSustain (int note);   // Taste losgelassen: freigeben oder vom Pedal halten lassen
    void fadeNoteVoices (int note);
    Voice* firstSoundingVoice (int note);
    bool regionTriggers (const PlayableRegion& r, int note, int velocity, bool otherKeysHeld) const noexcept;
    bool passesSequenceAndRandom (const PlayableRegion& r) noexcept;   // zählt Round Robin weiter
    bool isKeyswitchKey (int note) const noexcept;
    bool anyRegionOnKey (int note) const noexcept;
    float nextRandom() noexcept;
    double playbackStep (double cents, double fileSampleRate) const noexcept;   // Tonhöhe → Abspielschritt
    bool renderVoice (Voice& v, float* left, float* right, int numSamples);   // false = beendet
    void updateSmoothing (int numSamples) noexcept;

    // Leerlauf (seit 18.10.2026): Klingt seit mindestens kIdleSeconds keine Stimme mehr, entfällt die Stimmen-
    // Schleife. Mono-LFO und Reglerglättung laufen unverändert in 16-Sample-Schritten weiter (bitgleich);
    // Lo-Fi pausiert (seit 21.10.2026) und beginnt beim nächsten Ton frisch.
    static constexpr double kIdleSeconds = 0.1;
    int64_t idleSamples = 0;
    void renderIdle (float* left, float* right, int numSamples) noexcept;
    static float cutoffOctaves (float knob) noexcept { return 10.0f * knob; }   // Oktaven über 20 Hz

    // Gehaltene Tasten in Anschlagsreihenfolge (für Mono/Legato: zuletzt gespielte hat Vorrang)
    void pushHeld (int note) noexcept;
    void removeHeld (int note) noexcept;
    int topHeld() const noexcept { return heldCount > 0 ? heldStack[(size_t) heldCount - 1] : -1; }

    std::shared_ptr<const Instrument> instrument, outgoing;
    std::array<Voice, voicePoolSize> voices {};
    Envelope::Params envParams;
    double sampleRate = 44100.0;

    VoiceMode voiceMode = VoiceMode::Poly;
    int maxVoices = 32;
    float glideSeconds = 0.0f;
    int monoNote = -1;                  // in Mono/Legato: aktuell klingende Note
    double lastMonoPitch = -1.0;        // Ausgangston für Glide im Mono-Modus

    float bendRange = 2.0f;
    float bendNormalized = 0.0f;        // Stellung des Rads, -1 ... +1
    double bendFactor = 1.0;
    float velocityRangeDecibels = 30.0f;   // Vel (Amp): Abstand von Velocity 1 zu 127 in dB
    float ampKeyScale = 0.0f, ampKeyScaleSmoothed = 0.0f;
    bool pedalDown = false;
    uint64_t orderCounter = 0;
    float fadeStep = 0.0f;

    // Sample Start and Tune
    float startKnob = 0.0f, startVelKnob = 0.0f;
    bool randomStart = false;
    int coarseTune = 0, keyShift = 0;
    double fineFactor = 1.0;
    float crushLevels = 0.0f;           // 0 = aus, sonst 2^(Bittiefe-1)
    float crushInverse = 0.0f;          // 1 / crushLevels (Multiplikation statt Division)
    float crushRate = 0.0f;             // Hz, 0 = keine Ratenreduktion (gesetzt in applyCrushAmount bzw. setCrushDirect)
    // Ratenreduktion in Stufen (seit 21.10.2026): Haltelänge in halben Samples, 0 = aus, 3 = 1½, 4 = 2 … 8 = 4.
    // Nur ganze oder halbe Haltelängen – dazwischen hält das Sample & Hold unregelmäßig (z. B. 3-3-2-3-2) und
    // erzeugt schrille, unharmonische Töne (Björns Hörtest: gut klangen genau 2, 2½, 3, 3½ und 4).
    int crushHalfHold = 0;

    // Filter: Zielwerte von der Oberfläche und geglättete Werte (in Blöcken von 16 Samples)
    FilterType filterType = FilterType::None;
    Envelope::Params filterEnvParams;
    float cutoffTarget = 10.0f, cutoffSmoothed = 10.0f;          // Oktaven über 20 Hz
    float resonanceTarget = 0.0f, resonanceSmoothed = 0.0f;
    float driveTarget = 0.0f, driveSmoothed = 0.0f;
    float envAmountTarget = 0.0f, envAmountSmoothed = 0.0f;
    float filterKeyTrack = 0.0f, filterKeyTrackSmoothed = 0.0f;   // Filter-Keytracking wirkt laufend (geglättet)
    float filterVelocity = 0.0f, filterVelocityCutoff = 0.0f;
    float smoothingCoeff = 0.0f;        // Glättungsfaktor für einen vollen 16-Sample-Block
    // Je Block einmal für alle Stimmen (seit 23.11.2026, vorher je Stimme): Resonanz- und Drive-Werte.
    // Neu berechnet nur, wenn sich der geglättete Regler geändert hat (im Ruhezustand also gar nicht).
    Filter::ResonanceInfo blockResonance;
    float blockResonanceFor = -1.0f;    // Reglerwert, für den blockResonance gilt (-1 = noch nicht berechnet)
    float blockDriveGain = 1.0f, blockDriveMakeup = 1.0f;
    float blockDriveFor = -1.0f;
    float blockSmoothingCoeff = 0.0f;   // Glättungsfaktor für den aktuellen Block (abhängig von seiner Länge)
    bool smoothingPrimed = false;       // erster Block nach prepare(): sofort auf die Zielwerte

    // LFOs
    LfoParams polyLfoParams, monoLfoParams;
    Lfo monoLfo;
    float monoLfoValue = 0.0f;                  // Wert des aktuellen 16-Sample-Blocks
    float routePitchTarget = 0.0f, routeFilterTarget = 0.0f, routeVolumeTarget = 0.0f, routePanTarget = 0.0f;
    float routePitch = 0.0f, routeFilter = 0.0f, routeVolume = 0.0f, routePan = 0.0f;
    float wheelPitch = 0.0f, wheelFilter = 0.0f, wheelAmp = 0.0f;
    // Crush-Filter wie bei einem alten Wandler (2-polig, Butterworth, 12 dB/Okt., Grenze 0,45 × Crush-Rate):
    // vor der Reduktion gegen Aliasing (Anti-Aliasing-Filter), optional danach zum Glätten der Stufen
    // (Rekonstruktionsfilter). Läuft auf der Summe, also nur einmal für alle Stimmen.
    struct CrushFilter
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1[2] {}, z2[2] {};
        void design (float cutoffHz, double sampleRate) noexcept;
        float process (float x, int ch) noexcept
        {
            const float y = b0 * x + z1[ch];          // Direktform II transponiert
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
    };
    // Emphasis wie bei CD und frühen Digitalgeräten: Höhen vor Crush anheben, danach genau spiegelbildlich absenken.
    // Senkt das Quantisierungsrauschen im Hochtonbereich (leise Stellen, ausklingende Töne). Filter 1. Ordnung
    // (Kuhschwanz: H(s) = (1 + s·t1) / (1 + s·t2), Anhebung bis t1/t2 ≈ +10,5 dB), Absenkung = Kehrwert.
    struct EmphasisFilter
    {
        float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
        float x1[2] {}, y1[2] {};
        void design (double t1, double t2, double sampleRate) noexcept;   // Bilineartransformation
        float process (float x, int ch) noexcept
        {
            const float y = b0 * x + b1 * x1[ch] - a1 * y1[ch];
            x1[ch] = x;
            y1[ch] = y;
            return y;
        }
    };
    // Eine vollständige Lo-Fi-Kette (Filter vor → Anhebung → Sample & Hold + Bittiefe → Absenkung → Filter nach).
    // Zwei Ketten: Beim Wechsel der Raten-Stufe wird in ca. 10 ms von der alten auf die neue übergeblendet.
    struct CrushChain
    {
        CrushFilter pre, post;
        EmphasisFilter emphasis, deemphasis;
        int halfHold = 0;               // 0 = Rate aus; sonst Haltelänge × 2 (ganzzahlig, exakt ohne Rundungsdrift)
        float freeStep = 0.0f;          // nur setCrushDirect: beliebige Rate (Phase als Kommazahl)
        int accumulator = 0;            // zählt in halben Samples
        float phase = 1.0f;
        float sumL = 0.0f, sumR = 0.0f, heldL = 0.0f, heldR = 0.0f;
        int count = 0;
        void configure (int newHalfHold, float directRateHz, double sampleRate) noexcept;
        void reset() noexcept;
        void process (float& l, float& r, float levels, float inverse) noexcept;
        bool rateOn() const noexcept { return halfHold > 0 || freeStep > 0.0f; }
    };
    CrushChain crushChains[2];
    int crushActive = 0;                             // welche Kette gerade gilt
    float crushFade = 1.0f;                          // 1 = keine Überblendung; sonst Anteil der neuen Kette
    float crushFadeStep = 0.0f;
    float crushDirectRate = 0.0f;                    // setCrushDirect: freie Rate
    bool crushRunning = false;                       // lief Lo-Fi im letzten Block? (sonst beim Einschalten neu beginnen)

    // --- Schalter zum Ausprobieren (Kette: Filter vor → Anhebung → Crush → Absenkung → Filter nach) ---
    // Filter vor Crush (Anti-Aliasing, 0,45 × Crush-Rate)
    static constexpr bool kCrushInputFilter = true;
    // Filter nach Crush (Rekonstruktion, glättet die Stufen) – Fassung C, im Hörtest bestätigt 04.10.2026
    static constexpr bool kCrushOutputFilter = true;
    // Emphasis: 0 = aus, 1 = klassisch 50/15 µs (wie CD), 2 = mit der Crush-Rate mitwandernd
    // (Anhebung immer im oberen Teil des verbleibenden Frequenzbereichs)
    static constexpr int kCrushEmphasis = 2;   // mitwandernd – im Hörtest gewählt 04.10.2026

    // Lo-Fi-Regler geglättet (wie Cutoff): sonst springen Bittiefe, Rate und Filter bei schnellem Drehen in
    // groben Stufen (je DAW-Block) – hörbar als Knistern
    float crushAmountTarget = 0.0f, crushAmountSmoothed = 0.0f;
    float crushAmountApplied = -1.0f;   // zuletzt an applyCrushAmount übergeben (-1 = noch nie)
    float fxAmount = 0.0f;                           // Anteil 0 … 100 für den gewählten Effekt (Regler LO-FI/CHORUS)
    FxMode fxRequested = FxMode::LoFi, fxActive = FxMode::LoFi;
    bjchorus::AnalogChorus chorus;
    double chorusAmountSet = -1.0;                   // zuletzt an den Chorus übergebener Anteil (nur bei Änderung neu)
    void processChorus (float* left, float* right, int numSamples) noexcept;
    void updateFxSwitch() noexcept;                  // umschalten, sobald der bisherige Effekt still ist
    bool crushDirect = false;                        // setCrushDirect benutzt: Werte nicht überschreiben
    void applyCrushAmount (float amount) noexcept;   // Lo-Fi-Wert → Bittiefe und Rate

    // Modulationsrad 0 … 1: Rohwert und geglättet. Aftertouch läuft je Stimme getrennt (siehe renderVoice).
    float modWheelTarget = 0.0f, modWheelSmoothed = 0.0f;
    std::array<float, 16> chanPressure {};             // Kanal-Aftertouch je MIDI-Kanal 0 … 1
    std::array<float, 128> notePressure {};            // polyphoner Aftertouch je Taste 0 … 1
    std::array<uint8_t, 128> noteChannel {};           // Kanal, auf dem die Taste zuletzt angeschlagen wurde
    static float toUnit (int value) noexcept           { return (float) std::min (127, std::max (0, value)) / 127.0f; }
    float blockSeconds = 0.0f;

    std::array<uint8_t, 128> ccValues {};   // Stand aller Controller (Start: 0)
    bool ccConditionsMet (const Region& s) const noexcept;
    static float velocityCrossfade (const Region& s, int velocity) noexcept;   // 0 … 1
    float controllerCrossfade (const Region& s) const noexcept;                // 0 … 1, aktueller Controllerstand

    int keyswitch = -1;                 // zuletzt gedrückter Keyswitch
    float noteRandom = 0.0f;            // Zufallswert des aktuellen Anschlags (lorand/hirand)
    uint32_t randomState = 0x9E3779B9u;

    std::array<int, 128> heldStack {};
    int heldCount = 0;
    std::array<int, 128> noteVelocity {};
    std::array<bool, 128> noteHadAttackVoices {};
};

} // namespace maermel
