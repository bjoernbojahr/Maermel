// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// MaermelLook.cpp – Märmel

#include "MaermelLook.h"
#include "DotMatrixFont.h"

// Barlow Semi Condensed als Binary Resource (Projucer: Resources/Fonts/*.ttf, „Binary Resource“ angehakt).
// Fehlt sie, wird die Systemschrift verwendet.
#if __has_include ("BinaryData.h")
 #include "BinaryData.h"
 #define MAERMEL_HAS_BARLOW 1
#else
 #define MAERMEL_HAS_BARLOW 0
#endif

#include <cmath>
#include <vector>

namespace maermel::ui
{

// ===========================================================================
// Schrift und Beschriftung

namespace
{
juce::String sansName()
{
   #if JUCE_MAC
    return "Helvetica Neue";
   #elif JUCE_WINDOWS
    return "Segoe UI";
   #else
    return juce::Font::getDefaultSansSerifFontName();
   #endif
}

#if MAERMEL_HAS_BARLOW
// Eingebettete Schrift (Resources/Fonts, SIL Open Font License): einmal laden, dann wiederverwenden
const juce::Typeface::Ptr& barlowMedium()
{
    static const juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (
        BinaryData::BarlowSemiCondensedMedium_ttf, (size_t) BinaryData::BarlowSemiCondensedMedium_ttfSize);
    return t;
}
const juce::Typeface::Ptr& barlowSemiBold()
{
    static const juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (
        BinaryData::BarlowSemiCondensedSemiBold_ttf, (size_t) BinaryData::BarlowSemiCondensedSemiBold_ttfSize);
    return t;
}
#endif
} // namespace

juce::Font labelFont (float height, bool strong)
{
   #if MAERMEL_HAS_BARLOW
    // Barlow Semi Condensed Medium, 10 % größer als die Maße der Stilprobe (Barlow hat niedrigere Großbuchstaben);
    // Schriftzug in SemiBold. Im Bild abgestimmt: trifft die gewählte Variante B.
    const auto& face = strong ? barlowSemiBold() : barlowMedium();
    if (face != nullptr)
        return juce::Font (juce::FontOptions (face).withPointHeight (strong ? height : height * 1.1f)
                                                    .withKerningFactor (0.04f));
   #endif
    // Ersatz, falls die Schrift nicht eingebunden ist: schmale Systemschrift
    return juce::Font (juce::FontOptions (sansName(), height, strong ? juce::Font::bold : juce::Font::plain)
                           .withHorizontalScale (0.93f)
                           .withKerningFactor (0.06f));
}

void drawLabel (juce::Graphics& g, const juce::String& text, float x, float y, float height,
                juce::Justification just, juce::Colour colour, bool strong)
{
    const auto font = labelFont (height, strong);
    g.setFont (font);
    g.setColour (colour);
    const float w = 300.0f, h = height + 4.0f;
    juce::Rectangle<float> r;
    if (just.testFlags (juce::Justification::horizontallyCentred)) r = { x - w / 2, y - 1.0f, w, h };
    else if (just.testFlags (juce::Justification::right))          r = { x - w, y - 1.0f, w, h };
    else                                                            r = { x, y - 1.0f, w, h };
    g.drawText (text, r, (just.getOnlyHorizontalFlags() | juce::Justification::top), false);
}

void drawFrame (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    const auto font = labelFont (12.0f);
    const float titleW = juce::GlyphArrangement::getStringWidth (font, title) + 12.0f;
    const juce::Rectangle<float> gap (r.getX() + 10.0f, r.getY() - 4.0f, titleW, 8.0f);

    g.saveState();
    g.excludeClipRegion (gap.toNearestInt());
    g.setColour (colours::frame);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.restoreState();

    drawLabel (g, title, r.getX() + 16.0f, r.getY() - 8.0f, 12.0f, juce::Justification::left);
}

void drawGlass (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (juce::Colour (20, 23, 27));
    g.fillRoundedRectangle (r.expanded (1.0f), radius + 1.0f);
    g.setColour (juce::Colour (5, 7, 9));
    g.fillRoundedRectangle (r, radius);

    // leichter Glanz in der oberen Hälfte
    const auto top = r.withHeight (r.getHeight() * 0.45f).reduced (radius, 0.0f);
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.10f), top.getX(), top.getY(),
                                juce::Colours::white.withAlpha (0.0f), top.getX(), top.getBottom(), false);
    g.setGradientFill (sheen);
    g.fillRect (top);

    g.setColour (juce::Colour (70, 78, 86));
    g.drawLine (r.getX() + radius, r.getBottom(), r.getRight() - radius, r.getBottom(), 1.0f);
}

