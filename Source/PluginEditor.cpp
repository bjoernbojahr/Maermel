// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// PluginEditor.cpp – Märmel
// Finales Design. Positionen in Punkten wie in der abgestimmten Stilprobe (tools/stilprobe_zeichnen.py, internes Hilfsskript – nicht im öffentlichen Repository).

#include "PluginEditor.h"
#include "ParameterControl.h"
#include "ParameterIds.h"
#include "FileDialogs.h"
#include "ExternalLaunch.h"

#include <cmath>
#include <cstring>
#include <iterator>

using namespace maermel::ui;

namespace
{
// Wie lange eine Werteänderung oder Meldung den Instrumentnamen ersetzt (nach der letzten Änderung)
constexpr juce::uint32 kStatusMs = 1500;

// Menü-IDs
constexpr int kSearchItem = 1;
constexpr int kChooseLibraryItem = 2;
constexpr int kOpenLibraryItem = 3;
constexpr int kNotesItem = 5;        // Tafel „Loading notes“
constexpr int kExportItem = 6;       // aktuelles Instrument mit Samples exportieren
constexpr int kFirstFileItem = 1000;

// Gemeinsame Linien (siehe Konzeptpapier „Gemeinsame Linien“)
constexpr float kLineLabel  = 255.0f;   // untere Beschriftungslinie der oberen Reihe
constexpr float kLineValue  = 233.0f;   // Wertefelder darüber
constexpr float kKnobTop    = 104.0f;   // obere Drehknöpfe (Höhe Drive)
constexpr float kKnobLow    = 207.0f;   // untere Drehknöpfe
constexpr float kLineLabel2 = 461.0f;   // untere Beschriftungslinie der unteren Reihe
constexpr float kLfoDy      = 18.0f;    // LFO-Bedienteil

// Kopfzeile
constexpr float kGx = 15.0f;            // Stimmenwahl; seit 15.10.2026 1 Punkt weiter links (Rahmen darunter 3 Punkte schmaler)
constexpr float kBx = 7.5f;             // LOCK/LOAD/SAVE
const juce::Rectangle<float> kDisplay (397.0f, 10.0f, 321.0f, 36.0f);
const juce::Rectangle<float> kLogo (950.0f, 10.0f, 72.0f, 32.0f);   // Klickfläche des Schriftzugs (About)
const juce::Rectangle<int> kFixLedArea (740, 0, 30, 18);
// AT→WHEEL: Taste links oben – Abstand zum linken Fensterrand wie das „l“ von „Märmel“ zum rechten (15 Punkte)
const juce::Rectangle<float> kAtWheelButton (15.0f, 17.0f, 61.0f, 22.0f);
const juce::Rectangle<int> kAtWheelLedArea (30, 0, 31, 18);
const juce::Rectangle<int> kEditLedArea (864, 0, 30, 18);           // rote LED über EDIT (Live-Edit)

// Menüs innerhalb des Plugin-Fensters öffnen statt als eigene Fenster (entschieden 15.10.2026, in Reaper und Logic
// getestet: Untermenüs klappen korrekt um und werden nicht mehr verdeckt; Farben passen besser zum Plugin).
//   true  = innerhalb des Fensters (kein Verdecken von Untermenüs durch die DAW; Menüs bleiben im Fensterrahmen,
//           lange Listen werden gescrollt)
//   false = wie bisher eigene Fenster (dürfen über das Plugin-Fenster hinausragen)
// Gilt für das Instrumenten-Menü (Klick aufs Display) und das Rechtsklick-Menü.
constexpr bool kMenusInsidePluginWindow = true;

// Notenname nach SFZ-Zählung (C4 = 60), z. B. 34 → "A#1"
juce::String noteName (int note)
{
    if (note < 0 || note > 127)
        return "-";                               // außerhalb des MIDI-Bereichs (note % 12 wäre sonst negativ)
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
}

// Wertefeld-Text: Leerzeichen weg und Einheiten am Ende abschneiden ("+25 ct" → "+25", "35 %" → "35")
juce::String shortValue (juce::Slider& s)
{
    juce::String t = s.getTextFromValue (s.getValue()).removeCharacters (" ");
    while (t.isNotEmpty() && (juce::CharacterFunctions::isLetter (t.getLastCharacter()) || t.getLastCharacter() == '%'))
        t = t.dropLastCharacters (1);
    if (t.length() > 3 && t.startsWithChar ('+'))   // z. B. Fine "+100" → "100": passt auch auf normalen Bildschirmen
        t = t.substring (1);
    return t.isEmpty() ? juce::String ("0") : t;
}

// LFO-Rate im Wertefeld: ohne Leerzeichen und ohne "Hz" ("2.50 Hz" → "2.50"); Notenwerte und DC bleiben.
// Im Display oben steht der Wert weiterhin mit Einheit.
juce::String lfoValue (juce::Slider& s)
{
    juce::String t = s.getTextFromValue (s.getValue()).removeCharacters (" ");
    if (t.endsWithIgnoreCase ("Hz"))
        t = t.dropLastCharacters (2);
    return t;
}

} // namespace

// ===========================================================================
// Aufbau

