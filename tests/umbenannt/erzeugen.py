# Märmel – SFZ Sample Player
# Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
# SPDX-License-Identifier: AGPL-3.0-or-later
# Additional terms under section 7 of the license (attribution, name): see NOTICE.md
#
# Erzeugt Testfälle für umbenannte Instrumente (siehe LIESMICH.md) im Ordner „faelle“.
import os, shutil, struct, math
sr = 44100
pcm = b''.join(struct.pack('<h', int(8000 * math.sin(2 * math.pi * 440 * i / sr))) for i in range(sr // 10))
def wav():
    fmt = struct.pack('<4sIHHIIHH', b'fmt ', 16, 1, 1, sr, sr * 2, 2, 16)
    data = struct.pack('<4sI', b'data', len(pcm)) + pcm
    return b'RIFF' + struct.pack('<I', 4 + len(fmt) + len(data)) + b'WAVE' + fmt + data

# SFZ wie von „ottowaalkes“ geschrieben: Unterordner Light/Dark/Heavy im Sample-Ordner
sfz_alt = ''.join(f'<region> sample=ottowaalkes Samples/{layer}/c4.wav lokey=60 hikey=60 lovel={lo} hivel={hi}\n'
                  for layer, lo, hi in (('Light', 1, 40), ('Dark', 41, 90), ('Heavy', 91, 127)))
sfz_default = '<control> default_path=ottowaalkes Samples/\n' + sfz_alt.replace('ottowaalkes Samples/', '')
faelle = {
    # Name: (Instrumentname, SFZ-Text, Ordner, Unterordner)
    '1_pfad_stimmt':        ('ottowaalkes', sfz_alt, 'ottowaalkes Samples', ('Light', 'Dark', 'Heavy')),
    '2_ordner_samples':     ('karldall', sfz_alt, 'karldall Samples', ('Light', 'Dark', 'Heavy')),
    '3_ordner_nur_name':    ('karldall', sfz_alt, 'karldall', ('Light', 'Dark', 'Heavy')),
    '4_default_path':       ('karldall', sfz_default, 'karldall Samples', ('Light', 'Dark', 'Heavy')),
    '5_unterordner_anders': ('karldall', sfz_alt, 'karldall Samples', ('Hell', 'Dunkel', 'Schwer')),
    '6_kein_passender':     ('karldall', sfz_alt, 'irgendwas Samples', ('Light', 'Dark', 'Heavy')),
    '7_beide_ordner':       ('karldall', sfz_alt, None, None),
}
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'faelle')
shutil.rmtree(root, ignore_errors=True)
for fall, (name, text, folder, subs) in faelle.items():
    d = os.path.join(root, fall)
    os.makedirs(d)
    open(os.path.join(d, name + '.sfz'), 'w').write(text)
    if folder is None:
        # alter Ordner enthält nur Light (gefunden, bleibt), Dark und Heavy liegen schon im neuen Ordner
        for f, sub in (('ottowaalkes Samples', 'Light'), ('karldall Samples', 'Dark'), ('karldall Samples', 'Heavy')):
            os.makedirs(os.path.join(d, f, sub))
            open(os.path.join(d, f, sub, 'c4.wav'), 'wb').write(wav())
        continue
    for sub in subs:
        os.makedirs(os.path.join(d, folder, sub))
        open(os.path.join(d, folder, sub, 'c4.wav'), 'wb').write(wav())
print('erzeugt in', root)
