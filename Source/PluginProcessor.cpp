// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// PluginProcessor.cpp – Märmel

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "engine/ExsImport.h"
#include "ValueParsing.h"
#include "VelocityTools.h"
#include "ErrorLog.h"
#include "ParameterControl.h"
#include "ParameterIds.h"
#include "engine/SfzExport.h"
#include "engine/InstrumentLocator.h"
#include "engine/FileUtil.h"
#include "ExternalLaunch.h"
#include "AboutPanel.h"   // Build-Angaben für das Logfile (wie in der About-Box)

#include <cmath>
#include <set>

namespace
{
// SFZP-Dateien (seit 14.10.2026): Der Rückfall-Pfad zur SFZ verrät keinen Benutzernamen mehr. Liegt die Datei im
// Home-Ordner, wird er als "~" geschrieben ("~/Documents/…"); Pfade außerhalb (z. B. USB-Stick) bleiben vollständig.
// Normalerweise wird die SFZ ohnehin über "instrument" gefunden (Dateiname, die SFZP liegt daneben).
juce::String pathWithoutUserName (const juce::File& file)
{
    const juce::File home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    if (home != juce::File() && file.isAChildOf (home))
        return "~/" + file.getRelativePathFrom (home).replaceCharacter ('\\', '/');
    return file.getFullPathName();
}

// Datei zu einem UTF-8-Pfad aus der Engine (z. B. Sample-Pfad aus der SFZ), mit denselben Rückfällen wie die Engine
// beim Lesen: andere Unicode-Form der Umlaute und (außer Windows) Groß/klein unabhängig. Nicht gefunden: der Pfad wie
// angegeben (existsAsFile() ist dann false).
juce::File existingFile (const std::string& utf8Path)
{
    auto fromUtf8 = [] (const std::string& p) { return juce::File (juce::String::fromUTF8 (p.c_str(), (int) p.size())); };
    for (const std::string& p : { utf8Path, maermel::unicode::composed (utf8Path), maermel::unicode::decomposed (utf8Path) })
        if (juce::File::isAbsolutePath (juce::String::fromUTF8 (p.c_str(), (int) p.size())) && fromUtf8 (p).existsAsFile())
            return fromUtf8 (p);
   #if ! JUCE_WINDOWS
    const std::string found = maermel::findIgnoringCase (utf8Path);
    if (! found.empty() && juce::File::isAbsolutePath (juce::String::fromUTF8 (found.c_str(), (int) found.size())))
        return fromUtf8 (found);
   #endif
    return juce::File::isAbsolutePath (juce::String::fromUTF8 (utf8Path.c_str(), (int) utf8Path.size())) ? fromUtf8 (utf8Path)
                                                                                                        : juce::File();
}

// Gegenstück zu pathWithoutUserName beim Lesen: "~/…" auf den Home-Ordner dieses Rechners; ältere SFZP-Dateien mit
// vollständigem Pfad werden weiter gelesen. Ein Pfad von einem anderen System (kein gültiger absoluter Pfad hier)
// ergibt keine Datei.
juce::File fileFromStoredPath (const juce::String& stored)
{
    if (stored.startsWith ("~/") || stored.startsWith ("~\\"))
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile (stored.substring (2).replaceCharacter ('/', juce::File::getSeparatorChar()));
    if (juce::File::isAbsolutePath (stored))
        return juce::File (stored);
    return {};
}

// Fader-Kurve: kleine Werte fein einstellbar, Maximalwerte trotzdem erreichbar (Faderstellung p hoch kCurve).
// Wirkt nur zwischen Fader und gespeichertem Wert; Klangberechnung und Anzeige bleiben unverändert.
// Bei bipolaren Bereichen (-max … +max) symmetrisch von der Mitte aus. Stärke: kFaderCurve (3 = x³).
constexpr float kFaderCurve = 3.0f;
constexpr float kVelEnvCurve = 1.4f;   // Filter Vel Env: flacher, siehe Parameter „filtervel“

juce::NormalisableRange<float> curvedRange (float lo, float hi, float exponent = kFaderCurve)
{
    const bool bipolar = (lo < 0.0f && juce::approximatelyEqual (lo, -hi));
    if (bipolar)
        return juce::NormalisableRange<float> (lo, hi,
                 [exponent] (float, float end, float p)   // Fader 0 … 1 → Wert
                 {
                     const float u = 2.0f * p - 1.0f;
                     return std::copysign (std::pow (std::abs (u), exponent), u) * end;
                 },
                 [exponent] (float, float end, float v)   // Wert → Fader 0 … 1
                 {
                     const float u = std::copysign (std::pow (std::abs (v) / end, 1.0f / exponent), v);
                     return juce::jlimit (0.0f, 1.0f, 0.5f * (u + 1.0f));
                 },
                 [] (float start, float end, float v) { return juce::jlimit (start, end, v); });

    return juce::NormalisableRange<float> (lo, hi,
             [exponent] (float start, float end, float p) { return start + (end - start) * std::pow (p, exponent); },
             [exponent] (float start, float end, float v)
             {
                 return juce::jlimit (0.0f, 1.0f, std::pow (juce::jmax (0.0f, (v - start) / (end - start)), 1.0f / exponent));
             },
             [] (float start, float end, float v) { return juce::jlimit (start, end, v); });
}

// Preset-Format (.sfzp und Zwischenablage)
constexpr const char* kPresetTag = "MaermelPreset";
// Version der Einstellungen in SFZP-Dateien, kopierten Einstellungen (Attribut "version") und DAW-Projekten
// (Eigenschaft "stateVersion"; fehlt sie, ist das Projekt älter als 1.0.3).
//  1 = bis 1.0.2
//  2 = seit 1.0.3: Notenwerte des LFO-Sync streng von langsam nach schnell (siehe lfoNoteValues). Ältere Dateien
//      werden beim Lesen umgerechnet (syncRateFromVersion1).
constexpr int kSettingsVersion = 2;
bool isPresetElement (const juce::XmlElement* xml)
{
    return xml != nullptr && xml->hasTagName (kPresetTag);
}

// --- Gespeicherte Werte prüfen (seit 1.0.3) ---------------------------------
// Beschädigte oder von Hand bearbeitete SFZP-Dateien, kopierte Einstellungen und Projekte dürfen keine unerwarteten
// Parameterzustände erzeugen. Ohne diese Prüfung:
//  - "nan" wurde von JUCE als NaN gelesen. NaN übersteht jede Klemmung (alle Vergleiche sind falsch), landet in der
//    Engine und macht Filter und Glättung dauerhaft unbrauchbar (Stille oder Unsinn bis zum Neuladen).
//  - Leerer oder unlesbarer Text ("", "abc") ergab 0 statt des Standardwerts – beim Cutoff also ein geschlossenes
//    Filter (Regler ganz unten).
//  - Werte außerhalb des Bereichs klemmt JUCE zwar, hält im Debug-Build aber jedes Mal per jassert an.

// Zahlentext vereinheitlichen: Leerzeichen am Rand entfernen; ein Komma als Dezimalzeichen wird zum Punkt – aber
// nur, wenn es genau EIN Komma ist und KEIN Punkt im Text steht ("0,5" → "0.5", "-6,5" → "-6.5"). So versteht eine
// von Hand bearbeitete Datei dasselbe wie die Wertefelder der Oberfläche (ValueParsing.h, normalize). Texte wie
// "1,000.5" oder "1,2,3" bleiben unverändert und gelten danach als unlesbar.
juce::String unifiedNumberText (const juce::String& text)
{
    const juce::String t = text.trim();
    int commas = 0;
    for (int i = 0; i < t.length(); ++i)
        if (t[i] == ',')
            ++commas;
    if (commas == 1 && ! t.containsChar ('.'))
        return t.replaceCharacter (',', '.');
    return t;
}

// Ist der (vereinheitlichte) Text eine Zahl? Erlaubt: Ziffern, Vorzeichen, Dezimalpunkt, Exponent (z. B. "-12.5",
// "1e-3"); mindestens eine Ziffer. Bewusst eine einfache Schleife statt eines regulären Ausdrucks.
bool isNumberText (const juce::String& t)
{
    if (t.isEmpty())
        return false;
    bool hasDigit = false;
    for (int i = 0; i < t.length(); ++i)
    {
        const juce::juce_wchar c = t[i];
        if (c >= '0' && c <= '9')
            hasDigit = true;
        else if (c != '.' && c != '-' && c != '+' && c != 'e' && c != 'E')
            return false;
    }
    return hasDigit;
}

// Gespeicherter Wert eines Parameters (tatsächlicher Wert, z. B. dB oder Hz): unlesbar oder nicht endlich (NaN,
// unendlich) → Standardwert des Parameters; sonst auf den Bereich des Parameters begrenzt. Dezimalkomma erlaubt.
float storedParameterValue (const juce::RangedAudioParameter& parameter, const juce::String& text)
{
    const float fallback = parameter.convertFrom0to1 (parameter.getDefaultValue());
    const juce::String number = unifiedNumberText (text);
    if (! isNumberText (number))
        return fallback;
    const double value = number.getDoubleValue();
    if (! std::isfinite (value))
        return fallback;
    const auto& range = parameter.getNormalisableRange();
    return (float) juce::jlimit ((double) range.start, (double) range.end, value);
}

// Zeitregler 0 ... 1 → Anzeige 10 s × x^4,2 (Envelope::kTimeCurveExponent)
juce::String timeText (float knob, int)
{
    const float seconds = maermel::Envelope::timeFromKnob (knob);
    if (seconds < 0.01f)  return juce::String (seconds * 1000.0f, 2) + " ms";
    if (seconds < 1.0f)   return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";
    return juce::String (seconds, 2) + " s";
}

// T (Sustain Time) als Stellung −64 ... +64 (− sinkt, + steigt, 0 = bleibt)
juce::String sustainTimeText (float t, int)
{
    const int steps = juce::roundToInt (t * 64.0f);
    if (steps == 0)
        return "0";
    return (steps > 0 ? "+" : "") + juce::String (steps);
}

juce::String percentText (float value, int)
{
    return juce::String (juce::roundToInt (value * 100.0f)) + " %";
}

constexpr float kVolumeMinDb = -60.0f;   // unterste Stellung = stumm
constexpr float kDefaultVelocityDb = 30.0f;   // Amp Velocity: Standard-Dynamikbereich (Parameter und Rückfall beim Eintippen)

juce::String volumeText (float db, int)
{
    if (db <= kVolumeMinDb + 0.01f)
        return "-inf dB";
    return juce::String (db, 1) + " dB";
}

juce::String decibelRangeText (float db, int)
{
    return juce::String (juce::roundToInt (db)) + " dB";
}

// Glide: 0 ... 5 s mit derselben Kennlinie wie die Hüllkurvenzeiten (x^4,2), Mitte ≈ 272 ms (bis 20.10.2026 x⁴: 310 ms)
constexpr float kMaxGlideSeconds = 5.0f;

float glideSeconds (float knob)
{
    return knob <= 0.0f ? 0.0f
                        : kMaxGlideSeconds * std::pow (std::min (1.0f, knob), maermel::Envelope::kTimeCurveExponent);
}

juce::String glideText (float knob, int)
{
    const float seconds = glideSeconds (knob);
    if (seconds <= 0.0f)  return "off";
    if (seconds < 1.0f)   return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";
    return juce::String (seconds, 2) + " s";
}

// Start: 0 ... 1 s mit kubischer Kennlinie (kSampleStartMaxMs). Die Anzeige nennt den Wert bei langen Samples;
// bei kurzen Samples wirkt der Regler über deren ganze Länge.
constexpr float kMaxStartMs = (float) maermel::kSampleStartMaxMs;

juce::String startMsText (float ms)   // „4.5 ms“, „125 ms“, „1.00 s“
{
    if (ms < 10.0f)   return juce::String (ms, 1) + " ms";
    if (ms < 999.5f)  return juce::String (juce::roundToInt (ms)) + " ms";
    return juce::String (ms / 1000.0f, 2) + " s";
}

juce::String startText (float knob, int)
{
    return startMsText (kMaxStartMs * knob * knob * knob);
}

juce::String startVelText (float v, int)
{
    if (std::abs (v) < 0.005f)
        return "0";
    const float ms = kMaxStartMs * std::abs (v * v * v);
    return (v > 0.0f ? "+" : "-") + (ms < 10.0f ? juce::String (juce::roundToInt (ms)) + " ms" : startMsText (ms));
}

juce::String semitoneText (int v, int)
{
    return (v > 0 ? "+" : "") + juce::String (v);
}

juce::String centText (float v, int)
{
    const int c = juce::roundToInt (v);
    return (c > 0 ? "+" : "") + juce::String (c) + " ct";
}

// Samplerate der DAW für die Lo-Fi-Anzeige (die Raten-Stufen teilen sie: ÷ 1½ … ÷ 4); gesetzt in prepareToPlay
std::atomic<double> displaySampleRate { 44100.0 };
// Anzeige des Reglers LO-FI/CHORUS in der DAW: im Chorus-Modus Prozent statt Bit/kHz. Der Text eines Parameters
// kennt seine Instanz nicht; bei mehreren Instanzen mit verschiedenem Modus zeigt die DAW-Automation daher das
// Format der zuletzt aktiven. Märmels eigene Oberfläche nutzt diese beiden Werte nicht (seit 1.0.3): Sie ruft
// lofiText/lofiValueFromText mit Modus und Samplerate ihrer eigenen Instanz auf.
std::atomic<bool> displayChorusMode { false };

juce::String crushText (float v, int)
{
    return MaermelAudioProcessor::lofiText (v, displayChorusMode.load(), displaySampleRate.load());
}

juce::String cutoffText (float knob, int)
{
    const float hz = maermel::Synth::cutoffHz (knob);
    return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, 2) + " kHz";
}

// Filterhüllkurve (Env): bei voller Stellung ±10 Oktaven
juce::String envAmountText (float v, int)
{
    if (std::abs (v) < 0.005f)
        return "0";
    return (v > 0.0f ? "+" : "-") + juce::String (std::abs (v) * maermel::Synth::kEnvOctaves, 1) + " oct";
}

// Vel Env: in Oktaven wie Env, wirkt je nach Anschlag (voll bei härtestem)
juce::String velEnvText (float v, int)
{
    if (std::abs (v) < 0.005f)
        return "0";
    return (v > 0.0f ? "+" : "-") + juce::String (std::abs (v) * maermel::Synth::kEnvOctaves, 1) + " oct";
}

// Vel (direkt): ±7 Oktaven bei härtestem Anschlag
juce::String velDirectText (float v, int)
{
    if (std::abs (v) < 0.005f)
        return "0";
    return (v > 0.0f ? "+" : "-") + juce::String (std::abs (v) * maermel::Synth::kVelocityCutoffOctaves, 1) + " oct";
}

