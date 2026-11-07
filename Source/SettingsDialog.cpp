// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SettingsDialog.cpp – Märmel

#include "SettingsDialog.h"
#include "ParameterControl.h"

#include <cmath>
#include <iterator>
#include "ErrorLog.h"
#include "VelocityTools.h"

namespace
{
juce::String noteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
}

juce::String controllerName (int cc)
{
    switch (cc)
    {
        case 1:  return "Modulation";
        case 7:  return "Volume";
        case 10: return "Pan";
        case 11: return "Expression";
        case 64: return "Sustain";
        case 65: return "Portamento";
        case 66: return "Sostenuto";
        case 67: return "Soft";
        default: return {};
    }
}

// ComboBox-IDs: 1 = aus, Wert v = ID v + 2
constexpr int kOffId = 1;
int idForValue (int v)  { return v < 0 ? kOffId : v + 2; }
int valueForId (int id) { return id <= kOffId ? -1 : id - 2; }

juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }
} // namespace

SettingsComponent::SettingsComponent (MaermelAudioProcessor& p)
    : audioProcessor (p), settings (p.library->getSettings())
{
    // --- Library ---
    libraryTitle.setText ("Library", juce::dontSendNotification);
    libraryTitle.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (libraryTitle);
    libraryPath.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (libraryPath);
    updateLibraryLabel();

    libraryButton.setButtonText ("Change ...");
    libraryButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Choose library folder",
                                                           audioProcessor.library->getRootFolder());
        juce::Component::SafePointer<SettingsComponent> safe (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [safe] (const juce::FileChooser& chooser)
        {
            const juce::File folder = chooser.getResult();
            if (safe == nullptr || ! folder.isDirectory())
                return;
            safe->audioProcessor.library->setRootFolder (folder);
            safe->updateLibraryLabel();
        });
    };
    addAndMakeVisible (libraryButton);

    stepWithinFolder.setButtonText ("Arrow keys and MIDI controllers change instruments only within the folder of the current instrument");
    stepWithinFolder.setToggleState (settings.getBoolValue ("stepWithinFolder", false), juce::dontSendNotification);
    stepWithinFolder.onClick = [this]
    {
        settings.setValue ("stepWithinFolder", stepWithinFolder.getToggleState());
        settings.saveIfNeeded();
    };
    addAndMakeVisible (stepWithinFolder);

    // EXS-Instrumente anzeigen und importieren (nur Sample-Zuordnung, keine Instrument-Parameter); Standard: aus
    showExs.setButtonText ("EXS import: show EXS instruments and try to import the sample mapping (no instrument parameters)");
    showExs.setToggleState (settings.getBoolValue (LibraryIndex::kShowExsKey, false), juce::dontSendNotification);
    showExs.onClick = [this]
    {
        settings.setValue (LibraryIndex::kShowExsKey, showExs.getToggleState());
        settings.saveIfNeeded();
        copyExsSamples.setEnabled (showExs.getToggleState());
    };
    addAndMakeVisible (showExs);

    copyExsSamples.setButtonText ("EXS import: when saving, copy the samples to \"<Instrument> Samples\" next to the SFZ");
    copyExsSamples.setToggleState (settings.getBoolValue ("exsCopySamples", true), juce::dontSendNotification);
    copyExsSamples.setTooltip ("On: the library stays self-contained (samples are stored twice on disk). "
                               "Off: the SFZ refers to the samples in their original location.");
    copyExsSamples.onClick = [this]
    {
        settings.setValue ("exsCopySamples", copyExsSamples.getToggleState());
        settings.saveIfNeeded();
    };
    addAndMakeVisible (copyExsSamples);
    copyExsSamples.setEnabled (showExs.getToggleState());   // nur sinnvoll, wenn EXS-Instrumente angezeigt werden

    // --- Externer Editor und Live-Edit ---
    editorTitle.setText ("External editor (edit SFZ)", juce::dontSendNotification);
    editorTitle.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (editorTitle);
    editorPath.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (editorPath);

    editorChoose.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Choose an application for editing SFZ files",
                                                           juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory));
        juce::Component::SafePointer<SettingsComponent> safe (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe] (const juce::FileChooser& chooser)
                                  {
                                      if (safe == nullptr || chooser.getResult() == juce::File())
                                          return;
                                      safe->settings.setValue (MaermelAudioProcessor::kEditorPathKey,
                                                               chooser.getResult().getFullPathName());
                                      safe->settings.saveIfNeeded();
                                      safe->updateEditorControls();
                                  });
    };
    addAndMakeVisible (editorChoose);

    editorClear.onClick = [this]
    {
        settings.removeValue (MaermelAudioProcessor::kEditorPathKey);
        settings.saveIfNeeded();
        audioProcessor.setLiveEditActive (false);
        updateEditorControls();
    };
    addAndMakeVisible (editorClear);

    // Live-Edit erlauben (global, gespeichert). Die Überwachung selbst startet erst mit der Taste EDIT
    // (rote LED darüber) und endet beim Instrumentwechsel.
    liveEdit.setButtonText ("Live edit: watch the loaded SFZ file and reload it automatically after saving in the SFZ editor");
    liveEdit.setTooltip ("Starts with the EDIT button (red LED above it) and stops when you switch instruments. "
                         "When reloading, all settings are kept and the preset is not reloaded.");
    liveEdit.onClick = [this]
    {
        settings.setValue (MaermelAudioProcessor::kLiveEditAllowedKey, liveEdit.getToggleState());
        settings.saveIfNeeded();
        if (! liveEdit.getToggleState())
            audioProcessor.setLiveEditActive (false);   // ausgeschaltet: laufende Überwachung beenden
    };
    addAndMakeVisible (liveEdit);
    updateEditorControls();

    // --- Logfile ---
    logTitle.setText ("Logfile", juce::dontSendNotification);
    logTitle.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (logTitle);
    logPath.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (logPath);

    logChoose.onClick = [this]
    {
        const juce::File current = ErrorLog::getFile (settings);
        const juce::File start = current != juce::File()
                                   ? current
                                   : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Maermel-Log.txt");
        fileChooser = std::make_unique<juce::FileChooser> ("Choose or create a text file for the log", start, "*.txt;*.log");
        juce::Component::SafePointer<SettingsComponent> safe (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe] (const juce::FileChooser& chooser)
                                  {
                                      if (safe == nullptr || chooser.getResult() == juce::File())
                                          return;
                                      const juce::File file = chooser.getResult();
                                      if (! file.existsAsFile())
                                          file.create();        // leere Datei anlegen; Meldungen werden später angehängt
                                      safe->settings.setValue (ErrorLog::kPathKey, file.getFullPathName());
                                      safe->settings.saveIfNeeded();
                                      safe->updateLogControls();
                                  });
    };
    addAndMakeVisible (logChoose);

    logClear.onClick = [this]
    {
        settings.removeValue (ErrorLog::kPathKey);
        settings.saveIfNeeded();
        audioProcessor.setLogErrorsActive (false);
        updateLogControls();
    };
    addAndMakeVisible (logClear);

    logErrors.setButtonText ("Write errors to the log file (e.g. missing samples, loading errors; until quit)");
    logErrors.setTooltip ("Only available with a valid, writable text file. Messages are appended, "
                          "the file is never overwritten.");
    logErrors.onClick = [this]
    {
        audioProcessor.setLogErrorsActive (logErrors.getToggleState());   // nur diese Instanz, nicht gespeichert
    };
    addAndMakeVisible (logErrors);

    // Fenstergröße ganz oben neben "Done" (global gespeichert; dieselbe Auswahl im Rechtsklick-Menü)
    guiTitle.setText ("GUI size", juce::dontSendNotification);
    guiTitle.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (guiTitle);
    for (size_t i = 0; i < guiSizeButtons.size(); ++i)
    {
        auto& b = guiSizeButtons[i];
        b.setButtonText (juce::String (juce::roundToInt (kGuiScales[i] * 100.0f)) + " %");
        b.onClick = [this, i]
        {
            const float scale = kGuiScales[i];
            showGuiScale (scale);
            settings.setValue (kGuiScaleKey, (double) scale);
            settings.saveIfNeeded();
            if (onGuiScaleChanged) onGuiScaleChanged (scale);
        };
        addAndMakeVisible (b);
    }
    showGuiScale ((float) settings.getDoubleValue (kGuiScaleKey, 1.0));

    // Hintergrund (mit Abstand hinter den Größen-Tasten)
    auto chooseBackground = [this] (bool blue)
    {
        showBackground (blue);
        settings.setValue (kBlueBackgroundKey, blue);
        settings.saveIfNeeded();
        if (onBackgroundChanged) onBackgroundChanged (blue);
    };
    greyButton.onClick = [chooseBackground] { chooseBackground (false); };
    blueButton.onClick = [chooseBackground] { chooseBackground (true); };
    addAndMakeVisible (greyButton);
    addAndMakeVisible (blueButton);
    showBackground (settings.getBoolValue (kBlueBackgroundKey, true));   // Standard: Blau
    updateLogControls();

    // --- Velocity-Kurve (global, für das eigene Masterkeyboard) ---
    velocityTitle.setText ("Velocity curve (master keyboard)", juce::dontSendNotification);
    velocityTitle.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (velocityTitle);
    velocityHint.setText ("Applies to all instruments. Negative: more sensitive when playing softly, positive: needs a harder touch. "
                          "The per-instrument velocity offset is on the main panel.",
                          juce::dontSendNotification);
    velocityHint.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    velocityHint.setFont (juce::Font (juce::FontOptions (12.0f)));
    velocityHint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (velocityHint);

    velocityCurve.setSliderStyle (juce::Slider::LinearHorizontal);
    velocityCurve.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    velocityCurve.setRange (-100.0, 100.0, 1.0);
    velocityCurve.setDoubleClickReturnValue (true, 0.0);
    velocityCurve.setValue (settings.getDoubleValue ("velocityCurve", 0.0) * 100.0, juce::dontSendNotification);
    curveView.curve = (float) velocityCurve.getValue() / 100.0f;
    velocityCurve.onValueChange = [this]
    {
        const double curve = velocityCurve.getValue() / 100.0;
        settings.setValue ("velocityCurve", curve);
        settings.saveIfNeeded();
        curveView.curve = (float) curve;
        curveView.repaint();
    };
    addAndMakeVisible (velocityCurve);

    // Aftertouch wie Modulationsrad (global, je nach Keyboard; Standard: an)
    aftertouchAsWheel.setButtonText ("Aftertouch acts like the modulation wheel (turn off for keyboards that send aftertouch too easily)");
    aftertouchAsWheel.setToggleState (settings.getBoolValue ("aftertouchAsWheel", true), juce::dontSendNotification);
    aftertouchAsWheel.onClick = [this]
    {
        settings.setValue ("aftertouchAsWheel", aftertouchAsWheel.getToggleState());
        settings.saveIfNeeded();   // der Prozessor liest die Einstellung regelmäßig neu ein (wie die Velocity-Kurve)
    };
    addAndMakeVisible (aftertouchAsWheel);
    addAndMakeVisible (curveView);

    // --- MIDI-Umschaltung ---
    midiTitle.setText ("Switch instruments via MIDI", juce::dontSendNotification);
    midiTitle.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (midiTitle);

    midiHint.setText ("These notes and controllers no longer reach the instrument. "
                      "Controllers switch like a pedal: once at value 64 or above, then again only after release.",
                      juce::dontSendNotification);
    midiHint.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    midiHint.setFont (juce::Font (juce::FontOptions (12.0f)));
    midiHint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (midiHint);

    const Target targets[] = { Target::NextNote, Target::PrevNote, Target::NextController, Target::PrevController };
    const juce::String rowTitles[] = { "Note: next", "Note: previous", "Controller: next", "Controller: previous" };

    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto& row = rows[i];
        row.target = targets[i];
        row.isNote = (i < 2);

        row.label.setText (rowTitles[i], juce::dontSendNotification);
        addAndMakeVisible (row.label);

        row.value.addItem ("off", kOffId);
        for (int v = 0; v < 128; ++v)
        {
            juce::String text (v);
            const juce::String extra = row.isNote ? noteName (v) : controllerName (v);
            if (extra.isNotEmpty())
                text << "  (" << extra << ")";
            row.value.addItem (text, idForValue (v));
        }
        loadSwitchValue (row);
        row.value.onChange = [this, i] { storeSwitchValue (rows[i], valueForId (rows[i].value.getSelectedId())); };
        addAndMakeVisible (row.value);

        row.learnButton.setClickingTogglesState (false);
        row.learnButton.onClick = [this, i]
        {
            if (learningRow == (int) i)
            {
                audioProcessor.stopLearning();   // nochmal gedrückt: abbrechen
                learningRow = -1;
            }
            else
            {
                learningControl = -1;
                audioProcessor.startLearning (rows[i].target);
                learningRow = (int) i;
            }
        };
        addAndMakeVisible (row.learnButton);
    }

    // --- Direkte Steuerung von Parametern per MIDI-Controller ---
    controlTitle.setText ("Direct parameter control (MIDI)", juce::dontSendNotification);
    controlTitle.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (controlTitle);
    controlHint.setText ("A controller moves a knob over its full range, like turning it with the mouse. "
                         "One controller may move several knobs. Red: the target is already controlled above "
                         "or the controller switches instruments - this row is inactive.",
                         juce::dontSendNotification);
    controlHint.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    controlHint.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (controlHint);

    namespace pc = maermel::control;
    for (int i = 0; i < pc::kSlots; ++i)
    {
        auto& row = controlRows[(size_t) i];
        row.number.setText (juce::String (i + 1), juce::dontSendNotification);
        row.number.setColour (juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible (row.number);

        row.controller.addItem ("off", kOffId);
        for (int v = 0; v < 120; ++v)
        {
            juce::String text ("CC " + juce::String (v));
            const juce::String extra = controllerName (v);
            if (extra.isNotEmpty())
                text << "  (" << extra << ")";
            row.controller.addItem (text, idForValue (v));
            if (! pc::isAssignable (v))
                row.controller.setItemEnabled (idForValue (v), false);   // Modulationsrad, Sustain
        }
        row.controller.setSelectedId (idForValue (settings.getIntValue (juce::String (pc::keyCc (i)), -1)),
                                      juce::dontSendNotification);
        row.controller.onChange = [this, i] { storeControl (i); };
        addAndMakeVisible (row.controller);

        row.learnButton.onClick = [this, i]
        {
            if (learningControl == i)
            {
                audioProcessor.stopLearning();   // nochmal gedrückt: abbrechen
                learningControl = -1;
            }
            else
            {
                learningRow = -1;
                audioProcessor.startLearning ((Target) ((int) Target::ParamSlot1 + i));
                learningControl = i;
            }
        };
        addAndMakeVisible (row.learnButton);

        const bool jump = settings.getBoolValue (juce::String (pc::keyJump (i)), true);
        row.jumpButton.setToggleState (jump, juce::dontSendNotification);
        row.pickupButton.setToggleState (! jump, juce::dontSendNotification);
        row.jumpButton.onClick   = [this, i] { controlRows[(size_t) i].jumpButton.setToggleState (true, juce::dontSendNotification);
                                               controlRows[(size_t) i].pickupButton.setToggleState (false, juce::dontSendNotification);
                                               storeControl (i); };
        row.pickupButton.onClick = [this, i] { controlRows[(size_t) i].jumpButton.setToggleState (false, juce::dontSendNotification);
                                               controlRows[(size_t) i].pickupButton.setToggleState (true, juce::dontSendNotification);
                                               storeControl (i); };
        addAndMakeVisible (row.jumpButton);
        addAndMakeVisible (row.pickupButton);

        // Ziele nach Abschnitten (Reihenfolge wie im Konzeptpapier); Menü-IDs = Index + 1
        juce::String section;
        for (int t = 0; t < pc::kNumTargets; ++t)
        {
            if (t > 0 && section != pc::kTargets[t].section)
            {
                section = pc::kTargets[t].section;
                row.target.addSectionHeading (section);
            }
            row.target.addItem (pc::kTargets[t].name, t + 1);
        }
        row.target.setSelectedId (pc::targetIndex (settings.getValue (juce::String (pc::keyTarget (i)), "none").toStdString()) + 1,
                                  juce::dontSendNotification);
        row.target.onChange = [this, i] { storeControl (i); };
        addAndMakeVisible (row.target);

        row.inactive.setText ("inactive", juce::dontSendNotification);
        row.inactive.setColour (juce::Label::textColourId, juce::Colour (0xffe8463c));
        row.inactive.setFont (juce::Font (juce::FontOptions (11.0f)));
        addChildComponent (row.inactive);
    }
    updateControlRows();

    closeButton.onClick = [this]
    {
        audioProcessor.stopLearning();
        if (onClose)
            onClose();
    };
    addAndMakeVisible (closeButton);

    startTimerHz (15);
}

