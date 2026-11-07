# Märmel – Überblick über den Quellcode

Diese Datei beschreibt, welche Datei welche Aufgabe hat. Wie man Märmel baut und die Tests ausführt,
steht in `BUILDING.md` (englisch).

## Aufbau in zwei Schichten

Märmel besteht aus zwei klar getrennten Teilen:

- **Engine** (`Source/engine/`): Klangerzeugung, SFZ-Leser, Laden der Samples, EXS-Import und Export.
  Reines C++17 **ohne JUCE**. Dadurch lässt sich die Engine mit einem einfachen Compiler-Aufruf im
  Terminal übersetzen und testen (siehe `tests/`).
- **Plugin-Schicht** (`Source/`): verbindet die Engine über JUCE mit DAW, MIDI, Oberfläche und
  gespeichertem Zustand.

Der Weg eines Instruments:

    SFZ-Datei ─► SfzParser (Text → Regionen mit Vererbung)
              ─► Instrument (Samples laden, Werte prüfen, flache Region-Tabelle)
              ─► Synth (Stimmen, Hüllkurven, Filter, LFOs, Effekte)
              ─► PluginProcessor (Audio an die DAW)

EXS-Dateien werden vorher im Speicher nach SFZ umgewandelt und laufen dann denselben Weg.

**Echtzeit-Regel:** Im Audio-Thread wird kein Speicher reserviert oder freigegeben, keine Datei gelesen
und nicht auf andere Threads gewartet. Instrumente werden im Hintergrund geladen; das neue ersetzt das
alte erst, wenn es vollständig bereitsteht.


## Engine (`Source/engine/`, ohne JUCE)

| Datei | Aufgabe |
| --- | --- |
| `SfzTypes.h` | Datentypen: Region, Loop-Modi, Trigger, Gruppen (`group`/`off_by`, Polyphonie) |
| `SfzParser.h/.cpp` | Liest den SFZ-Text, löst die Vererbung `<global>` → `<master>` → `<group>` → `<region>` auf, `<control>` mit `default_path`, `octave_offset`, `note_offset`; begrenzt alle Zahlen auf sinnvolle Bereiche |
| `SampleFile.h/.cpp` | Lädt WAV und AIFF vollständig in den Speicher, in der Originalauflösung (16 Bit als int16, 24 Bit als int32, 32 Bit Float); Loops und Grundton aus `smpl` bzw. `MARK`/`INST`; erkennt fehlende Füllbytes; jeweils der erste Chunk gilt; liest Speicherbedarf, Grundton und Loop auch nur aus den Kopfdaten |
| `Instrument.h/.cpp` | Lädt SFZ und Samples, lädt gleiche Samples nur einmal, prüft jede Region gegen die tatsächliche Datei und korrigiert unplausible Werte (mit Hinweis in „Loading notes“), schätzt vorab den Speicherbedarf (Speicherschutz); findet Samples auch bei anderer Umlaut-Schreibweise, Groß-/Kleinschreibung oder umbenanntem Sample-Ordner |
| `Synth.h/.cpp` | Stimmen und Wiedergabe: Region-Auswahl (Tasten, Velocity, Keyswitches, Round Robin, Zufall, Pedal-Schichten, Überblendungen), 4-Punkt-Hermite-Interpolation, Loops, Stimmenmodi (Legato, Mono, 2–64 Stimmen; die Stimmenzahl zählt Noten, nicht Samples) mit Glide, Sample Start (Start/Vel/Rnd), C.Tune, KeyShift, Fine, Pitch Bend, Sustain-Pedal, Release-Samples, Gruppen-Polyphonie und `group`/`off_by`, Aftertouch (Kanal und polyphon), Drive (2-fach überabgetastet), Lo-Fi bzw. Chorus auf der Summe, Ausblenden beim Voice Stealing und beim Instrumentwechsel |
| `Envelope.h` | ADSTR-Hüllkurve: Attack, Decay, Sustain mit Sustain Time (T), Release; Velocity-Zeiten für Attack und Decay, Key (Zeit-Keytracking); Zeiten und Kurvenformen wie beim EXS24 |
| `Filter.h` | Resonanzfilter: vier Stufen mit Rückkopplung (Zero-Delay-Feedback), LP12, LP24, HP, BP; weiche Sättigung; Überblenden beim Umschalten des Typs |
| `Lfo.h` | LFO mit Dreieck, Sinus, Sägezahn, Rechteck; Invert, Env Mode, Delay; Mono-LFO synchron zum Songtempo |
| `VelocityResponse.h` | Standard-Velocity-Kurve als Formel und einstellbare Kurve für das eigene Masterkeyboard |
| `ExsImport.h/.cpp` | EXS-Instrumente direkt laden (im Speicher nach SFZ umgewandelt; Samples an drei Orten gesucht, der Ort mit den meisten Treffern gilt); Hinweis bei Loop Tune, das SFZ nicht kennt |
| `SfzExport.h/.cpp` | „Export instrument…“: SFZ mit neuen Sample-Pfaden, jedes Sample nur einmal, eindeutige Dateinamen |
| `InstrumentLocator.h/.cpp` | Instrument eines DAW-Projekts auf einem anderen Rechner wiederfinden: Pfad relativ zur Library, sonst Suche nach gleichnamigen Dateien mit Fingerabdruck der Sample-Namen |
| `FileUtil.h` | Pfade und Dateizugriff für macOS, Windows und Linux (UTF-8-Pfade unter Windows, zwei Umlaut-Schreibweisen, Suche ohne Groß-/Kleinschreibung); öffnet nur reguläre Dateien |