// --- LFO ---------------------------------------------------------------------
// Freie Rate: 0,05 ... 30 Hz, gleichmäßig in Oktaven
float lfoFreeHz (float knob) { return 0.05f * std::pow (600.0f, knob); }

// Standardstellung des Rate-Reglers: 4,8 Hz (Umkehrung von lfoFreeHz). Auch Rückfallwert bei unlesbarer Eingabe.
const float kLfoDefaultKnob = std::log (4.8f / 0.05f) / std::log (600.0f);   // ≈ 0,7135

// Notenwerte für Sync, streng von langsam nach schnell, mit Länge in Vierteln.
// Gespeichert wird die Reglerstellung, also die Position in dieser Liste – Reihenfolge daher nicht mehr ändern.
// Bis 1.0.2 standen 1/2T vor 1/4. und 1/4T vor 1/8.; der LFO wurde beim Aufdrehen an diesen Stellen kurz
// langsamer. Ältere Projekte und Presets rechnet syncRateFromVersion1 beim Laden um.
struct NoteValue { const char* name; double quarters; };
const NoteValue lfoNoteValues[] = {
    { "4/1", 16.0 }, { "2/1", 8.0 }, { "1/1", 4.0 }, { "1/2", 2.0 }, { "1/4.", 1.5 },
    { "1/2T", 4.0 / 3.0 }, { "1/4", 1.0 }, { "1/8.", 0.75 }, { "1/4T", 2.0 / 3.0 }, { "1/8", 0.5 },
    { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 } };
constexpr int kNumNoteValues = (int) (sizeof (lfoNoteValues) / sizeof (lfoNoteValues[0]));

int lfoNoteIndex (float knob)
{
    return juce::jlimit (0, kNumNoteValues - 1, juce::roundToInt (knob * (float) (kNumNoteValues - 1)));
}

const NoteValue& lfoNoteValue (float knob)
{
    return lfoNoteValues[lfoNoteIndex (knob)];
}

// Umrechnung alter Einstellungen (Version 1, bis 1.0.2) bei eingeschaltetem Sync: Die Plätze 4 und 5 (1/2T, 1/4.)
// sowie 7 und 8 (1/4T, 1/8.) waren vertauscht. Damit ein altes Projekt denselben Notenwert spielt, wandert die
// Reglerstellung um genau einen Platz; alle anderen Stellungen bleiben unverändert. Nur beim Laden aufgerufen.
float syncRateFromVersion1 (float knob)
{
    const int oldIndex = lfoNoteIndex (knob);
    int newIndex = oldIndex;
    if (oldIndex == 4) newIndex = 5;        // 1/2T
    else if (oldIndex == 5) newIndex = 4;   // 1/4.
    else if (oldIndex == 7) newIndex = 8;   // 1/4T
    else if (oldIndex == 8) newIndex = 7;   // 1/8.
    if (newIndex == oldIndex)
        return knob;
    // Abstand zur Rastmitte beibehalten (spielt bei Sync keine Rolle, aber so ändert sich nur der Platz)
    return juce::jlimit (0.0f, 1.0f, knob + (float) (newIndex - oldIndex) / (float) (kNumNoteValues - 1));
}

// Linksstellung des Rate-Reglers = DC (konstanter Höchstwert statt LFO)
constexpr float kLfoDcKnob = 0.005f;
bool lfoIsDc (float knob) { return knob < kLfoDcKnob; }

juce::String lfoFreeText (float knob, int)
{
    if (lfoIsDc (knob))
        return "DC";
    const float hz = lfoFreeHz (knob);
    return hz < 10.0f ? juce::String (hz, 2) + " Hz" : juce::String (hz, 1) + " Hz";
}

juce::String bipolarPercentText (float v, int)
{
    const int p = juce::roundToInt (v * 100.0f);
    if (p == 0) return "0";
    return (p < 0 ? "Poly " : "Mono ") + juce::String (std::abs (p)) + " %";
}

// Eingetippten Text an die (JUCE-freien) Parser weiterreichen
std::string plain (const juce::String& t) { return t.toStdString(); }

// LFO-Rate: Hz oder – bei Sync – ein Notenwert wie "1/4", "1/8T", "1/4."
float lfoRateFromText (const juce::String& text)
{
    if (text.trim().equalsIgnoreCase ("dc"))
        return 0.0f;
    if (text.containsChar ('/'))
        for (int i = 0; i < kNumNoteValues; ++i)
            if (text.trim().equalsIgnoreCase (lfoNoteValues[i].name))
                return (float) i / (float) (kNumNoteValues - 1);
    return maermel::parse::lfoRateKnob (plain (text), kLfoDefaultKnob);   // unlesbar: Standard 4,8 Hz
}

const juce::StringArray lfoWaveNames { "Tri", "Sin", "Saw", "Sqr" };

const juce::StringArray filterTypeNames { "None", "LPF12", "LPF24", "HPF", "BPF" };

const juce::StringArray voiceModeNames { "Leg", "Mon", "2", "6", "8", "12", "16", "32", "64" };

float volumeGain (float db)
{
    return db <= kVolumeMinDb + 0.01f ? 0.0f : juce::Decibels::decibelsToGain (db);
}
} // namespace

MaermelAudioProcessor::MaermelAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "Maermel", createParameterLayout())   // Name des gespeicherten Zustands
{
    // Latenz schon hier melden (konstant, siehe prepareToPlay): Manche Hosts und Prüfprogramme wie pluginval
    // fragen sie ab, bevor prepareToPlay gelaufen ist.
    setLatencySamples (maermel::DriveStage::kDelay);

    for (auto* env : { &ampEnvParams, &filterEnvParams })
    {
        const juce::String prefix = (env == &ampEnvParams) ? "" : "f";
        env->attack      = rawParameter (prefix + "attack");
        env->attackVel   = rawParameter (prefix + "velattack");
        env->decay       = rawParameter (prefix + "decay");
        env->decayVel    = rawParameter (prefix + "veldecay");
        env->sustain     = rawParameter (prefix + "sustain");
        env->sustainTime = rawParameter (prefix + "sustaintime");
        env->release     = rawParameter (prefix + "release");
        env->key         = rawParameter (prefix == "" ? "ampkey" : "fkey");
    }
    volumeParam  = rawParameter ("volume");
    velocityParam = rawParameter ("velocity");
    wheelAmpParam = rawParameter ("wheelamp");
    atWheelParam = rawParameter ("atwheel");
    velOffsetParam = rawParameter ("veloffset");
    ampKeyScaleParam = rawParameter ("ampkeyscale");
    voiceModeParam = rawParameter ("voicemode");
    glideParam = rawParameter (maermel::ids::glide);
    startParam    = rawParameter ("start");
    startVelParam = rawParameter ("startvel");
    startRndParam = rawParameter ("startrnd");
    coarseParam   = rawParameter ("coarse");
    fineParam     = rawParameter ("fine");
    keyShiftParam = rawParameter ("keyshift");
    crushParam    = rawParameter (maermel::ids::lofi);
    fxModeParam   = rawParameter ("fxmode");
    pitchBendParam = rawParameter ("pitchbend");
    filterTypeParam = rawParameter ("filtertype");
    cutoffParam     = rawParameter (maermel::ids::cutoff);
    resonanceParam  = rawParameter (maermel::ids::resonance);
    driveParam      = rawParameter (maermel::ids::drive);
    filterKeyParam  = rawParameter ("filterkey");
    filterEnvParam  = rawParameter (maermel::ids::filterEnv);
    filterVelParam  = rawParameter ("filtervel");
    filterVelCutParam = rawParameter ("filtervelcutoff");
    for (auto* entry : { &polyLfoParams, &monoLfoParams })
    {
        const juce::String prefix = (entry == &polyLfoParams) ? "plfo" : "mlfo";
        entry->rate   = rawParameter (prefix + "rate");
        entry->sync   = rawParameter (prefix + "sync");
        entry->env    = rawParameter (prefix + "env");
        entry->wave   = rawParameter (prefix + "wave");
        entry->invert = rawParameter (prefix + "invert");
        entry->delay  = rawParameter (prefix + "delay");
    }
    lfoPitchParam    = rawParameter ("lfopitch");
    lfoFilterParam   = rawParameter ("lfofilter");
    lfoVolumeParam   = rawParameter ("lfovolume");
    lfoPanParam      = rawParameter ("lfopan");
    wheelPitchParam  = rawParameter ("wheelpitch");
    wheelFilterParam = rawParameter ("wheelfilter");

    syncSwitchSettings();
    startTimerHz (20);               // 50 ms: MIDI-Umschaltung, Aufräumen, Einstellungen
    loader.startThread();
}

std::atomic<float>* MaermelAudioProcessor::rawParameter (const juce::String& id)
{
    auto* value = parameters.getRawParameterValue (id);
    // Tippfehler in der ID: im Debug-Build sofort hier anhalten, statt später im Audio-Thread abzustürzen (seit 1.0.3)
    jassert (value != nullptr);
    return value;
}

MaermelAudioProcessor::~MaermelAudioProcessor()
{
    // BEKANNTE SCHWACHSTELLE (gesehen in der Prüfung zu 1.0.3, Behebung geplant – Konzept, To-Do „Abbruch-Flag
    // für Lader und Export“):
    // stopThread wartet höchstens die angegebene Zeit und beendet den Thread danach GEWALTSAM. Das ist undefiniertes
    // Verhalten und kann den Host abstürzen lassen. Normalerweise kommt es nicht so weit, aber:
    //  - Der Lader fragt threadShouldExit() während des eigentlichen Ladens (loadAnyInstrument, Samples einlesen)
    //    nicht ab. Wird das Projekt geschlossen, während ein großes Instrument von einer langsamen Platte oder
    //    einem Netzlaufwerk lädt, können die 5 s überschritten werden.
    //  - Der Export prüft threadShouldExit() nur zwischen zwei Samples; copyFileTo einer sehr großen Datei lässt
    //    sich nicht unterbrechen.
    // Geplante Lösung: ein Abbruch-Flag bis in loadAnyInstrument bzw. MemoryGuard durchreichen und zwischen den
    // Samples prüfen; beim Export große Dateien in Blöcken kopieren und dazwischen prüfen.
    if (exportJob != nullptr)
        exportJob->stopThread (10000);   // laufenden Export beenden (prüft threadShouldExit zwischen den Samples)
    loader.stopThread (5000);
    stopTimer();
    cancelPendingUpdate();               // keine Übergabe mehr nach dem Abbau (handleAsyncUpdate)
}

// --- Laden im Hintergrund ---------------------------------------------------

void MaermelAudioProcessor::InstrumentLoader::request (const juce::File& file)
{
    {
        const juce::ScopedLock sl (requestLock);
        requested = file;
        pending = true;
    }
    notify();
}

bool MaermelAudioProcessor::InstrumentLoader::hasNewerRequest() const
{
    const juce::ScopedLock sl (requestLock);
    return pending;
}

