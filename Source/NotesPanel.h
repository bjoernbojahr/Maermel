// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// NotesPanel.h – Märmel
// Tafel „Loading notes“: alle Hinweise zum geladenen Instrument (fehlende Samples, korrigierte Werte,
// unbekannte Opcodes …) mit Bildlauf. Liegt über dem Fenster wie die About-Tafel, ist aber breiter und
// dunkelt weniger ab; ringsum bleibt die Oberfläche sichtbar. Klick daneben oder „Close“ schließt.

#pragma once

#include <JuceHeader.h>

class NotesPanel : public juce::Component
{
public:
    NotesPanel (const juce::String& instrumentName, const juce::String& notes);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // Klick außerhalb der Tafel schließt

    std::function<void()> onClose;

    static constexpr float kWidth = 840.0f, kHeight = 400.0f;

private:
    juce::Rectangle<float> panelBounds() const { return getLocalBounds().toFloat().withSizeKeepingCentre (kWidth, kHeight); }

    juce::String title;
    juce::TextEditor text;
    juce::TextButton closeButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NotesPanel)
};