MaermelAudioProcessorEditor::MaermelAudioProcessorEditor (MaermelAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&look);
    auto& params = audioProcessor.parameters;

    // ---------------- Kopfzeile ----------------
    // GLIDE gehört zu LEG/MON: näher an der Stimmenwahl als an AT→WHEEL (ohne Schriftzug – Klick zeigt Name und Wert)
    addKnob (glide, maermel::ids::glide, "GLIDE", 111.0f, 28.0f, 12.0f);   // mit der Stimmenwahl 1 Punkt nach links (15.10.2026)

    // AT→WHEEL je Instrument: Aftertouch wirkt wie das Modulationsrad (zusätzlich zur globalen Einstellung)
    placeButton (atWheelButton, kAtWheelButton);
    atWheelButton.setClickingTogglesState (true);
    atWheelButton.getProperties().set ("ledOnly", true);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (params, "atwheel", atWheelButton));
    atWheelButton.onClick = [this]
    {
        showStatusIfUser (atWheelButton, [this] { return juce::String (atWheelButton.getToggleState() ? "AT->WHEEL ON" : "AT->WHEEL OFF"); });
        repaint (kAtWheelLedArea);
    };

    {
        const float widths[] = { 29, 29, 21, 21, 21, 21, 21, 21, 21 };
        const float gap = 2.5f;
        float total = -gap;
        for (float w : widths) total += w + gap;
        float x = 122.0f + kGx + (235.0f - total) / 2.0f;
        const juce::StringArray names { "LEG", "MON", "2", "6", "8", "12", "16", "32", "64" };
        std::vector<juce::TextButton*> list;
        for (size_t i = 0; i < voiceButtons.size(); ++i)
        {
            voiceButtons[i].setButtonText (names[(int) i]);
            placeButton (voiceButtons[i], { x, 18.0f, widths[i], 20.0f });
            x += widths[i] + gap;
            list.push_back (&voiceButtons[i]);
        }
        auto group = std::make_unique<ChoiceButtons> (*params.getParameter ("voicemode"), list);
        group->onChoice = [this, names] (int i) { showStatus ("VOICES " + names[i]); };
        choiceGroups.push_back (std::move (group));
    }

    display.setBounds (kDisplay.toNearestInt());
    display.onPrev = [this] { stepInstrument (-1); };
    display.onNext = [this] { stepInstrument (+1); };
    display.onCentre = [this]
    {
        // Steht die rote Meldung (fehlende Samples), öffnet ein Klick die Ladehinweise statt des Menüs
        const bool alertShown = statusIsAlert && juce::Time::getMillisecondCounter() < statusUntil;
        if (alertShown && audioProcessor.getLoadingNotes().isNotEmpty())
            showNotes();
        else
            showLibraryMenu();
    };
    addAndMakeVisible (display);

    // LOCK bleibt an seiner Stelle; LOAD, SAVE, EDIT, PATH gleichmäßig bis kurz vor den Schriftzug (je 37 breit,
    // Abstand 6 wie zwischen LOCK und LOAD)
    placeButton (fixButton,  { 727.0f + kBx, 17.0f, 33.0f, 22.0f });
    placeButton (loadButton, { 773.5f, 17.0f, 37.0f, 22.0f });
    placeButton (saveButton, { 816.5f, 17.0f, 37.0f, 22.0f });
    placeButton (editButton, { 859.5f, 17.0f, 37.0f, 22.0f });
    placeButton (pathButton, { 902.5f, 17.0f, 37.0f, 22.0f });

    // LOCK (bis 25.10.2026 „FIX“): rote LED darüber (statt blauem Leuchten der Taste), solange Einstellungen
    // erhalten bleiben; auf der Taste ein gezeichnetes Vorhängeschloss statt Text (siehe MaermelLook)
    fixButton.setClickingTogglesState (true);
    fixButton.setToggleState (audioProcessor.isFixActive(), juce::dontSendNotification);
    fixButton.getProperties().set ("ledOnly", true);
    fixButton.getProperties().set ("icon", "lock");
    fixButton.onClick = [this]
    {
        audioProcessor.setFixActive (fixButton.getToggleState());
        showStatus (fixButton.getToggleState() ? "LOCK ON" : "LOCK OFF");
        repaint (kFixLedArea);
    };
    loadButton.onClick = [this] { chooseFile(); };
    pathButton.onClick = [this] { toggleSettings(); };

    // EDIT: aktuelles Instrument im externen SFZ-Editor öffnen; ist Live-Edit in PATH erlaubt, beobachtet
    // Märmel ab jetzt genau diese Datei (rote LED darüber) – bis zum Instrumentwechsel oder Beenden
    editButton.getProperties().set ("ledOnly", true);
    editButton.onClick = [this]
    {
        juce::String message;
        const bool ok = audioProcessor.openCurrentInstrumentInEditor (message);
        if (! ok)
            showStatus (message.toUpperCase());               // z. B. "NO SFZ EDITOR FOUND"
        else if (message.isNotEmpty())
            showStatus (message.toUpperCase());               // z. B. Hinweis zur EXS-Umwandlung
        else if (audioProcessor.isLiveEditActive())
            showStatus ("LIVE EDIT ON");
        repaint (kEditLedArea);
    };
    saveButton.onClick = [this]
    {
        if (audioProcessor.savePreset())
        {
            showStatus ("SAVED " + audioProcessor.getPresetFile().getFileNameWithoutExtension());
            // Nach dem Speichern wird LOCK nicht mehr gebraucht (Einstellungen sind jetzt im Preset): ausschalten
            audioProcessor.setFixActive (false);
            fixButton.setToggleState (false, juce::dontSendNotification);
            repaint (kFixLedArea);
        }
        else
            showStatus ("SAVE FAILED - SEE LOG");   // Einzelheiten im Logfile
    };

    // ---------------- Sample Start and Tune ----------------
    addVFader (start,    "start",    "START",     42.0f, 82.0f, 222.0f, false);
    addVFader (startVel, "startvel", "START VEL", 84.0f, 82.0f, 222.0f, true);
    placeButton (rndButton, { 26.0f, 251.0f, 36.0f, 20.0f });
    rndButton.setClickingTogglesState (true);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (params, "startrnd", rndButton));
    // Meldung nur beim Anklicken (onClick kommt auch, wenn ein geladenes Instrument den Knopf setzt)
    rndButton.onClick = [this] { showStatusIfUser (rndButton, [this] { return juce::String (rndButton.getToggleState() ? "START RANDOM ON" : "START RANDOM OFF"); }); };

    addVFader (coarse, "coarse", "C.TUNE", 140.0f, 82.0f, 222.0f, true);
    addVFader (fine,   "fine",   "FINE",   186.0f, 82.0f, 222.0f, true);
    addValueBox (coarse, 140.0f, kLineValue, 40.0f, shortValue);
    addValueBox (fine,   186.0f, kLineValue, 40.0f, shortValue);

    addKnob (keyShift, "keyshift", "KEYSHIFT", 255.0f, kKnobTop, 17.0f);
    addKnob (velOffset, "veloffset", "VEL OFFSET", 322.0f, kKnobTop, 17.0f);   // früher stand hier LO-FI (bis 07.10.2026)
    addValueBox (keyShift,  255.0f, kKnobTop + 27.0f, 40.0f, shortValue);
    addValueBox (velOffset, 322.0f, kKnobTop + 27.0f, 40.0f, shortValue);
    addKnob (pitchBend,  "pitchbend",  "P.BEND",          255.0f, kKnobLow, 17.0f);
    addKnob (wheelPitch, "wheelpitch", "PITCH WHEEL LFO", 322.0f, kKnobLow, 17.0f);   // Spalte 3 Punkte nach rechts (Platz für VEL OFFSET)
    addValueBox (pitchBend,  255.0f, kLineValue, 40.0f, shortValue);
    addValueBox (wheelPitch, 322.0f, kLineValue, 40.0f, shortValue);

    // ---------------- Filter ----------------
    addKnob (cutoff,    maermel::ids::cutoff, "CUTOFF",     450.0f, 136.0f, 52.0f);
    addKnob (drive,     maermel::ids::drive, "DRIVE",      568.0f, kKnobTop, 17.0f);
    addKnob (reso,      maermel::ids::resonance, "RESO",       537.0f, 178.0f, 17.0f);
    addKnob (filterKey, "filterkey", "FILTER KEY", 599.0f, 178.0f, 17.0f);

    {
        const float ox = 395.0f, oy = 225.0f;   // Lage der Auswahlliste
        std::vector<LedChoice::Item> items {
            { "NONE",  { 410.0f - ox, kLineLabel - 22.0f + 6.5f - oy } },
            { "LPF12", { 410.0f - ox, kLineLabel + 6.5f - oy } },
            { "LPF24", { 488.0f - ox, kLineLabel + 6.5f - oy } },
            { "HPF",   { 561.0f - ox, kLineLabel - 22.0f + 6.5f - oy } },
            { "BPF",   { 561.0f - ox, kLineLabel + 6.5f - oy } } };
        filterType = std::make_unique<LedChoice> (*params.getParameter ("filtertype"), items);
        filterType->setBounds (juce::roundToInt (ox), juce::roundToInt (oy), 240, 50);
        filterType->onChoice = [this] (int, const juce::String& label) { showStatus ("FILTER " + label); };
        addAndMakeVisible (*filterType);
    }

    addVFader (filterVelCut, "filtervelcutoff", "FILTER VEL FIX",   652.0f, 82.0f, 236.0f, true);
    addVFader (filterVelEnv, "filtervel",       "FILTER VEL ENV",   692.0f, 82.0f, 236.0f, true);
    addVFader (filterEnv,    maermel::ids::filterEnv, "FILTER ENV",       731.0f, 82.0f, 236.0f, true);
    addVFader (wheelFilter,  "wheelfilter",     "FILTER WHEEL LFO", 771.0f, 82.0f, 236.0f, false);

    // ---------------- Amp ----------------
    // LO-FI/CHORUS (oberer Knopf)
    addKnob (crush, maermel::ids::lofi, "LO-FI", 856.0f, kKnobTop, 17.0f);   // früher „Crush“; seit 07.10.2026 hier oben (an der Stelle von PAN)
    addValueBox (crush, 856.0f, kKnobTop + 27.0f, 40.0f, [] (juce::Slider& s) { return juce::String (juce::roundToInt (s.getValue())); });
    // Eingabefeld (Doppelklick) mit Modus und Samplerate dieser Instanz statt der globalen Werte für die
    // DAW-Anzeige – sonst las eine Lo-Fi-Instanz z. B. Prozent, wenn eine andere auf Chorus stand (seit 1.0.3).
    // Ersetzt die Funktionen, die die SliderAttachment in addKnob gesetzt hat.
    crush.textFromValueFunction = [this] (double v)
    {
        return MaermelAudioProcessor::lofiText ((float) v, audioProcessor.isChorusMode(), audioProcessor.getSampleRate());
    };
    crush.valueFromTextFunction = [this] (const juce::String& t)
    {
        return (double) MaermelAudioProcessor::lofiValueFromText (t, audioProcessor.isChorusMode());
    };
    // Statt der Beschriftung LO-FI ein Umschalter LO-FI/CHORUS (26.10.2026); der Regler steuert den Anteil des gewählten
    // Effekts. Die Anzeige beim Drehen richtet sich nach dem Effekt.
    shownChorus = audioProcessor.isChorusMode();
    fxButton.setButtonText (shownChorus ? "CHORUS" : "LO-FI");
    placeButton (fxButton, { 831.0f, kKnobTop + 49.0f, 50.0f, 18.0f });
    fxButton.onClick = [this] { toggleFxMode(); };
    crush.onValueChange = [this]
    {
        if (crush.isMouseButtonDown())
            showStatus (fxStatusText());
        else
            showStatusIfUser (crush, [this] { return fxStatusText(); });
    };
    crush.onDragStart = [this] { showStatus (fxStatusText()); };

    // WHEEL LFO der Amp-Sektion: Modulationsrad-Anteil für LFO-Volume und LFO-Pan (wie in Pitch und Filter)
    addKnob (wheelAmp, "wheelamp", "AMP WHEEL LFO", 856.0f, kKnobLow, 17.0f);
    addValueBox (wheelAmp, 856.0f, kLineValue, 40.0f, shortValue);
    addVFader (ampVel, "velocity",    "AMP VEL", 913.0f, 82.0f, 236.0f, false);
    addVFader (ampKey, "ampkeyscale", "AMP KEY", 953.0f, 82.0f, 236.0f, true);
    addVFader (volume, "volume",      "VOLUME",  991.0f, 82.0f, 236.0f, false);

    // ---------------- LFOs ----------------
    for (size_t l = 0; l < lfos.size(); ++l)
    {
        auto& c = lfos[l];
        const juce::String prefix = (l == 0) ? "plfo" : "mlfo";
        const juce::String title = (l == 0) ? "POLY LFO" : "MONO LFO";
        const float kx = (l == 0) ? 66.0f : 400.0f;
        const float dx0 = (l == 0) ? 112.0f : 446.0f, dx1 = (l == 0) ? 198.0f : 532.0f;
        const float wx = (l == 0) ? 24.0f : 358.0f;

        addKnob (c.rate, prefix + "rate", title + " RATE", kx, 345.0f + kLfoDy, 25.0f);
        // Anzeige: Hz, bei Sync der Notenwert; ganz links DC (mit Invert -DC)
        c.rate.textFromValueFunction = [&c] (double v)
        {
            const juce::String text = MaermelAudioProcessor::lfoRateText ((float) v, c.sync.getToggleState());
            return (text == "DC" && c.invert.getToggleState()) ? juce::String ("-DC") : text;
        };
        addValueBox (c.rate, kx, 381.0f + kLfoDy, 66.0f, lfoValue);

        placeButton (c.sync, { kx + 50.0f, 323.0f + kLfoDy, 100.0f, 14.0f });
        placeButton (c.env,  { kx + 50.0f, 347.0f + kLfoDy, 100.0f, 14.0f });
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (params, prefix + "sync", c.sync));
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (params, prefix + "env", c.env));
        // Meldungen nur beim Anklicken (nicht, wenn ein geladenes Instrument die Taste setzt)
        c.sync.onClick = [this, &c, title] { c.rate.updateText(); showStatusIfUser (c.sync, [&c, title] { return title + (c.sync.getToggleState() ? " SYNC ON" : " SYNC OFF"); }); };
        c.env.onClick  = [this, &c, title] { showStatusIfUser (c.env, [&c, title] { return title + (c.env.getToggleState() ? " ENV MODE ON" : " ENV MODE OFF"); }); };

        addHFader (c.delay, prefix + "delay", title + " DELAY", dx0, dx1, 392.0f + kLfoDy, false, false);

        const juce::StringArray waveNames { "TRI", "SIN", "SAW", "SQR" };
        std::vector<juce::TextButton*> list;
        float x = wx;
        const float waveY = kLineLabel2 - 3.0f;
        for (size_t w = 0; w < 4; ++w)
        {
            c.waves[w].setButtonText (waveNames[(int) w]);
            placeButton (c.waves[w], { x, waveY, 30.0f, 19.0f });
            x += 32.0f;
            list.push_back (&c.waves[w]);
        }
        auto group = std::make_unique<ChoiceButtons> (*params.getParameter (prefix + "wave"), list);
        group->onChoice = [this, title, waveNames] (int i) { showStatus (title + " " + waveNames[i]); };
        choiceGroups.push_back (std::move (group));

        placeButton (c.invert, { x + 4.0f, waveY, 44.0f, 19.0f });
        c.invert.setClickingTogglesState (true);
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (params, prefix + "invert", c.invert));
        c.invert.onClick = [this, &c, title] { c.rate.updateText(); showStatusIfUser (c.invert, [&c, title] { return title + (c.invert.getToggleState() ? " INVERT ON" : " INVERT OFF"); }); };
    }

    {
        const char* ids[] = { "lfopitch", "lfofilter", "lfovolume", "lfopan" };
        const char* names[] = { "LFO PITCH", "LFO FILTER", "LFO VOLUME", "LFO PAN" };
        const float first = 337.0f, last = kLineLabel2 - 10.0f;
        for (size_t i = 0; i < routes.size(); ++i)
            addHFader (routes[i], ids[i], names[i], 214.0f, 342.0f, first + (last - first) * (float) i / 3.0f, true, true);
    }

    // ---------------- Hüllkurven ----------------
    {
        const float xs[2][6] = { { 586, 622, 658, 694, 730, 770 }, { 836, 868, 900, 932, 964, 997 } };
        for (size_t e = 0; e < 2; ++e)
        {
            const juce::String prefix = (e == 0) ? "f" : "";
            const juce::String name = (e == 0) ? "F.ENV " : "AMP ENV ";
            const juce::String ids[] = { prefix + "attack", prefix + "decay", prefix + "sustain", prefix + "sustaintime",
                                         prefix + "release", (e == 0) ? juce::String ("fkey") : juce::String ("ampkey") };
            const char* names[] = { "A", "D", "S", "T", "R", "KEY" };
            for (size_t i = 0; i < 6; ++i)
                addVFader (envs[e][i], ids[i], name + names[i], xs[e][i], 318.0f, 452.0f, i == 3);

            // VelA / VelD: hellblauer Pfeil rechts am A- bzw. D-Fader
            const juce::String velIds[] = { prefix + "velattack", prefix + "veldecay" };
            const char* velNames[] = { "VELA", "VELD" };
            for (size_t v = 0; v < 2; ++v)
            {
                auto marker = std::make_unique<VelMarker> (envs[e][v], *params.getParameter (velIds[v]));
                marker->setBounds (juce::roundToInt (xs[e][v] + 10.0f), 318, 14, 134);
                const juce::String label = name + velNames[v];
                marker->onChange = [this, label] (float f) { showStatus (label + " " + juce::String (juce::roundToInt (f * 100.0f)) + "%"); };
                addAndMakeVisible (*marker);
                velMarkers.push_back (std::move (marker));
            }
        }
    }

    shownControlFeedback = audioProcessor.getControlFeedbackCount();
    shownExportState = audioProcessor.getExportStatus().state;   // alten Export-Stand nicht erneut melden
    shownMidiNotes = audioProcessor.getMidiNoteCount();            // MIDI-Anzeige: nur neue Noten
    midiGlowTimer.tick = [this] { updateMidiGlow(); };
    midiGlowTimer.startTimerHz (30);
    addMouseListener (&inputWatcher, true);   // merkt sich, welches Bedienelement der Anwender gerade benutzt
    setWantsKeyboardFocus (true);   // für Cmd/Strg+C und +V
    setSize (kWidth, kHeight);
    userScale = (float) library->getSettings().getDoubleValue (SettingsComponent::kGuiScaleKey, 1.0);
    blueBackground = library->getSettings().getBoolValue (SettingsComponent::kBlueBackgroundKey, true);   // Standard: Blau
    applyScale();

    // Warnungen beim Öffnen des Fensters (seit 1.0.3): Die Zähler im Prozessor zählen alle Warnungen seit dem Start der
    // Instanz. Ein neues Fenster begann bei 0 und zeigte deshalb eine alte Warnung erneut – auch wenn längst ein anderes
    // Instrument ohne Fehler geladen war (z. B. Fenster in Logic schließen und wieder öffnen). Jetzt: Zähler übernehmen
    // (nur neue Warnungen lösen etwas aus) und nur die Warnung zeigen, die zum aktuellen Instrument gehört.
    shownMissingAlert = audioProcessor.getMissingSamplesAlert();
    shownMemoryAlert = audioProcessor.getMemoryAlert();
    shownMissingFileAlert = audioProcessor.getMissingFileAlert();
    shownMissingInstrumentAlert = audioProcessor.getMissingInstrumentAlert();
    const juce::String currentAlert = audioProcessor.getCurrentAlert();
    if (currentAlert.isNotEmpty())
        showAlert (currentAlert);

    startTimerHz (20);
    timerCallback();
}

