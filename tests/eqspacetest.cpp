// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// eqspacetest.cpp – Märmel: prüft den Parser bei Leerzeichen um '=' und bei Pfaden mit Leerzeichen/'=' (seit 1.0.3)
//   - "key =66", "hikey  =  69", Tabs: werden als Opcodes erkannt (handgeschriebene Dateien)
//   - sample=…: Dateinamen bleiben bis zur Audio-Endung ganz ("Piano = Soft.wav", "Mix a=b.wav")
//   - default_path=…: Ordner bleibt bis zum Trenner ganz ("My Samples = Old/")
//   - Großgeschriebene Wörter mit Leerzeichen vor '=' gelten nicht als Opcode (Schutz für andere Werte)
// Braucht keine Sample-Dateien: geprüft wird nur, was der Parser aus dem Text liest.
// Aufruf: g++ -std=c++17 -I Source/engine -o eqspacetest tests/eqspacetest.cpp Source/engine/*.cpp Source/engine/exs/*.cpp
//         ./eqspacetest

#include "../Source/engine/SfzParser.h"
#include <cstdio>
#include <string>
using namespace maermel;

static int bad = 0;
static void check (bool ok, const std::string& what)
{
    if (! ok) ++bad;
    std::printf ("%s %s\n", ok ? "OK  " : "FEHL", what.c_str());
}

// Eine Region aus einem SFZ-Text prüfen: Sample-Pfad und Tastenbereich
static void region (const std::string& sfz, size_t index, const std::string& path, int lo, int hi)
{
    const ParsedInstrument p = SfzParser::parse (sfz);
    if (index >= p.regions.size())
    {
        check (false, "Region " + std::to_string (index) + " fehlt: " + sfz);
        return;
    }
    const Region& r = p.regions[index];
    check (r.samplePath == path && r.loKey == lo && r.hiKey == hi,
           "\"" + r.samplePath + "\" " + std::to_string (r.loKey) + "-" + std::to_string (r.hiKey)
           + "  (erwartet \"" + path + "\" " + std::to_string (lo) + "-" + std::to_string (hi) + ")");
}

int main()
{
    // Beispiel aus einer echten Datei (MiMoog): Leerzeichen vor und nach '='
    const std::string mimoog =
        "<control>\ndefault_path=MiMoog SnglSwBs BB Samples/\n<group>\nvolume=5\n"
        "<region> sample=MiSnglSwBsBB_54.wav key=54 hikey=57 lokey=36\n"
        "<region> sample=MiSnglSwBsBB_60.wav key= 60 hikey= 63 lokey=58\n"
        "<region> sample=MiSnglSwBsBB_66.wav key =66 hikey =69 lokey=64\n"
        "<region> sample=MiSnglSwBsBB_72.wav key = 72 hikey = 127 lokey=70\n";
    region (mimoog, 0, "MiMoog SnglSwBs BB Samples/MiSnglSwBsBB_54.wav", 36, 57);
    region (mimoog, 1, "MiMoog SnglSwBs BB Samples/MiSnglSwBsBB_60.wav", 58, 63);
    region (mimoog, 2, "MiMoog SnglSwBs BB Samples/MiSnglSwBsBB_66.wav", 64, 69);
    region (mimoog, 3, "MiMoog SnglSwBs BB Samples/MiSnglSwBsBB_72.wav", 70, 127);

    // Mehrere Leerzeichen und Tabs
    region ("<region> sample=a.wav lokey  =  40 hikey\t=\t44\n", 0, "a.wav", 40, 44);

    // Dateinamen mit Leerzeichen und '=' bleiben ganz
    region ("<region> sample=Piano = Soft.wav key =60\n", 0, "Piano = Soft.wav", 60, 60);
    region ("<region> sample=piano = soft.wav lokey =61 hikey=62\n", 0, "piano = soft.wav", 61, 62);
    region ("<region> sample=Mix a=b.wav key=63\n", 0, "Mix a=b.wav", 63, 63);
    region ("<region> sample=Alt.wav Dateien/a.WAV key =66\n", 0, "Alt.wav Dateien/a.WAV", 66, 66);
    region ("<region> sample=Strings Sus.aiff lokey =20 hikey=30\n", 0, "Strings Sus.aiff", 20, 30);

    // Zwei Regionen in einer Zeile
    const std::string twoInLine = "<region> sample=x.wav key =64 <region> sample=y.wav key= 65\n";
    region (twoInLine, 0, "x.wav", 64, 64);
    region (twoInLine, 1, "y.wav", 65, 65);

    // default_path mit Leerzeichen und '='
    region ("<control> default_path=My Samples = Old/\n<region> sample=z.wav key =70\n", 0, "My Samples = Old/z.wav", 70, 70);

    // Unverändert wie bisher: ohne Leerzeichen
    region ("<region> sample=plain.wav lokey=10 hikey=20\n", 0, "plain.wav", 10, 20);

    std::printf (bad == 0 ? "Alle Prüfungen bestanden.\n" : "%d Prüfung(en) fehlgeschlagen.\n", bad);
    return bad == 0 ? 0 : 1;
}