void drawGlow (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float strength)
{
    // mehrere Ellipsen nach außen mit abnehmender Deckkraft
    for (int i = 6; i >= 1; --i)
    {
        const float grow = (float) i * area.getWidth() * 0.12f;
        g.setColour (colour.withAlpha (strength * 0.10f * (float) (7 - i) / 6.0f));
        g.fillEllipse (area.expanded (grow, grow * area.getHeight() / juce::jmax (1.0f, area.getWidth())));
    }
}

void drawLed (juce::Graphics& g, juce::Point<float> c, bool on, float r)
{
    if (on)
        drawGlow (g, juce::Rectangle<float> (r * 2, r * 2).withCentre (c), colours::ledRed, 0.9f);
    g.setColour (juce::Colour (12, 13, 15));
    g.fillEllipse (juce::Rectangle<float> ((r + 1) * 2, (r + 1) * 2).withCentre (c));
    g.setColour (on ? colours::ledRed : colours::ledRedOff);
    g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c));
    if (on)
    {
        g.setColour (juce::Colour (255, 200, 190));
        g.fillEllipse (c.x - r * 0.5f, c.y - r * 0.6f, r * 0.5f, r * 0.45f);
    }
}

juce::Image makeBrushedBackground (int w, int h, bool blue)
{
    const float brightShare = blue ? 0.25f : 0.55f;                   // Anteil der hellen Bürstung
    const float tintR = blue ? 0.84f : 0.96f, tintG = blue ? 0.97f : 1.0f, tintB = blue ? 1.22f : 1.05f;
    juce::Image img (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), false);
    juce::Random rng (3);
    auto gauss = [&rng] { return (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 1.5f) * 2.0f; };

    const int k = juce::jmax (8, (int) std::round (140.0f * (float) w / 2064.0f * 2.0f));   // Länge der Schliffspuren
    std::vector<float> noise ((size_t) w), streak ((size_t) w);
    const float norm = 1.0f / std::sqrt ((float) k);

    // Index mit Umlauf am Bildrand
    auto wrap = [w] (int i) { const int m = i % w; return (size_t) (m < 0 ? m + w : m); };

    juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
    {
        for (auto& n : noise) n = gauss();
        // gleitende Summe über k Werte (mit Umlauf) = lange waagerechte Spuren
        float sum = 0.0f;
        for (int i = 0; i < k; ++i) sum += noise[wrap (i - k)];
        for (int x = 0; x < w; ++x)
        {
            sum += noise[(size_t) x] - noise[wrap (x - k)];
            streak[(size_t) x] = sum * norm;
        }
        const float row = gauss() * 2.8f;
        for (int x = 0; x < w; ++x)
        {
            // Abweichung vom Grundton; helle Anteile nur zu 55 % (weniger Kontrast, ruhiger auf älteren Displays),
            // dunkle Anteile unverändert
            float d = streak[(size_t) x] * 6.0f + row + gauss() * 1.8f;
            if (d > 0.0f)
                d *= brightShare;
            const float v = juce::jlimit (0.0f, 255.0f, 42.0f + d);
            data.setPixelColour (x, y, juce::Colour ((juce::uint8) juce::jlimit (0.0f, 255.0f, v * tintR),
                                                     (juce::uint8) juce::jlimit (0.0f, 255.0f, v * tintG),
                                                     (juce::uint8) juce::jlimit (0.0f, 255.0f, v * tintB)));
        }
    }
    return img;
}

// ===========================================================================
// Punktmatrix