MaermelAudioProcessorEditor::~MaermelAudioProcessorEditor()
{
    stopTimer();
    midiGlowTimer.stopTimer();
    removeMouseListener (&inputWatcher);
    // Verbindungen zu den Parametern zuerst lösen, solange alle Regler garantiert noch existieren
    aboutPanel.reset();
    notesPanel.reset();
    settingsViewport.reset();
    settingsPanel.reset();
    sliderAttachments.clear();
    buttonAttachments.clear();
    choiceGroups.clear();
    velMarkers.clear();
    valueBoxes.clear();
    filterType.reset();
    setLookAndFeel (nullptr);
}

void MaermelAudioProcessorEditor::attachSlider (juce::Slider& s, const juce::String& id, const juce::String& displayName)
{
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (s);
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, id, s));

    // Doppelklick: zurück auf den Standardwert des Parameters
    if (auto* param = audioProcessor.parameters.getParameter (id))
        s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    // Beim Bedienen erscheint Name und Wert im Display (nicht bei Automation, Instrumentwechsel, Preset oder MIDI) –
    // schon beim bloßen Anklicken, ohne etwas zu verstellen (wichtig für Regler ohne Beschriftung wie GLIDE).
    // Bis 16.10.2026 genügte es, dass die Maus über dem Regler stand – dann erschien nach einem Instrumentwechsel
    // z. B. „AMP ENV A 1.19 ms“ statt des Instrumentnamens.
    s.onValueChange = [this, &s, displayName]
    {
        const auto text = [&s, displayName] { return displayName + " " + s.getTextFromValue (s.getValue()); };
        if (s.isMouseButtonDown())
            showStatus (text());
        else
            showStatusIfUser (s, text);   // Mausrad
    };
    s.onDragStart = [this, &s, displayName]
    {
        showStatus (displayName + " " + s.getTextFromValue (s.getValue()));
    };
}