void MaermelAudioProcessor::InstrumentLoader::run()
{
    while (! threadShouldExit())
    {
        wait (-1);
        while (! threadShouldExit())
        {
            juce::File file;
            {
                const juce::ScopedLock sl (requestLock);
                if (! pending)
                    break;
                file = requested;
                pending = false;
            }
            owner.loadInstrumentNow (file);
            owner.loadFinished (file);       // warten Einstellungen auf dieses Instrument: Message-Thread anstoßen
        }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MaermelAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Alle Parameter werden zuerst gesammelt und am Ende in der Reihenfolge der Oberfläche in Gruppen eingeordnet
    // (so zeigen DAWs sie in ihrer Parameteransicht). Gespeicherte Zustände finden die Werte über die Namen (IDs),
    // nicht über die Reihenfolge – Presets und Projekte bleiben gültig.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> made;
    auto add = [&made] (std::unique_ptr<juce::RangedAudioParameter> p) { made.push_back (std::move (p)); };

    // id und name als juce::String (nicht const char*): so darf der Aufrufer auch zusammengesetzte Texte übergeben,
    // ohne dass ein Zeiger auf einen temporären String entsteht
    auto timeParam = [] (const juce::String& id, const juce::String& name, float defaultKnob)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), defaultKnob,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (timeText)
                .withValueFromStringFunction ([defaultKnob] (const juce::String& t)
                                              { return maermel::parse::timeKnob (plain (t), 10.0, defaultKnob); }));
    };

    add (timeParam ("attack",  "Amp Attack",  0.0f));
    // Standardwerte der Hüllkurven (ohne Preset): Decay in grafischer Mittelstellung (10 s × 0,5^4,2 = 544 ms) für
    // Amp und Filter; Amp: Sustain 100 %, Release 90 ms; Filter: Sustain 50 %, Release 544 ms (Mittelstellung)
    const float defaultDecayKnob   = 0.5f;                                                    // 544 ms
    const float defaultReleaseKnob = std::pow (0.09f / 10.0f, 1.0f / maermel::Envelope::kTimeCurveExponent);   // 90 ms (Amp)
    const float filterReleaseKnob  = 0.5f;                                                    // 544 ms (Filter)
    add (timeParam ("decay",   "Amp Decay",   defaultDecayKnob));
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sustain", 1 }, "Amp Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)
            .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::percent (plain (t), 1.0f); })));
    add (timeParam ("release", "Amp Release", defaultReleaseKnob));

    // Hüllkurven-Erweiterungen (Amp und Filter): Vel für A und D, T, Key
    auto bipolarParam = [] (const juce::String& id, const juce::String& name, std::function<juce::String (float, int)> text,
                            std::function<float (const juce::String&)> parse)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))
                                                 .withValueFromStringFunction (std::move (parse)));
    };
    auto parseSustainTime    = [] (const juce::String& t) { return maermel::parse::sustainTime (plain (t), 0.0f); };
    auto parsePercentZero    = [] (const juce::String& t) { return maermel::parse::percent (plain (t), 0.0f); };
    for (const juce::String& prefix : { juce::String(), juce::String ("f") })
    {
        const juce::String name = prefix.isEmpty() ? "Amp " : "Filter ";
        // VelA / VelD: Anteil der Zeit bei Velocity 127 an der Zeit bei Velocity 1 (100 % = keine Velocity-Wirkung).
        // Als Anteil der Reglerstellung gespeichert, damit VelA/VelD beim Drehen von Attack/Decay mitwandern.
        const std::pair<const char*, const char*> velocityParams[] = { { "velattack", "Vel Attack" }, { "veldecay", "Vel Decay" } };
        for (const auto& vel : velocityParams)
            add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { prefix + vel.first, 1 }, name + vel.second,
                juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)
                    .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::percent (plain (t), 1.0f); })));
        add (bipolarParam (prefix + "sustaintime", name + "Sustain Time", sustainTimeText, parseSustainTime));
        add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix.isEmpty() ? "ampkey" : "fkey", 1 }, name + "Env Key",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)
                                                 .withValueFromStringFunction (parsePercentZero)));
    }

    // Vol: −60 dB (= stumm) bis +6 dB, −12 dB in der Mitte des Regelwegs (0 dB bei etwa 80 %)
    juce::NormalisableRange<float> volumeRange (kVolumeMinDb, 6.0f);
    volumeRange.setSkewForCentre (-12.0f);
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "volume", 1 }, "Amp Volume", volumeRange, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (volumeText)
            .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::decibels (plain (t), kVolumeMinDb, 6.0f, 0.0f); })));

    // Velocity-Offset: verschiebt die eingehende Velocity (z. B. gegen Mapping-Fehler oder um Layer auszublenden)
    add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "veloffset", 1 }, "Velocity Offset", -32, 32, 0,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + juce::String (v); })
            .withValueFromStringFunction ([] (const juce::String& t) { return (int) std::lround (maermel::parse::plainNumber (plain (t), -32, 32, 0)); })));


    // Amp Key: Lautstärke nach Note (Bezug Note 60), ±3 dB je Oktave bei voller Auslenkung, höchstens ±12 dB
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ampkeyscale", 1 }, "Amp Key Scale", curvedRange (-1.0f, 1.0f), 0.0f,   // x³ wie die Filter-Fader: kleine Werte fein
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                const float dbPerOctave = v * 3.0f;
                if (std::abs (dbPerOctave) < 0.05f) return juce::String ("0");
                return (dbPerOctave > 0 ? "+" : "") + juce::String (dbPerOctave, 1) + " dB/Oct";
            })
            .withValueFromStringFunction ([] (const juce::String& t)
            {
                return maermel::parse::plainNumber (plain (t), -3.0, 3.0, 0.0f) / 3.0f;
            })));

    // Vel: Dynamikbereich zwischen Velocity 1 und 127, 0 = keine Anschlagsdynamik, 84 = SFZ-Standard
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "velocity", 1 }, "Amp Velocity", curvedRange (0.0f, 84.0f), kDefaultVelocityDb,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (decibelRangeText)
            .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::decibels (plain (t), 0.0f, 84.0f, kDefaultVelocityDb); })));

    // Stimmen-Schnellauswahl: Legato, Mono oder feste Stimmenzahl
    add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "voicemode", 1 }, "Voices", voiceModeNames, 6));   // Standard: 16

    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { maermel::ids::glide, 1 }, "Glide", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (glideText)
            .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::timeKnob (plain (t), 5.0, 0.0f); })));

    // --- Sample Start and Tune ---
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "start", 1 }, "Sample Start", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (startText)
            .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::startKnob (plain (t), 0.0f); })));
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "startvel", 1 }, "Start Velocity", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,   // x³ steckt bereits in der Umrechnung (wie START), nicht noch einmal hier
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (startVelText)
            .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::startVelocity (plain (t), 0.0f); })));
    add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "startrnd", 1 }, "Start Random", false));
    add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "coarse", 1 }, "Coarse Tune", -24, 24, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction (semitoneText)
            .withValueFromStringFunction ([] (const juce::String& t) { return (int) std::lround (maermel::parse::plainNumber (plain (t), -24, 24, 0)); })));
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "fine", 1 }, "Fine Tune", juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (centText)
            .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::plainNumber (plain (t), -100, 100, 0); })));
    add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "keyshift", 1 }, "Key Shift", -12, 12, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction (semitoneText)
            .withValueFromStringFunction ([] (const juce::String& t) { return (int) std::lround (maermel::parse::plainNumber (plain (t), -12, 12, 0)); })));
    add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "pitchbend", 1 }, "Pitch Bend Range", 0, 12, 2,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
        {
            return juce::String (v);   // reiner Zahlenwert in Halbtönen
        }).withValueFromStringFunction ([] (const juce::String& t) { return (int) std::lround (maermel::parse::plainNumber (plain (t), 0, 12, 2)); })));
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { maermel::ids::lofi, 1 }, "Lo-Fi / Chorus", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f,   // in SFZP-Dateien „crush“
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (crushText)
            .withValueFromStringFunction ([] (const juce::String& t)
            {
                return MaermelAudioProcessor::lofiValueFromText (t, displayChorusMode.load());
            })));
    // Umschalter Lo-Fi / Chorus (seit 26.10.2026): bewusst nicht automatisierbar; gespeichert mit Preset und Projekt
    add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "fxmode", 1 }, "Lo-Fi / Chorus Mode", juce::StringArray { "Lo-Fi", "Chorus" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    // --- Filter ---
    auto percentParam = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)
                .withValueFromStringFunction ([def] (const juce::String& t) { return maermel::parse::percent (plain (t), def); }));
    };
    add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "filtertype", 1 }, "Filter Type", filterTypeNames, 1));      // Standard: LPF12
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { maermel::ids::cutoff, 1 }, "Cutoff", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (cutoffText)
            .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::cutoffKnob (plain (t), 1.0f); })));
    add (percentParam (maermel::ids::resonance, "Resonance", 0.0f));
    add (percentParam (maermel::ids::drive, "Drive", 0.0f));
    add (percentParam ("filterkey", "Filter Key", 0.0f));
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { maermel::ids::filterEnv, 1 }, "Filter Env", curvedRange (-1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (envAmountText)
            .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::envOctaves (plain (t), 0.0f); })));
    // Vel Env: ±10 Oktaven zusätzliche Hüllkurventiefe je nach Anschlag (addiert sich wie Env)
    add (std::make_unique<juce::AudioParameterFloat> (
        // Kurve x^1,4 statt x³ (12.10.2026): ausgewertete 94 Sounds nutzen meist 2,7 … 6,7 Okt – mit x³ lagen sie
        // gedrängt am oberen Ende (Median 78 % des Weges), jetzt um die Mitte (Median 59 %). Gespeichert sind die
        // tatsächlichen Werte, die Sounds klingen also unverändert; nur die Fader-Stellung ändert sich.
        juce::ParameterID { "filtervel", 1 }, "Filter Vel Env", curvedRange (-1.0f, 1.0f, kVelEnvCurve), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (velEnvText)
            .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::velEnv (plain (t), 0.0f); })));
    // Vel (direkt): verschiebt den Cutoff je nach Anschlag, ±7 Oktaven bei härtestem Anschlag
    add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "filtervelcutoff", 1 }, "Filter Vel", curvedRange (-1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (velDirectText)
            .withValueFromStringFunction ([] (const juce::String& t)
            {
                return maermel::parse::plainNumber (plain (t), -maermel::Synth::kVelocityCutoffOctaves,
                                                      maermel::Synth::kVelocityCutoffOctaves, 0.0f)
                       / maermel::Synth::kVelocityCutoffOctaves;
            })));

    add (timeParam ("fattack",  "Filter Attack",  0.0f));
    add (timeParam ("fdecay",   "Filter Decay",   defaultDecayKnob));
    add (percentParam ("fsustain", "Filter Sustain", 0.5f));
    add (timeParam ("frelease", "Filter Release", filterReleaseKnob));

    // --- LFOs ---
    for (const char* prefix : { "plfo", "mlfo" })
    {
        const juce::String id (prefix);
        const juce::String name = (id == "plfo") ? "Poly LFO " : "Mono LFO ";
        // Rate: frei 0,05 ... 30 Hz; bei Sync wählt derselbe Regler einen Notenwert (Anzeige im Editor)
        add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id + "rate", 1 }, name + "Rate", juce::NormalisableRange<float> (0.0f, 1.0f),
            kLfoDefaultKnob,   // Standard: 4,8 Hz (Kennlinie 0,05 Hz × 600^Stellung)
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (lfoFreeText)
                .withValueFromStringFunction (lfoRateFromText)));
        add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id + "sync", 1 }, name + "Sync", false));
        add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id + "env", 1 }, name + "Env Mode", false));
        add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id + "wave", 1 }, name + "Wave", lfoWaveNames, 0));
        add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id + "invert", 1 }, name + "Invert", false));
        add (timeParam (id + "delay", name + "Delay", 0.0f));
    }

    // Routing: links Poly-LFO, rechts Mono-LFO
    const std::pair<const char*, const char*> routingTargets[] = {
        { "lfopitch", "LFO Pitch" }, { "lfofilter", "LFO Filter" }, { "lfovolume", "LFO Volume" }, { "lfopan", "LFO Pan" } };
    for (const auto& target : routingTargets)
        add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { target.first, 1 }, target.second, juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (bipolarPercentText)
                .withValueFromStringFunction ([] (const juce::String& t) { return maermel::parse::routing (plain (t), 0.0f); })));

    add (percentParam ("wheelpitch", "Wheel LFO Pitch", 0.0f));
    add (percentParam ("wheelfilter", "Wheel LFO Filter", 0.0f));
    add (percentParam ("wheelamp", "Wheel LFO Amp", 0.0f));   // gemeinsam für LFO-Volume und LFO-Pan
    // AT→WHEEL je Instrument (mit dem Preset gespeichert): Aftertouch wirkt wie das Modulationsrad. Wirkt nur,
    // wenn auch die globale Einstellung im PATH-Dialog an ist (Bühne mit empfindlichen Mini-Keyboards).
    add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "atwheel", 1 }, "Aftertouch to Wheel", true));

    // Reihenfolge und Gruppen wie auf der Oberfläche
    struct GroupDef { const char* id; const char* name; std::vector<const char*> ids; };
    const std::vector<GroupDef> groups = {
        { "voices", "Voices", { "atwheel", maermel::ids::glide, "voicemode" } },
        { "sample", "Sample Start and Tune", { "start", "startvel", "startrnd", "coarse", "fine", "keyshift", "veloffset",
                                               "pitchbend", "wheelpitch" } },
        { "filter", "Filter", { maermel::ids::cutoff, maermel::ids::resonance, maermel::ids::drive, "filterkey", "filtertype",
                                "filtervelcutoff", "filtervel", maermel::ids::filterEnv, "wheelfilter" } },
        { "amp", "Amp", { maermel::ids::lofi, "fxmode", "wheelamp", "velocity", "ampkeyscale", "volume" } },
        { "lfo", "LFO", { "plforate", "plfosync", "plfoenv", "plfodelay", "plfowave", "plfoinvert",
                          "lfopitch", "lfofilter", "lfovolume", "lfopan",
                          "mlforate", "mlfosync", "mlfoenv", "mlfodelay", "mlfowave", "mlfoinvert" } },
        { "fenv", "Filter Envelope", { "fattack", "fvelattack", "fdecay", "fveldecay", "fsustain", "fsustaintime",
                                       "frelease", "fkey" } },
        { "aenv", "Amp Envelope", { "attack", "velattack", "decay", "veldecay", "sustain", "sustaintime", "release",
                                    "ampkey" } },
    };
    auto take = [&made] (const juce::String& id) -> std::unique_ptr<juce::RangedAudioParameter>
    {
        for (auto& p : made)
            if (p != nullptr && p->getParameterID() == id)
                return std::move (p);
        jassertfalse;   // ID in der Gruppenliste, aber nicht angelegt
        return {};
    };
    for (const auto& g : groups)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (g.id, g.name, " | ");
        for (const char* id : g.ids)
            if (auto p = take (id))
                group->addChild (std::move (p));
        layout.add (std::move (group));
    }
    // Sicherheitsnetz: Parameter, die in keiner Gruppe stehen, kommen ans Ende (sollte nicht vorkommen)
    for (auto& p : made)
        if (p != nullptr)
        {
            jassertfalse;
            layout.add (std::move (p));
        }

    return layout;
}

// ---------------------------------------------------------------------------

void MaermelAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    displaySampleRate = sampleRate;   // Lo-Fi-Anzeige: Raten-Stufen hängen von der DAW-Samplerate ab
    // Latenz an den Host melden: Der Drive (2-fache Überabtastung) verzögert das Signal immer um genau
    // DriveStage::kDelay Samples – bei Drive aus wird gleich verzögert, damit Auf- und Zudrehen nicht knackt.
    // 19 Samples sind 0,43 ms bei 44,1 kHz; die DAW kann damit im Projekt exakt ausgleichen.
    setLatencySamples (maermel::DriveStage::kDelay);

    synth.prepare (sampleRate);
    renderBuffer.setSize (2, juce::jmax (samplesPerBlock, 32));
    masterGain.reset (sampleRate, 0.02);                          // 20 ms Rampe
    masterGain.setCurrentAndTargetValue (volumeGain (volumeParam->load()));
    lastAmpEnv.fill (-2.0f);          // Hüllkurven beim nächsten Block neu übertragen
    lastFilterEnv.fill (-2.0f);
}

bool MaermelAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void MaermelAudioProcessor::takePendingInstrument() noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (instrumentLock);
    if (! lock.isLocked() || pendingInstrument == nullptr || retiredInstrument != nullptr)
        return;   // beim nächsten Block erneut versuchen

    // Klingende Noten des alten Instruments werden ausgeblendet. Zurück kommt höchstens ein
    // noch älteres Instrument, dessen Ausblendung abgebrochen wurde.
    retiredInstrument = synth.exchangeInstrument (std::move (pendingInstrument));
    pendingInstrument = nullptr;
}

void MaermelAudioProcessor::collectFinishedInstrument() noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (instrumentLock);
    if (lock.isLocked() && retiredInstrument == nullptr)
        retiredInstrument = synth.takeFinishedInstrument();   // erst, wenn es ausgeblendet ist
}

void MaermelAudioProcessor::updateVoiceSettings() noexcept
{
    static constexpr int polyVoices[] = { 0, 0, 2, 6, 8, 12, 16, 32, 64 };
    const int index = juce::jlimit (0, 8, (int) voiceModeParam->load());

    if (index == 0)      synth.setVoiceMode (maermel::Synth::VoiceMode::Legato);
    else if (index == 1) synth.setVoiceMode (maermel::Synth::VoiceMode::Mono);
    else                 synth.setVoiceMode (maermel::Synth::VoiceMode::Poly, polyVoices[index]);

    synth.setGlideTime (glideSeconds (glideParam->load()));
}

void MaermelAudioProcessor::updateFilterSettings() noexcept
{
    synth.setFilterType ((maermel::FilterType) juce::jlimit (0, 4, juce::roundToInt (filterTypeParam->load())));
    synth.setFilterCutoff (cutoffParam->load());
    synth.setFilterResonance (resonanceParam->load());
    synth.setDrive (driveParam->load());
    synth.setFilterKeyTrack (filterKeyParam->load());
    synth.setFilterEnvAmount (filterEnvParam->load());
    synth.setFilterVelocity (filterVelParam->load());
    synth.setFilterVelocityCutoff (filterVelCutParam->load());

}

