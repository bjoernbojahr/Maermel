# Märmel – Pegel einzelner Frequenzen in Zeitfenstern messen (Überblendungs-Test, 19.10.2026)
# Aufruf: python3 pruefen.py ausgabe.wav 220,660 0.2,0.7,1.2
# Erwartet: ev-pedal 660 Hz aus/an/aus; ev-wheel 1100 Hz 0 / halb / voll; ev-vel 220 voll, beide 0,71, 660 voll
import struct, array, math, sys
def rd(p):
    d=open(p,'rb').read(); f=d.find(b'fmt '); sr=struct.unpack('<I',d[f+12:f+16])[0]; ch=struct.unpack('<H',d[f+10:f+12])[0]
    i=d.find(b'data'); n=struct.unpack('<I',d[i+4:i+8])[0]; a=array.array('f',d[i+8:i+8+n]); return a[0::ch], sr
def mag(x, sr, f):
    w=2*math.pi*f/sr; c=2*math.cos(w); s1=s2=0.0
    for v in x: s0=v+c*s1-s2; s2=s1; s1=s0
    return math.sqrt(max(0.0, s1*s1+s2*s2-c*s1*s2))/len(x)*2
x, sr = rd(sys.argv[1]); freqs=[float(f) for f in sys.argv[2].split(',')]
for t0 in [float(t) for t in sys.argv[3].split(',')]:
    seg=x[int(t0*sr):int((t0+0.2)*sr)]
    print('  %.1f-%.1f s: ' % (t0, t0+0.2) + '  '.join('%d Hz %.3f' % (f, mag(seg, sr, f)) for f in freqs))
