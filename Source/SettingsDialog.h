// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SettingsDialog.h – Märmel
// Einstellungen, die für alle Märmel-Instanzen gelten:
// Library-Ordner, Blättern nur im aktuellen Ordner, Instrumentwechsel per MIDI-Note oder Controller.
// Wird als Tafel über der Oberfläche eingeblendet (kein eigenes Fenster), damit sie nie länger
// lebt als das Plugin.

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>
#include <functional>

class SettingsComponent : public juce::Component,
                          private juce::Timer
{
public:
    explicit SettingsComponent (MaermelAudioProcessor& processor);
    ~SettingsComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;   // "Fertig" gedrückt
    std::function<void (float)> onGuiScaleChanged;   // neue Fenstergröße (1.0 = 100 %)
    void showGuiScale (float scale);                 // Tasten anpassen (z. B. nach Wahl im Rechtsklick-Menü)

    // Hintergrund: Grau (Standard) oder Blau, ruhiger; global gespeichert
    std::function<void (bool blue)> onBackgroundChanged;
    static constexpr const char* kBlueBackgroundKey = "blueBackground";

    // Fenstergröße: global gespeichert, gilt für alle Instanzen
    static constexpr const char* kGuiScaleKey = "guiScale";
    static constexpr float kGuiScales[] = { 1.0f, 1.25f, 1.5f, 2.0f };

private:
    using Target = MaermelAudioProcessor::LearnTarget;

    struct SwitchRow
    {
        Target target;
        bool isNote;
        juce::Label label;
        juce::ComboBox value;
        juce::TextButton learnButton { "Learn" };
    };

    // Direkte Steuerung von Parametern per MIDI-Controller (vier Zeilen; Einstellungen pro Rechner)
    struct ControlRow
    {
        juce::Label number, inactive;
        juce::ComboBox controller, target;
        juce::TextButton learnButton { "LEARN" }, jumpButton { "JUMP" }, pickupButton { "PICK-UP" };
        juce::Rectangle<float> arrowArea;   // Pfeil zwischen Verhalten und Ziel (gezeichnet)
    };
    std::array<ControlRow, 4> controlRows;
    juce::Label controlTitle, controlHint;
    int learningControl = -1;
    void storeControl (int row);
    void updateControlRows();

    void timerCallback() override;
    void updateLibraryLabel();
    void storeSwitchValue (SwitchRow& row, int value);
    void loadSwitchValue (SwitchRow& row);

    MaermelAudioProcessor& audioProcessor;
    juce::PropertiesFile& settings;

    juce::Label libraryTitle, libraryPath;
    juce::TextButton libraryButton, closeButton { "Done" };
    juce::ToggleButton stepWithinFolder;
    juce::ToggleButton copyExsSamples;     // beim Speichern eines EXS-Instruments Samples kopieren

    // Externer Editor (Pfad global gespeichert) und Live-Edit (nur diese Instanz, nicht gespeichert)
    juce::Label editorTitle, editorPath;
    juce::TextButton editorChoose { "Choose ..." }, editorClear { "Remove" };
    juce::ToggleButton liveEdit;
    void updateEditorControls();

    // Logfile: Pfad zu einer Textdatei (global gespeichert) und Schalter "Fehler ins Logfile schreiben"
    juce::Label logTitle, logPath;
    juce::TextButton logChoose { "Set ..." }, logClear { "Remove" };
    juce::ToggleButton logErrors;
    void updateLogControls();
    
    // Velocity-Kurve für das eigene Masterkeyboard (global) mit kleiner Kurvenanzeige
    struct CurveView : public juce::Component
    {
        float curve = 0.0f;
        void paint (juce::Graphics& g) override;
    };
    juce::Label velocityTitle, velocityHint;
    juce::Slider velocityCurve;
    juce::ToggleButton aftertouchAsWheel;
    CurveView curveView;
    juce::Label midiTitle, midiHint;
    juce::Label guiTitle;
    std::array<juce::TextButton, 4> guiSizeButtons;   // 100 / 125 / 150 / 200 %: ein Klick, kein Aufklappmenü
    juce::TextButton greyButton { "GREY" }, blueButton { "BLUE" };
    void showBackground (bool blue);
    juce::ToggleButton showExs;
    std::array<SwitchRow, 4> rows;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int learningRow = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsComponent)
};