void MaermelAudioProcessorEditor::addKnob (juce::Slider& s, const juce::String& id, const juce::String& displayName,
                                           float cx, float cy, float radius)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    const float side = 2.0f * (radius + 7.0f);
    s.setBounds (juce::Rectangle<float> (side, side).withCentre ({ cx, cy }).toNearestInt());
    attachSlider (s, id, displayName);
}

void MaermelAudioProcessorEditor::addVFader (juce::Slider& s, const juce::String& id, const juce::String& displayName,
                                             float cx, float y0, float y1, bool bipolar)
{
    s.setSliderStyle (juce::Slider::LinearVertical);
    s.setSliderSnapsToMousePosition (false);     // Griff wird relativ gezogen, springt nicht zum Klickpunkt
    s.getProperties().set ("bipolar", bipolar);
    s.setBounds (juce::roundToInt (cx - 20.0f), juce::roundToInt (y0), 40, juce::roundToInt (y1 - y0));
    attachSlider (s, id, displayName);
}

void MaermelAudioProcessorEditor::addHFader (juce::Slider& s, const juce::String& id, const juce::String& displayName,
                                             float x0, float x1, float cy, bool bipolar, bool compact)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setSliderSnapsToMousePosition (false);
    s.getProperties().set ("bipolar", bipolar);
    s.getProperties().set ("compact", compact);
    const int h = compact ? 24 : 30;
    s.setBounds (juce::roundToInt (x0), juce::roundToInt (cy) - h / 2, juce::roundToInt (x1 - x0), h);
    attachSlider (s, id, displayName);
}

void MaermelAudioProcessorEditor::placeButton (juce::Button& b, juce::Rectangle<float> key)
{
    b.setBounds (key.expanded (LookAndFeel::kButtonGlowMargin).toNearestInt());
    addAndMakeVisible (b);
}

void MaermelAudioProcessorEditor::addValueBox (juce::Slider& s, float cx, float y, float width,
                                               std::function<juce::String (juce::Slider&)> format)
{
    auto box = std::make_unique<ValueBox> (s, std::move (format));
    box->setBounds (juce::Rectangle<float> (cx - width / 2.0f, y - 1.0f, width, 19.0f).toNearestInt());
    addAndMakeVisible (*box);
    valueBoxes.push_back (std::move (box));
}

// ===========================================================================
// Display

void MaermelAudioProcessorEditor::showStatus (const juce::String& text)
{
    // Eine rote Warnung nicht durch eine gewöhnliche Meldung verdrängen, solange sie noch steht
    if (statusIsAlert && juce::Time::getMillisecondCounter() < statusUntil)
        return;
    statusText = text;
    statusIsAlert = false;
    statusUntil = juce::Time::getMillisecondCounter() + kStatusMs;
    refreshDisplay();
}

void MaermelAudioProcessorEditor::showStatusIfUser (juce::Component& c, std::function<juce::String()> text)
{
    if (inputWatcher.touched (c))
    {
        showStatus (text());
        return;
    }
    // Die Maus-Meldung erreicht inputWatcher erst NACH der Komponente selbst (Mausrad, Loslassen eines Knopfs) –
    // deshalb gleich danach noch einmal prüfen. Bei Wertänderungen ohne Bedienung bleibt das Display unberührt.
    juce::Component::SafePointer<MaermelAudioProcessorEditor> safe (this);
    juce::Component::SafePointer<juce::Component> component (&c);
    juce::MessageManager::callAsync ([safe, component, text]
    {
        if (safe != nullptr && component != nullptr && safe->inputWatcher.touched (*component))
            safe->showStatus (text());
    });
}

void MaermelAudioProcessorEditor::showAlert (const juce::String& text)
{
    statusText = text;
    statusIsAlert = true;
    statusUntil = juce::Time::getMillisecondCounter() + 2 * kStatusMs;   // 3 s
    refreshDisplay();
}

void MaermelAudioProcessorEditor::refreshDisplay()
{
    const bool statusActive = statusText.isNotEmpty() && juce::Time::getMillisecondCounter() < statusUntil;
    if (! statusActive)
    {
        statusText.clear();
        statusIsAlert = false;
    }
    // Rote Warnung: Pfeile ausblenden; normale Meldungen und der Instrumentname mit Pfeilen
    display.setText (statusActive ? statusText : audioProcessor.getInstrumentName(),
                     statusIsAlert ? colours::red : colours::cyan, ! statusIsAlert);
}

juce::String MaermelAudioProcessorEditor::fxStatusText() const
{
    const float v = (float) crush.getValue();
    if (audioProcessor.isChorusMode())
        return v <= 0.0f ? juce::String ("CHORUS OFF") : "CHORUS " + juce::String (juce::roundToInt (v)) + " %";
    // Samplerate dieser Instanz, nicht die globale Anzeige der DAW (seit 1.0.3; siehe lofiText)
    return "LO-FI " + MaermelAudioProcessor::lofiText (v, false, audioProcessor.getSampleRate());
}

// Umschalten nur hier (Bedienung in der Oberfläche): erst den Anteil auf 0 % (mit Change-Gesture, damit die DAW es als
// Bedienung aufzeichnet), dann den Modus. Die Engine blendet den bisherigen Effekt aus und schaltet erst danach um;
// es rechnet immer höchstens einer. Beim Laden von Presets oder Projekten passiert das nicht – dort gilt der Anteil.
void MaermelAudioProcessorEditor::toggleFxMode()
{
    auto* amount = audioProcessor.parameters.getParameter (maermel::ids::lofi);
    auto* mode = audioProcessor.parameters.getParameter ("fxmode");
    if (amount == nullptr || mode == nullptr)
        return;
    const bool toChorus = ! audioProcessor.isChorusMode();
    amount->beginChangeGesture();
    amount->setValueNotifyingHost (0.0f);
    amount->endChangeGesture();
    mode->beginChangeGesture();
    mode->setValueNotifyingHost (toChorus ? 1.0f : 0.0f);
    mode->endChangeGesture();
    shownChorus = toChorus;
    fxButton.setButtonText (toChorus ? "CHORUS" : "LO-FI");
    showStatus (toChorus ? "CHORUS" : "LO-FI");
}

