#!/usr/bin/env python3
# Märmel – SFZ Sample Player
# Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
# SPDX-License-Identifier: AGPL-3.0-or-later
# Additional terms under section 7 of the license (attribution, name): see NOTICE.md
#
# rootkeycheck.py – Grundton-Abgleich für eine SFZ-Library (liest nur, ändert nichts)
#
# Vergleicht für jede Region drei Angaben zum Grundton:
#   1. Name  – Notennummer am Ende des Sample-Dateinamens, z. B. "Piano_60.wav" → 60
#              (auch Notennamen wie "Piano_C4.wav" → 60; ohne erkennbare Note: wird ignoriert)
#   2. SFZ   – pitch_keycenter bzw. key= aus der SFZ-Datei (mit Vererbung aus <global>/<master>/<group>,
#              octave_offset/note_offset wie in Märmel: Wert + 12 × octave_offset + note_offset)
#   3. Datei – Root-Key im Header: smpl-Chunk (WAV) bzw. INST-Chunk (AIFF), falls vorhanden
# Ausgegeben werden nur Regionen, bei denen sich vorhandene Angaben unterscheiden.
#
# Aufruf im Terminal (im Library-Ordner oder mit Pfad):
#   python3 rootkeycheck.py                     → prüft den aktuellen Ordner mit allen Unterordnern
#   python3 rootkeycheck.py "/Pfad/zur/Library"
#   python3 rootkeycheck.py . > Ergebnis.txt     → Ergebnis in eine Textdatei

import os, re, struct, sys, unicodedata

NOTE_PC = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}

def parse_note(value, octave_offset=0, note_offset=0):
    v = value.strip().lower()
    if re.fullmatch(r'-?\d+', v):
        return int(v) + 12 * octave_offset + note_offset
    m = re.fullmatch(r'([a-g])(#|b)?(-?\d+)', v)
    if not m:
        return None
    pc = NOTE_PC[m.group(1)] + (1 if m.group(2) == '#' else -1 if m.group(2) == 'b' else 0)
    return (int(m.group(3)) + 1) * 12 + pc + 12 * octave_offset + note_offset

