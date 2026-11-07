// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// PluginEditor.h – Märmel
// Finales Design nach der abgestimmten Stilprobe (Konzeptpapier, Abschnitt „GUI-Entwurf“).
// Fenster 1032 × 505 Punkte. Alle Positionen entsprechen tools/stilprobe_zeichnen.py.

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LibraryIndex.h"
#include "SettingsDialog.h"
#include "MaermelLook.h"
#include "MaermelControls.h"
#include "AboutPanel.h"
#include "NotesPanel.h"

#include <array>
#include <memory>
#include <vector>

class MaermelAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::FileDragAndDropTarget,
                                    private juce::Timer
{
public:
    explicit MaermelAudioProcessorEditor (MaermelAudioProcessor&);
    ~MaermelAudioProcessorEditor() override;

    // Drag & Drop aus Finder/Explorer: .sfz, .sfzp (und .exs, wenn EXS-Import an) aufs Fenster ziehen lädt sie
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void paintOverChildren (juce::Graphics&) override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Rechtsklick auf die Oberfläche: Einstellungen kopieren/einfügen; Klick auf den Schriftzug: About
    void mouseDown (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    static constexpr int kWidth = 1032, kHeight = 505;

    // Fenstergröße: Auswahl des Nutzers (100 … 200 %) × Skalierung durch das System/den Host
    void setScaleFactor (float newScale) override;
    void setGuiScale (float scale);

private:
    void timerCallback() override;

    // --- Library und Instrumente ---
    void showLibraryMenu();
    void addFolderItems (juce::PopupMenu& menu, const LibraryIndex::Folder& folder, const juce::File& current);
    void menuItemChosen (int result);
    void stepInstrument (int delta);
    void showSearchDialog();
    void toggleSettings();
    void chooseFile();
    void showContextMenu();
    juce::PopupMenu::Options menuOptions();   // Menüs im Plugin-Fenster oder als eigene Fenster (kMenusInsidePluginWindow)
    void copySettings();
    void pasteSettings();
    void showAbout();
    void showNotes();   // alle Ladehinweise des aktuellen Instruments
    void applyScale();

    // --- Display: Instrumentname, Werteänderungen und kurze Meldungen (1,5 s) ---
    void showStatus (const juce::String& text);
    void showAlert (const juce::String& text);   // rot, doppelt so lange (z. B. fehlende Samples)
    void refreshDisplay();

    // Name und Wert eines Bedienelements nur zeigen, wenn der Anwender es gerade selbst bedient (Klick, Ziehen,
    // Mausrad) – nicht, wenn sich der Wert durch Instrumentwechsel, Preset, MIDI oder Automation ändert, während
    // die Maus zufällig darüber steht (gefunden 16.10.2026: „AMP ENV A 1.19 ms“ statt Instrumentname)
    void showStatusIfUser (juce::Component& c, std::function<juce::String()> text);

    struct InputWatcher : juce::MouseListener
    {
        juce::Component::SafePointer<juce::Component> component;   // zuletzt bediente Komponente
        juce::uint32 time = 0;
        void stamp (const juce::MouseEvent& e) { component = e.originalComponent; time = juce::Time::getMillisecondCounter(); }
        void mouseDown (const juce::MouseEvent& e) override        { stamp (e); }
        void mouseDrag (const juce::MouseEvent& e) override        { stamp (e); }
        void mouseUp (const juce::MouseEvent& e) override          { stamp (e); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { stamp (e); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails&) override { stamp (e); }
        bool touched (juce::Component& c) const
        {
            auto* last = component.getComponent();
            return last != nullptr && (last == &c || c.isParentOf (last))
                   && juce::Time::getMillisecondCounter() - time < 250;
        }
    };
    InputWatcher inputWatcher;

    // --- Aufbau ---
    void addKnob (juce::Slider& s, const char* id, const juce::String& displayName, float cx, float cy, float radius);
    void addVFader (juce::Slider& s, const char* id, const juce::String& displayName, float cx, float y0, float y1, bool bipolar);
    void addHFader (juce::Slider& s, const char* id, const juce::String& displayName, float x0, float x1, float cy,
                    bool bipolar, bool compact);
    void attachSlider (juce::Slider& s, const juce::String& id, const juce::String& displayName);
    void placeButton (juce::Button& b, juce::Rectangle<float> key);   // Bounds = sichtbare Taste + Leuchtrand
    void addValueBox (juce::Slider& s, float cx, float y, float width,
                      std::function<juce::String (juce::Slider&)> format = {});

    MaermelAudioProcessor& audioProcessor;   // "processor" ist in AudioProcessorEditor bereits vergeben
    juce::SharedResourcePointer<LibraryIndex> library;

    // Gestaltung zuerst deklarieren: lebt länger als alle Bedienelemente
    maermel::ui::LookAndFeel look;   // keine Tooltips auf der Oberfläche (bewusst, wirkten irritierend)

    // --- Kopfzeile ---
    juce::Slider glide;
    std::array<juce::TextButton, 9> voiceButtons;
    maermel::ui::InstrumentDisplay display;
    // AT→WHEEL je Instrument (wie LOCK: Zustand über die rote LED darüber; der Pfeil wird gezeichnet)
    juce::TextButton atWheelButton { "AT->WHEEL" };
    bool shownAtWheel = false;
    juce::TextButton fixButton { "LOCK" }, loadButton { "LOAD" }, saveButton { "SAVE" }, editButton { "EDIT" },
                     pathButton { "PATH" };
    bool shownLiveEdit = false;   // Stand der roten LED über EDIT (Live-Edit-Überwachung läuft)

    // --- Sample Start and Tune ---
    juce::Slider start, startVel, coarse, fine, keyShift, crush, pitchBend, wheelPitch;
    juce::TextButton rndButton { "RND" };
    // Umschalter unter dem Regler: zeigt den gewählten Effekt („LO-FI“ oder „CHORUS“), Klick wechselt
    juce::TextButton fxButton { "LO-FI" };
    bool shownChorus = false;
    void toggleFxMode();
    juce::String fxStatusText() const;   // z. B. „CHORUS 50 %“ oder „LO-FI 11.7 bit/12.6 kHz“

    // --- Filter ---
    juce::Slider cutoff, reso, drive, filterKey, filterVelCut, filterVelEnv, filterEnv, wheelFilter;

    // --- Amp ---
    juce::Slider wheelAmp, velOffset, ampVel, ampKey, volume;   // wheelAmp: WHEEL LFO der Amp-Sektion (LFO-Volume und -Pan)

    // --- LFO: [0] = Poly, [1] = Mono ---
    struct Lfo
    {
        juce::Slider rate, delay;
        juce::ToggleButton sync { "SYNC" }, env { "ENV MODE" };
        std::array<juce::TextButton, 4> waves;
        juce::TextButton invert { "INVERT" };
    };
    std::array<Lfo, 2> lfos;
    std::array<juce::Slider, 4> routes;

    // --- Hüllkurven: [0] = Filter, [1] = Amp; A D S T R KEY ---
    std::array<std::array<juce::Slider, 6>, 2> envs;

    // Hilfskomponenten und Verbindungen zu den Parametern (nach den Reglern deklariert: werden zuerst abgebaut)
    std::vector<std::unique_ptr<maermel::ui::ValueBox>> valueBoxes;
    std::vector<std::unique_ptr<maermel::ui::VelMarker>> velMarkers;
    std::unique_ptr<maermel::ui::LedChoice> filterType;
    std::vector<std::unique_ptr<maermel::ui::ChoiceButtons>> choiceGroups;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    // Einstellungen als Tafel über der Oberfläche (mit Bildlauf, weil das Fenster flacher ist als die Tafel)
    std::unique_ptr<SettingsComponent> settingsPanel;
    std::unique_ptr<juce::Viewport> settingsViewport;

    std::unique_ptr<AboutPanel> aboutPanel;
    std::unique_ptr<NotesPanel> notesPanel;   // Tafel „Loading notes“

    float userScale = 1.0f, hostScale = 1.0f;

    std::unique_ptr<juce::FileChooser> fileChooser;
    void exportInstrument();                       // Speichern-Dialog, dann Export im Hintergrund
    int shownExportState = 0, shownExportDone = -1;   // zuletzt angezeigter Export-Stand
    juce::Array<juce::File> menuFiles;               // Menüeintrag → Datei

    juce::Image background;                          // gebürsteter Hintergrund, je Bildschirm-Auflösung
    float backgroundScale = 0.0f;
    bool blueBackground = false, backgroundIsBlue = false;   // gewählte bzw. zuletzt erzeugte Variante

    int shownVersion = -1;
    int shownKeyswitch = -2;
    juce::String statusText;
    juce::uint32 statusUntil = 0;
    bool statusIsAlert = false;
    int shownMissingAlert = 0;
    int shownMemoryAlert = 0;
    int shownMissingFileAlert = 0;
    int shownMissingInstrumentAlert = 0;
    bool fileDragActive = false;   // Datei wird übers Fenster gezogen: blauer Rahmen

    // MIDI-Anzeige: dünner roter Lichtstreifen an der Unterkante des Displays, leuchtet bei jeder Note auf
    // (leiser Anschlag schwächer, ab Anschlag ≈ 72 gleich hell) und ist nach 400 ms aus. Eigener Timer mit 30 Hz, damit das
    // Ausklingen weich ist; er liest nur einen Zähler und zeichnet nur den schmalen Streifen neu.
    struct MidiGlowTimer : public juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { if (tick) tick(); }
    } midiGlowTimer;
    int shownMidiNotes = 0;
    float midiGlow = 0.0f;                 // aktuelle Helligkeit 0 … 1
    float midiGlowPeak = 0.0f;             // Helligkeit beim letzten Aufleuchten
    double midiGlowStartMs = 0.0;          // Zeitpunkt des letzten Aufleuchtens
    void updateMidiGlow();
    void paintMidiGlow (juce::Graphics&) const;
    juce::Rectangle<int> midiGlowArea() const;
    juce::File droppableFile (const juce::StringArray& files) const;
    int shownControlFeedback = 0;   // direkte Steuerung per MIDI-Controller: zuletzt angezeigte Bewegung

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaermelAudioProcessorEditor)
};