SettingsComponent::~SettingsComponent()
{
    stopTimer();
    audioProcessor.stopLearning();
}

void SettingsComponent::updateLibraryLabel()
{
    const juce::File root = audioProcessor.library->getRootFolder();
    libraryPath.setText (root.isDirectory() ? root.getFullPathName() : juce::String ("(not set yet)"),
                         juce::dontSendNotification);
}

void SettingsComponent::loadSwitchValue (SwitchRow& row)
{
    const int v = settings.getIntValue (MaermelAudioProcessor::settingKey (row.target), -1);
    row.value.setSelectedId (idForValue (v), juce::dontSendNotification);
}

void SettingsComponent::storeSwitchValue (SwitchRow& row, int value)
{
    settings.setValue (MaermelAudioProcessor::settingKey (row.target), value);
    settings.saveIfNeeded();
    row.value.setSelectedId (idForValue (value), juce::dontSendNotification);
    updateControlRows();   // Controller der Instrumentumschaltung sperren die direkte Steuerung
}

void SettingsComponent::storeControl (int i)
{
    namespace pc = maermel::control;
    auto& row = controlRows[(size_t) i];
    const int targetIndex = juce::jlimit (0, pc::kNumTargets - 1, row.target.getSelectedId() - 1);
    settings.setValue (juce::String (pc::keyCc (i)), valueForId (row.controller.getSelectedId()));
    settings.setValue (juce::String (pc::keyTarget (i)), juce::String (pc::kTargets[targetIndex].key));
    settings.setValue (juce::String (pc::keyJump (i)), row.jumpButton.getToggleState());
    settings.saveIfNeeded();   // der Prozessor liest die Zuordnung regelmäßig neu ein
    updateControlRows();
}