def note_name(n):
    if n is None:
        return '-'
    names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    return '%d (%s%d)' % (n, names[n % 12], n // 12 - 1)

def key_from_filename(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    m = re.search(r'(?:^|[_\- .x])(\d{1,3})$', stem)          # "…_60", "… 60", "…x60"
    if m and 0 <= int(m.group(1)) <= 127:
        return int(m.group(1))
    m = re.search(r'(?:^|[_\- .])([A-Ga-g][#b]?-?\d)$', stem)   # "…_C4", "…_F#2"
    if m:
        return parse_note(m.group(1))
    return None

def key_from_header(path):
    try:
        with open(path, 'rb') as f:
            data = f.read(4 * 1024 * 1024)                       # Kopfdaten liegen vorn oder nach den Audiodaten
            if len(data) >= 4 * 1024 * 1024:
                f.seek(0, 2); size = f.tell()
                if size > len(data):
                    f.seek(max(0, size - 1024 * 1024)); data_tail = f.read()
                else:
                    data_tail = b''
            else:
                data_tail = b''
    except OSError:
        return None, 'nicht lesbar'
    if data[:4] == b'RIFF' and data[8:12] == b'WAVE':
        for blob in (data, data_tail):
            i = blob.find(b'smpl')
            while i >= 0:
                if i + 8 + 16 <= len(blob):
                    size = struct.unpack('<I', blob[i + 4:i + 8])[0]
                    if 36 <= size < 100000:
                        return struct.unpack('<I', blob[i + 8 + 12:i + 8 + 16])[0], None
                i = blob.find(b'smpl', i + 1)
        return None, None
    if data[:4] == b'FORM' and data[8:12] in (b'AIFF', b'AIFC'):
        for blob in (data, data_tail):
            i = blob.find(b'INST')
            if i >= 0 and i + 9 <= len(blob):
                return blob[i + 8], None
        return None, None
    return None, 'kein WAV/AIFF'

def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n\r]*', '', text)

TOKEN = re.compile(r'<(\w+)>|([A-Za-z0-9_]+)=')

def parse_sfz(text):
    """Liefert (Regionen, octave_offset, note_offset, default_path).
    Region = Liste von (Opcode, Wert) in wirksamer Reihenfolge: <global>, <master>, <group>, dann die Region selbst."""
    text = strip_comments(text)
    scopes = {'global': [], 'master': [], 'group': []}
    control = {}
    regions = []
    current = None
    region = None
    # Werte reichen bis zum nächsten "name=" oder "<header>" (Leerzeichen in Dateinamen erlaubt)
    for line in text.splitlines():
        matches = list(TOKEN.finditer(line))
        for idx, m in enumerate(matches):
            if m.group(1):
                header = m.group(1).lower()
                if region is not None:
                    regions.append(region); region = None
                current = header
                if header == 'global':
                    scopes = {'global': [], 'master': [], 'group': []}
                elif header == 'master':
                    scopes['master'] = []; scopes['group'] = []
                elif header == 'group':
                    scopes['group'] = []
                elif header == 'region':
                    region = scopes['global'] + scopes['master'] + scopes['group']
                continue
            name = m.group(2).lower()
            end = matches[idx + 1].start() if idx + 1 < len(matches) else len(line)
            value = line[m.end():end].strip()
            if current == 'control':
                control[name] = value
            elif current in scopes:
                scopes[current].append((name, value))
            elif current == 'region' and region is not None:
                region.append((name, value))
    if region is not None:
        regions.append(region)
    try: oo = int(control.get('octave_offset', '0'))
    except ValueError: oo = 0
    try: no = int(control.get('note_offset', '0'))
    except ValueError: no = 0
    return regions, oo, no, control.get('default_path', '')

def region_info(region, oo, no):
    """(Sample, Grundton) – Opcodes der Reihe nach wie in Märmel: key= setzt den Grundton,
    ein späteres pitch_keycenter überschreibt ihn; ohne beides gilt 60. 'sample' = aus der Datei."""
    sample, kc = None, 60
    for name, value in region:
        if name == 'sample':
            sample = value
        elif name == 'key':
            n = parse_note(value, oo, no)
            if n is not None: kc = n
        elif name == 'pitch_keycenter':
            if value.strip().lower() == 'sample':
                kc = 'sample'
            else:
                n = parse_note(value, oo, no)
                if n is not None: kc = n
    return sample, kc

def find_file(base_dir, rel):
    rel = rel.replace('\\', '/')
    path = rel if os.path.isabs(rel) else os.path.join(base_dir, rel)
    if os.path.exists(path):
        return path
    for form in ('NFC', 'NFD'):
        p = unicodedata.normalize(form, path)
        if os.path.exists(p):
            return p
    return None

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    sfz_files = []
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if not x.startswith('.') and x != '__MACOSX']
        sfz_files += [os.path.join(d, f) for f in files if f.lower().endswith('.sfz') and not f.startswith('._')]
    sfz_files.sort()

    problems = 0
    checked = 0
    for sfz in sfz_files:
        try:
            text = open(sfz, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        regions, oo, no, default_path = parse_sfz(text)
        base = os.path.dirname(sfz)
        lines = []
        seen = set()
        for r in regions:
            sample, kc = region_info(r, oo, no)
            if not sample or sample.startswith('*'):
                continue
            rel = default_path.replace('\\', '/') + sample
            key = (rel, kc)
            if key in seen:
                continue
            seen.add(key)
            checked += 1
            from_name = key_from_filename(sample)
            path = find_file(base, rel)
            from_header, note = (None, 'fehlt') if path is None else key_from_header(path)
            sfz_key = from_header if kc == 'sample' else kc
            notes = []
            if from_name is not None and sfz_key is not None and from_name != sfz_key:
                notes.append('Name ≠ SFZ')
            if from_header is not None and sfz_key is not None and from_header != sfz_key and kc != 'sample':
                notes.append('Header ≠ SFZ')
            if from_name is not None and from_header is not None and from_name != from_header:
                notes.append('Name ≠ Header')
            if note:
                notes.append('Datei ' + note)
            if notes:
                lines.append('  %-40s Name %-12s SFZ %-12s Header %-12s %s' % (
                    os.path.basename(sample)[:40], note_name(from_name),
                    'sample' if kc == 'sample' else note_name(kc), note_name(from_header), ', '.join(notes)))
        if lines:
            problems += len(lines)
            print(os.path.relpath(sfz, root))
            print('\n'.join(lines))
            print()

    print('%d SFZ-Dateien, %d Samples geprüft, %d auffällig.' % (len(sfz_files), checked, problems))

if __name__ == '__main__':
    main()