namespace
{
struct DotGeometry
{
    float scale;      // Bildpunkte je logischem Punkt
    float pitch;      // logischer Punktabstand
    float dot;        // logische Punktgröße
    bool small;       // kleine Darstellung: weniger Leuchten, schwächere Aus-Punkte
};

DotGeometry geometry (juce::Graphics& g)
{
    const float scale = juce::jmax (0.5f, g.getInternalContext().getPhysicalPixelScaleFactor());
    // ganze Bildpunkte je Punktabstand; mindestens 2 (Punkt 1 + Lücke 1)
    const float pitchPx = juce::jmax (2.0f, std::round (DotText::kPitch * scale));
    const float dotPx = pitchPx <= 3.0f ? pitchPx - 1.0f : std::round (pitchPx * 0.8f);
    return { scale, pitchPx / scale, dotPx / scale, pitchPx <= 3.0f };
}
} // namespace

float DotText::lineHeight (juce::Graphics& g)
{
    return geometry (g).pitch * 8.0f;
}

bool DotText::draw (juce::Graphics& g, const juce::String& textIn, float x, float topY, juce::Justification just,
                    float maxWidth, juce::Colour colour)
{
    const auto geo = geometry (g);
    const float cell = geo.pitch * 6.0f;

    juce::String text = textIn;
    bool truncated = false;
    if (maxWidth > 0.0f)
    {
        const int maxChars = (int) std::floor ((maxWidth + geo.pitch) / cell);
        if (text.length() > maxChars && maxChars > 3)
        {
            text = text.substring (0, maxChars - 3).trimEnd() + "...";
            truncated = true;
        }
    }

    const float width = (float) text.length() * cell - geo.pitch;
    float x0 = x;
    if (just.testFlags (juce::Justification::horizontallyCentred)) x0 = x - width / 2.0f;
    else if (just.testFlags (juce::Justification::right))          x0 = x - width;

    // auf das Bildpunktraster legen, damit jeder Punkt genau ganze Bildpunkte trifft
    x0 = std::round (x0 * geo.scale) / geo.scale;
    const float y0 = std::round (topY * geo.scale) / geo.scale;

    auto onColour = geo.small && geo.scale < 1.5f ? colour.brighter (0.25f) : colour;   // normale Bildschirme etwas heller
    const auto offColour = colours::cyanOff.withAlpha (geo.small ? 0.55f : 1.0f);
    float glowGrow = (geo.small ? 0.5f : 0.9f) / geo.scale;
    float glowAlpha = 0.22f;
    float dot = geo.dot;
    if (fractionalScale)
    {
        // Ausgleich für 125 % / 150 % (siehe MaermelLook.h); Lücke zwischen den Punkten bleibt erhalten
        dot = juce::jmin (geo.dot + 0.5f / geo.scale, geo.pitch - 0.6f / geo.scale);
        onColour = onColour.brighter (0.3f);
        glowGrow *= 1.4f;
        glowAlpha = 0.32f;
    }

    int index = 0;
    for (auto it = text.getCharPointer(); ! it.isEmpty(); ++index)
    {
        const juce::juce_wchar ch = it.getAndAdvance();
        const auto* glyph = dotfont::find ((char32_t) ch);
        if (glyph == nullptr)
            glyph = dotfont::find (U'?');
        if (glyph == nullptr)
            continue;
        const float cx = x0 + (float) index * cell;
        for (int row = 0; row < 8; ++row)
        {
            for (int col = 0; col < 5; ++col)
            {
                const bool on = ((glyph->rows[row] >> (4 - col)) & 1) != 0;
                const juce::Rectangle<float> r (cx + (float) col * geo.pitch, y0 + (float) row * geo.pitch, dot, dot);
                if (on)
                {
                    g.setColour (onColour.withAlpha (glowAlpha));
                    g.fillRect (r.expanded (glowGrow));
                    g.setColour (onColour);
                    g.fillRect (r);
                }
                else
                {
                    g.setColour (offColour);
                    g.fillRect (r);
                }
            }
        }
    }
    return truncated;
}

// ===========================================================================
// Look and Feel

LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (24, 26, 30));
    setColour (juce::PopupMenu::textColourId, colours::label);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::blueBottom);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (5, 7, 9));
    setColour (juce::TextEditor::textColourId, colours::cyan);
    setColour (juce::TextEditor::outlineColourId, colours::frame);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::cyan);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                    float startAngle, float endAngle, juce::Slider&)
{
    const auto c = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).getCentre();
    const float R = juce::jmin (width, height) * 0.5f - 7.0f;

    // Skalenpunkte
    g.setColour (juce::Colour (205, 212, 218));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const float r = R + 5.0f;
        g.fillEllipse (c.x + std::sin (a) * r - 1.1f, c.y - std::cos (a) * r - 1.1f, 2.2f, 2.2f);
    }

    // Schatten, Körper, Rändelung
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c.translated (1.0f, 3.0f)));
    g.setColour (juce::Colour (14, 15, 17));
    g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
    g.setColour (juce::Colour (46, 49, 54));
    const int n = juce::jmax (12, (int) (R * 2.4f));
    for (int k = 0; k < n; ++k)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) k / (float) n;
        g.drawLine (c.x + std::cos (a) * (R - 2.2f), c.y + std::sin (a) * (R - 2.2f),
                    c.x + std::cos (a) * R, c.y + std::sin (a) * R, 1.0f);
    }
    const float ri = R * 0.80f;
    g.setColour (juce::Colour (30, 32, 36));
    g.fillEllipse (juce::Rectangle<float> (ri * 2.0f, ri * 2.0f).withCentre (c));
    g.setColour (juce::Colour (40, 43, 48));
    g.fillEllipse (c.x - ri * 0.85f, c.y - ri * 0.9f, ri * 1.7f, ri);

    // roter Zeiger
    const float a = startAngle + (endAngle - startAngle) * pos;
    g.setColour (colours::red);
    g.drawLine (c.x + std::sin (a) * ri * 0.95f, c.y - std::cos (a) * ri * 0.95f,
                c.x + std::sin (a) * ri * 0.25f, c.y - std::cos (a) * ri * 0.25f, juce::jmax (2.0f, R / 12.0f));
}

int LookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    if (s.isHorizontal())
        return 7;                                   // Kappe 14 breit
    return 8;                                       // Kappe 16 hoch
}

