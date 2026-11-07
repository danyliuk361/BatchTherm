"""Build an explicit test precondition, not a captured journal."""
import argparse
import struct
import zlib
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('sectors', type=int, choices=range(513), metavar='RECORDS')
p.add_argument('output', type=Path)
a=p.parse_args()
data=bytearray(2097152)
for index in range(a.sectors):
    seq=index+1
    pos=(128+index)*512
    struct.pack_into('<5I7hH',data,pos,0x31544842,seq,seq,seq*1000,5000,600,20,350,5,250,240,250,600)
    # Layout: profile hold_s at 26, extrema at 32/34, phase/reason at 36/37.
    data[pos+36]=4
    crc=zlib.crc32(data[pos:pos+60])
    struct.pack_into('<I',data,pos+60,crc)
    for i in range(4): data[pos+508+i]=1+((crc>>(i*8))%255)
with a.output.open('xb') as f: f.write(data)
print('Synthetic fixture:',a.sectors,'CRC-sealed record sectors')