### `Source/engine/exs/` – EXS-Dateiformat

| Datei | Aufgabe |
| --- | --- |
| `EXSInstrument.h` | Datenmodell eines eingelesenen EXS-Instruments (Zonen, Gruppen, Samples) |
| `EXSParser.h/.cpp` | Liest EXS-Dateien (little und big endian, ältere und neuere Chunk-Größen) |
| `SFZWriter.h/.cpp` | Erzeugt aus dem EXS-Datenmodell den SFZ-Text und hilft bei der Suche nach den Samples |

### `Source/engine/chorus/` – Chorus (nur Header, eingebunden über `Synth.h`)

| Datei | Aufgabe |
| --- | --- |
| `AnalogChorus.h` | Ein-Knopf-Chorus im Stil klassischer Analog-Synthesizer: Mono-Summe → Vorfilter → Kompressor → Verzögerungsleitung mit zwei gegenphasigen Leseköpfen → Expander → Nachfilter; Kurve für Rate, Filter, Anteil und Pegelausgleich; bei 0 % rechnet er nichts |
| `ChorusBuildingBlocks.h` | Bausteine dafür: Filter, Kompressor/Expander, Verzögerungsleitung, LFO, Glättung |


## Plugin-Schicht (`Source/`, JUCE)

| Datei | Aufgabe |
| --- | --- |
| `PluginProcessor.h/.cpp` | Plugin: MIDI, Parameter, Instrumentwechsel (Maus, Pfeiltasten, MIDI), Laden im Hintergrund, Presets (`.sfzp`), LOCK (Einstellungen beim Instrumentwechsel behalten), Zustand im DAW-Projekt, EXS-Import, Export, Live-Edit mit externem SFZ-Editor |
| `PluginEditor.h/.cpp` | Oberfläche: Library-Menü mit Suche, Instrument-Display, alle Sektionen (Voices, Sample Start and Tune, Filter, Filter- und Amp-Hüllkurve, LFOs mit Routing und Wheel LFO, Lo-Fi/Chorus, Vol), Rechtsklick-Menü, Ziehen von Dateien aufs Fenster |
| `ParameterIds.h` | Feste interne Parameter-IDs; die sechs ersten erscheinen in GarageBand als Smart Controls. Ab der Veröffentlichung unveränderlich (DAW-Automation hängt daran) |
| `ParameterControl.h` | Direkte Steuerung von Parametern per MIDI-Controller: Zielliste und Konfliktregel |
| `ValueParsing.h` | Eingetippter Text → Parameterwert (Wertefelder und DAW-Automation) |
| `VelocityTools.h` | Velocity-Kurve (global) und Velocity-Offset (pro Instrument), bevor die Note die Engine erreicht |
| `SettingsDialog.h/.cpp` | Einstellungen im PATH-Dialog: Library-Ordner, Fenstergröße, Hintergrund, EXS, Instrumentwechsel per MIDI, Velocity-Kurve, Aftertouch, MIDI-Controller-Zuordnung, SFZ-Editor und Live-Edit, Logfile |
| `LibraryIndex.h/.cpp` | Verzeichnis aller Instrumente unter dem Library-Ordner, im Hintergrund eingelesen, mit Cache und Aktualisierung pro Ordner; globale Einstellungsdatei |
| `NotesPanel.h/.cpp` | Tafel „Loading notes“ mit allen Hinweisen zum geladenen Instrument |
| `ErrorLog.h` | Logfile für Fehler und Hinweise, mit Kopfzeile (Version, Build, Plattform, Format) |
| `AboutPanel.h/.cpp`, `LicenseTexts.h` | About-Tafel mit Version, Build-Zeitpunkt und Lizenzhinweisen |
| `MaermelLook.h/.cpp` | Gestaltung: Drehknöpfe, Fader, Tasten, Glasflächen, Rahmen, gebürsteter Hintergrund, Punktmatrix; eingebettete Schrift |
| `MaermelControls.h/.cpp` | Eigene Bedienelemente: Instrument-Display, Wertefelder (Doppelklick = Wert eintippen), Filtertyp-Liste mit LEDs, Tastengruppen, VelA/VelD-Pfeile |
| `DotMatrixFont.h` | Eigene Punktmatrix-Schrift (5 × 8 Punkte) |