void MaermelAudioProcessor::updateLfoSettings() noexcept
{
    // Songtempo und -position von der DAW (Standalone: 120 BPM)
    double bpm = 120.0, ppq = 0.0;
    bool playing = false, hasPosition = false;
    if (auto* head = getPlayHead())
    {
        if (const auto pos = head->getPosition())
        {
            if (const auto b = pos->getBpm()) bpm = *b;
            if (const auto q = pos->getPpqPosition()) { ppq = *q; hasPosition = true; }
            playing = pos->getIsPlaying();
        }
    }

    // Umrechnung nur, wenn sich Regler oder Tempo geändert haben (seit 18.10.2026: spart die Potenzrechnungen
    // in jedem Audioblock); sonst die zuletzt berechneten, identischen Werte erneut übergeben. Betrifft nur die
    // Umrechnung der Reglerwerte – der Mono-LFO selbst läuft unverändert in der Engine (16-Sample-Schritte).
    auto makeParams = [bpm] (const LfoParamPointers& p, LfoCache& cache, double& quarters)
    {
        const std::array<float, 6> raw { p.rate->load(), p.sync->load(), p.env->load(), p.wave->load(),
                                         p.invert->load(), p.delay->load() };
        if (cache.valid && raw == cache.raw && bpm == cache.bpm)
        {
            quarters = cache.quarters;
            return cache.params;
        }

        // Durchgehend die Werte aus raw (nicht erneut load()), damit Zwischenspeicher und Ergebnis garantiert
        // zusammenpassen, auch wenn sich ein Parameter genau jetzt ändert (seit 1.0.3)
        const float rate = raw[0];
        maermel::LfoParams lp;
        const bool sync = raw[1] > 0.5f;
        quarters = sync ? lfoNoteValue (rate).quarters : 0.0;
        lp.rateHz = sync ? (float) (bpm / 60.0 / quarters) : lfoFreeHz (rate);
        lp.envMode = raw[2] > 0.5f;
        lp.wave = (maermel::LfoWave) juce::jlimit (0, 3, juce::roundToInt (raw[3]));
        lp.invert = raw[4] > 0.5f;
        lp.delaySeconds = maermel::Envelope::timeFromKnob (raw[5]);
        lp.dc = lfoIsDc (rate);
        cache = { true, raw, bpm, quarters, lp };
        return lp;
    };

    double polyQuarters = 0.0, monoQuarters = 0.0;
    synth.setPolyLfo (makeParams (polyLfoParams, polyLfoCache, polyQuarters));
    const auto mono = makeParams (monoLfoParams, monoLfoCache, monoQuarters);
    synth.setMonoLfo (mono);

    // Mono-LFO mit Sync läuft taktgenau zur Songposition, solange die DAW spielt
    if (monoQuarters > 0.0 && playing && hasPosition && ! mono.envMode && ! mono.dc)
        synth.alignMonoLfo (ppq / monoQuarters);

    synth.setLfoRouting (lfoPitchParam->load(), lfoFilterParam->load(), lfoVolumeParam->load(), lfoPanParam->load());
    synth.setWheelAmounts (wheelPitchParam->load(), wheelFilterParam->load(), wheelAmpParam->load());
}

void MaermelAudioProcessor::updateSampleSettings() noexcept
{
    synth.setSampleStart (startParam->load(), startVelParam->load(), startRndParam->load() > 0.5f);
    synth.setCoarseTune (juce::roundToInt (coarseParam->load()));
    synth.setKeyShift (juce::roundToInt (keyShiftParam->load()));
    synth.setFineTune (fineParam->load());
    synth.setCrush (crushParam->load());
    const bool chorusMode = fxModeParam->load() > 0.5f;
    synth.setFxMode (chorusMode ? maermel::Synth::FxMode::Chorus : maermel::Synth::FxMode::LoFi);
    displayChorusMode = chorusMode;
    synth.setPitchBendRange ((float) juce::roundToInt (pitchBendParam->load()));
}

maermel::Envelope::Params MaermelAudioProcessor::envelopeFromKnobs (const EnvelopeParamPointers& p,
                                                                       std::array<float, 8>& last, bool& changed)
{
    const std::array<float, 8> now { p.attack->load(), p.attackVel->load(), p.decay->load(), p.decayVel->load(),
                                     p.sustain->load(), p.sustainTime->load(), p.release->load(), p.key->load() };
    changed = (now != last);
    last = now;
    if (! changed)
        return {};   // unverändert: nichts umrechnen (seit 18.10.2026; der Aufrufer überträgt dann ohnehin nichts)

    // Hauptregler = Zeit bei Velocity 1; VelA/VelD = Anteil davon für Velocity 127 (nie länger)
    maermel::Envelope::Params e;
    e.attackSecondsVel1  = maermel::Envelope::timeFromKnob (now[0]);
    e.attackSeconds      = maermel::Envelope::timeFromKnob (now[0] * juce::jlimit (0.0f, 1.0f, now[1]));
    e.decaySecondsVel1   = maermel::Envelope::timeFromKnob (now[2]);
    e.decaySeconds       = maermel::Envelope::timeFromKnob (now[2] * juce::jlimit (0.0f, 1.0f, now[3]));
    e.sustainLevel       = now[4];
    e.sustainTimeSeconds = maermel::Envelope::sustainTimeFromKnob (now[5]);
    e.releaseSeconds     = maermel::Envelope::timeFromKnob (now[6]);
    e.keyTrack           = now[7];
    return e;
}

void MaermelAudioProcessor::updateEnvelopeIfChanged() noexcept
{
    // Nur bei Änderungen an alle Stimmen übertragen (spart Rechenzeit)
    bool changed = false;
    const auto amp = envelopeFromKnobs (ampEnvParams, lastAmpEnv, changed);
    if (changed)
        synth.setEnvelope (amp);

    const auto filter = envelopeFromKnobs (filterEnvParams, lastFilterEnv, changed);
    if (changed)
        synth.setFilterEnvelope (filter);
}

const char* MaermelAudioProcessor::settingKey (LearnTarget target)
{
    switch (target)
    {
        case LearnTarget::NextNote:       return "midiNextNote";
        case LearnTarget::PrevNote:       return "midiPrevNote";
        case LearnTarget::NextController: return "midiNextCC";
        case LearnTarget::PrevController: return "midiPrevCC";
        case LearnTarget::ParamSlot1: case LearnTarget::ParamSlot2:
        case LearnTarget::ParamSlot3: case LearnTarget::ParamSlot4:   // eigene Schlüssel (ParameterControl.h)
        case LearnTarget::None:           break;
    }
    return "";
}

juce::String MaermelAudioProcessor::logHeader() const
{
    // z. B. "Märmel 1.0.0 – Built: 2026/10/03 19:00 (macOS ARM / AU)"
    return juce::String::fromUTF8 ("Märmel ") + JucePlugin_VersionString + juce::String::fromUTF8 (" – ")
         + AboutPanel::buildInfo (wrapperType);
}

void MaermelAudioProcessor::syncSwitchSettings()
{
    auto& settings = library->getSettings();
    switchNextNote = settings.getIntValue (settingKey (LearnTarget::NextNote), -1);
    switchPrevNote = settings.getIntValue (settingKey (LearnTarget::PrevNote), -1);
    switchNextCC   = settings.getIntValue (settingKey (LearnTarget::NextController), -1);
    switchPrevCC   = settings.getIntValue (settingKey (LearnTarget::PrevController), -1);
    velocityCurve  = (float) settings.getDoubleValue ("velocityCurve", 0.0);   // global, für alle Instrumente
    aftertouchAsWheel = settings.getBoolValue ("aftertouchAsWheel", true);      // global, je nach Keyboard

    // Direkte Steuerung von Parametern: Zuordnung lesen und Konflikte auswerten (gleiche Regel wie im Dialog)
    namespace pc = maermel::control;
    std::array<int, pc::kSlots> cc {}, target {};
    for (int i = 0; i < pc::kSlots; ++i)
    {
        cc[(size_t) i] = settings.getIntValue (juce::String (pc::keyCc (i)), -1);
        target[(size_t) i] = pc::targetIndex (settings.getValue (juce::String (pc::keyTarget (i)), "none").toStdString());
    }
    std::array<bool, pc::kSlots> active {}, conflict {};
    pc::evaluate (cc, target, switchNextCC.load(), switchPrevCC.load(), active, conflict);
    for (int i = 0; i < pc::kSlots; ++i)
    {
        auto& slot = controlSlots[(size_t) i];
        const bool jump = settings.getBoolValue (juce::String (pc::keyJump (i)), true);
        if (slot.cc.load() != cc[(size_t) i] || slot.target.load() != target[(size_t) i]
            || slot.jump.load() != jump || slot.active.load() != active[(size_t) i])
        {
            // Die Werte sind einzeln atomar, aber nicht gemeinsam. Damit der Audio-Thread keine Mischung aus alter
            // und neuer Zuordnung sieht (z. B. neuer Parameter mit altem Controller), die Zeile zuerst abschalten
            // und erst zum Schluss wieder freigeben (seit 1.0.3).
            slot.active = false;
            slot.param = active[(size_t) i] ? parameters.getParameter (pc::kTargets[target[(size_t) i]].paramId) : nullptr;
            slot.cc = cc[(size_t) i];
            slot.target = target[(size_t) i];
            slot.jump = jump;
            slot.active = active[(size_t) i] && slot.param.load() != nullptr;
            ++slot.generation;
        }
    }
}

bool MaermelAudioProcessor::handleParameterControllers (const juce::MidiMessage& m) noexcept
{
    // Direkte Steuerung: Ein Controller bewegt einen Knopf über seinen ganzen Weg (wie mit der Maus).
    // Zugeordnete Controller werden abgefangen und erreichen das Instrument nicht.
    if (! m.isController())
        return false;
    namespace pc = maermel::control;
    const int cc = m.getControllerNumber();
    const int value = m.getControllerValue();
    bool used = false;
    for (int i = 0; i < pc::kSlots; ++i)
    {
        auto& slot = controlSlots[(size_t) i];
        if (! slot.active.load() || slot.cc.load() != cc)
            continue;
        auto* param = slot.param.load();
        if (param == nullptr)
            continue;
        used = true;

        const int generation = slot.generation.load();
        if (generation != slot.seenGeneration)
        {
            slot.seenGeneration = generation;          // Zuordnung geändert: Abholen neu beginnen
            slot.captured = false;
            slot.lastIncoming = slot.lastSet = -1.0f;
        }

        const float wanted = pc::normalisedFor (pc::kTargets[slot.target.load()], value);
        const float current = param->getValue();

        if (! slot.jump.load())
        {
            // PICK-UP: erst übernehmen, wenn der Regler die aktuelle Stellung erreicht oder überquert.
            // Hat sich der Wert inzwischen anders verändert (Maus, Preset), neu abholen.
            if (slot.captured && slot.lastSet >= 0.0f && std::abs (current - slot.lastSet) > 0.002f)
                slot.captured = false;
            if (! slot.captured)
            {
                // Halber Regelweg (LFO-Hälften, Vel Env positiv): Steht der Wert auf der anderen Seite der Mitte,
                // ist er mit dem Controller nicht erreichbar – dann gilt die Mitte als Abholpunkt (Controller auf 0)
                const int half = pc::kTargets[slot.target.load()].half;
                const float reach = half > 0 ? std::max (current, 0.5f) : half < 0 ? std::min (current, 0.5f) : current;
                const bool close = std::abs (wanted - reach) <= 1.5f / 127.0f;
                const bool crossed = slot.lastIncoming >= 0.0f
                                     && (slot.lastIncoming - reach) * (wanted - reach) <= 0.0f;
                slot.lastIncoming = wanted;
                if (! (close || crossed))
                    continue;
                slot.captured = true;
            }
        }
        slot.lastIncoming = wanted;

        // JUMP bzw. abgeholt: Wert setzen (die Parameterglättung verhindert Knacken).
        // Bewusst ohne begin/endChangeGesture: Die Gesten gehören auf den Message-Thread. Folge: Im Touch- oder
        // Latch-Modus zeichnen manche DAWs die Bewegung evtl. nicht zuverlässig auf. Erst ändern, wenn ein Test mit
        // Automationsaufnahme Probleme zeigt (Konzept, To-Do „Controller → Parameter über den Timer“).
        if (std::abs (wanted - current) > 1.0e-6f)
            param->setValueNotifyingHost (wanted);
        slot.lastSet = wanted;
        controlFeedbackSlot = i;
        ++controlFeedbackCount;
    }
    return used;
}

bool MaermelAudioProcessor::handleInstrumentSwitching (const juce::MidiMessage& m) noexcept
{
    // --- Lernen: nächste Taste bzw. nächster gedrückter Controller wird übernommen ---
    const int learn = learnTarget.load();
    if (learn >= (int) LearnTarget::ParamSlot1)
    {
        if (m.isController() && maermel::control::isAssignable (m.getControllerNumber()))
        {
            learnedValue = m.getControllerNumber();
            learnTarget = 0;
            return true;
        }
        return false;
    }
    if (learn != 0)
    {
        const bool wantsNote = learn == (int) LearnTarget::NextNote || learn == (int) LearnTarget::PrevNote;
        if (wantsNote && m.isNoteOn())
        {
            learnedValue = m.getNoteNumber();
            learnTarget = 0;
            return true;
        }
        if (! wantsNote && m.isController() && m.getControllerValue() >= 64)
        {
            learnedValue = m.getControllerNumber();
            learnTarget = 0;
            return true;
        }
    }

    // --- Noten: schalten um und erzeugen nie einen Ton ---
    if (m.isNoteOnOrOff())
    {
        const int note = m.getNoteNumber();
        if (note == switchNextNote.load() || note == switchPrevNote.load())
        {
            if (m.isNoteOn())
                pendingSteps += (note == switchNextNote.load()) ? 1 : -1;
            return true;
        }
        return false;
    }

    // --- Controller: wie ein Pedal – auslösen ab 64, erst unter 64 wieder bereit ---
    if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const bool high = m.getControllerValue() >= 64;

        if (cc == switchNextCC.load())
        {
            if (high && ! nextCCDown) ++pendingSteps;
            nextCCDown = high;
            return true;
        }
        if (cc == switchPrevCC.load())
        {
            if (high && ! prevCCDown) --pendingSteps;
            prevCCDown = high;
            return true;
        }
    }
    return false;
}

