// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// ExsImport.h – Märmel Engine
// EXS24-Instrumente: Umwandlung nach SFZ und direktes Laden.
//
// Laden spielt die EXS-Datei nur im Speicher (keine Dateien werden angelegt).
// Erst beim Speichern (Save) entsteht daneben eine SFZ-Datei, auf die dann das Preset verweist;
// wahlweise werden die gefundenen Samples in "<Instrument> Samples" daneben kopiert.
// Die Instrument-Parameter des EXS (Filter, Hüllkurven ...) werden noch nicht übernommen.
//
// Herkunft des Formatwissens: ermittelt durch Analyse selbst erstellter EXS-Dateien (Björn Bojahrs eigene
// Library, ursprünglich vor über 20 Jahren); kein Code und keine Dokumentation von Apple verwendet, nichts
// dekompiliert. Zweck ist allein die Kompatibilität (Import des Sample-Mappings). EXS24 und Logic sind Marken
// von Apple Inc.; Märmel steht in keiner Verbindung zu Apple.

#pragma once

#include "Instrument.h"

#include <string>
#include <vector>

namespace maermel
{

struct ExsConversion
{
    bool ok = false;
    std::string error;
    std::string sfzText;                 // fertiger SFZ-Text

    struct Copy { std::string source; std::string targetName; };
    std::vector<Copy> copies;            // nur wenn Samples kopiert werden sollen

    int found = 0, missing = 0;
    std::vector<std::string> warnings;
};

// Wandelt eine EXS-Datei in SFZ-Text um. Die SFZ-Datei liegt neben der EXS-Datei.
// samplesFolderName leer: Samples bleiben, wo sie sind (Pfade relativ zum Ordner der EXS-Datei,
//                         wenn sie darin liegen, sonst absolut).
// samplesFolderName gesetzt: Samples werden in diesen Unterordner kopiert (Liste in 'copies');
//                         gleichnamige Samples aus verschiedenen Ordnern bekommen "_2", "_3" ...
ExsConversion convertExsToSfz (const std::string& exsPath, const std::string& samplesFolderName = {});

std::unique_ptr<Instrument> loadExsInstrument (const std::string& exsPath, const MemoryGuard& guard = {});

// Lädt je nach Dateiendung (.sfz oder .exs)
std::unique_ptr<Instrument> loadAnyInstrument (const std::string& path, const MemoryGuard& guard = {});

} // namespace maermel
