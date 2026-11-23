// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// SfzExport.h – Märmel Engine
// Plant den Export eines Instruments: liest den SFZ-Text nach denselben Regeln wie der SFZ-Leser,
// sammelt alle verwendeten Samples (jedes nur einmal) und erzeugt den neuen Text, in dem jede
// sample=-Angabe durch einen Platzhalter ersetzt ist und default_path entfällt. Kommentare, Gruppen und
// alle übrigen Opcodes bleiben unverändert. Die endgültigen Dateinamen (auch passend zu bereits
// vorhandenen Dateien im Zielordner) setzt der Prozessor mit fillExportText ein.

#pragma once

#include <string>
#include <vector>

namespace maermel
{

struct SfzExportPlan
{
    bool ok = false;
    std::string error;

    struct Sample
    {
        std::string source;        // vollständiger Pfad der Original-Datei ('/' als Trenner)
        std::string fileName;      // Dateiname im Samples-Ordner, innerhalb des Plans eindeutig
    };
    std::vector<Sample> samples;

    std::string text;              // neuer SFZ-Text mit Platzhaltern (Index in samples)
};

// sfzText: Inhalt der SFZ-Datei; sfzDirectory: Ordner der SFZ-Datei mit '/' am Ende
SfzExportPlan planSfzExport (const std::string& sfzText, const std::string& sfzDirectory);

// Platzhalter durch den Dateinamen ersetzen und „default_path=<Ordner>/“ in den <control>-Bereich schreiben
// (seit 15.10.2026; vorher „Ordner/Dateiname“ in jeder Region); finalNames[i] gehört zu plan.samples[i]
std::string fillExportText (const SfzExportPlan& plan, const std::string& samplesFolder,
                            const std::vector<std::string>& finalNames);

// Eindeutiger Name: „C4.wav“, „C4 (2).wav“, „C4 (3).wav“ …
std::string numberedFileName (const std::string& fileName, int number);

} // namespace maermel