void SettingsComponent::updateControlRows()
{
    // Gleiche Regel wie im Prozessor (ParameterControl.h): Konflikte rot und unwirksam,
    // bereits gesteuerte Ziele im Menü der anderen Zeilen ausgegraut
    namespace pc = maermel::control;
    std::array<int, pc::kSlots> cc {}, target {};
    for (int i = 0; i < pc::kSlots; ++i)
    {
        cc[(size_t) i] = valueForId (controlRows[(size_t) i].controller.getSelectedId());
        target[(size_t) i] = juce::jlimit (0, pc::kNumTargets - 1, controlRows[(size_t) i].target.getSelectedId() - 1);
    }
    std::array<bool, pc::kSlots> active {}, conflict {};
    pc::evaluate (cc, target, settings.getIntValue (MaermelAudioProcessor::settingKey (Target::NextController), -1),
                  settings.getIntValue (MaermelAudioProcessor::settingKey (Target::PrevController), -1), active, conflict);

    const juce::Colour red (0xffe8463c);
    for (int i = 0; i < pc::kSlots; ++i)
    {
        auto& row = controlRows[(size_t) i];
        const juce::Colour colour = conflict[(size_t) i] ? red : juce::Colours::white;
        row.controller.setColour (juce::ComboBox::textColourId, colour);
        row.target.setColour (juce::ComboBox::textColourId, colour);
        row.inactive.setVisible (conflict[(size_t) i]);
        for (int t = 1; t < pc::kNumTargets; ++t)
        {
            bool usedElsewhere = false;
            for (int j = 0; j < pc::kSlots; ++j)
                if (j != i && active[(size_t) j]
                    && std::string (pc::kTargets[target[(size_t) j]].paramId) == pc::kTargets[t].paramId)
                    usedElsewhere = true;
            row.target.setItemEnabled (t + 1, ! usedElsewhere || t == target[(size_t) i]);
        }
        row.controller.repaint();
        row.target.repaint();
    }
}

