#!/usr/bin/env python3
"""Decode/encode SoH OTEX texture resources (0x40 header + type/w/h/size + data)."""
import struct
from PIL import Image
TYPES = {1:'RGBA32',2:'RGBA16',3:'CI4',4:'CI8',5:'I4',6:'I8',7:'IA4',8:'IA8',9:'IA16'}
def parse(b):
    t,w,h,n = struct.unpack_from('<IIII', b, 0x40)
    return TYPES.get(t, t), w, h, b[0x50:0x50+n], b[:0x40]
def decode(b):
    t,w,h,d,_ = parse(b)
    im = Image.new('RGBA',(w,h)); px=[]
    if t=='RGBA16':
        for i in range(w*h):
            v=(d[2*i]<<8)|d[2*i+1]
            px.append((((v>>11)&31)*255//31,((v>>6)&31)*255//31,((v>>1)&31)*255//31,255 if v&1 else 0))
    elif t=='RGBA32':
        px=[tuple(d[4*i:4*i+4]) for i in range(w*h)]
    elif t in('I4','IA4','CI4'):
        for i in range(w*h):
            v=(d[i//2]>>(4*(1-i%2)))&15
            if t=='IA4': I=(v>>1)*255//7; px.append((I,I,I,255 if v&1 else 0))
            else: I=v*17; px.append((I,I,I,255))
    elif t in('I8','CI8'):
        px=[(v,v,v,255) for v in d[:w*h]]
    elif t=='IA8':
        px=[((v>>4)*17,(v>>4)*17,(v>>4)*17,(v&15)*17) for v in d[:w*h]]
    elif t=='IA16':
        px=[(d[2*i],d[2*i],d[2*i],d[2*i+1]) for i in range(w*h)]
    else: return None
    im.putdata(px); return im
def encode_rgba16(im, header):
    im = im.convert('RGBA'); w,h = im.size; out = bytearray()
    for r,g,b,a in im.getdata():
        v = ((r*31+127)//255)<<11 | ((g*31+127)//255)<<6 | ((b*31+127)//255)<<1 | (1 if a>=128 else 0)
        out += bytes([v>>8, v&255])
    return header + struct.pack('<IIII', 2, w, h, len(out)) + bytes(out)