void MaermelAudioProcessorEditor::timerCallback()
{
    // Umschalter nachführen (Preset, Projekt, Einstellungen einfügen)
    if (audioProcessor.isChorusMode() != shownChorus)
    {
        shownChorus = audioProcessor.isChorusMode();
        fxButton.setButtonText (shownChorus ? "CHORUS" : "LO-FI");
    }
    const int version = audioProcessor.getInstrumentVersion();
    const int ks = audioProcessor.getActiveKeyswitch();

    if (version != shownVersion)
    {
        // Neues Instrument (Hinweise dazu stehen im Logfile, nicht auf der Oberfläche)
        shownVersion = version;
        shownKeyswitch = ks;
        fixButton.setToggleState (audioProcessor.isFixActive(), juce::dontSendNotification);   // z. B. nach Projekt-Laden
        repaint (kFixLedArea);
    }
    else if (ks != shownKeyswitch)
    {
        shownKeyswitch = ks;
        if (ks >= 0)
            showStatus ("KEYSWITCH " + noteName (ks));
    }

    // Export im Hintergrund: Fortschritt und Ergebnis im Display
    {
        const auto status = audioProcessor.getExportStatus();
        using Proc = MaermelAudioProcessor;
        if (status.state == Proc::exportRunning && status.done != shownExportDone && status.total > 0)
            showStatus ("EXPORT " + juce::String (status.done) + "/" + juce::String (status.total));
        if (status.state != shownExportState)
        {
            if (status.state == Proc::exportFinished)
                showStatus ("EXPORTED " + audioProcessor.getExportMessage().toUpperCase());
            else if (status.state == Proc::exportFailed)
                showAlert ("EXPORT FAILED - SEE LOG");
        }
        shownExportState = status.state;
        shownExportDone = status.state == Proc::exportRunning ? status.done : -1;
    }

    // LED über AT→WHEEL: Zustand kommt auch aus geladenen Presets (Instrumentwechsel)
    if (atWheelButton.getToggleState() != shownAtWheel)
    {
        shownAtWheel = atWheelButton.getToggleState();
        repaint (kAtWheelLedArea);
    }

    // Rote LED über EDIT: Live-Edit-Überwachung beginnt mit EDIT und endet beim Instrumentwechsel
    if (audioProcessor.isLiveEditActive() != shownLiveEdit)
    {
        shownLiveEdit = audioProcessor.isLiveEditActive();
        repaint (kEditLedArea);
    }

    // Direkte Steuerung per MIDI-Controller: Controller, Ziel und Wert kurz im Display zeigen
    // (passt es nicht vollständig, entfällt die Controllernummer)
    const int feedback = audioProcessor.getControlFeedbackCount();
    if (feedback != shownControlFeedback)
    {
        shownControlFeedback = feedback;
        const int slot = audioProcessor.getControlFeedbackSlot();
        const int targetIndex = slot >= 0 ? audioProcessor.getControlTarget (slot) : 0;
        if (targetIndex > 0 && targetIndex < maermel::control::kNumTargets)
        {
            const auto& target = maermel::control::kTargets[targetIndex];
            if (auto* param = audioProcessor.parameters.getParameter (target.paramId))
            {
                // Lo-Fi/Chorus: Name und Wert je nach gewähltem Effekt („CHORUS 50 %“)
                const juce::String text = std::strcmp (target.paramId, maermel::ids::lofi) == 0
                                            ? fxStatusText()
                                            : juce::String (target.display) + " " + param->getCurrentValueAsText();
                const juce::String withCc = "CC" + juce::String (audioProcessor.getControlCc (slot)) + " " + text;
                showStatus (display.fits (withCc) ? withCc : text);   // CC-Nummer nur, wenn sie noch Platz hat
            }
        }
    }

    // Fehlende Samples: rote Meldung (nur einmal je Ladevorgang)
    const int missingAlert = audioProcessor.getMissingSamplesAlert();
    if (missingAlert != shownMissingAlert)
    {
        shownMissingAlert = missingAlert;
        showAlert (audioProcessor.wereNoSamplesLoaded() ? "NO SAMPLES !!" : "MISSING SAMPLES !!");
    }
    // SFZ-/EXS-Datei nicht gefunden (z. B. gelöscht, die Liste war noch nicht neu aufgebaut); Klick aufs Display
    // zeigt wie bei fehlenden Samples die Loading notes
    const int missingFileAlert = audioProcessor.getMissingFileAlert();
    if (missingFileAlert != shownMissingFileAlert)
    {
        shownMissingFileAlert = missingFileAlert;
        showAlert (audioProcessor.wasMissingFileExs() ? "NO EXS !!" : "NO SFZ !!");
    }
    // Instrument eines DAW-Projekts auf diesem Rechner nicht gefunden (Klick aufs Display: gespeicherter Pfad, Kandidaten)
    const int missingInstrumentAlert = audioProcessor.getMissingInstrumentAlert();
    if (missingInstrumentAlert != shownMissingInstrumentAlert)
    {
        shownMissingInstrumentAlert = missingInstrumentAlert;
        showAlert ("INSTRUMENT NOT FOUND !!");
    }
    // Instrument zu groß für den Arbeitsspeicher (Klick aufs Display zeigt die Einzelheiten)
    const int memoryAlert = audioProcessor.getMemoryAlert();
    if (memoryAlert != shownMemoryAlert)
    {
        shownMemoryAlert = memoryAlert;
        showAlert ("NOT ENOUGH MEMORY !!");
    }

    refreshDisplay();
    // LFO-Rate hängt zusätzlich von Sync/Invert ab, daher regelmäßig prüfen. Neu gezeichnet wird nur ein Feld,
    // dessen Text sich wirklich geändert hat (spart auf älteren Rechnern Zeichenarbeit, 20-mal pro Sekunde).
    for (auto& box : valueBoxes)
        box->refresh();
}

// ===========================================================================
// Library

void MaermelAudioProcessorEditor::showLibraryMenu()
{
    library->requestRefresh();   // läuft im Hintergrund; unveränderte Ordner kosten fast nichts

    const juce::String filter = audioProcessor.libraryFilter;
    const auto view = LibraryIndex::filtered (library->getVisibleSnapshot(), filter);

    juce::PopupMenu menu;
    menuFiles.clearQuick();

    if (filter.isEmpty())
        menu.addItem (kSearchItem, "Search ...");
    else
        menu.addItem (kSearchItem, "Show all  (search: \"" + filter + "\")");
    // Öffnet immer den Hauptordner der Library im Finder bzw. Explorer
    menu.addItem (kOpenLibraryItem, "Open library folder ...",
                  library->getRootFolder().isDirectory());
    // Export: nur, wenn ein Instrument geladen ist und alle Samples gefunden wurden
    if (audioProcessor.canExportInstrument())
        menu.addItem (kExportItem, "Export current instrument ...");
    // Ladehinweise des aktuellen Instruments: Eintrag nur, wenn es welche gibt (sonst gar nicht sichtbar)
    if (audioProcessor.getLoadingNotes().isNotEmpty())
        menu.addItem (kNotesItem, "Show loading notes ...");
    menu.addSeparator();

    if (! library->getRootFolder().isDirectory())
        menu.addItem (kChooseLibraryItem, "Set library folder ...");
    else if (view == nullptr)
        menu.addItem (-1, "Scanning library ...", false);
    else if (view->countInstruments() == 0)
        menu.addItem (-1, filter.isEmpty() ? "No instruments found" : "No matches", false);
    else
        addFolderItems (menu, *view, audioProcessor.getInstrumentFile());

    juce::Component::SafePointer<MaermelAudioProcessorEditor> safe (this);
    menu.showMenuAsync (menuOptions().withTargetComponent (&display),
                        [safe] (int result) { if (safe != nullptr) safe->menuItemChosen (result); });
}

juce::PopupMenu::Options MaermelAudioProcessorEditor::menuOptions()
{
    auto options = juce::PopupMenu::Options();
    if (kMenusInsidePluginWindow)
        options = options.withParentComponent (this);   // Menü als Teil des Plugin-Fensters (siehe kMenusInsidePluginWindow)
    return options;
}

