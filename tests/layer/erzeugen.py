#!/usr/bin/env python3
# Märmel – Testdateien für die Noten-Zählung (19.10.2026): kurze Sinus-Samples (Mono und Stereo), 2 s lang
import math, struct, wave
def write(name, freq, stereo=False, seconds=2.0, sr=44100):
    n = int(seconds * sr)
    w = wave.open(name, 'wb'); w.setnchannels(2 if stereo else 1); w.setsampwidth(2); w.setframerate(sr)
    frames = bytearray()
    for i in range(n):
        v = int(9000 * math.sin(2 * math.pi * freq * i / sr))
        frames += struct.pack('<h', v) * (2 if stereo else 1)
    w.writeframes(bytes(frames)); w.close()
for k in range(6):
    write('layer%d.wav' % k, 220.0 * (k + 1))
write('single.wav', 330.0, stereo=True)
write('rel.wav', 880.0, seconds=0.5)