void SettingsComponent::timerCallback()
{
    // Lernen für die direkte Steuerung: übernommenen Controller in die Zeile eintragen
    if (learningControl >= 0 && audioProcessor.getLearnTarget() == Target::None)
    {
        const int v = audioProcessor.takeLearnedValue();
        if (v >= 0)
        {
            controlRows[(size_t) learningControl].controller.setSelectedId (idForValue (v), juce::dontSendNotification);
            storeControl (learningControl);
        }
        learningControl = -1;
    }
    for (int i = 0; i < (int) controlRows.size(); ++i)
    {
        const bool learning = (learningControl == i);
        controlRows[(size_t) i].learnButton.setButtonText (learning ? "Move a control ..." : "LEARN");
        controlRows[(size_t) i].learnButton.setColour (juce::TextButton::buttonColourId,
                                                       learning ? juce::Colours::darkorange : juce::Colour (0xff3a4148));
    }

    // Lernen: sobald der Audio-Thread einen Wert übernommen hat, speichern
    if (learningRow >= 0 && audioProcessor.getLearnTarget() == Target::None)
    {
        const int v = audioProcessor.takeLearnedValue();
        if (v >= 0)
            storeSwitchValue (rows[(size_t) learningRow], v);
        learningRow = -1;
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const bool learning = (learningRow == (int) i);
        rows[i].learnButton.setButtonText (learning ? juce::String (rows[i].isNote ? "Play a key ..." : "Press the pedal ...")
                                                    : juce::String ("Learn"));
        rows[i].learnButton.setColour (juce::TextButton::buttonColourId,
                                       learning ? juce::Colours::darkorange : juce::Colour (0xff3a4148));
    }
}