namespace
{
juce::Colour silver (float t)
{
    const float v = 225.0f - 152.0f * std::abs (t - 0.38f);
    return juce::Colour ((juce::uint8) v, (juce::uint8) juce::jmin (255.0f, v + 2.0f), (juce::uint8) juce::jmin (255.0f, v + 5.0f));
}

void silverGradient (juce::Graphics& g, juce::Rectangle<float> r, bool vertical)
{
    juce::ColourGradient grad (silver (0.0f), r.getX(), r.getY(),
                               silver (1.0f), vertical ? r.getX() : r.getRight(), vertical ? r.getBottom() : r.getY(), false);
    grad.addColour (0.38, silver (0.38f));
    g.setGradientFill (grad);
}
} // namespace

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const bool compact = (bool) s.getProperties().getWithDefault ("compact", false);
    const auto tickColour = [bipolar] (int i)
    {
        if (bipolar && i == 5) return juce::Colour (230, 233, 236);
        return (i == 0 || i == 5 || i == 10) ? juce::Colour (175, 182, 188) : juce::Colour (120, 128, 136);
    };

    if (s.isVertical())
    {
        const float cx = (float) x + (float) width * 0.5f;
        const float thumb = (float) getSliderThumbRadius (s);
        // Skalenstriche links und rechts
        for (int i = 0; i <= 10; ++i)
        {
            const float ty = (float) y + (float) height * (float) i / 10.0f;
            const bool major = (i == 0 || i == 5 || i == 10);
            const float half = major ? 7.0f : 4.0f;
            g.setColour (tickColour (i));
            g.drawLine (cx - 6.0f - half, ty, cx - 6.0f, ty, 1.0f);
            g.drawLine (cx + 6.0f, ty, cx + 6.0f + half, ty, 1.0f);
        }
        // Schlitz
        g.setColour (juce::Colour (6, 7, 8));
        g.fillRoundedRectangle (cx - 2.5f, (float) y - thumb, 5.0f, (float) height + thumb * 2.0f, 2.0f);
        // silberne Kappe
        const juce::Rectangle<float> cap (cx - 12.0f, sliderPos - 8.0f, 24.0f, 16.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (cap.translated (0.0f, 2.0f).expanded (1.0f), 2.5f);
        silverGradient (g, cap, true);
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (juce::Colour (30, 32, 36));
        g.drawLine (cx - 10.0f, sliderPos, cx + 10.0f, sliderPos, 1.0f);
        g.setColour (juce::Colour (40, 42, 46));
        g.drawRoundedRectangle (cap, 2.0f, 1.0f);
        return;
    }

    // waagerecht (Delay, LFO-Routing)
    const float cy = (float) y + (float) height * 0.5f;
    const float thumb = (float) getSliderThumbRadius (s);
    for (int i = 0; i <= 10; ++i)
    {
        const float tx = (float) x + (float) width * (float) i / 10.0f;
        const bool major = (i == 0 || i == 5 || i == 10);
        const float h = compact ? (major ? 4.0f : 2.5f) : (major ? 6.0f : 3.5f);
        g.setColour (tickColour (i));
        g.drawLine (tx, cy - 6.0f - h, tx, cy - 6.0f, 1.0f);
        if (! compact)
            g.drawLine (tx, cy + 6.0f, tx, cy + 6.0f + h, 1.0f);
    }
    g.setColour (juce::Colour (6, 7, 8));
    g.fillRoundedRectangle ((float) x - thumb, cy - 2.5f, (float) width + thumb * 2.0f, 5.0f, 2.0f);
    const float hh = compact ? 8.0f : 11.0f;
    const juce::Rectangle<float> cap (sliderPos - 7.0f, cy - hh, 14.0f, hh * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (cap.translated (1.0f, 2.0f).expanded (1.0f), 2.5f);
    silverGradient (g, cap, false);
    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (juce::Colour (30, 32, 36));
    g.drawLine (sliderPos, cy - hh + 2.0f, sliderPos, cy + hh - 2.0f, 1.0f);
    g.setColour (juce::Colour (40, 42, 46));
    g.drawRoundedRectangle (cap, 2.0f, 1.0f);
}

void LookAndFeel::drawKey (juce::Graphics& g, juce::Rectangle<float> key, bool active, bool highlighted)
{
    if (active)
        drawGlow (g, key.reduced (key.getWidth() * 0.15f, key.getHeight() * 0.1f), colours::glowBlue, 0.75f);

    const auto top = active ? colours::blueTop : juce::Colour (44, 48, 54);
    const auto bottom = active ? colours::blueBottom : juce::Colour (24, 26, 30);
    juce::ColourGradient grad (highlighted ? top.brighter (0.08f) : top, key.getX(), key.getY(),
                               highlighted ? bottom.brighter (0.08f) : bottom, key.getX(), key.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (key, 3.0f);
    g.setColour (juce::Colour (10, 11, 13));
    g.drawRoundedRectangle (key, 3.0f, 1.0f);
    g.setColour (active ? juce::Colour (150, 200, 255) : juce::Colour (80, 86, 94));
    g.drawLine (key.getX() + 3.0f, key.getY() + 1.0f, key.getRight() - 3.0f, key.getY() + 1.0f, 1.0f);
}

// Tasten mit Eigenschaft "ledOnly" (LOCK, AT->WHEEL) zeigen ihren Zustand über eine LED, nicht durch Leuchten
static bool glows (juce::Button& b)
{
    return b.getToggleState() && ! (bool) b.getProperties().getWithDefault ("ledOnly", false);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto key = b.getLocalBounds().toFloat().reduced (kButtonGlowMargin);
    if (! b.isEnabled())
    {
        // Deaktiviert: grau und blass, ohne Leuchten – sieht nicht klickbar aus
        g.beginTransparencyLayer (0.4f);
        drawKey (g, key, false, false);
        g.endTransparencyLayer();
        return;
    }
    drawKey (g, down ? key.translated (0.0f, 0.5f) : key, glows (b), highlighted);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return labelFont (10.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (labelFont (10.0f));
    if (! b.isEnabled())
        g.setColour (colours::label.withAlpha (0.35f));      // deaktiviert: gedämpfte Beschriftung
    else
        g.setColour (glows (b) ? juce::Colours::white : colours::label);

    const juce::String text = b.getButtonText();
    const auto area = b.getLocalBounds().reduced ((int) kButtonGlowMargin).toFloat();

    // LOCK: Vorhängeschloss statt Text (25.10.2026, Variante „B2“ der Stilprobe): Körper als abgerundetes Rechteck,
    // Bügel aus zwei kurzen Beinen und einem flachen, runden Bogen (nicht spitz – sonst wirkt es wie eine Tasche)
    if (b.getProperties()["icon"].toString() == "lock")
    {
        const float k = 1.15f, cx = area.getCentreX(), cy = area.getCentreY();
        const float bw = 7.6f * k, bh = 5.6f * k;
        const float top = cy - bh * 0.5f + 1.6f * k;                 // Oberkante des Körpers
        g.fillRoundedRectangle (cx - bw * 0.5f, top, bw, bh, 1.0f * k);
        const float lw = 1.1f * k, r = (6.0f - 1.1f) * k * 0.5f, ry = 2.2f * k, yb = top - 1.0f * k;
        juce::Path shackle;
        shackle.startNewSubPath (cx - r, top + 0.3f * k);
        shackle.lineTo (cx - r, yb);
        shackle.addCentredArc (cx, yb, r, ry, 0.0f, -juce::MathConstants<float>::halfPi,
                               juce::MathConstants<float>::halfPi, false);
        shackle.lineTo (cx + r, top + 0.3f * k);
        g.strokePath (shackle, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        g.setColour (juce::Colour (24, 26, 30));                       // Schlüsselloch
        g.fillEllipse (cx - 0.75f * k, top + bh * 0.38f, 1.5f * k, 1.5f * k);
        return;
    }

    // „AT->WHEEL“: Barlow hat kein „→“, der Pfeil wird zwischen die beiden Wörter gezeichnet
    if (text.contains ("->"))
    {
        const juce::String left = text.upToFirstOccurrenceOf ("->", false, false);
        const juce::String right = text.fromFirstOccurrenceOf ("->", false, false);
        const juce::Font font = labelFont (10.0f);
        const float wl = juce::GlyphArrangement::getStringWidth (font, left);
        const float wr = juce::GlyphArrangement::getStringWidth (font, right);
        const float gap = 9.0f;
        float x = area.getCentreX() - (wl + gap + wr) * 0.5f;
        g.drawText (left, juce::Rectangle<float> (x, area.getY(), wl + 1.0f, area.getHeight()), juce::Justification::centredLeft, false);
        g.drawText (right, juce::Rectangle<float> (x + wl + gap, area.getY(), wr + 1.0f, area.getHeight()), juce::Justification::centredLeft, false);
        // Pfeil auf Höhe der Mittelstriche von H und E (Textmitte liegt höher als die Mitte der Großbuchstaben)
        const float y = area.getCentreY() + 1.25f, ax0 = x + wl + 2.0f, ax1 = x + wl + gap - 2.0f;
        g.drawLine (ax0, y, ax1 - 1.5f, y, 1.1f);
        juce::Path head;
        head.addTriangle (ax1, y, ax1 - 3.0f, y - 1.8f, ax1 - 3.0f, y + 1.8f);
        g.fillPath (head);
        return;
    }
    g.drawText (text, area.toNearestInt(), juce::Justification::centred, false);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // kleine Taste (14 × 14), die aktiv hellblau leuchtet, Beschriftung rechts daneben
    const auto bounds = b.getLocalBounds().toFloat();
    const juce::Rectangle<float> key (kButtonGlowMargin, bounds.getCentreY() - 7.0f, 14.0f, 14.0f);
    const bool enabled = b.isEnabled();
    if (enabled)
    {
        drawKey (g, key, b.getToggleState(), highlighted);
    }
    else
    {
        // Deaktiviert: grau und blass, ohne blaues Leuchten (auch wenn die Option gespeichert eingeschaltet ist)
        g.beginTransparencyLayer (0.4f);
        drawKey (g, key, false, false);
        g.endTransparencyLayer();
    }
    g.setFont (labelFont (11.0f));
    g.setColour (enabled ? colours::label : colours::label.withAlpha (0.35f));
    g.drawText (b.getButtonText(), bounds.withTrimmedLeft (key.getRight() + 6.0f), juce::Justification::centredLeft, false);
}

} // namespace maermel::ui
