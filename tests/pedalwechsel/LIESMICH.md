# Test: Pedalwechsel beim Klavier

Typischer Ablauf beim Klavierspiel: Eine Taste klingt unter dem Pedal noch nach, wird erneut angeschlagen,
dann wird das Pedal gelüftet und neu getreten, während die Taste gedrückt bleibt.

Richtig (wie beim Klavier): Die neu angeschlagene Note klingt weiter, bis das Pedal bei 2,5 s gelüftet wird.
Falsch (Fehler bis 03.10.2026): Beim Lüften bei 0,7 s wurde auch die neue, gehaltene Stimme freigegeben.

Prüfen:

    g++ -std=c++17 -O2 -o sfzrender ../sfzrender.cpp ../../Source/engine/*.cpp ../../Source/engine/exs/*.cpp
    ./sfzrender <ein Instrument>.sfz pedalwechsel.txt ergebnis.wav

Im Ergebnis muss zwischen 1,0 s und 2,4 s weiterhin Klang zu hören bzw. zu messen sein.