void SettingsComponent::updateEditorControls()
{
    const juce::File editor = audioProcessor.getEditorApplication();
    const bool valid = MaermelAudioProcessor::isValidEditor (editor);
    if (editor == juce::File())
        editorPath.setText ("No editor set", juce::dontSendNotification);
    else
        editorPath.setText (editor.getFullPathName() + (valid ? juce::String() : juce::String ("  (not found)")),
                            juce::dontSendNotification);
    editorClear.setEnabled (editor != juce::File());

    // Live-Edit erlauben nur mit gültigem Editor wählbar; die gespeicherte Einstellung bleibt dabei erhalten
    liveEdit.setEnabled (valid);
    liveEdit.setToggleState (settings.getBoolValue (MaermelAudioProcessor::kLiveEditAllowedKey, false),
                             juce::dontSendNotification);
}

void SettingsComponent::updateLogControls()
{
    const juce::File file = ErrorLog::getFile (settings);
    const bool writable = ErrorLog::isWritable (file);
    if (file == juce::File())
        logPath.setText ("No file set", juce::dontSendNotification);
    else
        logPath.setText (file.getFullPathName() + (writable ? juce::String() : juce::String ("  (not writable)")),
                         juce::dontSendNotification);
    logClear.setEnabled (file != juce::File());

    // "Fehler ins Logfile schreiben" nur mit gültiger, beschreibbarer Datei
    if (! writable && audioProcessor.isLogErrorsActive())
        audioProcessor.setLogErrorsActive (false);
    if (settings.containsKey (ErrorLog::kEnabledKey))   // früher dauerhaft gespeicherten Schalter entfernen
    {
        settings.removeValue (ErrorLog::kEnabledKey);
        settings.saveIfNeeded();
    }
    logErrors.setEnabled (writable);
    logErrors.setToggleState (writable && audioProcessor.isLogErrorsActive(), juce::dontSendNotification);
}

