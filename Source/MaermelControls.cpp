// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// MaermelControls.cpp – Märmel

#include "MaermelControls.h"

namespace maermel::ui
{

// Höhe der sichtbaren Zeichen (Zeilen 0–6) für die senkrechte Mitte
static float glyphHeight (juce::Graphics& g) { return DotText::lineHeight (g) / 8.0f * 7.0f; }

// ===========================================================================
// InstrumentDisplay

void InstrumentDisplay::setText (const juce::String& newText, juce::Colour colour, bool arrows)
{
    if (newText != text || colour != textColour || arrows != showArrows)
    {
        text = newText;
        textColour = colour;
        showArrows = arrows;
        repaint();
    }
}

void InstrumentDisplay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    drawGlass (g, r, 4.0f);
    const float top = (r.getHeight() - glyphHeight (g)) * 0.5f;
    if (showArrows)
    {
        DotText::draw (g, juce::String::fromUTF8 ("◂"), 9.0f, top, juce::Justification::left);
        DotText::draw (g, juce::String::fromUTF8 ("▸"), r.getWidth() - 9.0f, top, juce::Justification::right);
    }
    truncated = DotText::draw (g, text, r.getCentreX(), top, juce::Justification::centred,
                               r.getWidth() - 2.0f * kArrowZone - 4.0f, textColour);
}

void InstrumentDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (e.position.x < kArrowZone)                         { if (onPrev) onPrev(); }
    else if (e.position.x > (float) getWidth() - kArrowZone) { if (onNext) onNext(); }
    else if (onCentre)                                      onCentre();
}

// ===========================================================================
// ValueBox

ValueBox::ValueBox (juce::Slider& s, std::function<juce::String (juce::Slider&)> f)
    : slider (s), format (std::move (f))
{
    slider.addListener (this);
    setMouseCursor (juce::MouseCursor::IBeamCursor);
}

ValueBox::~ValueBox()
{
    slider.removeListener (this);
}

void ValueBox::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    drawGlass (g, r, 2.0f);
    const juce::String text = format ? format (slider) : slider.getTextFromValue (slider.getValue());
    DotText::draw (g, text, r.getCentreX(), (r.getHeight() - glyphHeight (g)) * 0.5f, juce::Justification::centred,
                   r.getWidth() - 4.0f);
}

void ValueBox::resized() {}

void ValueBox::mouseDoubleClick (const juce::MouseEvent&)
{
    // Wert eintippen: kleines Eingabefeld über dem Wertefeld (im übergeordneten Fenster, damit es breiter sein darf)
    auto* parent = getParentComponent();
    if (parent == nullptr || editor != nullptr)
        return;

    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    editor->setJustification (juce::Justification::centred);
    editor->setText (slider.getTextFromValue (slider.getValue()), false);
    editor->selectAll();
    juce::Component::SafePointer<ValueBox> safe (this);
    editor->onReturnKey = [safe] { if (safe != nullptr) safe->finishEditing (true); };
    editor->onEscapeKey = [safe] { if (safe != nullptr) safe->finishEditing (false); };
    editor->onFocusLost = [safe] { if (safe != nullptr) safe->finishEditing (true); };
    parent->addAndMakeVisible (*editor);
    editor->setBounds (getBounds().withSizeKeepingCentre (juce::jmax (96, getWidth()), getHeight() + 4));
    editor->grabKeyboardFocus();
}

void ValueBox::finishEditing (bool apply)
{
    if (editor == nullptr)
        return;
    if (apply)
        slider.setValue (slider.getValueFromText (editor->getText()), juce::sendNotificationSync);

    // Nicht innerhalb des eigenen Tastatur-Ereignisses löschen, sondern kurz danach
    juce::Component::SafePointer<ValueBox> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe != nullptr && safe->editor != nullptr)
        {
            if (auto* p = safe->editor->getParentComponent())
                p->removeChildComponent (safe->editor.get());
            safe->editor.reset();
        }
    });
}

// ===========================================================================
// LedChoice