void MaermelAudioProcessor::stepInstrument (int delta)
{
    const auto view = LibraryIndex::filtered (library->getVisibleSnapshot(), libraryFilter);
    if (view == nullptr || delta == 0)
        return;

    const juce::File current = getInstrumentFile();
    juce::Array<juce::File> list;

    if (library->getSettings().getBoolValue ("stepWithinFolder", false))
    {
        // Nur im Ordner des aktuellen Instruments blättern (z. B. eine Gig-Liste)
        if (auto* folder = LibraryIndex::findFolder (*view, current.getParentDirectory()))
            list = folder->instruments;
    }
    if (list.isEmpty())
        list = LibraryIndex::flatten (*view);           // gleiche Reihenfolge wie im Menü
    if (list.isEmpty())
        return;

    int index = list.indexOf (current);
    if (index < 0)
        index = (delta > 0) ? -1 : 0;
    index = ((index + delta) % list.size() + list.size()) % list.size();   // am Ende wieder von vorn
    loadInstrumentFile (list[index]);
}

void MaermelAudioProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    // MIDI-Anzeige: nur Note-On mit Anschlag > 0 (keine Controller, kein Clock/Active Sensing). Auch Noten
    // der Instrumentumschaltung zählen – sie sind ja angekommen.
    if (m.isNoteOn())
    {
        midiNoteVelocity.store (m.getVelocity());
        midiNoteCount.fetch_add (1);
    }
    if (handleInstrumentSwitching (m) || handleParameterControllers (m))
        return;

    // Stand jedes Controllers merken (für loccN/hiccN, z. B. Pedal-Schichten mit locc64)
    if (m.isController())
        synth.controller (m.getControllerNumber(), m.getControllerValue());

    if (m.isNoteOn())
    {
        // Erst die globale Kurve (eigenes Keyboard), dann der Offset des Instruments; Ergebnis 1 ... 127
        const int velocity = maermel::velocity::transform (m.getVelocity(), velocityCurve.load(),
                                                             juce::roundToInt (velOffsetParam->load()));
        synth.noteOn (m.getNoteNumber(), velocity, m.getChannel() - 1);
    }
    else if (m.isNoteOff())
        synth.noteOff (m.getNoteNumber());
    else if (m.isSustainPedalOn())
        synth.sustainPedal (true);
    else if (m.isSustainPedalOff())
        synth.sustainPedal (false);
    else if (m.isPitchWheel())
        synth.pitchBend (m.getPitchWheelValue());
    else if (m.isControllerOfType (1))
        synth.modWheel (m.getControllerValue());   // Modulationsrad für Wheel LFO
    else if (m.isChannelPressure() || m.isAftertouch())
    {
        // Aftertouch wirkt wie das Modulationsrad – je Stimme: Kanal-Aftertouch auf die Noten seines Kanals
        // (bei MPE also auf genau eine Note), polyphoner Aftertouch auf seine Taste. Abschaltbar für Keyboards,
        // die schon bei leichtem Druck hohe Werte senden (z. B. flache Tasten)
        if (aftertouchAsWheel.load() && atWheelParam->load() > 0.5f)   // global und je Instrument
        {
            if (m.isChannelPressure())
                synth.channelPressure (m.getChannel() - 1, m.getChannelPressureValue());
            else
                synth.polyPressure (m.getNoteNumber(), m.getAfterTouchValue());
            aftertouchActive = true;
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        synth.allNotesOff();
    else if (m.isResetAllControllers())
        synth.resetControllers();   // CC121 (seit 1.0.3), z. B. von Cubase beim Stoppen
}

void MaermelAudioProcessor::renderRange (juce::AudioBuffer<float>& buffer, int start, int end) noexcept
{
    const int outChannels = buffer.getNumChannels();

    // Schutz (seit 23.11.2026): Ohne prepareToPlay ist der Zwischenpuffer leer. Die Schleife käme dann mit n = 0
    // nie voran und würde den Audio-Thread für immer blockieren. Normale Hosts rufen prepareToPlay immer vorher auf,
    // fehlerhafte Hosts oder Prüfprogramme nicht unbedingt – dann bleibt der Block einfach still.
    if (renderBuffer.getNumSamples() <= 0)
        return;

    while (start < end)
    {
        const int n = juce::jmin (end - start, renderBuffer.getNumSamples());
        renderBuffer.clear (0, n);
        synth.render (renderBuffer.getWritePointer (0), renderBuffer.getWritePointer (1), n);

        if (outChannels >= 2)
        {
            buffer.addFrom (0, start, renderBuffer, 0, 0, n);
            buffer.addFrom (1, start, renderBuffer, 1, 0, n);
        }
        else if (outChannels == 1)
        {
            buffer.addFrom (0, start, renderBuffer, 0, 0, n, 0.5f);   // Mono-Plugin: L und R zusammenfassen
            buffer.addFrom (0, start, renderBuffer, 1, 0, n, 0.5f);
        }
        start += n;
    }
}

void MaermelAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    takePendingInstrument();
    if (allNotesOffRequested.exchange (false))
    {
        // Wie MIDI „All Notes Off“: Stimmen gehen in die Release-Phase, Pedal aus (allNotesOff setzt seit 1.0.3
        // auch den CC-64-Wert für locc64/hicc64 selbst zurück), Aftertouch zurück. Das Modulationsrad bleibt.
        synth.allNotesOff();
        aftertouchActive = false;
    }
    updateEnvelopeIfChanged();
    updateVoiceSettings();
    updateSampleSettings();
    updateFilterSettings();
    updateLfoSettings();
    if (aftertouchActive && ! (aftertouchAsWheel.load() && atWheelParam->load() > 0.5f))
    {
        synth.resetPressure();   // Option gerade abgeschaltet (global oder je Instrument): Aftertouch zurücksetzen
        aftertouchActive = false;
    }
    synth.setVelocityRange (velocityParam->load());
    synth.setAmpKeyScale (ampKeyScaleParam->load());
    masterGain.setTargetValue (volumeGain (volumeParam->load()));

    // Sample-genau: bis zu jedem MIDI-Ereignis rendern, dann das Ereignis ausführen
    const int total = buffer.getNumSamples();
    int position = 0;
    for (const auto meta : midi)
    {
        const int eventPos = juce::jlimit (0, total, meta.samplePosition);
        renderRange (buffer, position, eventPos);
        position = eventPos;
        // SysEx und andere lange Nachrichten (mehr als 3 Byte) braucht Märmel nicht. getMessage() würde für sie
        // Speicher anfordern – im Audio-Thread zu vermeiden (seit 1.0.3).
        if (meta.numBytes > 3)
            continue;
        handleMidi (meta.getMessage());
    }
    renderRange (buffer, position, total);

    masterGain.applyGain (buffer, total);
    collectFinishedInstrument();
    activeKeyswitch.store (synth.currentKeyswitch());
}

// ---------------------------------------------------------------------------

void MaermelAudioProcessor::timerCallback()
{
    // Vom Audio-Thread angeforderte Instrumentwechsel ausführen
    if (const int steps = pendingSteps.exchange (0))
        stepInstrument (steps);

    syncSwitchSettings();   // Änderungen aus dem Einstellungsdialog (auch anderer Instanzen)
    checkLiveEdit();        // Live-Edit: geänderte SFZ-Datei neu laden

    std::shared_ptr<const maermel::Instrument> toRelease;
    {
        const juce::SpinLock::ScopedLockType lock (instrumentLock);
        toRelease = std::move (retiredInstrument);
        retiredInstrument = nullptr;
    }
    // toRelease wird hier auf dem Message-Thread freigegeben
}

// ===========================================================================
// Live-Edit

juce::File MaermelAudioProcessor::getEditorApplication() const
{
    const juce::String path = library->getSettings().getValue (kEditorPathKey);
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

bool MaermelAudioProcessor::isValidEditor (const juce::File& editor)
{
   #if JUCE_MAC
    if (editor.isDirectory())
        return editor.hasFileExtension ("app");   // Programm-Paket
   #endif
    return editor.existsAsFile();
}

void MaermelAudioProcessor::setLiveEditActive (bool active)
{
    liveEditActive = active;
    candidatePending = false;
    if (active)
    {
        // Überwachung genau der jetzt geladenen Datei; aktueller Stand als Ausgangspunkt
        watchedFile = getInstrumentFile();
        watchedTime = watchedFile.getLastModificationTime();
        watchedSize = watchedFile.getSize();
    }
    else
    {
        watchedFile = juce::File();
    }
}

bool MaermelAudioProcessor::isLiveEditAllowed() const
{
    return library->getSettings().getBoolValue (kLiveEditAllowedKey, false);
}

void MaermelAudioProcessor::logError (const juce::String& text) const
{
    ErrorLog::write (library->getSettings(), logErrorsActive.load(), logHeader(), getInstrumentName(), text);
}

bool MaermelAudioProcessor::openCurrentInstrumentInEditor (juce::String& message)
{
    const bool ok = openInEditorInternal (message);
    if (! ok)
    {
        logError ("Edit SFZ: " + message);
        return false;
    }
    // Live-Edit erlaubt: ab jetzt diese Datei beobachten (bis zum Instrumentwechsel oder Beenden)
    if (isLiveEditAllowed())
        setLiveEditActive (true);
    return true;
}

bool MaermelAudioProcessor::openInEditorInternal (juce::String& message)
{
    const juce::File editor = getEditorApplication();
    if (! isValidEditor (editor))
    {
        message = "No SFZ editor found";
        return false;
    }

    // EXS: erst wie beim Speichern ein SFZ daneben erzeugen (oder ein vorhandenes verwenden)
    // und auf dieses umschalten; ab dann wird das SFZ bearbeitet und beobachtet
    if (getInstrumentFile().hasFileExtension ("exs"))
    {
        juce::String conversionMessage;
        if (! convertCurrentExsToSfz (conversionMessage))
        {
            message = conversionMessage;
            return false;
        }
        message = conversionMessage;
    }

    const juce::File file = getInstrumentFile();
    if (! file.hasFileExtension ("sfz") || ! file.existsAsFile())
    {
        message = "No SFZ file to edit";
        return false;
    }

    juce::StringArray args;
   #if JUCE_MAC
    if (editor.hasFileExtension ("app"))
        args = { "/usr/bin/open", "-a", editor.getFullPathName(), file.getFullPathName() };   // Datei im Programm öffnen
    else
   #endif
        args = { editor.getFullPathName(), file.getFullPathName() };

   #if JUCE_LINUX || JUCE_BSD
    // Linux: eigener Start ohne fork() und ohne die Bibliothekspfade des Hosts (siehe ExternalLaunch.h)
    if (! maermel::external::startProgram (args))
    {
        message = "Could not start the editor";
        return false;
    }
   #else
    // Das Programm läuft unabhängig weiter; das ChildProcess-Objekt beendet es nicht
    editorProcess = std::make_unique<juce::ChildProcess>();
    if (! editorProcess->start (args, 0))
    {
        message = "Could not start the editor";
        return false;
    }
   #endif
    return true;
}

void MaermelAudioProcessor::checkLiveEdit()
{
    if (! liveEditActive || ++liveEditTicks < 10)   // alle 0,5 s (Timer läuft mit 20 Hz)
        return;
    liveEditTicks = 0;

    // Einstellung ausgeschaltet oder anderes Instrument geladen: Überwachung beenden (LED aus).
    // So wird nie eine Datei neu geladen, die zu einem gar nicht mehr gespielten Instrument gehört.
    const juce::File file = getInstrumentFile();
    if (! isLiveEditAllowed() || file != watchedFile || ! file.hasFileExtension ("sfz"))
    {
        setLiveEditActive (false);
        return;
    }

    // Editoren speichern oft über eine temporäre Datei; fehlt die Datei kurz, später erneut prüfen
    if (! file.existsAsFile())
        return;

    // Nur Datum und Größe lesen – die Datei wird nicht geöffnet, kein Konflikt mit dem Editor
    const juce::Time time = file.getLastModificationTime();
    const juce::int64 size = file.getSize();
    if (time == watchedTime && size == watchedSize)
    {
        candidatePending = false;
        return;
    }

    // Erst neu laden, wenn sich seit einer halben Sekunde nichts mehr geändert hat (Speichern abgeschlossen)
    if (candidatePending && time == candidateTime && size == candidateSize)
    {
        watchedTime = time;
        watchedSize = size;
        candidatePending = false;
        liveReloadPending = true;
        loader.request (file);       // Einstellungen und Preset bleiben unverändert
    }
    else
    {
        candidatePending = true;
        candidateTime = time;
        candidateSize = size;
    }
}

void MaermelAudioProcessor::applyParameterValues (const juce::XmlElement* preset)
{
    // Eintrag eines Parameters im Preset. SFZP-Dateien nennen die lesbaren Namen ("cutoff"); die kurze interne ID
    // wird ebenfalls angenommen. nullptr = Parameter fehlt im Preset.
    auto findEntry = [preset] (const juce::String& id) -> const juce::XmlElement*
    {
        if (preset == nullptr)
            return nullptr;
        auto* entry = preset->getChildByAttribute ("id", juce::String (maermel::ids::presetName (id.toRawUTF8())));
        return entry != nullptr ? entry : preset->getChildByAttribute ("id", id);
    };
    // Presets und kopierte Einstellungen bis 1.0.2 (Version 1, auch ohne Angabe): LFO-Sync-Notenwerte umrechnen
    const bool version1 = preset != nullptr && preset->getIntAttribute ("version", 1) < 2;

    for (auto* parameter : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr)
            continue;

        // Gespeichert sind tatsächliche Werte; fehlt ein Parameter im Preset, gilt sein Standardwert
        float value = ranged->convertFrom0to1 (ranged->getDefaultValue());
        const juce::String id = ranged->getParameterID();
        if (auto* entry = findEntry (id))
            if (entry->hasAttribute ("value"))
                value = storedParameterValue (*ranged, entry->getStringAttribute ("value"));   // geprüft, s. o.

        // Umrechnung Version 1 → 2 (siehe kSettingsVersion): nur die Rate eines LFOs, dessen Sync im Preset an ist
        if (version1 && (id == "plforate" || id == "mlforate"))
        {
            auto* sync = findEntry (id.dropLastCharacters (4) + "sync");   // "plforate" → "plfosync"
            if (sync != nullptr && sync->getDoubleAttribute ("value", 0.0) > 0.5)
                value = syncRateFromVersion1 (value);
        }

        ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
    }
}

std::unique_ptr<juce::XmlElement> MaermelAudioProcessor::createSettingsXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (kPresetTag);
    xml->setAttribute ("version", kSettingsVersion);

    for (auto* parameter : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            auto* entry = xml->createNewChildElement ("PARAM");
            // Lesbarer Name in der SFZP-Datei ("cutoff" statt der kurzen internen ID, siehe ParameterIds.h)
            entry->setAttribute ("id", juce::String (maermel::ids::presetName (ranged->getParameterID().toRawUTF8())));
            entry->setAttribute ("value", ranged->convertFrom0to1 (ranged->getValue()));
            entry->setAttribute ("text", ranged->getCurrentValueAsText());   // nur zum Lesen
        }
    }
    return xml;
}

