// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// texttest.cpp – Märmel: prüft die Rückübersetzung eingetippter Werte (ValueParsing.h)
// Aufruf: clang++ -std=c++17 -o texttest tests/texttest.cpp && ./texttest

#include "../Source/ValueParsing.h"
#include <cstdio>
#include <cmath>
using namespace maermel::parse;
// Anzeigefunktionen wie im Plugin, um Hin- und Rückweg zu prüfen
static double timeOf (float knob, double max) { return max * std::pow (knob, 4.2); }   // Kennlinie x^4,2 (seit 20.10.2026)
int main()
{
    int bad = 0;
    auto check = [&] (const char* what, const char* in, double got, double expected, double tol = 1e-3)
    {
        const bool ok = std::fabs (got - expected) <= tol;
        if (! ok) ++bad;
        std::printf ("%s %-16s %-22s → %10.4f  (erwartet %.4f)\n", ok ? "ok  " : "FEHL", what, in, got, expected);
    };
    for (const char* t : { "500", "500 ms", "500ms", "0,5 s", "0.5s", " 0,5 sek" })
        check ("Hüllkurve", t, timeOf (timeKnob (t, 10.0, -1), 10.0), 0.5);
    check ("Hüllkurve", "3,16 s", timeOf (timeKnob ("3,16 s", 10.0, -1), 10.0), 3.16);
    check ("Hüllkurve", "20 s (zu groß)", timeOf (timeKnob ("20 s", 10.0, -1), 10.0), 10.0);
    check ("Hüllkurve", "Unsinn", timeKnob ("abc", 10.0, 0.42f), 0.42);
    check ("Glide", "aus", timeKnob ("aus", 5.0, -1), 0.0);
    check ("Glide", "310 ms", timeOf (timeKnob ("310 ms", 5.0, -1), 5.0), 0.31);
    check ("Prozent", "75 %", percent ("75 %", -1), 0.75);
    check ("Prozent", "75", percent ("75", -1), 0.75);
    check ("Routing", "Poly 30 %", routing ("Poly 30 %", 9), -0.3);
    check ("Routing", "Mono 30", routing ("Mono 30", 9), 0.3);
    check ("Routing", "-45", routing ("-45", 9), -0.45);
    check ("Volume", "-6 dB", decibels ("-6 dB", -60, 6, 99), -6);
    check ("Volume", "-inf dB", decibels ("-inf dB", -60, 6, 99), -60);
    check ("Volume", "+3", decibels ("+3", -60, 6, 99), 3);
    for (const char* t : { "440", "440 Hz", "0,44 kHz", "0.44k" })
        check ("Cutoff (Hz)", t, 20.0 * std::pow (1000.0, cutoffKnob (t, -1)), 440.0, 0.01);
    check ("Filter Env", "+5 Okt", envOctaves ("+5 Okt", 9), 0.5);
    check ("Filter Vel Env", "+5 Okt", velEnv ("+5 Okt", 9), 0.5);
    check ("Filter Vel Env", "-10 Okt", velEnv ("-10 Okt", 9), -1.0);
    check ("Filter Vel Env", "-2,5", velEnv ("-2,5", 9), -0.25);
    check ("Start", "25 ms", maermel::kSampleStartMaxMs * std::pow (startKnob ("25 ms", -1), 3.0), 25.0);
    check ("Start", "1 s", maermel::kSampleStartMaxMs * std::pow (startKnob ("1 s", -1), 3.0), 1000.0);
    check ("Start", "2 s → 1 s", maermel::kSampleStartMaxMs * std::pow (startKnob ("2 s", -1), 3.0), 1000.0);
    check ("Start Vel", "-50 ms", startVelocity ("-50 ms", 9) < 0 ? -maermel::kSampleStartMaxMs * std::pow (std::fabs (startVelocity ("-50 ms", 9)), 3.0) : 0, -50.0);
    check ("Start Vel", "+800 ms", maermel::kSampleStartMaxMs * std::pow (startVelocity ("+800 ms", 9), 3.0), 800.0);
    check ("T", "-30", sustainTime ("-30", 9), -30.0 / 64.0);
    check ("T", "+10", sustainTime ("+10", 9), 10.0 / 64.0);
    check ("T", "sinkt 30", sustainTime ("sinkt 30", 9), -30.0 / 64.0);
    check ("T", "+64", sustainTime ("+64", 9), 1.0);
    check ("T", "-80 → -64", sustainTime ("-80", 9), -1.0);
    check ("T", "0", sustainTime ("0", 9), 0.0);
    check ("Fine", "+25 ct", plainNumber ("+25 ct", -100, 100, 99), 25);
    check ("Crush", "aus", crushAmount ("aus", 9), 0.0);
    check ("Crush", "15 Bit", crushAmount ("15 Bit", 9), 30.0);
    check ("Crush", "11", crushAmount ("11", 9), 100.0);
    check ("Crush", "13 Bit / 16 kHz", crushAmount ("13 Bit / 16 kHz", 9), 65.0, 0.01);
    check ("LFO-Rate", "4 Hz", 0.05 * std::pow (600.0, lfoRateKnob ("4 Hz", -1)), 4.0);
    std::printf ("\n%d Fehler\n", bad);
}
