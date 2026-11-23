// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// MaermelLook.h – Märmel
// Finales Design (nach der abgestimmten Stilprobe, siehe Konzeptpapier „Gestaltung“):
// gebürstetes Grau, dezente Sektionsrahmen, schwarzes „Plastikglas“ mit türkisblauer Punktmatrix,
// schwarze Drehknöpfe mit rotem Zeiger, silberne Fader, Tasten mit hellblauem Leuchten, rote LEDs.
// Alles wird im Code gezeichnet (keine Bilder), damit es in jeder Größe und auf jedem Bildschirm scharf ist.

#pragma once

#include <JuceHeader.h>

namespace maermel::ui
{

// ---------------------------------------------------------------------------
// Farben
namespace colours
{
    const juce::Colour label      { 226, 229, 232 };   // weiße Beschriftung
    const juce::Colour sub        { 150, 158, 166 };   // Zusatzangaben
    const juce::Colour frame      { 104, 122, 138 };   // Sektionsrahmen
    const juce::Colour cyan       {  88, 220, 255 };   // Punktmatrix
    const juce::Colour cyanOff    {  16,  34,  42 };   // ausgeschaltete Punkte
    const juce::Colour red        { 232,  52,  40 };   // Zeiger, Schriftzug
    const juce::Colour ledRed     { 232,  52,  40 };
    const juce::Colour ledRedOff  {  70,  30,  28 };
    const juce::Colour blueTop    {  92, 170, 240 };   // aktive Taste
    const juce::Colour blueBottom {  40, 105, 190 };
    const juce::Colour glowBlue   {  60, 160, 255 };
    const juce::Colour velMarker  { 110, 200, 255 };   // VelA/VelD-Pfeil
}

// ---------------------------------------------------------------------------
// Schrift: schmale serifenlose Schrift, Großbuchstaben, etwas leichter als Mittelgewicht.
// Solange keine eigene Schrift (z. B. Barlow Semi Condensed) eingebunden ist, die Systemschrift.
juce::Font labelFont (float height, bool strong = false);

// Beschriftung zeichnen: x = Mitte (centred) bzw. linker/rechter Rand, y = Oberkante
void drawLabel (juce::Graphics& g, const juce::String& text, float x, float y, float height,
                juce::Justification just = juce::Justification::centred,
                juce::Colour colour = colours::label, bool strong = false);

// Sektionsrahmen mit Titel oben links (Linie hinter dem Titel ausgespart)
void drawFrame (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title);

// Schwarzes „Plastikglas“ (Displays, Wertefelder, Tastengruppen)
// Anzeige-Glas. topEdge: Höhe (Punkte) des dunklen Rands oben innen, unter dem der Glanz beginnt (seit 23.11.2026,
// nach Björns Skizze): 1 für die kleinen Wertefelder, 2 für die Stimmenwahl, 3 für die Instrument-Anzeige.
void drawGlass (juce::Graphics& g, juce::Rectangle<float> r, float radius, float topEdge = 1.0f);

// Rote LED (an/aus) mit Leuchten
void drawLed (juce::Graphics& g, juce::Point<float> centre, bool on, float radius = 3.2f);

// Weiches Leuchten (Ellipse mit nach außen abnehmender Deckkraft)
void drawGlow (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float strength);

// Gebürsteter Hintergrund in Bildpunkten (lange waagerechte Schliffspuren); einmal erzeugen und zwischenspeichern.
// blue = false: Grau (Standard), helle Anteile der Bürstung auf 55 %.
// blue = true:  leicht blau wie in den Entwürfen und ruhiger (helle Anteile auf 25 %), z. B. für ältere Displays.
juce::Image makeBrushedBackground (int widthPx, int heightPx, bool blue = false);

// ---------------------------------------------------------------------------
// Punktmatrix (eigene 5 × 8-Schrift, DotMatrixFont.h)
//
// Rastergerecht: Ein Punkt bekommt immer ganze Bildpunkte. Auf Retina (Faktor 2) ist der logische
// Punktabstand 1,5 genau 3 Bildpunkte (Punkt 2 + Lücke 1). Auf normalen Bildschirmen passt 1,5 nicht ins
// Raster – dort wird ein Abstand von 2 Bildpunkten gewählt (Punkt 1 + Lücke 1), etwas größer, aber scharf.
struct DotText
{
    static constexpr float kPitch = 1.5f;   // logischer Punktabstand (Konzept: einheitlich 1,5)

    // Fenstergröße mit Bruchteil (125 %, 150 %): Die Bedienelemente liegen dann teils auf halben Bildpunkten,
    // jeder Punkt verteilt sich auf zwei Pixel und wirkt dunkler. Ausgleich: Punkte etwas größer und heller,
    // Leuchten etwas kräftiger. Bei 100 % und 200 % unverändert. Wird vom Fenster bei jeder Größenänderung gesetzt.
    static inline bool fractionalScale = false;

    // Zeichnet Text; just = links/Mitte/rechts bezogen auf x. maxWidth > 0: zu lange Texte werden mit „...“
    // gekürzt. Rückgabe: true, wenn gekürzt wurde.
    static bool draw (juce::Graphics& g, const juce::String& text, float x, float topY, juce::Justification just,
                      float maxWidth = 0.0f, juce::Colour colour = colours::cyan);

    // Höhe einer Zeile (8 Punkte) in logischen Punkten für den aktuellen Bildschirm
    static float lineHeight (juce::Graphics& g);
};

// ---------------------------------------------------------------------------
// Look and Feel: Drehknöpfe, Fader, Tasten, kleine Schalter
//
// Eigenschaften an den Reglern (getProperties()):
//   "bipolar"  → Mittelmarkierung hell
//   "compact"  → waagerechter Fader nur mit Skalenstrichen oben, kleinere Kappe (LFO-Routing)
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    // Abstand zwischen sichtbarer Taste und Komponentenrand (Platz für das Leuchten)
    static constexpr float kButtonGlowMargin = 3.0f;

    // Eine Taste zeichnen (auch für eigene Komponenten)
    static void drawKey (juce::Graphics& g, juce::Rectangle<float> key, bool active, bool highlighted);
};

} // namespace maermel::ui
