// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// AboutPanel.h – Märmel
// About-Box als Tafel über der Oberfläche (Klick auf den Schriftzug): roter Schriftzug, Version,
// Build-Zeitpunkt mit Plattform und Plug-in-Format, Autor mit Link, darunter ein Feld mit Lizenzhinweisen.

#pragma once

#include <JuceHeader.h>
#include <functional>

class AboutPanel : public juce::Component
{
public:
    explicit AboutPanel (juce::AudioProcessor::WrapperType wrapperType);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // Klick außerhalb der Tafel schließt

    std::function<void()> onClose;

    // "Built: 2026/10/02 19:00 (macOS ARM / AU)"
    static juce::String buildInfo (juce::AudioProcessor::WrapperType wrapperType);

private:
    juce::String info;
    juce::HyperlinkButton link { "www.bjoernbojahr.de", juce::URL ("https://www.bjoernbojahr.de") };
    juce::TextEditor licenseText;
    juce::TextButton closeButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutPanel)
};
