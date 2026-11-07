# Märmel – SFZ Sample Player
# Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
# SPDX-License-Identifier: AGPL-3.0-or-later
# Additional terms under section 7 of the license (attribution, name): see NOTICE.md
#
# Erzeugt Testdateien für fehlende Füllbytes in WAV und AIFF (siehe LIESMICH.md).
import struct, math, numpy as np
sr = 44100
n = sr // 4
le = (np.sin(2 * np.pi * 440 * np.arange(n) / sr) * 0.5 * 32767).astype('<i2').tobytes()
be = (np.sin(2 * np.pi * 440 * np.arange(n) / sr) * 0.5 * 32767).astype('>i2').tobytes()

def wchunk(cid, body, pad=True, declared=None):
    out = cid + struct.pack('<I', len(body) if declared is None else declared) + body
    return out + (b'\x00' if pad and len(body) % 2 else b'')

def achunk(cid, body, pad=True):
    out = cid + struct.pack('>I', len(body)) + body
    return out + (b'\x00' if pad and len(body) % 2 else b'')

fmt = wchunk(b'fmt ', struct.pack('<HHIIHH', 1, 1, sr, sr * 2, 2, 16))
data = wchunk(b'data', le)
smpl = wchunk(b'smpl', struct.pack('<9I', 0, 0, 0, 69, 0, 0, 0, 1, 0) + struct.pack('<6I', 0, 0, 1000, 8000, 0, 0))
inst7 = bytes([69, 0, 0, 0, 127, 1, 127])
wavs = {
    '1_gueltig':         fmt + wchunk(b'inst', inst7) + smpl + data,
    '2_ohne_fuellbyte':  fmt + wchunk(b'inst', inst7, pad=False) + smpl + data,
    '3_laenge_8':        fmt + wchunk(b'inst', inst7 + b'\x00', declared=8) + smpl + data,
    '4_ende_ohne_fuell': fmt + smpl + data + wchunk(b'inst', inst7, pad=False),
    # inst ohne Füllbyte direkt vor data, Audiodaten beginnen mit Stille: mit Füllbyte gelesen entsteht der
    # Schein-Chunk „atab“ mit plausibler Länge (gefunden 08.10.2026; erkannt am Füllbyte ≠ 0 und an der Kennung)
    '7_inst_vor_data_still': fmt + wchunk(b'inst', inst7, pad=False) + wchunk(b'data', bytes(2) + le[2:30000]),
}
for name, body in wavs.items():
    open(name + '.wav', 'wb').write(b'RIFF' + struct.pack('<I', 4 + len(body)) + b'WAVE' + body)
    open(name + '.sfz', 'w').write('<region> sample=' + name + '.wav pitch_keycenter=69 loop_mode=loop_continuous\n')

def ext80(v):
    e = int(math.floor(math.log2(v)))
    return struct.pack('>HQ', e + 16383, int(v / 2 ** e * 2 ** 63))
comm = achunk(b'COMM', struct.pack('>hIh', 1, n, 16) + ext80(sr))
ssnd = achunk(b'SSND', struct.pack('>II', 0, 0) + be)
for name, pad in (('5_aiff_gueltig', True), ('6_aiff_ohne_fuellbyte', False)):
    body = b'AIFF' + comm + achunk(b'NAME', b'Flote C', pad=pad) + ssnd
    open(name + '.aif', 'wb').write(b'FORM' + struct.pack('>I', len(body)) + body)
    open(name + '.sfz', 'w').write('<region> sample=' + name + '.aif pitch_keycenter=69\n')
print('Testdateien erzeugt')
