// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// MaermelControls.h – Märmel
// Eigene Bedienelemente des finalen Designs:
//   InstrumentDisplay  – Glas-Display mit Instrumentname bzw. Werteänderung, Pfeile zum Blättern
//   ValueBox           – kleines Wertefeld (Punktmatrix) zu einem Regler; Doppelklick = Wert eintippen
//   LedChoice          – Auswahlliste mit roten LEDs (Filtertyp)
//   ChoiceButtons      – Tastengruppe, genau eine leuchtet (Stimmen, Wellenform)
//   VelMarker          – hellblauer Pfeil rechts am A-/D-Fader (VelA/VelD)

#pragma once

#include <JuceHeader.h>
#include "MaermelLook.h"

#include <functional>
#include <memory>
#include <vector>

namespace maermel::ui
{

// ---------------------------------------------------------------------------
class InstrumentDisplay : public juce::Component   // bewusst ohne Tooltips (siehe „look“ in PluginEditor.h)
{
public:
    // arrows = false: Pfeile ausblenden (während einer roten Warnung)
    void setText (const juce::String& newText, juce::Colour colour = colours::cyan, bool arrows = true);
    // Passt der Text in voller Länge ins Display? (Gleiche Breite und Schrift wie beim Zeichnen; seit 1.0.3)
    bool fits (const juce::String& candidate) const;

    std::function<void()> onPrev, onNext, onCentre;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    static constexpr float kArrowZone = 22.0f;
    juce::String text;
    juce::Colour textColour = colours::cyan;
    bool showArrows = true;
    int maxChars = 0;                        // Platz in Zeichen beim letzten Zeichnen (0 = noch nicht gezeichnet)
};

// ---------------------------------------------------------------------------
class ValueBox : public juce::Component, private juce::Slider::Listener
{
public:
    // format: wie der Wert im Feld erscheint (kurz, ohne Einheit); leer = Text des Reglers
    ValueBox (juce::Slider& slider, std::function<juce::String (juce::Slider&)> format = {});
    ~ValueBox() override;

    void paint (juce::Graphics&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Neu zeichnen nur, wenn sich der angezeigte Text geändert hat (für den regelmäßigen Timer des Editors).
    // Der Textvergleich ist billig, das Neuzeichnen (Glas, Punktmatrix) teuer.
    void refresh();

private:
    void sliderValueChanged (juce::Slider*) override { repaint(); }
    void finishEditing (bool apply);
    juce::String currentText();

    juce::Slider& slider;
    std::function<juce::String (juce::Slider&)> format;
    std::unique_ptr<juce::TextEditor> editor;
    juce::String lastText;                   // zuletzt gezeichneter Text (für refresh)
};

// ---------------------------------------------------------------------------
class LedChoice : public juce::Component
{
public:
    struct Item { juce::String label; juce::Point<float> position; };   // Position der LED (Mitte), relativ

    LedChoice (juce::RangedAudioParameter& parameter, std::vector<Item> items);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (int index, const juce::String& label)> onChoice;

private:
    std::vector<Item> items;
    int selected = 0;
    juce::ParameterAttachment attachment;
};

// ---------------------------------------------------------------------------
class ChoiceButtons
{
public:
    // buttons müssen länger leben als dieses Objekt; Index im Parameter = Index in buttons
    ChoiceButtons (juce::RangedAudioParameter& parameter, std::vector<juce::TextButton*> buttons);
    std::function<void (int index)> onChoice;

private:
    std::vector<juce::TextButton*> buttons;
    juce::ParameterAttachment attachment;
};

// ---------------------------------------------------------------------------
class VelMarker : public juce::Component, private juce::Slider::Listener   // bewusst ohne Tooltips
{
public:
    // mainFader: A- bzw. D-Fader (gleiche Höhe wie diese Komponente); fraction: VelA/VelD (0 … 1, 1 = keine Wirkung)
    VelMarker (juce::Slider& mainFader, juce::RangedAudioParameter& fraction);
    ~VelMarker() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void (float fraction)> onChange;

private:
    void sliderValueChanged (juce::Slider*) override { repaint(); }   // wandert mit dem Hauptgriff
    float markerY() const;
    float mainNorm() const;
    float yForProportion (float proportion) const;   // Lage auf dem Fader, vom Fader selbst berechnet
    void setFromY (float y);

    juce::Slider& main;
    float fraction = 1.0f;
    bool dragging = false;                   // linke Maustaste gedrückt (Change-Gesture läuft)
    juce::ParameterAttachment attachment;
};

} // namespace maermel::ui
