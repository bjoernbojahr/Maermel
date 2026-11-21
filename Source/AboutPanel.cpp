// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// AboutPanel.cpp – Märmel
//
// Hinweis zum Build-Zeitpunkt: __DATE__ und __TIME__ gelten für den Zeitpunkt, an dem DIESE Datei übersetzt
// wurde. Xcode übersetzt nur geänderte Dateien neu. Damit die Angabe immer stimmt, im Projucer beim
// Xcode-Exporter als "Pre-Build Shell Script" eintragen:   touch "$PROJECT_DIR/../../Source/AboutPanel.cpp"

#include "AboutPanel.h"
#include "ExternalLaunch.h"
#include "MaermelLook.h"
#include "LicenseTexts.h"

using namespace maermel::ui;

juce::String AboutPanel::buildInfo (juce::AudioProcessor::WrapperType wrapperType)
{
    // __DATE__ = "Oct  2 2026", __TIME__ = "19:00:12"
    auto parts = juce::StringArray::fromTokens (__DATE__, " ", "");
    parts.removeEmptyStrings();
    const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int month = 1;
    for (int i = 0; i < 12; ++i)
        if (parts[0] == months[i])
            month = i + 1;
    const juce::String date = parts[2] + "/" + juce::String (month).paddedLeft ('0', 2) + "/" + parts[1].paddedLeft ('0', 2);
    const juce::String time = juce::String (__TIME__).substring (0, 5);

   #if JUCE_MAC
    const juce::String platform = "macOS";
   #elif JUCE_WINDOWS
    const juce::String platform = "Windows";
   #else
    const juce::String platform = "Linux";
   #endif
   #if JUCE_ARM
    const juce::String cpu = "ARM";
   #elif JUCE_INTEL
    const juce::String cpu = "Intel";
   #else
    const juce::String cpu;
   #endif
    const juce::String format = juce::AudioProcessor::getWrapperTypeDescription (wrapperType);

    return "Built: " + date + " " + time + " (" + (platform + " " + cpu).trim() + " / " + format + ")";
}

AboutPanel::AboutPanel (juce::AudioProcessor::WrapperType wrapperType)
{
    info = buildInfo (wrapperType);

    link.setFont (labelFont (12.0f), false, juce::Justification::centred);
    link.setColour (juce::HyperlinkButton::textColourId, colours::cyan);
   #if JUCE_LINUX || JUCE_BSD
    // Linux: nicht JUCEs eigenen Start benutzen (fork, Bibliothekspfade des Hosts) – siehe ExternalLaunch.h.
    // Ohne gültige URL tut der HyperlinkButton beim Klick selbst nichts; onClick öffnet die Seite.
    link.setURL (juce::URL());
    link.onClick = [] { maermel::external::openUrl ("https://www.bjoernbojahr.de"); };
   #endif
    addAndMakeVisible (link);

    // Lizenzhinweise: unten, scrollbar, nur lesbar
    licenseText.setMultiLine (true, true);
    licenseText.setReadOnly (true);
    licenseText.setScrollbarsShown (true);
    licenseText.setCaretVisible (false);
    licenseText.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), 11.5f, juce::Font::plain)));
    licenseText.setColour (juce::TextEditor::backgroundColourId, juce::Colour (5, 7, 9));
    licenseText.setColour (juce::TextEditor::textColourId, colours::label.withAlpha (0.85f));
    licenseText.setColour (juce::TextEditor::outlineColourId, colours::frame.withAlpha (0.6f));
    juce::String all;
    for (const char* text : maermel::licenses::kAll)
        all << (all.isEmpty() ? "" : "\n\n----------------------------------------\n\n") << juce::String::fromUTF8 (text);
    licenseText.setText (all, false);
    licenseText.moveCaretToTop (false);
    addAndMakeVisible (licenseText);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
}

void AboutPanel::paint (juce::Graphics& g)
{
    // Abdunkeln der Oberfläche dahinter, dann die Tafel
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto panel = getLocalBounds().toFloat().withSizeKeepingCentre (500.0f, 430.0f);
    g.setColour (juce::Colour (30, 33, 37));
    g.fillRoundedRectangle (panel, 6.0f);
    g.setColour (colours::frame);
    g.drawRoundedRectangle (panel, 6.0f, 1.0f);

    const float cx = panel.getCentreX();
    float y = panel.getY() + 18.0f;
    drawLabel (g, juce::String::fromUTF8 ("Märmel"), cx, y, 40.0f, juce::Justification::centred, colours::red, true);
    y += 54.0f;
    drawLabel (g, "SFZ SAMPLE PLAYER", cx, y, 12.0f, juce::Justification::centred, colours::label);
    y += 18.0f;
    drawLabel (g, "Version " + juce::String (JucePlugin_VersionString), cx, y, 11.0f, juce::Justification::centred, colours::sub);
    y += 16.0f;
    drawLabel (g, info, cx, y, 11.0f, juce::Justification::centred, colours::sub);
    y += 28.0f;
    drawLabel (g, juce::String::fromUTF8 ("Björn Bojahr"), cx, y, 14.0f, juce::Justification::centred, colours::label);
    // darunter der Link (Komponente), dann die Mitwirkung beim Programmieren
    y += 42.0f;
    drawLabel (g, "Programmed with Claude (Anthropic)", cx, y, 11.0f, juce::Justification::centred, colours::sub);
}

void AboutPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto panel = getLocalBounds().toFloat().withSizeKeepingCentre (500.0f, 430.0f);
    if (! panel.contains (e.position) && onClose)
        onClose();
}

void AboutPanel::resized()
{
    const auto panel = getLocalBounds().withSizeKeepingCentre (500, 430);
    link.setBounds (panel.getCentreX() - 90, panel.getY() + 154, 180, 20);
    auto bottom = panel.reduced (18).removeFromBottom (190);
    closeButton.setBounds (bottom.removeFromBottom (26).withSizeKeepingCentre (80, 26));
    bottom.removeFromBottom (8);
    licenseText.setBounds (bottom);
}