void MaermelAudioProcessorEditor::addFolderItems (juce::PopupMenu& menu, const LibraryIndex::Folder& folder,
                                                  const juce::File& current)
{
    for (const auto& sub : folder.subfolders)
    {
        if (sub.countInstruments() == 0)
            continue;
        juce::PopupMenu subMenu;
        addFolderItems (subMenu, sub, current);
        // Häkchen am Ordner, in dem das aktuelle Instrument liegt
        menu.addSubMenu (sub.name, subMenu, true, juce::Image(), current.isAChildOf (sub.directory));
    }

    for (const auto& file : folder.instruments)
    {
        menuFiles.add (file);
        const juce::String label = file.getFileNameWithoutExtension() + (file.hasFileExtension ("exs") ? "  (EXS)" : "");
        menu.addItem (kFirstFileItem + menuFiles.size() - 1, label, true, file == current);
    }
}

void MaermelAudioProcessorEditor::menuItemChosen (int result)
{
    if (result == kSearchItem)
    {
        if (audioProcessor.libraryFilter.isEmpty())
        {
            showSearchDialog();
        }
        else
        {
            audioProcessor.libraryFilter.clear();
            showStatus ("ALL INSTRUMENTS");
        }
    }
    else if (result == kChooseLibraryItem)
    {
        toggleSettings();
    }
    else if (result == kExportItem)
    {
        exportInstrument();
    }
    else if (result == kNotesItem)
    {
        showNotes();
    }
    else if (result == kOpenLibraryItem)
    {
        const juce::File root = library->getRootFolder();
        // Finder (Mac), Explorer (Windows), Dateimanager über xdg-open (Linux) – siehe ExternalLaunch.h
        if (! maermel::external::openFolder (root))
            showStatus ("LIBRARY FOLDER NOT FOUND");
    }
    else if (result >= kFirstFileItem && result - kFirstFileItem < menuFiles.size())
    {
        audioProcessor.loadInstrumentFile (menuFiles[result - kFirstFileItem]);
    }
}

void MaermelAudioProcessorEditor::stepInstrument (int delta)
{
    audioProcessor.stepInstrument (delta);   // gleiche Logik wie bei der MIDI-Umschaltung
}

void MaermelAudioProcessorEditor::showSearchDialog()
{
    auto* dialog = new juce::AlertWindow ("Search", "Text in instrument name:", juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("text", {});
    dialog->addButton ("Search", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MaermelAudioProcessorEditor> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, dialog] (int result)
    {
        if (result != 1 || safe == nullptr)
            return;

        const juce::String text = dialog->getTextEditorContents ("text").trim();
        if (text.isEmpty())
            return;

        safe->audioProcessor.libraryFilter = text;
        const auto view = LibraryIndex::filtered (safe->library->getVisibleSnapshot(), text);
        const int hits = view != nullptr ? view->countInstruments() : 0;
        safe->showStatus (juce::String (hits) + " FOUND: " + text);
        if (hits > 0)
            safe->showLibraryMenu();                   // Ergebnis gleich zeigen
    }), true);

    // Cursor gleich ins Eingabefeld setzen, damit man sofort tippen kann. Ein zweites Mal kurz danach,
    // weil das Fenster in manchen DAWs erst einen Moment später wirklich sichtbar und aktiv ist.
    juce::Component::SafePointer<juce::AlertWindow> safeDialog (dialog);
    auto focusTextField = [safeDialog]
    {
        if (safeDialog == nullptr)
            return;
        safeDialog->toFront (true);
        if (auto* field = safeDialog->getTextEditor ("text"))
            field->grabKeyboardFocus();
    };
    focusTextField();
    juce::Timer::callAfterDelay (80, focusTextField);
}

void MaermelAudioProcessorEditor::toggleSettings()
{
    if (settingsPanel != nullptr)
    {
        settingsViewport.reset();
        settingsPanel.reset();
        pathButton.setToggleState (false, juce::dontSendNotification);
        return;
    }
    pathButton.setToggleState (true, juce::dontSendNotification);   // leuchtet blau: hier wieder schließen

    settingsPanel = std::make_unique<SettingsComponent> (audioProcessor);
    juce::Component::SafePointer<MaermelAudioProcessorEditor> safe (this);
    settingsPanel->onClose = [safe]
    {
        // Nicht innerhalb des eigenen Klick-Handlers löschen, sondern kurz danach
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
            {
                safe->settingsViewport.reset();
                safe->settingsPanel.reset();
                safe->pathButton.setToggleState (false, juce::dontSendNotification);
            }
        });
    };

    settingsPanel->onGuiScaleChanged = [safe] (float scale) { if (safe != nullptr) safe->setGuiScale (scale); };
    settingsPanel->onBackgroundChanged = [safe] (bool blue)
    {
        if (safe != nullptr)
        {
            safe->blueBackground = blue;
            safe->repaint();   // Hintergrund wird beim Zeichnen neu erzeugt
        }
    };

    // Die Tafel ist höher als das Fenster: in einem Bildlaufbereich unter der Kopfzeile
    settingsViewport = std::make_unique<juce::Viewport>();
    settingsViewport->setScrollBarsShown (true, false);
    addAndMakeVisible (*settingsViewport);
    settingsViewport->setBounds (getLocalBounds().withTrimmedTop (52).reduced (10, 0).withTrimmedBottom (8));
    settingsPanel->setSize (settingsViewport->getWidth() - settingsViewport->getScrollBarThickness(), 870);
    settingsViewport->setViewedComponent (settingsPanel.get(), false);
}

void MaermelAudioProcessorEditor::chooseFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load instrument or preset", audioProcessor.getInstrumentFile(),
                                                       library->isShowingExs() ? "*.sfz;*.sfzp;*.exs" : "*.sfz;*.sfzp",
                                                       maermel::kUseNativeFileDialogs);   // Linux: siehe FileDialogs.h
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<MaermelAudioProcessorEditor> safe (this);
    fileChooser->launchAsync (flags, [safe] (const juce::FileChooser& chooser)
    {
        const juce::File file = chooser.getResult();
        if (safe != nullptr && file.existsAsFile())
            safe->audioProcessor.loadInstrumentFile (file);
    });
}

void MaermelAudioProcessorEditor::exportInstrument()
{
    // Speichern-Dialog: Ordner und Name wählen (z. B. im Gig-Ordner); das System fragt vor dem Überschreiben.
    // Daneben entstehen die .sfzp und der Ordner „<Name> Samples“.
    auto& settings = library->getSettings();
    juce::File start (settings.getValue ("lastExportFolder"));
    if (! start.isDirectory())
        start = library->getRootFolder();
    const juce::File suggestion = start.getChildFile (audioProcessor.getInstrumentFile().getFileNameWithoutExtension() + ".sfz");

    fileChooser = std::make_unique<juce::FileChooser> ("Export instrument with samples", suggestion, "*.sfz",
                                                       maermel::kUseNativeFileDialogs);
    juce::Component::SafePointer<MaermelAudioProcessorEditor> safe (this);
    const int flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::warnAboutOverwriting;
    fileChooser->launchAsync (flags, [safe] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr)
            return;
        const juce::File target = chooser.getResult();
        if (target == juce::File())
            return;                                            // abgebrochen
        safe->library->getSettings().setValue ("lastExportFolder", target.getParentDirectory().getFullPathName());
        safe->audioProcessor.startExport (target.withFileExtension ("sfz"));
        safe->showStatus ("EXPORT ...");
    });
}

// ---------------------------------------------------------------------------
// Drag & Drop aus Finder/Explorer

juce::File MaermelAudioProcessorEditor::droppableFile (const juce::StringArray& files) const
{
    // Die erste ladbare Datei zählt (bei mehreren gezogenen Dateien)
    const juce::String extensions = library->isShowingExs() ? "sfz;sfzp;exs" : "sfz;sfzp";
    for (const auto& path : files)
    {
        const juce::File f (path);
        if (f.existsAsFile() && f.hasFileExtension (extensions))
            return f;
    }
    return {};
}

bool MaermelAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return droppableFile (files) != juce::File();
}

void MaermelAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    fileDragActive = true;
    repaint();
}

void MaermelAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    fileDragActive = false;
    repaint();
}

void MaermelAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    fileDragActive = false;
    repaint();
    const juce::File file = droppableFile (files);
    if (file != juce::File())
        audioProcessor.loadInstrumentFile (file);   // wie LOAD: .sfzp bringt seine Einstellungen mit
}

// ---------------------------------------------------------------------------
// MIDI-Anzeige im Display

