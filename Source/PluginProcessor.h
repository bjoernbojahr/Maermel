// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// PluginProcessor.h – Märmel
// JUCE-Schicht: verbindet die Engine mit DAW, MIDI und gespeichertem Zustand.

#pragma once

#include <JuceHeader.h>
#include "engine/Synth.h"
#include "LibraryIndex.h"
#include "ParameterControl.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

class MaermelAudioProcessor : public juce::AudioProcessor,
                                private juce::Timer,
                                private juce::AsyncUpdater
{
public:
    MaermelAudioProcessor();
    ~MaermelAudioProcessor() override;

    // --- Audio -------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // --- Editor und Beschreibung ------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }
    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    // Der "Preset-Name" für die DAW ist der Name des geladenen Instruments.
    // Logic zeigt ihn über dem Plug-in-Fenster an und kann damit die Spur benennen.
    const juce::String getProgramName (int) override    { return getInstrumentName(); }
    void changeProgramName (int, const juce::String&) override {}

    // --- Zustand im DAW-Projekt -------------------------------------------
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Für die Oberfläche (nur vom Message-Thread aufrufen) ---------------
    // Lädt im Hintergrund; bei schnell aufeinanderfolgenden Aufrufen gewinnt der letzte.
    // .sfz/.exs: Ist LOCK aus, werden die Einstellungen aus der gleichnamigen .sfzp übernommen,
    //            ohne Preset die Standardwerte. Ist LOCK an, bleiben alle Einstellungen erhalten.
    // .sfzp:     lädt das zugehörige Instrument und immer die Einstellungen des Presets.
    // applyPreset = false: Einstellungen nie ändern (z. B. beim Öffnen eines DAW-Projekts).
    void loadInstrumentFile (const juce::File& file, bool applyPreset = true);

    // Preset neben dem aktuellen Instrument speichern (Instrument.sfzp); true = gespeichert.
    // Ist das aktuelle Instrument eine EXS-Datei, entsteht vorher daneben eine SFZ-Datei
    // (wahlweise mit kopierten Samples, siehe Einstellungen), und das Preset verweist auf das SFZ.
    bool savePreset();
    juce::String getLastSaveMessage() const  { return lastSaveMessage; }

    // Einstellungen als Text (gleiches Format wie .sfzp, aber ohne Instrument) für die Zwischenablage
    juce::String copySettingsAsText() const;
    // Einstellungen aus einem solchen Text übernehmen; false = kein Märmel-Text
    bool pasteSettingsFromText (const juce::String& text);
    static bool isSettingsText (const juce::String& text);
    void resetSettingsToDefaults()     { applyParameterValues (nullptr); }
    juce::File getPresetFile() const  { return getInstrumentFile().withFileExtension ("sfzp"); }

    // LOCK (früher FIX): beim Instrumentwechsel alle Einstellungen behalten, nur die Samples austauschen
    bool isFixActive() const noexcept   { return fixActive.load(); }
    bool isChorusMode() const noexcept  { return fxModeParam != nullptr && fxModeParam->load() > 0.5f; }
    void setFixActive (bool active) noexcept { fixActive = active; }
    juce::File getInstrumentFile() const;
    juce::String getInstrumentName() const;
    juce::String getInstrumentInfo() const;
    int getInstrumentVersion() const noexcept           { return instrumentVersion.load(); }

    // Fehlende Samples beim letzten Laden: Zähler steigt bei jedem Laden mit fehlenden Samples (für die rote
    // Meldung im Display); noSamplesLoaded = kein einziges Sample gefunden
    int getMissingSamplesAlert() const noexcept         { return missingSamplesAlert.load(); }
    bool wereNoSamplesLoaded() const noexcept            { return noSamplesLoaded.load(); }
    // Zähler: Instrument zu groß für den Arbeitsspeicher, nicht geladen (rote Meldung im Display)
    int getMemoryAlert() const noexcept                  { return memoryAlert.load(); }
    // Zähler: SFZ- bzw. EXS-Datei nicht gefunden (z. B. gelöscht, bevor die Liste neu aufgebaut war)
    // Rechtsklick-Menü „All Notes Off“: alle Noten loslassen, Sustain-Pedal aus (im nächsten Audio-Block)
    void requestAllNotesOff() noexcept                   { allNotesOffRequested = true; }
    int getMissingFileAlert() const noexcept             { return missingFileAlert.load(); }
    // Zähler: Instrument eines DAW-Projekts auf diesem Rechner nicht gefunden (rote Meldung, Einzelheiten in Loading notes)
    int getMissingInstrumentAlert() const noexcept       { return missingInstrumentAlert.load(); }
    bool wasMissingFileExs() const noexcept              { return missingFileExs.load(); }
    int getActiveKeyswitch() const noexcept             { return activeKeyswitch.load(); }   // -1 = keiner

    // Anzeige der LFO-Rate: Hz oder Notenwert (bei Sync)
    static juce::String lfoRateText (float knob, bool sync);

    // Anzeige einer Hüllkurvenzeit zu einer Reglerstellung (z. B. für die Zeit bei Velocity 1)
    static juce::String envelopeTimeText (float knob);

    // Zum vorherigen/nächsten Instrument blättern (Pfeiltasten und MIDI-Umschaltung)
    void stepInstrument (int delta);

    // --- Live-Edit: SFZ in einem externen Editor bearbeiten -------------------
    // Externer SFZ-Editor und Live-Edit (Stand 04.10.2026):
    //  - Pfad zum Editor und die Einstellung „Live edit“ stehen in den Einstellungen (global, bleiben gespeichert).
    //    Die Einstellung erlaubt die Überwachung nur – sie startet sie nicht.
    //  - Die Taste EDIT öffnet das aktuelle Instrument im Editor. Ist Live-Edit erlaubt, beginnt zugleich die
    //    Überwachung genau dieser Datei (rote LED über EDIT): Märmel prüft alle 0,5 s Änderungsdatum und Größe
    //    (die Datei wird dabei nicht geöffnet) und lädt sie nach dem Speichern automatisch neu – alle
    //    Einstellungen bleiben, das Preset wird nicht neu geladen.
    //  - Die Überwachung endet beim Wechsel des Instruments (sonst bearbeitet man im Editor womöglich ein
    //    Instrument, das gar nicht mehr gespielt wird), beim Ausschalten der Einstellung und beim Beenden.
    static constexpr const char* kEditorPathKey = "editorPath";
    juce::File getEditorApplication() const;
    static bool isValidEditor (const juce::File& editor);
    bool isLiveEditActive() const noexcept        { return liveEditActive; }   // Überwachung läuft (LED)
    static constexpr const char* kLiveEditAllowedKey = "liveEditAllowed";
    bool isLiveEditAllowed() const;               // Einstellung „Live edit“ (global, gespeichert)

    // Logfile-Schalter: wie Live-Edit nur für diese Instanz und nur bis zum Beenden (wird nicht gespeichert)
    bool isLogErrorsActive() const noexcept       { return logErrorsActive.load(); }
    void setLogErrorsActive (bool active) noexcept { logErrorsActive = active; }
    void setLiveEditActive (bool active);         // false: Überwachung beenden
    // Öffnet das aktuelle Instrument im Editor. Bei einer EXS-Datei wird vorher wie beim Speichern
    // ein SFZ daneben erzeugt (oder ein vorhandenes verwendet) und Märmel darauf umgeschaltet.
    // message: Hinweis für die Statusanzeige (z. B. "SFZ created from EXS"); false = Fehler
    // Ist Live-Edit erlaubt, beginnt danach die Überwachung genau dieser Datei.
    bool openCurrentInstrumentInEditor (juce::String& message);

    // --- MIDI-Umschaltung (Einstellungen gelten für alle Instanzen) ------------
    enum class LearnTarget { None = 0, NextNote, PrevNote, NextController, PrevController,
                             ParamSlot1, ParamSlot2, ParamSlot3, ParamSlot4 };   // ParamSlot: direkte Steuerung
    static const char* settingKey (LearnTarget target);
    void startLearning (LearnTarget target) noexcept    { learnedValue = -1; learnTarget = (int) target; }
    void stopLearning() noexcept                         { learnTarget = 0; }
    LearnTarget getLearnTarget() const noexcept          { return (LearnTarget) learnTarget.load(); }
    int takeLearnedValue() noexcept                      { return learnedValue.exchange (-1); }

    juce::AudioProcessorValueTreeState parameters;

    // Direkte Steuerung von Parametern per MIDI-Controller: Rückmeldung fürs Display
    // (Zähler steigt bei jeder Bewegung; Zeile und Controller der letzten Bewegung)
    int getControlFeedbackCount() const noexcept   { return controlFeedbackCount.load(); }
    // MIDI-Anzeige im Display: Zähler der angekommenen Noten (nur Note-On) und Anschlag der letzten
    int getMidiNoteCount() const noexcept          { return midiNoteCount.load(); }
    int getMidiNoteVelocity() const noexcept       { return midiNoteVelocity.load(); }
    int getControlFeedbackSlot() const noexcept    { return controlFeedbackSlot.load(); }
    // Ungültige Slot-Nummer (z. B. -1): kein Controller (-1) bzw. kein Ziel (0) – statt stillschweigend einen anderen Slot
    int getControlCc (int slot) const noexcept      { return isValidSlot (slot) ? controlSlots[(size_t) slot].cc.load() : -1; }
    int getControlTarget (int slot) const noexcept  { return isValidSlot (slot) ? controlSlots[(size_t) slot].target.load() : 0; }
    static bool isValidSlot (int slot) noexcept     { return slot >= 0 && slot < maermel::control::kSlots; }

    // Export des aktuellen Instruments: SFZ (Sample-Pfade umgeschrieben), Preset (.sfzp) und Ordner
    // „<Name> Samples“ mit allen benötigten Samples. Nur wenn alle Samples gefunden wurden. Kopiert wird im
    // Hintergrund; der Stand ist über getExportStatus abrufbar.
    bool canExportInstrument() const;
    void startExport (const juce::File& targetSfz);
    // Zustand des Exports (exportState, ExportStatus::state). Einfaches enum, damit es sich ohne Umwandlung in
    // std::atomic<int> speichern und mit int vergleichen lässt.
    enum ExportState : int { exportIdle = 0, exportRunning = 1, exportFinished = 2, exportFailed = 3 };
    struct ExportStatus { int state = exportIdle, done = 0, total = 0; };
    ExportStatus getExportStatus() const noexcept   { return { exportState.load(), exportDone.load(), exportTotal.load() }; }
    juce::String getExportMessage() const           { const juce::ScopedLock sl (infoLock); return exportMessage; }

    // Ladehinweise des aktuellen Instruments vollständig (leer = keine Hinweise), für die Tafel „Loading notes“
    juce::String getLoadingNotes() const            { const juce::ScopedLock sl (infoLock); return loadingNotes; }
    juce::String getLoadingNotesInstrument() const  { const juce::ScopedLock sl (infoLock); return loadingNotesInstrument; }

    // Suchtext für das Library-Menü dieser Instanz (nur Message-Thread)
    juce::String libraryFilter;

    // Hält den gemeinsamen Library-Index am Leben, auch wenn keine Oberfläche offen ist
    juce::SharedResourcePointer<LibraryIndex> library;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Lädt Instrumente in einem eigenen Thread, damit weder Oberfläche noch Audio warten
    class InstrumentLoader : public juce::Thread
    {
    public:
        explicit InstrumentLoader (MaermelAudioProcessor& p) : juce::Thread ("Maermel Loader"), owner (p) {}
        void request (const juce::File& file);
        bool hasNewerRequest() const;
        void run() override;
    private:
        MaermelAudioProcessor& owner;
        mutable juce::CriticalSection requestLock;
        juce::File requested;
        bool pending = false;
    };

    void loadInstrumentNow (const juce::File& file);   // Loader-Thread
    void notifyHostAboutName();                        // Message-Thread: DAW über neuen Namen informieren
    void applyParameterValues (const juce::XmlElement* preset);   // nullptr = Standardwerte
    bool convertCurrentExsToSfz (juce::String& message);          // Message-Thread
    juce::String lastSaveMessage;
    std::unique_ptr<juce::XmlElement> createSettingsXml() const;  // alle Parameter, ohne Instrument

    // LOCK. Atomar, weil setStateInformation (Projekt laden) bei manchen Hosts nicht auf dem Message-Thread läuft,
    // die Oberfläche den Wert aber gleichzeitig liest.
    std::atomic<bool> fixActive { false };

    // Live-Edit (nur Message-Thread, außer liveReloadPending)
    bool liveEditActive = false;
    std::atomic<bool> logErrorsActive { false };   // auch vom Loader-Thread gelesen
    juce::File watchedFile;
    juce::Time watchedTime, candidateTime;
    juce::int64 watchedSize = -1, candidateSize = -1;
    bool candidatePending = false;
    int liveEditTicks = 0;
    std::atomic<bool> liveReloadPending { false };   // das laufende Laden ist ein Live-Edit-Neuladen
    std::unique_ptr<juce::ChildProcess> editorProcess;
    void checkLiveEdit();

    // Logfile: Meldung mit dem aktuellen Instrumentnamen anhängen (nur wenn in den Einstellungen aktiv)
    void logError (const juce::String& text) const;
    bool savePresetInternal();
    bool openInEditorInternal (juce::String& message);
    juce::String presetNote;                      // für die Info-Zeile, z. B. "Preset: Piano.sfzp" (unter infoLock)

    // --- Einstellungen erst zusammen mit dem neuen Instrument übernehmen (seit 23.11.2026) ---
    // Früher wurden beim Instrumentwechsel die Einstellungen des neuen Instruments (Preset bzw. Standardwerte)
    // sofort gesetzt, das Instrument selbst kam aber erst nach dem Laden. Dazwischen spielte das alte Instrument
    // mit den neuen Einstellungen – z. B. ein lautes Instrument mit dem angehobenen Volume des leisen neuen
    // (Lautstärkesprung, im Debug-Build deutlich, auf langsamen Platten auch in der Release-Fassung).
    // Jetzt: Die Einstellungen werden nur gemerkt. Ist das Instrument geladen, setzt der Message-Thread erst die
    // Einstellungen und übergibt gleich danach das Instrument an den Audio-Thread (handleAsyncUpdate). Die Lücke
    // ist damit höchstens ein Audioblock; die Glättung der Regler (20 ms) verdeckt sie.
    struct DeferredSettings
    {
        bool active = false;                                 // es warten Einstellungen auf ein Instrument
        juce::File file;                                     // zu diesem Ladeauftrag gehören sie
        std::unique_ptr<juce::XmlElement> preset;            // Werte; nullptr = Standardwerte
        std::shared_ptr<const maermel::Instrument> loaded;   // fertig geladen, wartet auf die Übergabe
    };
    DeferredSettings deferred;                     // unter deferredLock
    juce::CriticalSection deferredLock;
    void deferSettings (const juce::File& file, const juce::XmlElement* preset);   // Message-Thread
    void cancelDeferredSettings();                 // Message-Thread: neuer Wechsel ohne neue Einstellungen
    void loadFinished (const juce::File& file);    // Loader-Thread: Message-Thread anstoßen, falls Einstellungen warten
    void handleAsyncUpdate() override;             // Message-Thread: Einstellungen setzen, dann Instrument übergeben
    void handOverInstrument (std::shared_ptr<const maermel::Instrument> instrument);   // an den Audio-Thread
    void timerCallback() override;              // gibt ausgetauschte Instrumente frei
    void takePendingInstrument() noexcept;      // Audio-Thread
    void collectFinishedInstrument() noexcept;  // Audio-Thread
    void updateVoiceSettings() noexcept;        // Audio-Thread
    void updateSampleSettings() noexcept;       // Audio-Thread
    void updateFilterSettings() noexcept;       // Audio-Thread
    void updateLfoSettings() noexcept;          // Audio-Thread (liest auch das Songtempo)
    void updateEnvelopeIfChanged() noexcept;    // Audio-Thread
    void handleMidi (const juce::MidiMessage& m) noexcept;
    void renderRange (juce::AudioBuffer<float>& buffer, int start, int end) noexcept;

    maermel::Synth synth;

    // Instrumentwechsel ohne Speicheroperationen im Audio-Thread:
    // Die Oberfläche legt das neue Instrument in 'pending', der Audio-Thread tauscht es ein
    // und legt das alte in 'retired', der Timer gibt es auf dem Message-Thread frei.
    juce::SpinLock instrumentLock;
    std::shared_ptr<const maermel::Instrument> pendingInstrument, retiredInstrument;

    mutable juce::CriticalSection infoLock;
    juce::String loadingNotes, loadingNotesInstrument;   // geschützt durch infoLock
    juce::String exportMessage;                          // geschützt durch infoLock
    std::atomic<int> currentMissingSamples { 0 };        // fehlende Samples des aktuellen Instruments
    class ExportJob;                                     // Hintergrund-Thread für den Export
    std::unique_ptr<juce::Thread> exportJob;
    std::atomic<int> exportState { exportIdle }, exportDone { 0 }, exportTotal { 0 };
    juce::File currentFile;
    juce::String instrumentName { "No Instrument" }, instrumentInfo;
    std::atomic<int> instrumentVersion { 0 };
    std::atomic<int> missingSamplesAlert { 0 };
    std::atomic<bool> noSamplesLoaded { false };
    std::atomic<int> memoryAlert { 0 };
    std::atomic<int> missingFileAlert { 0 };
    std::atomic<bool> missingFileExs { false };
    std::atomic<int> missingInstrumentAlert { 0 };

    // Instrument im DAW-Projekt wiederfinden (siehe locateInstrument), alles geschützt durch infoLock:
    //  - Fingerabdruck (Sample-Namen) des geladenen Instruments, für das Projekt
    //  - Angaben eines Projekts, dessen Instrument hier nicht gefunden wurde: werden beim Speichern unverändert
    //    zurückgeschrieben, damit das Projekt auf dem anderen Rechner weiter funktioniert
    //  - Hinweis für Loading notes, wenn das Instrument an anderer Stelle gefunden wurde
    juce::String currentFingerprint;
    juce::File currentFingerprintFile;
    juce::String unresolvedPath, unresolvedRelPath, unresolvedFingerprint;
    juce::String locateNote;
    juce::File locateNoteFile;
    juce::String libraryRelativePath (const juce::File& file) const;
    juce::File locateInstrument (const juce::String& savedPath, const juce::String& savedRelPath,
                                 const juce::String& fingerprint, juce::String& report) const;
    std::atomic<int> midiNoteCount { 0 }, midiNoteVelocity { 0 };   // für die MIDI-Anzeige
    // Speicherschutz (nur Loader-Thread): zuletzt übergebenes Instrument und sein Speicherbedarf
    std::weak_ptr<const maermel::Instrument> lastInstrument;
    uint64_t lastInstrumentBytes = 0;
    void releaseInstrumentBeforeLoading();   // Loader-Thread: aktuelles Instrument freigeben und warten
    std::atomic<int> activeKeyswitch { -1 };

    // MIDI-Umschaltung: Werte aus den Einstellungen (-1 = aus), vom Timer aktualisiert
    std::atomic<int> switchNextNote { -1 }, switchPrevNote { -1 };
    std::atomic<int> switchNextCC { -1 }, switchPrevCC { -1 };
    bool nextCCDown = false, prevCCDown = false;   // nur Audio-Thread: wie ein Pedal erst wieder loslassen
    std::atomic<int> pendingSteps { 0 };           // vom Audio-Thread angefordert, vom Timer ausgeführt
    std::atomic<int> learnTarget { 0 };

    // Direkte Steuerung von Parametern (vier Zeilen, Einstellungen pro Rechner; siehe ParameterControl.h)
    struct ControlSlot
    {
        std::atomic<int> cc { -1 }, target { 0 };
        std::atomic<bool> jump { true }, active { false };
        std::atomic<juce::RangedAudioParameter*> param { nullptr };
        std::atomic<int> generation { 0 };       // steigt, wenn sich die Zuordnung ändert (Abholen neu beginnen)
        // nur Audio-Thread:
        int seenGeneration = -1;
        bool captured = false;
        float lastIncoming = -1.0f, lastSet = -1.0f;
    };
    std::array<ControlSlot, maermel::control::kSlots> controlSlots;
    std::atomic<int> controlFeedbackCount { 0 }, controlFeedbackSlot { -1 };
    bool handleParameterControllers (const juce::MidiMessage& m) noexcept;   // Audio-Thread
    std::atomic<int> learnedValue { -1 };

    void syncSwitchSettings();                     // Message-Thread
    juce::String logHeader() const;                // Version, Build, Plattform, Format für das Logfile

    bool handleInstrumentSwitching (const juce::MidiMessage& m) noexcept;   // true = verschluckt

    // Hüllkurven: je A, A-Vel, D, D-Vel, S, T, R, Key (Amp und Filter)
    struct EnvelopeParamPointers
    {
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* attackVel = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* decayVel = nullptr;
        std::atomic<float>* sustain = nullptr;
        std::atomic<float>* sustainTime = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* key = nullptr;
    };
    EnvelopeParamPointers ampEnvParams, filterEnvParams;
    std::array<float, 8> lastAmpEnv {}, lastFilterEnv {};
    static maermel::Envelope::Params envelopeFromKnobs (const EnvelopeParamPointers& p, std::array<float, 8>& last, bool& changed);
    std::atomic<float>* volumeParam  = nullptr;   // dB
    std::atomic<float>* velocityParam = nullptr;  // Dynamikbereich in dB
    std::atomic<float>* wheelAmpParam = nullptr;  // Wheel LFO für LFO-Volume und LFO-Pan, 0 ... 1
    std::atomic<float>* atWheelParam = nullptr;   // AT→WHEEL je Instrument (Taste mit LED), 0/1
    std::atomic<float>* velOffsetParam = nullptr; // -32 ... +32, pro Instrument (Preset)
    std::atomic<float> velocityCurve { 0.0f };    // -1 ... +1, global (Einstellungen), vom Timer aktualisiert
    std::atomic<bool> aftertouchAsWheel { true };  // Aftertouch wirkt wie das Modulationsrad (global, abschaltbar)
    bool aftertouchActive = false;                 // nur Audio-Thread: wirkt gerade ein Aftertouch-Wert?
    std::atomic<bool> allNotesOffRequested { false };   // „All Notes Off“ aus dem Menü, ausgeführt im Audio-Thread
    std::atomic<float>* ampKeyScaleParam = nullptr; // -1 ... +1 (±3 dB je Oktave, höchstens ±12 dB)
    std::atomic<float>* voiceModeParam = nullptr; // Index: Leg, Mon, 2, 6, 8, 12, 16, 32, 64
    std::atomic<float>* glideParam = nullptr;     // Reglerstellung 0 ... 1

    // Sample Start and Tune
    std::atomic<float>* startParam = nullptr;     // 0 ... 1
    std::atomic<float>* startVelParam = nullptr;  // -1 ... +1
    std::atomic<float>* startRndParam = nullptr;  // 0 / 1
    std::atomic<float>* coarseParam = nullptr;    // -24 ... +24
    std::atomic<float>* fineParam = nullptr;      // Cent
    std::atomic<float>* keyShiftParam = nullptr;  // -12 ... +12
    std::atomic<float>* crushParam = nullptr;     // 0 ... 100 (Anteil des gewählten Effekts: Lo-Fi oder Chorus)
    std::atomic<float>* fxModeParam = nullptr;    // 0 = Lo-Fi, 1 = Chorus (nicht automatisierbar)
    std::atomic<float>* pitchBendParam = nullptr; // 0 ... 12 Halbtöne

    // Filter
    std::atomic<float>* filterTypeParam = nullptr;
    std::atomic<float>* cutoffParam = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* filterKeyParam = nullptr;
    std::atomic<float>* filterEnvParam = nullptr;    // -1 ... +1
    std::atomic<float>* filterVelParam = nullptr;      // Vel Env: Velocity auf die Hüllkurventiefe
    std::atomic<float>* filterVelCutParam = nullptr;   // Vel: Velocity direkt auf den Cutoff

    // LFOs (je Poly und Mono: Rate, Sync, Env Mode, Wellenform, Invert, Delay), Routing, Wheel LFO
    struct LfoParamPointers
    {
        std::atomic<float>* rate = nullptr;
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* env = nullptr;
        std::atomic<float>* wave = nullptr;
        std::atomic<float>* invert = nullptr;
        std::atomic<float>* delay = nullptr;
    };
    LfoParamPointers polyLfoParams, monoLfoParams;

    // Zuletzt umgerechnete LFO-Werte (nur Audio-Thread): neu rechnen nur bei geänderten Reglern oder Tempo
    struct LfoCache
    {
        bool valid = false;
        std::array<float, 6> raw {};
        double bpm = 0.0;
        double quarters = 0.0;
        maermel::LfoParams params;
    };
    LfoCache polyLfoCache, monoLfoCache;
    std::atomic<float>* lfoPitchParam = nullptr;
    std::atomic<float>* lfoFilterParam = nullptr;
    std::atomic<float>* lfoVolumeParam = nullptr;
    std::atomic<float>* lfoPanParam = nullptr;
    std::atomic<float>* wheelPitchParam = nullptr;
    std::atomic<float>* wheelFilterParam = nullptr;

    juce::SmoothedValue<float> masterGain;        // Vol: geglättet, damit Bewegungen nicht knacken

    juce::AudioBuffer<float> renderBuffer;      // Stereo-Zwischenpuffer der Engine

    InstrumentLoader loader { *this };          // zuletzt deklariert: startet nach allen anderen Mitgliedern

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaermelAudioProcessor)
};