bool MaermelAudioProcessor::convertCurrentExsToSfz (juce::String& message)
{
    const juce::File exsFile = getInstrumentFile();
    const juce::File sfzFile = exsFile.withFileExtension ("sfz");
    auto utf8 = [] (const std::string& t) { return juce::String::fromUTF8 (t.c_str(), (int) t.size()); };

    auto switchToSfz = [this, &sfzFile] (const juce::String& note)
    {
        {
            const juce::ScopedLock sl (infoLock);
            currentFile = sfzFile;
            instrumentName = sfzFile.getFileNameWithoutExtension();
            presetNote = note;
        }
        ++instrumentVersion;
        notifyHostAboutName();
        liveReloadPending = false;
        loader.request (sfzFile);          // ab jetzt aus dem SFZ (und ggf. den kopierten Samples)
        library->requestRefresh();         // im Menü erscheint nun das SFZ statt der EXS
    };

    // Gibt es schon ein SFZ daneben, wird es verwendet und nicht überschrieben
    if (sfzFile.existsAsFile())
    {
        switchToSfz ("Using existing SFZ next to the EXS");
        message = "Using existing SFZ";
        return true;
    }

    const bool copySamples = library->getSettings().getBoolValue ("exsCopySamples", true);
    const juce::String samplesFolderName = exsFile.getFileNameWithoutExtension() + " Samples";

    const auto conversion = maermel::convertExsToSfz (
        exsFile.getFullPathName().replaceCharacter ('\\', '/').toStdString(),
        copySamples ? samplesFolderName.toStdString() : std::string());

    if (! conversion.ok)
    {
        message = "Cannot read EXS: " + utf8 (conversion.error);
        return false;
    }

    // Samples kopieren (vorhandene Dateien gleichen Namens bleiben unangetastet)
    int copied = 0, failed = 0;
    if (! conversion.copies.empty())
    {
        const juce::File folder = exsFile.getSiblingFile (samplesFolderName);
        if (! folder.createDirectory())
        {
            message = "Could not create folder: " + samplesFolderName;
            return false;
        }
        for (const auto& c : conversion.copies)
        {
            const juce::File source = existingFile (c.source);   // auch bei abweichender Groß/klein-Schreibung
            const juce::File target = folder.getChildFile (utf8 (c.targetName));
            if (target.existsAsFile() || source.copyFileTo (target))
                ++copied;
            else
                ++failed;
        }
    }

    if (! sfzFile.replaceWithText (utf8 (conversion.sfzText), false, false, "\n"))
    {
        message = "Could not write SFZ";
        return false;
    }

    juce::String note = "SFZ created from EXS";
    if (copySamples)
        note << ", " << copied << " samples copied to \"" << samplesFolderName << "\"";
    if (failed > 0)
        note << ", " << failed << " could not be copied";
    if (conversion.missing > 0)
        note << ", " << conversion.missing << " samples not found";
    switchToSfz (note);
    message = note;
    return true;
}

// --- Export des aktuellen Instruments -------------------------------------------------------------------
class MaermelAudioProcessor::ExportJob : public juce::Thread
{
public:
    ExportJob (MaermelAudioProcessor& p, juce::File src, juce::File dst, std::unique_ptr<juce::XmlElement> preset)
        : juce::Thread ("Maermel Export"), owner (p), source (std::move (src)), target (std::move (dst)),
          presetXml (std::move (preset)) {}

    // Sicherheitsnetz (seit 1.0.3, wie beim Laden in loadInstrumentNow): Eine Ausnahme im Export-Thread (z. B. kein
    // Speicher mehr) würde sonst den ganzen Host beenden. So scheitert nur dieser Export, mit Meldung.
    void run() override
    {
        try
        {
            runExport();
        }
        catch (const std::bad_alloc&)
        {
            finish (false, "Export failed: not enough memory");
        }
        catch (const std::exception& e)
        {
            finish (false, "Export failed: " + juce::String::fromUTF8 (e.what()));
        }
    }

    void runExport()
    {
        auto utf8 = [] (const std::string& t) { return juce::String::fromUTF8 (t.c_str(), (int) t.size()); };

        // 1. SFZ-Text holen (EXS: im Speicher umgewandelt, Sample-Pfade zeigen auf die Originale)
        std::string text;
        if (source.hasFileExtension ("exs"))
        {
            const auto conversion = maermel::convertExsToSfz (
                source.getFullPathName().replaceCharacter ('\\', '/').toStdString(), std::string());
            if (! conversion.ok)
            {
                finish (false, "Cannot read EXS: " + utf8 (conversion.error));
                return;
            }
            text = conversion.sfzText;
        }
        else
        {
            text = source.loadFileAsString().toStdString();
        }
        const std::string directory = source.getParentDirectory().getFullPathName().replaceCharacter ('\\', '/').toStdString() + "/";
        const auto plan = maermel::planSfzExport (text, directory);
        if (! plan.ok)
        {
            finish (false, utf8 (plan.error));
            return;
        }

        // 2. Samples sammeln: gleiche Dateien im Zielordner werden übersprungen, verschiedene bekommen
        //    eindeutige Namen („C4 (2).wav“)
        const juce::String folderName = target.getFileNameWithoutExtension() + " Samples";
        const juce::File folder = target.getSiblingFile (folderName);
        if (! plan.samples.empty() && ! folder.createDirectory())
        {
            finish (false, "Could not create folder: " + folderName);
            return;
        }

        owner.exportTotal = (int) plan.samples.size();
        owner.exportDone = 0;
        std::vector<std::string> finalNames;
        std::set<juce::String> taken;                     // in diesem Export vergeben (ohne Groß/klein, wie auf dem Mac)
        for (const auto& sample : plan.samples)
        {
            if (threadShouldExit())
            {
                finish (false, "Export cancelled");
                return;
            }
            const juce::File from = existingFile (sample.source);   // auch bei abweichender Groß/klein-Schreibung
            if (! from.existsAsFile())
            {
                finish (false, "Sample not found: " + from.getFullPathName());
                return;
            }

            juce::String name;
            for (int number = 1; ; ++number)
            {
                name = utf8 (maermel::numberedFileName (sample.fileName, number));
                if (taken.count (name.toLowerCase()) != 0)
                    continue;
                const juce::File to = folder.getChildFile (name);
                if (! to.exists())
                {
                    // Nicht unterbrechbar (siehe Destruktor, „bekannte Schwachstelle“): große Dateien später in
                    // Blöcken kopieren und dazwischen threadShouldExit() prüfen.
                    if (! from.copyFileTo (to))
                    {
                        finish (false, "Could not copy: " + name);
                        return;
                    }
                    break;
                }
                if (to == from || to.hasIdenticalContentTo (from))
                    break;                                 // schon vorhanden und gleich: wiederverwenden
            }
            taken.insert (name.toLowerCase());
            finalNames.push_back (name.toStdString());
            ++owner.exportDone;
        }

        // 3. SFZ und Preset schreiben (Preset verweist relativ auf die SFZ daneben)
        const juce::String sfzText = utf8 (maermel::fillExportText (plan, folderName.toStdString(), finalNames));
        if (! target.replaceWithText (sfzText, false, false, "\n"))
        {
            finish (false, "Could not write " + target.getFileName());
            return;
        }
        presetXml->setAttribute ("instrument", target.getFileName());
        presetXml->setAttribute ("instrumentPath", pathWithoutUserName (target));   // ohne Benutzernamen
        if (! presetXml->writeTo (target.withFileExtension ("sfzp")))
        {
            finish (false, "Could not write " + target.withFileExtension ("sfzp").getFileName());
            return;
        }

        finish (true, juce::String ((int) plan.samples.size()) + " samples");
    }

private:
    void finish (bool ok, const juce::String& message)
    {
        {
            const juce::ScopedLock sl (owner.infoLock);
            owner.exportMessage = message;
        }
        if (! ok)
            owner.logError ("Export " + target.getFullPathName() + ": " + message);
        owner.exportState = ok ? exportFinished : exportFailed;
    }

    MaermelAudioProcessor& owner;
    juce::File source, target;
    std::unique_ptr<juce::XmlElement> presetXml;
};

bool MaermelAudioProcessor::canExportInstrument() const
{
    const juce::File file = getInstrumentFile();
    return file.existsAsFile() && file.hasFileExtension ("sfz;exs") && currentMissingSamples.load() == 0
           && getInstrumentName().isNotEmpty() && exportState.load() != exportRunning;
}

void MaermelAudioProcessor::startExport (const juce::File& targetSfz)
{
    if (exportState.load() == exportRunning || ! canExportInstrument())
        return;
    if (exportJob != nullptr)
        exportJob->stopThread (2000);                     // vorheriger Export ist beendet; Thread-Objekt freigeben
    exportState = exportRunning;
    exportDone = 0;
    exportTotal = 0;
    // Preset mit den aktuellen Einstellungen hier (Message-Thread) erzeugen, geschrieben wird es im Hintergrund
    exportJob = std::make_unique<ExportJob> (*this, getInstrumentFile(), targetSfz.withFileExtension ("sfz"),
                                             createSettingsXml());
    exportJob->startThread();
}

bool MaermelAudioProcessor::savePreset()
{
    const bool ok = savePresetInternal();
    if (! ok)
        logError ("Save failed: " + lastSaveMessage);
    return ok;
}

bool MaermelAudioProcessor::savePresetInternal()
{
    lastSaveMessage.clear();
    juce::File instrument = getInstrumentFile();
    if (! instrument.existsAsFile())
        return false;

    // EXS: erst ein SFZ daneben erzeugen; das Preset verweist dann nur auf SFZ-Dateien
    if (instrument.hasFileExtension ("exs"))
    {
        if (! convertCurrentExsToSfz (lastSaveMessage))
            return false;
        instrument = getInstrumentFile();
    }

    auto xml = createSettingsXml();
    xml->setAttribute ("instrument", instrument.getFileName());                  // relativ: Preset liegt daneben
    xml->setAttribute ("instrumentPath", pathWithoutUserName (instrument));      // Rückfall, ohne Benutzernamen
    return xml->writeTo (instrument.withFileExtension ("sfzp"));
}

juce::String MaermelAudioProcessor::copySettingsAsText() const
{
    return createSettingsXml()->toString();
}

bool MaermelAudioProcessor::isSettingsText (const juce::String& text)
{
    if (! text.trimStart().startsWith ("<"))
        return false;
    auto xml = juce::XmlDocument::parse (text);
    return isPresetElement (xml.get());
}

bool MaermelAudioProcessor::pasteSettingsFromText (const juce::String& text)
{
    if (! text.trimStart().startsWith ("<"))
        return false;
    auto xml = juce::XmlDocument::parse (text);   // nur einmal lesen (bis 1.0.2 zweimal über isSettingsText)
    if (! isPresetElement (xml.get()))
        return false;
    applyParameterValues (xml.get());
    return true;
}

void MaermelAudioProcessor::loadInstrumentFile (const juce::File& requestedFile, bool applyPreset)
{
    {
        const juce::ScopedLock sl (infoLock);
        unresolvedPath.clear();                    // ein geladenes Instrument ersetzt ein nicht gefundenes
        unresolvedRelPath.clear();
        unresolvedFingerprint.clear();
    }
    juce::File file = requestedFile;
    juce::String note;   // Hinweis für die Info-Zeile

    if (requestedFile.hasFileExtension ("sfzp"))
    {
        // Preset gewählt: Instrument dazu suchen, Einstellungen immer übernehmen
        auto preset = juce::XmlDocument::parse (requestedFile);
        if (! isPresetElement (preset.get()))
            return;
        file = requestedFile.getSiblingFile (preset->getStringAttribute ("instrument"));
        if (! file.existsAsFile())
            file = fileFromStoredPath (preset->getStringAttribute ("instrumentPath"));
        if (! file.existsAsFile())
        {
            {
                const juce::ScopedLock sl (infoLock);
                instrumentInfo = juce::String ("Instrument for preset not found: ")
                               + preset->getStringAttribute ("instrument");
            }
            ErrorLog::write (library->getSettings(), logErrorsActive.load(), logHeader(), requestedFile.getFileNameWithoutExtension(),
                             juce::String ("Instrument for preset not found: ")
                             + preset->getStringAttribute ("instrument") + " (" + requestedFile.getFullPathName() + ")");
            ++instrumentVersion;
            return;
        }
        deferSettings (file, preset.get());        // gesetzt erst mit dem geladenen Instrument (siehe Header)
        note = "Preset: " + requestedFile.getFileName();
    }
    else if (applyPreset && ! fixActive)
    {
        // Instrument gewählt, LOCK aus: gleichnamiges Preset oder Standardwerte
        const juce::File presetFile = file.withFileExtension ("sfzp");
        auto preset = presetFile.existsAsFile() ? juce::XmlDocument::parse (presetFile) : nullptr;
        const bool valid = isPresetElement (preset.get());
        deferSettings (file, valid ? preset.get() : nullptr);   // erst mit dem geladenen Instrument
        note = valid ? "Preset: " + presetFile.getFileName() : juce::String ("Default settings (no preset)");
    }
    else
    {
        cancelDeferredSettings();                 // Einstellungen bleiben; frühere, noch wartende verfallen
        if (fixActive)
            note = "LOCK: settings kept";
    }

    {
        const juce::ScopedLock sl (infoLock);
        currentFile = file;                       // sofort, damit Pfeiltasten vom neuen Stand aus weiterzählen
        instrumentName = file.getFileNameWithoutExtension();
        presetNote = note;                        // wird vom Loader-Thread gelesen, daher unter infoLock
        instrumentInfo = "Loading ...";
    }
    ++instrumentVersion;
    notifyHostAboutName();
    liveReloadPending = false;   // ein bewusst gewähltes Instrument ist nie ein Live-Edit-Neuladen
    loader.request (file);
}

void MaermelAudioProcessor::unloadInstrument()
{
    // Instrument entladen (seit 1.0.3), z. B. wenn ein DAW-Projekt in eine laufende Instanz geladen wird und sein
    // Instrument hier nicht gefunden wird: Stille statt des bisherigen Instruments. So stimmen Klang, Anzeige und
    // das, was beim nächsten Speichern ins Projekt geschrieben wird, überein – wie bei einem frisch geöffneten Projekt.
    // Läuft gerade noch ein Ladevorgang, wird er durch diese neue Anforderung verworfen (hasNewerRequest).
    bool wasLoaded = false;
    {
        const juce::ScopedLock sl (infoLock);
        wasLoaded = (currentFile != juce::File());
        currentFile = juce::File();
        instrumentName = "No Instrument";   // wie in einem frischen Projekt (bis 25.11.2026 leer – Display ohne Text)
        presetNote.clear();
        currentAlert.clear();               // die Warnung des alten Instruments gilt nicht mehr
    }
    if (! wasLoaded)
        return;   // nichts geladen (frisches Projekt): wie bisher nichts tun
    ++instrumentVersion;
    notifyHostAboutName();
    liveReloadPending = false;
    loader.request (juce::File());   // leere Datei = entladen (loadInstrumentNow)
}