namespace
{
constexpr float kGlowCentreY = 42.8f;     // knapp über der Unterkante des Displays (y 10 … 46), unter der Schrift
constexpr float kGlowHalfWidth = 0.36f;   // halbe Länge als Anteil der Displaybreite
constexpr double kGlowFadeMs = 400.0;     // Ausklingen: gut sichtbar bis ≈ 300 ms, nach 400 ms ganz aus
constexpr float kGlowMin = 0.35f;         // Helligkeit bei Anschlag 1 (Vorschau A, noch gut erkennbar)
constexpr float kGlowMax = 0.66f;         // Obergrenze: Helligkeit bei Anschlag ≈ 72 (harte Anschläge nicht heller)
constexpr int kGlowFullVelocity = 72;     // ab diesem Anschlag volle (gedeckelte) Helligkeit
}

juce::Rectangle<int> MaermelAudioProcessorEditor::midiGlowArea() const
{
    return juce::Rectangle<float> (kDisplay.getX(), kGlowCentreY - 4.0f, kDisplay.getWidth(), 8.0f)
               .getSmallestIntegerContainer();
}

void MaermelAudioProcessorEditor::updateMidiGlow()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float before = midiGlow;
    const int notes = audioProcessor.getMidiNoteCount();
    if (notes != shownMidiNotes)
    {
        // Neue Note: sofort an (nie dunkler als gerade), Ausklingen beginnt von vorn
        shownMidiNotes = notes;
        const int velocity = juce::jlimit (1, kGlowFullVelocity, audioProcessor.getMidiNoteVelocity());
        const float v = (float) (velocity - 1) / (float) (kGlowFullVelocity - 1);
        midiGlowPeak = juce::jmax (midiGlow, kGlowMin + (kGlowMax - kGlowMin) * v);
        midiGlowStartMs = now;
    }
    // Gleichmäßiges Ausklingen: nach 100 ms 75 %, 200 ms 50 %, 300 ms 25 % (noch gut sichtbar), 400 ms aus
    const double t = (now - midiGlowStartMs) / kGlowFadeMs;
    midiGlow = t >= 1.0 ? 0.0f : midiGlowPeak * (float) (1.0 - t);
    if (midiGlow != before)
        repaint (midiGlowArea());                 // nur der schmale Streifen, und nur solange er sich ändert
}

void MaermelAudioProcessorEditor::paintMidiGlow (juce::Graphics& g) const
{
    if (midiGlow <= 0.0f)
        return;
    // Rot wie die Mini-LEDs; in der Mitte am hellsten, zu den Enden weich auslaufend (wie in der Vorschau)
    const juce::Colour red (255, 70, 55);
    const float cx = kDisplay.getCentreX(), half = kDisplay.getWidth() * kGlowHalfWidth;
    auto stripe = [&] (float height, float alpha)
    {
        juce::ColourGradient grad (red.withAlpha (0.0f), cx - half, 0.0f, red.withAlpha (0.0f), cx + half, 0.0f, false);
        for (int i = 1; i < 16; ++i)
        {
            const float x = (float) i / 16.0f * 2.0f - 1.0f;                  // −1 … +1
            const float profile = std::pow (std::cos (x * juce::MathConstants<float>::halfPi), 1.6f);
            grad.addColour ((double) i / 16.0, red.withAlpha (alpha * profile));
        }
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (cx - half, kGlowCentreY - height * 0.5f, half * 2.0f, height));
    };
    stripe (5.2f, 0.92f * 0.22f * midiGlow);      // leichter Schein
    stripe (2.0f, 0.92f * midiGlow);              // Kern: 2 Punkte hoch
}

void MaermelAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    paintMidiGlow (g);
    if (! fileDragActive)
        return;
    // Blauer Rahmen wie das Leuchten der Tasten: „hier loslassen lädt das Instrument“
    g.setColour (juce::Colour (0xff4fc3f7).withAlpha (0.85f));
    g.drawRect (getLocalBounds(), 3);
    g.setColour (juce::Colour (0xff4fc3f7).withAlpha (0.08f));
    g.fillRect (getLocalBounds());
}

void MaermelAudioProcessorEditor::showNotes()
{
    if (notesPanel != nullptr)
        return;
    notesPanel = std::make_unique<NotesPanel> (audioProcessor.getLoadingNotesInstrument(), audioProcessor.getLoadingNotes());
    juce::Component::SafePointer<MaermelAudioProcessorEditor> safe (this);
    notesPanel->onClose = [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->notesPanel.reset(); });
    };
    addAndMakeVisible (*notesPanel);
    notesPanel->setBounds (getLocalBounds());
}

void MaermelAudioProcessorEditor::showAbout()
{
    if (aboutPanel != nullptr)
        return;
    aboutPanel = std::make_unique<AboutPanel> (audioProcessor.wrapperType);
    juce::Component::SafePointer<MaermelAudioProcessorEditor> safe (this);
    aboutPanel->onClose = [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->aboutPanel.reset(); });
    };
    addAndMakeVisible (*aboutPanel);
    aboutPanel->setBounds (getLocalBounds());
}

void MaermelAudioProcessorEditor::applyScale()
{
    const float scale = hostScale * userScale;
    // Punktmatrix: Ausgleich bei Größen mit Bruchteil (125 %, 150 %), damit sie nicht dunkler wirkt
    DotText::fractionalScale = std::abs (scale - std::round (scale)) > 0.01f;
    AudioProcessorEditor::setScaleFactor (scale);   // skaliert alles, Positionen bleiben gleich
    repaint();
}

void MaermelAudioProcessorEditor::setScaleFactor (float newScale)
{
    hostScale = newScale;   // z. B. Windows bei hoher Bildschirmauflösung
    applyScale();
}

void MaermelAudioProcessorEditor::setGuiScale (float scale)
{
    if (std::abs (scale - userScale) < 0.001f)
        return;
    userScale = scale;
    if (settingsPanel != nullptr)
        settingsPanel->showGuiScale (scale);
    library->getSettings().setValue (SettingsComponent::kGuiScaleKey, (double) scale);
    library->getSettings().saveIfNeeded();
    applyScale();
}


// ===========================================================================
// Maus und Tastatur, Einstellungen kopieren und einfügen

void MaermelAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showContextMenu();
    else if (kLogo.contains (e.position))
        showAbout();
    else if (kEditLedArea.toFloat().contains (e.position) && audioProcessor.isLiveEditActive())
    {
        // Klick auf die rote LED über EDIT beendet Live-Edit (Einschalten nur über EDIT)
        audioProcessor.setLiveEditActive (false);
        shownLiveEdit = false;
        repaint (kEditLedArea);
        showStatus ("LIVE EDIT OFF");
    }
    else
        grabKeyboardFocus();   // Klick auf eine freie Stelle: Tastenkürzel gehen an Märmel
}

bool MaermelAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // commandModifier ist auf dem Mac Cmd, unter Windows Strg
    if (! key.getModifiers().isCommandDown() || key.getModifiers().isShiftDown() || key.getModifiers().isAltDown())
        return false;

    const int code = key.getKeyCode();
    if (code == 'C' || code == 'c')
    {
        copySettings();
        return true;
    }
    if (code == 'V' || code == 'v')
    {
        pasteSettings();
        return true;
    }
    return false;
}

void MaermelAudioProcessorEditor::copySettings()
{
    juce::SystemClipboard::copyTextToClipboard (audioProcessor.copySettingsAsText());
    showStatus ("SETTINGS COPIED");
}

void MaermelAudioProcessorEditor::pasteSettings()
{
    if (audioProcessor.pasteSettingsFromText (juce::SystemClipboard::getTextFromClipboard()))
        showStatus ("SETTINGS PASTED");
    else
        showStatus ("NO SETTINGS IN CLIPBOARD");
}