void SettingsComponent::CurveView::paint (juce::Graphics& g)
{
    // Eingehende Velocity (waagerecht) → wirksame Velocity (senkrecht)
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff101418));
    g.fillRect (r);
    g.setColour (juce::Colour (0xff3a4148));
    g.drawRect (r, 1.0f);
    g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f);   // linear zum Vergleich

    juce::Path p;
    for (int v = 1; v <= 127; ++v)
    {
        const float x = r.getX() + r.getWidth() * (float) (v - 1) / 126.0f;
        const float y = r.getBottom() - r.getHeight() * (float) (maermel::velocity::applyCurve (v, curve) - 1) / 126.0f;
        if (v == 1) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xff4fc3f7));
    g.strokePath (p, juce::PathStrokeType (2.0f));
}

void SettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xf0181c20));
    g.setColour (juce::Colour (0xff4fc3f7));
    g.drawRect (getLocalBounds(), 1);

    // Pfeile der direkten Steuerung (gezeichnet, da die Schrift das Zeichen nicht enthält)
    g.setColour (juce::Colours::grey);
    for (const auto& row : controlRows)
    {
        if (row.arrowArea.isEmpty())
            continue;
        const float y = row.arrowArea.getCentreY(), x0 = row.arrowArea.getX() + 9.0f, x1 = row.arrowArea.getRight() - 9.0f;
        g.drawLine (x0, y, x1 - 5.0f, y, 2.0f);
        juce::Path head;
        head.addTriangle (x1, y, x1 - 7.0f, y - 4.5f, x1 - 7.0f, y + 4.5f);
        g.fillPath (head);
    }
}

void SettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (14, 10);

    // Oberste Zeile: Fenstergröße links (vier Tasten, ohne Scrollen erreichbar), "Done" rechts für sich
    {
        auto line = area.removeFromTop (24);
        closeButton.setBounds (line.removeFromRight (90));
        guiTitle.setBounds (line.removeFromLeft (80));
        for (auto& b : guiSizeButtons)
        {
            b.setBounds (line.removeFromLeft (62));
            line.removeFromLeft (4);
        }
        line.removeFromLeft (24);   // Abstand zu den Größen-Tasten
        greyButton.setBounds (line.removeFromLeft (62));
        line.removeFromLeft (4);
        blueButton.setBounds (line.removeFromLeft (62));
    }
    area.removeFromTop (14);

    auto sectionGap = [&area] { area.removeFromTop (14); };

    // 1. Library (mit Blättern im Ordner und EXS-Import)
    libraryTitle.setBounds (area.removeFromTop (22));
    {
        auto line = area.removeFromTop (26);
        libraryButton.setBounds (line.removeFromRight (100));
        line.removeFromRight (8);
        libraryPath.setBounds (line);
    }
    area.removeFromTop (2);
    stepWithinFolder.setBounds (area.removeFromTop (24));
    showExs.setBounds (area.removeFromTop (24));
    copyExsSamples.setBounds (area.removeFromTop (24));
    sectionGap();

    // 2. Instrument wechseln per MIDI: zwei Spalten, Noten links, Controller rechts
    midiTitle.setBounds (area.removeFromTop (22));
    midiHint.setBounds (area.removeFromTop (30));
    area.removeFromTop (4);
    {
        auto block = area.removeFromTop (64);
        auto left = block.removeFromLeft (block.getWidth() / 2);
        auto right = block.withTrimmedLeft (10);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto& column = (i < 2) ? left : right;
            auto r = column.removeFromTop (28);
            rows[i].label.setBounds (r.removeFromLeft (130));
            rows[i].learnButton.setBounds (r.removeFromRight (120));
            r.removeFromRight (6);
            rows[i].value.setBounds (r);
            column.removeFromTop (4);
        }
    }
    sectionGap();

    // 3. Velocity-Kurve: Titel, Hinweis, Regler und Kurvenanzeige daneben
    {
        auto block = area.removeFromTop (90);
        curveView.setBounds (block.removeFromRight (90).reduced (4));
        block.removeFromRight (10);
        velocityTitle.setBounds (block.removeFromTop (22));
        velocityHint.setBounds (block.removeFromTop (34));
        velocityCurve.setBounds (block.removeFromTop (26).withWidth (360));
    }
    aftertouchAsWheel.setBounds (area.removeFromTop (24));
    sectionGap();

    // Direkte Steuerung von Parametern: Controller → Verhalten → Ziel
    controlTitle.setBounds (area.removeFromTop (22));
    controlHint.setBounds (area.removeFromTop (30));
    area.removeFromTop (4);
    for (auto& row : controlRows)
    {
        auto r = area.removeFromTop (28);
        row.number.setBounds (r.removeFromLeft (18));
        row.controller.setBounds (r.removeFromLeft (150).reduced (0, 1));
        r.removeFromLeft (6);
        row.learnButton.setBounds (r.removeFromLeft (124));
        r.removeFromLeft (18);
        row.jumpButton.setBounds (r.removeFromLeft (58));
        r.removeFromLeft (2);
        row.pickupButton.setBounds (r.removeFromLeft (76));
        row.arrowArea = r.removeFromLeft (40).toFloat();
        row.target.setBounds (r.removeFromLeft (230).reduced (0, 1));
        r.removeFromLeft (8);
        row.inactive.setBounds (r.removeFromLeft (70));
        area.removeFromTop (4);
    }
    sectionGap();

    // 4. Externer Editor und Live-Edit
    editorTitle.setBounds (area.removeFromTop (22));
    {
        auto row = area.removeFromTop (26);
        editorClear.setBounds (row.removeFromRight (90).reduced (2));
        editorChoose.setBounds (row.removeFromRight (100).reduced (2));
        editorPath.setBounds (row);
    }
    liveEdit.setBounds (area.removeFromTop (24));
    sectionGap();

    // 5. Logfile
    logTitle.setBounds (area.removeFromTop (22));
    {
        auto row = area.removeFromTop (26);
        logClear.setBounds (row.removeFromRight (90).reduced (2));
        logChoose.setBounds (row.removeFromRight (100).reduced (2));
        logPath.setBounds (row);
    }
    logErrors.setBounds (area.removeFromTop (24));
}

void SettingsComponent::showBackground (bool blue)
{
    greyButton.setToggleState (! blue, juce::dontSendNotification);
    blueButton.setToggleState (blue, juce::dontSendNotification);
}

void SettingsComponent::showGuiScale (float scale)
{
    for (size_t i = 0; i < guiSizeButtons.size(); ++i)
        guiSizeButtons[i].setToggleState (std::abs (kGuiScales[i] - scale) < 0.01f, juce::dontSendNotification);
}
