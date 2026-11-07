# Test: fehlende Füllbytes in WAV- und AIFF-Dateien

Nach Vorschrift folgt auf einen Chunk ungerader Länge ein Füllbyte. Manche Programme vergessen es
(bekannt beim 7 Byte langen WAV-„inst“-Chunk). Ohne Korrektur gingen dann alle folgenden Chunks verloren –
im schlimmsten Fall der „data“-Chunk, und das Sample fehlt.

Märmel prüft zuerst die korrekte Position (mit Füllbyte); nur wenn dort kein plausibler Chunk-Kopf steht,
aber ohne Füllbyte einer, geht es dort weiter. Gültige Dateien werden also nie anders gelesen.

    python3 erzeugen.py
    clang++ -std=c++17 -O2 -o sfzinfo ../sfzinfo.cpp ../../Source/engine/*.cpp ../../Source/engine/exs/*.cpp
    for f in *.sfz; do ./sfzinfo "$f"; done

Erwartet: alle sieben Instrumente ohne Fehler; bei den WAV-Dateien 1–4 der Loop 1000–8000.

Regel (verbessert 08.10.2026): Ist nur eine Stelle plausibel, gilt diese. Sind beide plausibel, gewinnt eine bekannte
Kennung (data, fmt , smpl, LIST, SSND …). Bleibt es offen, entscheidet das Füllbyte selbst: Es muss 0 sein – steht dort
etwas anderes, fehlt es. Fall 7 zeigt, warum: Ohne diese Ergänzung wurde dort ein Schein-Chunk „atab“ gelesen
(in einem Test mit 995 zufälligen fehlerhaften Dateien lag die alte Regel bei 73 daneben, die neue bei keiner).