## Weitere Ordner

| Ordner / Datei | Inhalt |
| --- | --- |
| `Resources/Fonts/` | Barlow Semi Condensed (SIL Open Font License 1.1, `OFL.txt`), als Binary Resource eingebettet |
| `Resources/Icon/` | App-Icon der Standalone-Version |
| `Maermel.jucer` | Projucer-Projekt mit allen Einstellungen |
| `BUILDING.md` | Bauanleitung für macOS, Windows und Linux, Engine-Tests |
| `NOTICE.md`, `LICENSE` | Lizenz (AGPL-3.0-or-later) mit Zusatzbedingungen, Hinweise zu fremden Bestandteilen und Marken |


## Tests (`tests/`, ohne JUCE)

Testprogramme und Testdaten liegen gemeinsam im Ordner `tests/`. Bauen und Aufruf: siehe `BUILDING.md`, Abschnitt 5.

### Testprogramme und Skripte

| Datei | Aufgabe |
| --- | --- |
| `stucktest.cpp` | Belastungstest gegen hängende Noten: tausende zufällige Folgen, am Ende muss alles verstummen |
| `layertest.cpp` | Stimmenzahl zählt Noten: Layer werden nie halbiert, es weichen immer ganze Noten |
| `offbytest.cpp` | `group`/`off_by`, z. B. geschlossene Hi-Hat beendet die offene |
| `texttest.cpp` | Rückübersetzung eingetippter Werte |
| `headertest.cpp` | Grundton und Loop nur aus den Kopfdaten, verglichen mit dem vollständigen Laden |
| `veltest.cpp` | Velocity-Kurven und -Offsets |
| `locatortest.cpp` | Wiederfinden eines Instruments auf einem anderen Rechner |
| `exporttest.cpp` | Exportiert ein Instrument ohne Plugin und vergleicht es mit dem Original |
| `sfzrender.cpp` | Spielt ein Instrument nach einer Ereignisliste ab und schreibt eine WAV-Datei – zum Anhören der Engine ohne DAW |
| `sfzinfo.cpp` | Zeigt die geprüfte Region-Tabelle eines Instruments |
| `paramidtest.py` | Prüft, dass die Parameternummern fest bleiben und keine doppelt vorkommt |
| `rootkeycheck.py` | Vergleicht in einer Library den Grundton aus Dateiname, SFZ und Sample-Kopfdaten (liest nur) |


### Testdaten

| Ordner | Inhalt |
| --- | --- |
| `layer/` | Erzeugte Sinus-Samples und Testinstrument für Layer-Tests; auch von `offby/` und `xfade/` benutzt (`erzeugen.py` erzeugt sie neu) |
| `offby/` | Hi-Hat-Gruppen für `group`/`off_by` |
| `xfade/` | Überblendungen per Velocity, Modulationsrad und Sustain-Pedal; `pruefen.py` misst den Pegel einzelner Frequenzen in der gerenderten Datei |
| `doppelchunks/` | WAV/AIFF mit doppelten Chunks – der erste gilt (`erzeugen.py` einmal ausführen) |
| `fuellbyte/` | WAV/AIFF mit fehlenden Füllbytes (`erzeugen.py` einmal ausführen) |
| `umbenannt/` | Umbenanntes Instrument samt Sample-Ordner, ohne angepasste Pfade (`erzeugen.py` einmal ausführen) |
| `pedalwechsel/` | Ereignisliste für einen typischen Pedalwechsel beim Klavier; läuft mit jedem Instrument |

In jedem Ordner mit `LIESMICH.md` steht, was der Test erwartet.
