// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// EXSParser.h
// Liest EXS24-Dateien (little und big endian, ältere und neuere Chunk-Größen)
// in ein exs::Instrument ein. Arbeitet auf einem Speicherpuffer; das Laden der
// Datei übernimmt der Aufrufer (z. B. juce::File::loadFileAsData).
//
// Herkunft des Formatwissens: ermittelt durch Analyse selbst erstellter EXS-Dateien (Björn Bojahrs eigene
// Library, ursprünglich vor über 20 Jahren); kein Code und keine Dokumentation von Apple verwendet, nichts
// dekompiliert. Zweck ist allein die Kompatibilität (Import des Sample-Mappings). EXS24 und Logic sind Marken
// von Apple Inc.; Märmel steht in keiner Verbindung zu Apple.

#pragma once

#include "EXSInstrument.h"
#include <cstddef>
#include <string>

namespace exs
{

class Parser
{
public:
    // Gibt false zurück, wenn die Daten keine lesbare EXS-Datei sind; der Grund
    // steht dann in 'error'. Nicht fatale Auffälligkeiten landen in out.warnings.
    static bool parse (const uint8_t* data, size_t size, Instrument& out, std::string& error);
};

} // namespace exs
