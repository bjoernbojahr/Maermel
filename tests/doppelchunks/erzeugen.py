#!/usr/bin/env python3
# Märmel – Testdateien mit doppelten Chunks (17.10.2026). Aufruf im Ordner: python3 erzeugen.py, dann t.sfz laden.
# Erwartet (erster Chunk gilt): A Root 48 + Loop 100-1100, B 2000 Frames, C Root 55 (smpl, nicht inst), D Root 40 + Loop 300-1299.
import struct, sys
n=2000
pcm=b''.join(struct.pack('<h', int(8000*((i%100)/50-1))) for i in range(n))
def ck(tag,body):
    b=tag+struct.pack('<I',len(body))+body
    return b+(b'\0' if len(body)%2 else b'')
fmt=struct.pack('<HHIIHH',1,1,44100,88200,2,16)
def smpl(root, ls=None, le=None):
    loops = 1 if ls is not None else 0
    b=struct.pack('<9I',0,0,22675,root,0,0,0,loops,0)
    if loops: b+=struct.pack('<6I',0,0,ls,le,0,0)
    return ck(b'smpl',b)
def wav(name, chunks):
    body=b'WAVE'+b''.join(chunks)
    open(name,'wb').write(b'RIFF'+struct.pack('<I',len(body))+body)
wav('A_smpl2x.wav',[ck(b'fmt ',fmt),smpl(48,100,1100),ck(b'data',pcm),smpl(72)])
wav('B_data2x.wav',[ck(b'fmt ',fmt),ck(b'data',pcm),ck(b'data',pcm[:200]),smpl(50,200,900)])
wav('C_inst.wav',[ck(b'fmt ',fmt),smpl(55,100,1000),ck(b'inst',bytes([60,0,0,0,127,1,127])),ck(b'data',pcm)])
# AIFF with two INST
def bck(tag,body):
    b=tag+struct.pack('>I',len(body))+body
    return b+(b'\0' if len(body)%2 else b'')
import math
def ext80(x):
    e=int(math.floor(math.log2(x))); m=int(x/2**e*2**63)
    return struct.pack('>HQ',16383+e,m)
comm=struct.pack('>hIh',1,n,16)+ext80(44100)
ssnd=struct.pack('>II',0,0)+b''.join(struct.pack('>h', int(8000*((i%100)/50-1))) for i in range(n))
def mark(pts):
    b=struct.pack('>H',len(pts))
    for i,p in pts: b+=struct.pack('>hI',i,p)+b'\x00\x00'
    return bck(b'MARK',b)
def inst(root,b,e): return bck(b'INST',struct.pack('>bbbbbbh',root,0,0,127,1,127,0)+struct.pack('>hhh',1,b,e)+struct.pack('>hhh',0,0,0))
body=b'AIFF'+bck(b'COMM',comm)+mark([(1,300),(2,1300)])+inst(40,1,2)+bck(b'SSND',ssnd)+mark([(1,10),(2,20)])+inst(80,1,2)
open('D_inst2x.aif','wb').write(b'FORM'+struct.pack('>I',len(body))+body)