void MaermelAudioProcessor::notifyHostAboutName()
{
    // "Programm geändert": Die DAW fragt daraufhin getProgramName() neu ab
    // (Logic: Name über dem Plug-in-Fenster, bei nicht umbenannter Spur auch der Spurname).
    // Nur vom Message-Thread aus; beim Öffnen eines Projekts (evtl. anderer Thread) fragt die DAW selbst ab.
    if (juce::MessageManager::existsAndIsCurrentThread())
        updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}

juce::File MaermelAudioProcessor::getInstrumentFile() const
{
    const juce::ScopedLock sl (infoLock);
    return currentFile;
}

void MaermelAudioProcessor::loadInstrumentNow (const juce::File& file)
{
    // Leere Datei: Instrument entladen (unloadInstrument, seit 1.0.3). Ein leeres Instrument übergeben – der
    // Audio-Thread blendet das bisherige aus, der Timer gibt es frei. Keine Meldungen; die Hinweise zum nicht
    // gefundenen Instrument (Loading notes) bleiben stehen.
    if (file == juce::File())
    {
        currentMissingSamples = 0;
        lastInstrument.reset();
        lastInstrumentBytes = 0;
        handOverInstrument (std::make_shared<const maermel::Instrument>());
        ++instrumentVersion;
        return;
    }

    const std::string path = file.getFullPathName().replaceCharacter ('\\', '/').toStdString();

    // Speicherschutz: Die Samples eines Instruments dürfen höchstens die Hälfte des Arbeitsspeichers belegen
    // (der Rest bleibt für System, DAW und andere Plugins). Passt das neue Instrument nur ohne das aktuelle,
    // wird das aktuelle vorher freigegeben (kurze Stille statt Auslagern auf die Platte). Ist es allein schon zu
    // groß, wird es nicht geladen – rote Meldung „NOT ENOUGH MEMORY“, Einzelheiten in „Loading notes“.
    const uint64_t ramBytes = (uint64_t) juce::jmax (0, juce::SystemStats::getMemorySizeInMegabytes()) * 1024u * 1024u;
    const uint64_t budget = ramBytes / 2;
    // Zwei Stufen (siehe maermel::MemoryGuard): schnell über die Dateigrößen, genau aus den Kopfdaten nur wenn es
    // knapp wird. budget 0 (Arbeitsspeicher unbekannt) = keine Prüfung, wie bisher laden.
    maermel::MemoryGuard guard;
    guard.budgetBytes = budget;
    guard.currentBytes = lastInstrumentBytes;
    guard.releaseCurrent = [this] { releaseInstrumentBeforeLoading(); };

    std::unique_ptr<maermel::Instrument> loaded;
    try
    {
        // Nicht abbrechbar (siehe Destruktor, „bekannte Schwachstelle“): Abbruch-Flag ist für eine spätere Version geplant
        loaded = maermel::loadAnyInstrument (path, guard);   // .sfz oder .exs
    }
    catch (const std::bad_alloc&)
    {
        // Sicherheitsnetz: Speicher reichte trotz Schätzung nicht – nur dieses Laden scheitert, nicht der Host.
        // Bereits geladene Samples wurden beim Abbruch automatisch freigegeben.
        loaded = std::make_unique<maermel::Instrument>();
        loaded->sfzPath = path;
        loaded->notEnoughMemory = true;
        loaded->warnings.push_back ("Not enough memory while loading the samples - instrument not loaded");
    }
    catch (const std::exception& e)
    {
        // Sicherheitsnetz für alles andere (z. B. eine beschädigte Datei, die eine Bibliotheksfunktion ablehnt):
        // Eine Ausnahme im Lade-Thread würde sonst den ganzen Host beenden. Das Instrument bleibt leer.
        loaded = std::make_unique<maermel::Instrument>();
        loaded->sfzPath = path;
        loaded->warnings.push_back (std::string ("Instrument could not be loaded: ") + e.what());
    }
    if (loaded->notEnoughMemory)
        loaded->warnings.push_back ("This computer has " + std::to_string (ramBytes / (1024u * 1024u))
                                    + " MB of memory; Maermel uses at most half of it for one instrument.");
    {
        // Instrument wurde über die Suche gefunden (DAW-Projekt von einem anderen Rechner): Hinweis zuerst
        const juce::ScopedLock sl (infoLock);
        if (locateNote.isNotEmpty() && file == locateNoteFile)
            loaded->warnings.insert (loaded->warnings.begin(), locateNote.toStdString());
        locateNote.clear();
        locateNoteFile = juce::File();
    }
    // Fingerabdruck der Sample-Namen für das DAW-Projekt (liest nur den Text, keine Samples)
    const juce::String fingerprint (maermel::fingerprintToString (maermel::sampleFingerprint (path)));
    std::shared_ptr<const maermel::Instrument> instrument = std::move (loaded);

    // Meldungen der Engine sind UTF-8 (Umlaute)
    auto fromEngine = [] (const std::string& t) { return juce::String::fromUTF8 (t.c_str(), (int) t.size()); };

    juce::String info (fromEngine (instrument->summary()));
    const int maxWarnings = 5;
    for (size_t i = 0; i < instrument->warnings.size() && i < (size_t) maxWarnings; ++i)
        info << "\n" << fromEngine (instrument->warnings[i]);
    if (instrument->warnings.size() > (size_t) maxWarnings)
        info << "\n... and " << (int) (instrument->warnings.size() - (size_t) maxWarnings) << " more notes";

    // Wurde inzwischen schon ein anderes Instrument gewählt, dieses verwerfen
    if (loader.hasNewerRequest())
        return;

    currentMissingSamples = instrument->missingSamples;   // Export nur, wenn alle Samples gefunden wurden

    if (instrument->notEnoughMemory)
        ++memoryAlert;                                    // rote Meldung; abgelehntes Instrument ist stumm
    if (instrument->fileMissing)
    {
        missingFileExs = file.hasFileExtension ("exs");
        ++missingFileAlert;                               // rote Meldung „NO SFZ !!“ bzw. „NO EXS !!“
    }

    // Fehlende Samples: rote Meldung im Display (Einzelheiten im Logfile)
    if (instrument->missingSamples > 0)
    {
        noSamplesLoaded = (instrument->sampleCount == 0);
        ++missingSamplesAlert;
    }

    // Alle Hinweise vollständig: für die Tafel „Loading notes“ und das Logfile (im Display nur die ersten fünf)
    {
        juce::String notes;
        if (! instrument->warnings.empty() || instrument->regions.empty())
        {
            notes = fromEngine (instrument->summary()) + "\n" + file.getFullPathName() + "\n";
            for (const auto& w : instrument->warnings)
                notes << "\n" << fromEngine (w);
        }
        // Rote Warnung dieses Ladevorgangs (leer = keine). Gleiche Reihenfolge wie im Editor: Kommen mehrere Gründe
        // zusammen, zeigt er die letzte – hier gewinnt deshalb ebenfalls die letzte Zuweisung.
        juce::String alert;
        if (instrument->missingSamples > 0)
            alert = (instrument->sampleCount == 0) ? "NO SAMPLES !!" : "MISSING SAMPLES !!";
        if (instrument->fileMissing)
            alert = file.hasFileExtension ("exs") ? "NO EXS !!" : "NO SFZ !!";
        if (instrument->notEnoughMemory)
            alert = "NOT ENOUGH MEMORY !!";
        {
            const juce::ScopedLock sl (infoLock);
            currentAlert = alert;
            loadingNotes = notes;
            loadingNotesInstrument = file.getFileNameWithoutExtension();
            currentFingerprint = fingerprint;
            currentFingerprintFile = file;
        }
        if (notes.isNotEmpty())
        {
            juce::String entry = fromEngine (instrument->summary()) + "  (" + file.getFullPathName() + ")";
            for (const auto& w : instrument->warnings)
                entry << "\n" << fromEngine (w);
            ErrorLog::write (library->getSettings(), logErrorsActive.load(), logHeader(), file.getFileNameWithoutExtension(), entry);
        }
    }

    const bool liveReload = liveReloadPending.exchange (false);
    if (liveReload && instrument->regions.empty())
    {
        ErrorLog::write (library->getSettings(), logErrorsActive.load(), logHeader(), file.getFileNameWithoutExtension(),
                         juce::String ("Live edit: changed file has no playable regions, previous instrument kept"));
        // Live-Edit: Die geänderte Datei lässt sich (noch) nicht spielen – bisheriges Instrument behalten
        const juce::ScopedLock sl (infoLock);
        instrumentInfo = juce::String ("Live edit: the changed file has no playable regions - "
                                        "the previous instrument keeps playing.\n") + info;
        ++instrumentVersion;
        return;
    }

    {
        const juce::ScopedLock sl (infoLock);
        if (liveReload)
            info << "\nLive edit: reloaded at " << juce::Time::getCurrentTime().toString (false, true, true, true);
        else if (presetNote.isNotEmpty())
            info << "\n" << presetNote;
        instrumentInfo = info;
    }

    // Speicherschutz: Ab hier wird dieses Instrument übergeben – erst jetzt merken (seit 1.0.3). Bis 1.0.2 geschah
    // das weiter oben, auch für ein beim Live-Edit verworfenes Instrument; der Speicherschutz rechnete beim nächsten
    // Laden dann mit diesem (bereits freigegebenen) statt mit dem tatsächlich spielenden.
    lastInstrument = instrument;
    lastInstrumentBytes = instrument->sampleMemoryBytes;

    // Warten Einstellungen auf genau dieses Instrument (Instrumentwechsel mit Preset/Standardwerten), übergibt es
    // der Message-Thread zusammen mit ihnen (loadFinished → handleAsyncUpdate). Sonst sofort wie bisher.
    {
        const juce::ScopedLock sl (deferredLock);
        if (deferred.active && deferred.file == file)
        {
            deferred.loaded = std::move (instrument);
            ++instrumentVersion;
            return;
        }
    }
    handOverInstrument (std::move (instrument));
    ++instrumentVersion;
}

void MaermelAudioProcessor::handOverInstrument (std::shared_ptr<const maermel::Instrument> instrument)
{
    std::shared_ptr<const maermel::Instrument> notYetPlayed;
    {
        const juce::SpinLock::ScopedLockType lock (instrumentLock);
        notYetPlayed = std::move (pendingInstrument);   // falls noch ein älteres wartete
        pendingInstrument = std::move (instrument);
    }
    // notYetPlayed wird hier (außerhalb des Audio-Threads) freigegeben
}

void MaermelAudioProcessor::deferSettings (const juce::File& file, const juce::XmlElement* preset)
{
    std::shared_ptr<const maermel::Instrument> superseded;   // außerhalb der Sperre freigeben
    {
        const juce::ScopedLock sl (deferredLock);
        deferred.active = true;
        deferred.file = file;
        deferred.preset = (preset != nullptr) ? std::make_unique<juce::XmlElement> (*preset) : nullptr;
        superseded = std::move (deferred.loaded);           // ein noch nicht übergebenes älteres Instrument verfällt
    }
}

void MaermelAudioProcessor::cancelDeferredSettings()
{
    std::shared_ptr<const maermel::Instrument> superseded;
    {
        const juce::ScopedLock sl (deferredLock);
        deferred.active = false;
        deferred.preset.reset();
        superseded = std::move (deferred.loaded);
    }
}

void MaermelAudioProcessor::loadFinished (const juce::File& file)
{
    // Auch wenn das Laden abgebrochen wurde (z. B. kein Instrument übergeben): Die Einstellungen sollen trotzdem
    // gesetzt werden, wie früher. handleAsyncUpdate prüft selbst, ob noch alles zusammenpasst.
    const juce::ScopedLock sl (deferredLock);
    if (deferred.active && deferred.file == file)
        triggerAsyncUpdate();
}

void MaermelAudioProcessor::handleAsyncUpdate()
{
    // Message-Thread. Erst die Einstellungen, unmittelbar danach das Instrument – so spielen altes Instrument und
    // neue Einstellungen höchstens einen Audioblock lang zusammen.
    std::unique_ptr<juce::XmlElement> preset;
    std::shared_ptr<const maermel::Instrument> instrument;
    {
        const juce::ScopedLock sl (deferredLock);
        // Inzwischen abgebrochen oder durch einen neueren Wechsel ersetzt (deferSettings hat dann Datei und Werte
        // überschrieben und ein älteres geladenes Instrument verworfen): nichts zu tun bzw. die neuen Werte warten
        // auf ihr eigenes Instrument
        if (! deferred.active || (deferred.loaded == nullptr && loader.hasNewerRequest()))
            return;
        preset = std::move (deferred.preset);
        instrument = std::move (deferred.loaded);
        deferred.active = false;
    }
    applyParameterValues (preset.get());                     // nullptr = Standardwerte
    if (instrument != nullptr)
        handOverInstrument (std::move (instrument));
}

void MaermelAudioProcessor::releaseInstrumentBeforeLoading()
{
    // Ein leeres Instrument übergeben; der Audio-Thread blendet das aktuelle aus, der Timer gibt es frei.
    // Warten, bis es wirklich aus dem Speicher ist (höchstens 3 s – läuft gerade kein Audio, z. B. Host
    // gestoppt, wird trotzdem geladen; die Schätzung hat ja ergeben, dass das neue allein passt).
    std::shared_ptr<const maermel::Instrument> notYetPlayed;
    {
        const juce::SpinLock::ScopedLockType lock (instrumentLock);
        notYetPlayed = std::move (pendingInstrument);
        pendingInstrument = std::make_shared<const maermel::Instrument>();
    }
    notYetPlayed.reset();
    for (int i = 0; i < 60 && ! lastInstrument.expired() && ! loader.threadShouldExit(); ++i)
        juce::Thread::sleep (50);
    lastInstrument.reset();
    lastInstrumentBytes = 0;
}

juce::String MaermelAudioProcessor::getInstrumentName() const
{
    const juce::ScopedLock sl (infoLock);
    return instrumentName;
}

juce::String MaermelAudioProcessor::getInstrumentInfo() const
{
    const juce::ScopedLock sl (infoLock);
    return instrumentInfo;
}

// ---------------------------------------------------------------------------

void MaermelAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    const juce::File file = getInstrumentFile();
    const juce::String relPath = libraryRelativePath (file);   // außerhalb von infoLock (Library hat eigene Sperre)
    {
        const juce::ScopedLock sl (infoLock);
        if (file == juce::File() && unresolvedPath.isNotEmpty())
        {
            // Instrument wurde auf diesem Rechner nicht gefunden: Angaben unverändert zurückschreiben
            state.setProperty ("sfzPath", unresolvedPath, nullptr);
            state.setProperty ("libraryRelPath", unresolvedRelPath, nullptr);
            state.setProperty ("sampleFingerprint", unresolvedFingerprint, nullptr);
        }
        else
        {
            state.setProperty ("sfzPath", file.getFullPathName(), nullptr);
            state.setProperty ("libraryRelPath", relPath, nullptr);   // '/' als Trenner, leer = nicht in der Library
            state.setProperty ("sampleFingerprint", currentFingerprintFile == file ? currentFingerprint : juce::String(),
                               nullptr);
        }
        state.setProperty ("fix", fixActive.load(), nullptr);
    }
    state.setProperty ("stateVersion", kSettingsVersion, nullptr);   // seit 1.0.3, siehe kSettingsVersion
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MaermelAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    // Noch wartende Einstellungen eines vorherigen Instrumentwechsels verfallen: Ab jetzt gilt das Projekt. Sonst
    // könnten sie nach dem Laden die gerade gesetzten Projektwerte überschreiben (seit 1.0.3).
    cancelDeferredSettings();

    auto state = juce::ValueTree::fromXml (*xml);
    // Projekte von vor dem 12.10.2026: lesbare IDs ("cutoff" …) auf die kurzen internen umsetzen (ParameterIds.h)
    for (auto child : state)
        if (child.hasProperty ("id"))
            child.setProperty ("id", juce::String (maermel::ids::idFromPresetName (child.getProperty ("id").toString().toRawUTF8())),
                               nullptr);

    // Gespeicherte Werte prüfen, bevor JUCE sie übernimmt (seit 1.0.3, siehe storedParameterValue): NaN, unlesbarer
    // Text und Werte außerhalb des Bereichs werden zum Standardwert bzw. auf den Bereich begrenzt
    for (auto child : state)
        if (child.hasProperty ("id") && child.hasProperty ("value"))
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameters.getParameter (child.getProperty ("id").toString())))
                child.setProperty ("value", storedParameterValue (*ranged, child.getProperty ("value").toString()), nullptr);

    // Projekte bis 1.0.2 (ohne "stateVersion"): LFO-Sync-Notenwerte umrechnen (siehe kSettingsVersion)
    if ((int) state.getProperty ("stateVersion", 1) < 2)
    {
        for (const char* prefix : { "plfo", "mlfo" })
        {
            const juce::String id (prefix);
            auto rate = state.getChildWithProperty ("id", id + "rate");
            auto sync = state.getChildWithProperty ("id", id + "sync");
            if (rate.isValid() && sync.isValid() && (float) sync.getProperty ("value", 0.0f) > 0.5f)
                rate.setProperty ("value", syncRateFromVersion1 ((float) rate.getProperty ("value", kLfoDefaultKnob)), nullptr);
        }
    }

    const juce::String path = state.getProperty ("sfzPath").toString();
    // LOCK-Schalter. Die Eigenschaft heißt aus Kompatibilität weiter "fix" (so hieß der Knopf bis Oktober 2026),
    // damit ältere Projekte ihren Zustand behalten. Bitte nicht umbenennen.
    fixActive = (bool) state.getProperty ("fix", false);
    parameters.replaceState (state);

    if (path.isNotEmpty())
    {
        // Gespeicherter Pfad, sonst Pfad relativ zur Library, sonst Suche in der Library (siehe locateInstrument)
        const juce::String relPath = state.getProperty ("libraryRelPath").toString();
        const juce::String fingerprint = state.getProperty ("sampleFingerprint").toString();
        juce::String report;
        const juce::File file = locateInstrument (path, relPath, fingerprint, report);
        if (file != juce::File())
        {
            // Ruhig bleiben, wenn das Instrument im gleichnamigen Ordner gefunden wurde (z. B. auf beiden Rechnern
            // in „01 Pianos“) – der Normalfall auf einem zweiten Rechner mit anderem Library-Pfad (seit 19.10.2026).
            // Dann nur ins Logfile; ein Hinweis in „Loading notes“ nur bei einem ganz anderen Ordner.
            auto folderKey = [] (const juce::String& name)
            {
                return juce::String (maermel::unicode::composed (name.toLowerCase().toStdString()));
            };
            const juce::String savedParent = path.replaceCharacter ('\\', '/').upToLastOccurrenceOf ("/", false, false)
                                                 .fromLastOccurrenceOf ("/", false, false);
            const bool sameFolderName = savedParent.isNotEmpty()
                                        && folderKey (savedParent) == folderKey (file.getParentDirectory().getFileName());
            if (report.isNotEmpty() && sameFolderName)
            {
                ErrorLog::write (library->getSettings(), logErrorsActive.load(), logHeader(),
                                 file.getFileNameWithoutExtension(), report);
                report.clear();
            }
            {
                const juce::ScopedLock sl (infoLock);
                locateNote = report;                     // leer, wenn der gespeicherte Pfad stimmte oder der Ordner gleich heißt
                locateNoteFile = file;
            }
            loadInstrumentFile (file, false);   // Einstellungen kommen aus dem DAW-Projekt, nicht aus dem Preset
        }
        else
        {
            const juce::String name = path.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false)
                                          .upToLastOccurrenceOf (".", false, false);
            juce::String notes = "Instrument not found: " + name + "\nSaved path: " + path;
            if (relPath.isNotEmpty())
                notes << "\nPath in the library: " << relPath;
            notes << "\n\n" << report;
            // Spielte in dieser Instanz noch ein anderes Instrument (Projekt in laufende Instanz geladen), wird es
            // entladen – Stille statt des alten Klangs, und beim Speichern bleibt der gesuchte Pfad erhalten (1.0.3)
            unloadInstrument();
            {
                const juce::ScopedLock sl (infoLock);
                unresolvedPath = path;                   // beim Speichern des Projekts unverändert zurückschreiben
                unresolvedRelPath = relPath;
                unresolvedFingerprint = fingerprint;
                loadingNotes = notes;
                loadingNotesInstrument = name;
                instrumentInfo = "Instrument not found: " + name;
                currentAlert = "INSTRUMENT NOT FOUND !!";
            }
            ErrorLog::write (library->getSettings(), logErrorsActive.load(), logHeader(), name, notes);
            ++missingInstrumentAlert;                    // rote Meldung „INSTRUMENT NOT FOUND !!“
            ++instrumentVersion;
        }
    }
    else
    {
        // Projekt ohne Instrument (gespeichert, als keins geladen war): ebenfalls kein Instrument (seit 1.0.3).
        // Bis 1.0.2 spielte ein zuvor geladenes einfach weiter und wurde beim nächsten Speichern mitgeschrieben.
        unloadInstrument();
        const juce::ScopedLock sl (infoLock);
        unresolvedPath.clear();
        unresolvedRelPath.clear();
        unresolvedFingerprint.clear();
    }
}

juce::String MaermelAudioProcessor::libraryRelativePath (const juce::File& file) const
{
    const juce::File root = library->getRootFolder();
    if (file == juce::File() || ! root.isDirectory() || ! file.isAChildOf (root))
        return {};
    return file.getRelativePathFrom (root).replaceCharacter ('\\', '/');
}

// Instrument eines DAW-Projekts auf diesem Rechner finden (z. B. Library an anderer Stelle als beim Speichern):
//  1. der gespeicherte vollständige Pfad
//  2. der Pfad relativ zur Library – gespeichert oder (ältere Projekte) aus dem vollständigen Pfad abgeleitet:
//     alles hinter dem Ordner, der so heißt wie die Library dieses Rechners
//  3. Suche nach Dateien gleichen Namens in der Library; Auswahl über den Fingerabdruck der Sample-Namen
//     (mindestens die Hälfte gemeinsam), Vorrang für den gleichen Unterordner; bei Gleichstand wird nichts geladen
// report: Hinweis für Loading notes (leer, wenn der gespeicherte Pfad stimmte)
juce::File MaermelAudioProcessor::locateInstrument (const juce::String& savedPath, const juce::String& savedRelPath,
                                                    const juce::String& fingerprint, juce::String& report) const
{
    report.clear();
    const juce::String unified = savedPath.replaceCharacter ('\\', '/');
    const juce::String fileName = unified.fromLastOccurrenceOf ("/", false, false);
    const juce::String parentName = unified.upToLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("/", false, false);

    // 1. gespeicherter Pfad: "~/…" (seit 23.11.2026) oder vollständig (ältere Projekte). Ein Pfad von einem anderen
    //    System ist hier evtl. gar kein gültiger absoluter Pfad – fileFromStoredPath gibt dann keine Datei zurück.
    {
        const juce::File saved = fileFromStoredPath (savedPath);
        if (saved != juce::File() && saved.existsAsFile())
            return saved;
    }

    const juce::File root = library->getRootFolder();
    if (! root.isDirectory())
    {
        report = "No library folder set on this computer - cannot search for the instrument.";
        return {};
    }
    if (fileName.isEmpty())
        return {};

    const juce::String savedNote = "\n(saved path: " + savedPath + ")\nSave the project to store the new location.";
    auto shown = [&root] (const juce::File& f) { return f.getRelativePathFrom (root).replaceCharacter ('\\', '/'); };

    // 2. Pfad relativ zur Library
    juce::String rel = savedRelPath.replaceCharacter ('\\', '/');
    if (rel.isEmpty())
    {
        const juce::String marker = "/" + root.getFileName() + "/";
        const int pos = unified.lastIndexOfIgnoreCase (marker);
        if (pos >= 0)
            rel = unified.substring (pos + marker.length());
    }
    if (rel.isNotEmpty() && ! rel.startsWith ("/") && ! rel.startsWith ("..") && ! rel.contains ("/../"))
    {
        const juce::File candidate = root.getChildFile (rel.replaceCharacter ('/', juce::File::getSeparatorChar()));
        if (candidate.existsAsFile())
        {
            report = "Instrument not found at the saved path; loaded from the same place in the library: " + shown (candidate)
                   + savedNote;
            return candidate;
        }
    }

    // 3. Suche nach Dateien gleichen Namens (ohne Groß/klein; Umlaute in beiden Unicode-Formen)
    auto key = [] (const juce::String& s) { return maermel::unicode::composed (s.toLowerCase().toStdString()); };
    const std::string wanted = key (fileName);
    const juce::String extension = fileName.fromLastOccurrenceOf (".", true, false);
    std::vector<juce::File> candidates;
    // Alle Dateien durchgehen und die Endung selbst prüfen: Der Platzhalter "*.sfz" des Iterators unterscheidet
    // auf Linux Groß/klein, „Piano.SFZ“ würde sonst nicht gefunden (seit 1.0.3). Gelesen wird dadurch nicht mehr –
    // der Iterator listet den Ordner ohnehin vollständig und filtert erst danach.
    for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles))
        if (entry.getFile().hasFileExtension (extension) && key (entry.getFile().getFileName()) == wanted)
            candidates.push_back (entry.getFile());

    std::vector<std::string> parents;
    std::vector<std::vector<uint32_t>> prints;
    for (const auto& c : candidates)
    {
        parents.push_back (c.getParentDirectory().getFileName().toStdString());
        prints.push_back (maermel::sampleFingerprint (c.getFullPathName().replaceCharacter ('\\', '/').toStdString()));
    }
    const auto stored = maermel::fingerprintFromString (fingerprint.toStdString());
    const auto choice = maermel::chooseCandidate (parents, prints, parentName.toStdString(), stored);

    if (choice.index >= 0)
    {
        report = juce::String ("Instrument not found at the saved path; found in the library by name and ")
               + (stored.empty() ? "folder" : "samples") + ": " + shown (candidates[(size_t) choice.index]) + savedNote;
        return candidates[(size_t) choice.index];
    }

    juce::String list;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        list << "\n  " << shown (candidates[i]);
        if (i < choice.overlaps.size() && choice.overlaps[i] >= 0.0)
            list << "  (" << juce::roundToInt (choice.overlaps[i] * 100.0) << " % of the samples in common)";
    }
    if (candidates.empty())
        report = "No file named \"" + fileName + "\" in the library.";
    else if (choice.ambiguous)
        report = "Several matching files in the library - none loaded, so that no wrong instrument is played:" + list;
    else
        report = juce::String ("Files with the same name in the library, but none matches (")
               + (stored.empty() ? "different folder" : "less than half of the samples in common") + "):" + list;
    return {};
}

juce::String MaermelAudioProcessor::envelopeTimeText (float knob)
{
    return timeText (juce::jlimit (0.0f, 1.0f, knob), 0);
}

juce::String MaermelAudioProcessor::lfoRateText (float knob, bool sync)
{
    if (lfoIsDc (knob))
        return "DC";
    return sync ? juce::String (lfoNoteValue (knob).name) : lfoFreeText (knob, 0);
}

// Text des Reglers LO-FI/CHORUS: im Chorus-Modus Prozent, sonst Bittiefe und Samplerate, z. B. "11.7 bit/12.6 kHz".
// Die Oberfläche übergibt Modus und Samplerate ihrer eigenen Instanz, die DAW-Anzeige (crushText) die globalen Werte.
juce::String MaermelAudioProcessor::lofiText (float v, bool chorus, double sampleRate)
{
    if (v <= 0.0f)
        return "off";
    if (chorus)
        return juce::String (juce::roundToInt (v)) + " %";
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;   // vor prepareToPlay noch unbekannt
    const float bits = maermel::Synth::crushBits (v);
    const int stage = maermel::Synth::crushHoldStage (bits, sampleRate);
    juce::String text = juce::String (bits, 1) + " bit";
    if (stage > 0)
    {
        const float khz = (float) (sampleRate * 2.0 / stage / 1000.0);
        text << "/" << (khz < 20.0f ? juce::String (khz, 1) : juce::String (juce::roundToInt (khz))) << " kHz";   // kompakt fürs Display
    }
    return text;
}

// Eingetippter Wert für LO-FI/CHORUS: im Chorus-Modus oder mit „%“ Prozent, sonst Bit (Lo-Fi-Kennlinie)
float MaermelAudioProcessor::lofiValueFromText (const juce::String& text, bool chorus)
{
    if (chorus || text.containsChar ('%'))
        return maermel::parse::plainNumber (plain (text), 0, 100, 0);
    return maermel::parse::crushAmount (plain (text), 0.0f);
}

juce::AudioProcessorEditor* MaermelAudioProcessor::createEditor()
{
    return new MaermelAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MaermelAudioProcessor();
}
