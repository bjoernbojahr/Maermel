// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// NotesPanel.cpp – Märmel

#include "NotesPanel.h"
#include "MaermelLook.h"

using namespace maermel::ui;

NotesPanel::NotesPanel (const juce::String& instrumentName, const juce::String& notes)
    : title (instrumentName.isNotEmpty() ? "LOADING NOTES: " + instrumentName.toUpperCase() : juce::String ("LOADING NOTES"))
{
    text.setMultiLine (true, true);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setCaretVisible (false);
    text.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), 12.0f, juce::Font::plain)));
    text.setColour (juce::TextEditor::backgroundColourId, juce::Colour (5, 7, 9));
    text.setColour (juce::TextEditor::textColourId, colours::label.withAlpha (0.9f));
    text.setColour (juce::TextEditor::outlineColourId, colours::frame.withAlpha (0.6f));
    text.setText (notes.isNotEmpty() ? notes : juce::String ("No notes for this instrument."), false);
    text.moveCaretToTop (false);
    addAndMakeVisible (text);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
}

void NotesPanel::paint (juce::Graphics& g)
{
    // Leicht abdunkeln (die Oberfläche bleibt erkennbar), dann die Tafel
    g.fillAll (juce::Colours::black.withAlpha (0.35f));
    const auto panel = panelBounds();
    g.setColour (juce::Colour (30, 33, 37).withAlpha (0.96f));
    g.fillRoundedRectangle (panel, 6.0f);
    g.setColour (colours::frame);
    g.drawRoundedRectangle (panel, 6.0f, 1.0f);
    drawLabel (g, title, panel.getX() + 18.0f, panel.getY() + 14.0f, 13.0f, juce::Justification::left, colours::label, true);
}

void NotesPanel::resized()
{
    auto area = panelBounds().toNearestInt().reduced (18);
    area.removeFromTop (30);                                       // Titel
    closeButton.setBounds (area.removeFromBottom (26).withSizeKeepingCentre (80, 26));
    area.removeFromBottom (8);
    text.setBounds (area);
}

void NotesPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! panelBounds().contains (e.position) && onClose)
        onClose();
}
