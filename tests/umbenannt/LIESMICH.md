# Test: umbenanntes Instrument samt Sample-Ordner

Jemand benennt „altname.sfz“ und den Ordner „altname Samples“ in „neuname…“ um, ohne die Pfade in der SFZ
anzupassen. Fehlt ein Sample unter dem eigentlichen Pfad, ersetzt Märmel nur den ersten Ordner des Pfads – erst durch
„neuname Samples“, dann durch „neuname“. Unterordner (Light, Dark, Heavy) bleiben unverändert.

    python3 erzeugen.py
    clang++ -std=c++17 -O2 -I../../Source/engine -o sfzinfo ../sfzinfo.cpp ../../Source/engine/*.cpp ../../Source/engine/exs/*.cpp
    for f in faelle/*/*.sfz; do ./sfzinfo "$f"; done

Erwartet:
1. Pfad stimmt: 3 Samples, kein Hinweis.
2. Ordner „neuname Samples“: 3 Samples, ein Hinweis „3 samples loaded from folder 'neuname Samples' instead of 'altname Samples'“.
3. Ordner „neuname“: 3 Samples, ein Hinweis mit 'neuname'.
4. Wie 2, aber Ordner über default_path: gleiches Ergebnis wie 2.
5. Unterordner ebenfalls umbenannt: 0 Samples, 3 fehlende (bewusst kein weiterer Rückgriff).
6. Ordner passt zu keinem Namen: 0 Samples, 3 fehlende.
7. Light liegt noch im alten Ordner, Dark/Heavy im neuen: 3 Samples, Hinweis „2 samples“.