LedChoice::LedChoice (juce::RangedAudioParameter& parameter, std::vector<Item> list)
    : items (std::move (list)),
      attachment (parameter, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
{
    attachment.sendInitialUpdate();
}

void LedChoice::paint (juce::Graphics& g)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& it = items[i];
        drawLed (g, it.position, (int) i == selected);
        drawLabel (g, it.label, it.position.x + 10.0f, it.position.y - 6.5f, 11.0f, juce::Justification::left);
    }
}

void LedChoice::mouseDown (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& it = items[i];
        const float w = juce::GlyphArrangement::getStringWidth (labelFont (11.0f), it.label) + 18.0f;
        const juce::Rectangle<float> hit (it.position.x - 7.0f, it.position.y - 10.0f, w, 20.0f);
        if (hit.contains (e.position))
        {
            attachment.setValueAsCompleteGesture ((float) i);
            if (onChoice) onChoice ((int) i, it.label);
            return;
        }
    }
}

// ===========================================================================
// ChoiceButtons

ChoiceButtons::ChoiceButtons (juce::RangedAudioParameter& parameter, std::vector<juce::TextButton*> list)
    : buttons (std::move (list)),
      attachment (parameter, [this] (float v)
      {
          const int index = juce::roundToInt (v);
          for (size_t i = 0; i < buttons.size(); ++i)
              buttons[i]->setToggleState ((int) i == index, juce::dontSendNotification);
      })
{
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        buttons[i]->setClickingTogglesState (false);
        buttons[i]->onClick = [this, i]
        {
            attachment.setValueAsCompleteGesture ((float) i);
            if (onChoice) onChoice ((int) i);
        };
    }
    attachment.sendInitialUpdate();
}

// ===========================================================================
// VelMarker

VelMarker::VelMarker (juce::Slider& mainFader, juce::RangedAudioParameter& fractionParameter)
    : main (mainFader),
      attachment (fractionParameter, [this] (float v) { fraction = v; repaint(); })
{
    main.addListener (this);
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

VelMarker::~VelMarker()
{
    main.removeListener (this);
}

float VelMarker::mainNorm() const
{
    return (float) main.valueToProportionOfLength (main.getValue());
}

// Der Fader rechnet selbst aus, wo ein Wert liegt (inklusive seines Randes an den Enden). So sitzt der Pfeil
// bei gleichem Wert immer genau auf der Mittellinie des Griffs, an den Enden genauso wie in der Mitte.
float VelMarker::yForProportion (float proportion) const
{
    const double value = main.proportionOfLengthToValue (juce::jlimit (0.0, 1.0, (double) proportion));
    const float yOnFader = (float) main.getPositionOfValue (value);
    return getLocalPoint (&main, juce::Point<float> (0.0f, yOnFader)).y;
}

float VelMarker::markerY() const
{
    return yForProportion (mainNorm() * fraction);
}

void VelMarker::paint (juce::Graphics& g)
{
    const float y = markerY();
    drawGlow (g, juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ 6.5f, y }), colours::glowBlue, 0.7f);
    juce::Path p;
    p.addTriangle (3.0f, y, 10.0f, y - 4.5f, 10.0f, y + 4.5f);
    g.setColour (colours::velMarker);
    g.fillPath (p);
}

void VelMarker::setFromY (float y)
{
    // Umkehrung mit denselben Endpunkten wie der Fader (oben = Maximum, unten = Minimum)
    const float yTop = yForProportion (1.0f), yBottom = yForProportion (0.0f);
    const float span = juce::jmax (1.0f, yBottom - yTop);
    const float pos = juce::jlimit (0.0f, 1.0f, (yBottom - y) / span);
    const float m = mainNorm();
    const float f = m > 0.001f ? juce::jlimit (0.0f, 1.0f, pos / m) : 1.0f;
    attachment.setValueAsPartOfGesture (f);
    if (onChange) onChange (f);
}

void VelMarker::mouseDown (const juce::MouseEvent& e)
{
    attachment.beginGesture();
    setFromY (e.position.y);
}

void VelMarker::mouseDrag (const juce::MouseEvent& e)  { setFromY (e.position.y); }
void VelMarker::mouseUp (const juce::MouseEvent&)      { attachment.endGesture(); }

void VelMarker::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (1.0f);   // 100 % = keine Velocity-Wirkung
    if (onChange) onChange (1.0f);
}

} // namespace maermel::ui