void MaermelAudioProcessorEditor::showContextMenu()
{
   #if JUCE_MAC
    const juce::String modifier = "Cmd";
   #else
    const juce::String modifier = "Ctrl";
   #endif
    const bool canPaste = MaermelAudioProcessor::isSettingsText (juce::SystemClipboard::getTextFromClipboard());

    juce::PopupMenu menu;
    menu.addItem (1, "Copy settings   (" + modifier + "+C)");
    menu.addItem (2, "Paste settings   (" + modifier + "+V)", canPaste);
    menu.addSeparator();
    menu.addItem (3, "Reset to defaults");
    menu.addSeparator();
    for (int i = 0; i < (int) std::size (SettingsComponent::kGuiScales); ++i)
    {
        const float scale = SettingsComponent::kGuiScales[i];
        menu.addItem (10 + i, "GUI size " + juce::String (juce::roundToInt (scale * 100.0f)) + " %", true,
                      std::abs (scale - userScale) < 0.01f);
    }
    menu.addSeparator();
    menu.addItem (4, "All Notes Off");

    juce::Component::SafePointer<MaermelAudioProcessorEditor> safe (this);
    menu.showMenuAsync (menuOptions(), [safe] (int result)
    {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->copySettings();
        else if (result == 2)
            safe->pasteSettings();
        else if (result == 3)
        {
            safe->audioProcessor.resetSettingsToDefaults();
            safe->showStatus ("DEFAULTS LOADED");
        }
        else if (result == 4)
        {
            safe->audioProcessor.requestAllNotesOff();   // Noten loslassen, Sustain-Pedal aus
            safe->showStatus ("ALL NOTES OFF");
        }
        else if (result >= 10 && result < 10 + (int) std::size (SettingsComponent::kGuiScales))
        {
            safe->setGuiScale (SettingsComponent::kGuiScales[result - 10]);
        }
    });
}

// ===========================================================================
// Zeichnen

void MaermelAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Gebürsteter Hintergrund in der echten Auflösung des Bildschirms (einmal erzeugt, dann zwischengespeichert)
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (background.isNull() || ! juce::approximatelyEqual (scale, backgroundScale) || blueBackground != backgroundIsBlue)
    {
        background = makeBrushedBackground (juce::roundToInt ((float) getWidth() * scale),
                                            juce::roundToInt ((float) getHeight() * scale), blueBackground);
        backgroundScale = scale;
        backgroundIsBlue = blueBackground;
    }
    g.drawImage (background, getLocalBounds().toFloat());

    // ---------------- Kopfzeile ----------------
    drawGlass (g, { 122.0f + kGx, 13.0f, 235.0f, 30.0f }, 4.0f, 2.0f);   // Stimmenwahl: dunkler Rand oben 2 Punkte (Skizze)
    drawLed (g, { 743.5f + kBx, 9.0f }, fixButton.getToggleState());
    drawLed (g, { kAtWheelButton.getCentreX(), 9.0f }, atWheelButton.getToggleState());   // AT→WHEEL je Instrument
    drawLed (g, { 878.0f, 9.0f }, audioProcessor.isLiveEditActive());   // über EDIT: Live-Edit-Überwachung läuft
    drawLabel (g, juce::String::fromUTF8 ("Märmel"), 1018.0f, 15.0f, 21.0f, juce::Justification::right, colours::red, true);

    // ---------------- Sample Start and Tune ----------------
    // Rechter Rand 3 Punkte weiter links (15.10.2026): Abstand zu FILTER jetzt 13 wie FILTER zu AMP (vorher 10);
    // die Knopf-Spalten KEYSHIFT/P.BEND und VEL OFFSET/WHEEL LFO dafür 3 Punkte nach rechts (252 → 255, 319 → 322)
    drawFrame (g, { 14.0f, 60.0f, 358.0f, 225.0f }, "SAMPLE START AND TUNE");
    drawLabel (g, "START", 42.0f, 229.0f, 11.0f);
    drawLabel (g, "VEL", 84.0f, 229.0f, 11.0f);
    drawLabel (g, "C.TUNE", 140.0f, kLineLabel, 11.0f);
    drawLabel (g, "FINE", 186.0f, kLineLabel, 11.0f);
    drawLabel (g, "KEYSHIFT", 255.0f, kKnobTop + 48.0f, 11.0f);
    drawLabel (g, "VEL OFFSET", 322.0f, kKnobTop + 48.0f, 11.0f);
    drawLabel (g, "P.BEND", 255.0f, kLineLabel, 11.0f);
    drawLabel (g, "WHEEL LFO", 322.0f, kLineLabel, 11.0f);

    // ---------------- Filter ----------------
    drawFrame (g, { 385.0f, 60.0f, 412.0f, 225.0f }, "FILTER");
    drawLabel (g, "CUTOFF", 450.0f, 200.0f, 13.0f);
    drawLabel (g, "DRIVE", 568.0f, 128.0f, 11.0f);
    drawLabel (g, "RESO", 537.0f, 202.0f, 11.0f);
    drawLabel (g, "KEY", 599.0f, 202.0f, 11.0f);
    drawLabel (g, "VEL", 652.0f, kLineLabel - 13.0f, 11.0f);   // VEL FIX: zweizeilig wie VEL ENV
    drawLabel (g, "FIX", 652.0f, kLineLabel, 11.0f);
    drawLabel (g, "VEL", 692.0f, kLineLabel - 13.0f, 11.0f);
    drawLabel (g, "ENV", 692.0f, kLineLabel, 11.0f);
    drawLabel (g, "ENV", 731.0f, kLineLabel, 11.0f);
    drawLabel (g, "WHEEL", 771.0f, kLineLabel - 13.0f, 11.0f);
    drawLabel (g, "LFO", 771.0f, kLineLabel, 11.0f);

    // ---------------- Amp ----------------
    drawFrame (g, { 810.0f, 60.0f, 207.0f, 225.0f }, "AMP");
    // LO-FI: statt Beschriftung der Umschalter fxButton (LO-FI/CHORUS)
    drawLabel (g, "WHEEL LFO", 856.0f, kLineLabel, 11.0f);
    drawLabel (g, "VEL", 913.0f, kLineLabel, 11.0f);
    drawLabel (g, "KEY", 953.0f, kLineLabel, 11.0f);
    drawLabel (g, "VOL", 991.0f, kLineLabel, 11.0f);

    // ---------------- LFO ----------------
    drawFrame (g, { 14.0f, 297.0f, 529.0f, 195.0f }, "LFO");
    drawLabel (g, "POLY LFO", 66.0f, 409.0f + kLfoDy, 11.0f);
    drawLabel (g, "DELAY", 155.0f, 409.0f + kLfoDy, 11.0f);
    drawLabel (g, "MONO LFO", 400.0f, 409.0f + kLfoDy, 11.0f);
    drawLabel (g, "DELAY", 489.0f, 409.0f + kLfoDy, 11.0f);
    {
        // Routing-Überschrift: ◂ POLY … MONO ▸
        juce::Path left, right;
        left.addTriangle (216.0f, 312.0f, 222.0f, 308.5f, 222.0f, 315.5f);
        right.addTriangle (340.0f, 312.0f, 334.0f, 308.5f, 334.0f, 315.5f);
        g.setColour (colours::sub);
        g.fillPath (left);
        g.fillPath (right);
        drawLabel (g, "POLY", 226.0f, 306.0f, 10.0f, juce::Justification::left, colours::sub);
        drawLabel (g, "MONO", 330.0f, 306.0f, 10.0f, juce::Justification::right, colours::sub);
        const char* names[] = { "PITCH", "FILTER", "VOLUME", "PAN" };
        const float first = 337.0f, last = kLineLabel2 - 10.0f;
        for (int i = 0; i < 4; ++i)
            drawLabel (g, names[i], 278.0f, first + (last - first) * (float) i / 3.0f + 10.0f, 10.0f);
    }

    // ---------------- Hüllkurven ----------------
    drawFrame (g, { 557.0f, 297.0f, 240.0f, 195.0f }, "FILTER ENVELOPE");
    drawFrame (g, { 811.0f, 297.0f, 206.0f, 195.0f }, "AMP ENVELOPE");
    {
        const float xs[2][6] = { { 586, 622, 658, 694, 730, 770 }, { 836, 868, 900, 932, 964, 997 } };
        const char* names[] = { "A", "D", "S", "T", "R", "KEY" };
        for (const auto& row : xs)
            for (int i = 0; i < 6; ++i)
                drawLabel (g, names[i], row[i], kLineLabel2, 11.0f);
    }
}

void MaermelAudioProcessorEditor::resized()
{
    // Feste Anordnung (alle Positionen werden im Konstruktor gesetzt). Die Einstellungstafel folgt der Fenstergröße.
    if (settingsViewport != nullptr)
        settingsViewport->setBounds (getLocalBounds().withTrimmedTop (52).reduced (10, 0).withTrimmedBottom (8));
    if (aboutPanel != nullptr)
        aboutPanel->setBounds (getLocalBounds());
    if (notesPanel != nullptr)
        notesPanel->setBounds (getLocalBounds());   // wie aboutPanel (seit 1.0.3)
}
