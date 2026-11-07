# Märmel – SFZ Sample Player
# Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
# SPDX-License-Identifier: AGPL-3.0-or-later
# Additional terms under section 7 of the license (attribution, name): see NOTICE.md
#
# paramidtest.py – prüft die AU/VST3-Parameternummern (siehe Source/ParameterIds.h)
# JUCE bildet die Nummer aus der internen ID (String::hashCode, bei AU oberstes Bit gelöscht). GarageBand zeigt die
# sechs kleinsten Nummern als Smart Controls. Dieser Test liest alle Parameter-IDs aus der Gruppenliste in
# PluginProcessor.cpp und prüft: die sechs kleinsten sind g1 … g6 in dieser Reihenfolge, keine doppelten Nummern.
# Aufruf (im Projektordner):  python3 tests/paramidtest.py
import os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, '..', 'Source', 'PluginProcessor.cpp'), encoding='utf-8').read()
ids_h = open(os.path.join(here, '..', 'Source', 'ParameterIds.h'), encoding='utf-8').read()

consts = dict(re.findall(r'inline constexpr const char\* (\w+)\s*=\s*"([^"]+)"', ids_h))
block = src[src.index('const std::vector<GroupDef> groups = {'):]
block = block[:block.index('};')]
ids = []
for inner in re.findall(r'\{\s*"[^"]+",\s*"[^"]+",\s*\{([^}]*)\}', block):
    for tok in re.findall(r'"([^"]+)"|maermel::ids::(\w+)', inner):
        ids.append(tok[0] if tok[0] else consts[tok[1]])

def jhash(s):                       # juce::String::hashCode (UTF-8-Zeichen, hier nur ASCII)
    h = 0
    for c in s.encode('utf-8'):
        h = (31 * h + c) & 0xffffffff
    return h

au = {i: jhash(i) & 0x7fffffff for i in ids}
order = sorted(ids, key=lambda i: au[i])
want = ['g1', 'g2', 'g3', 'g4', 'g5', 'g6']
ok = True
print('%d Parameter' % len(ids))
print('kleinste Nummern:', ', '.join('%s=%d' % (i, au[i]) for i in order[:8]))
if order[:6] != want:
    print('FEHLER: GarageBand würde andere Parameter zeigen:', order[:6]); ok = False
if len(set(au.values())) != len(ids):
    print('FEHLER: doppelte Parameternummer'); ok = False
if len(set(ids)) != len(ids):
    print('FEHLER: doppelte ID'); ok = False
print('OK' if ok else 'NICHT OK')
sys.exit(0 if ok else 1)
