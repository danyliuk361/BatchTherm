"""Analyze actual event-driven Wokwi VCD; no stimulus or synthetic signals are produced."""
import json,sys,statistics
from pathlib import Path

def read_vcd(path):
 names={};events=[];t=0
 for line in Path(path).read_text().splitlines():
  if line.startswith('$var'):
   fields=line.split();names[fields[3]]=fields[4]
  elif line.startswith('#'):t=int(line[1:])
  elif line[:1] in ('0','1') and line[1:] in names:events.append((t,names[line[1:]],int(line[0])))
 return events

def stats(v):
 return {'n':len(v),'min':min(v),'avg':statistics.mean(v),'max':max(v)} if v else {'n':0}

def analyze(events):
 levels={};starts={};rises={};intervals={};widths={};frames=[];current=None;bits=[];byte_rises=[];last_scl=None;scl_period=[]
 from itertools import groupby
 for t,group in groupby(events,key=lambda e:e[0]):
  before=levels.copy();changes={n:v for _,n,v in group};levels.update(changes)
  for name,value in changes.items():
   prev=before.get(name)
   if value and prev==0:
    if name in rises:intervals.setdefault(name,[]).append((t-rises[name])/1e6)
    rises[name]=t;starts[name]=t
   if not value and prev==1 and name in starts:widths.setdefault(name,[]).append((t-starts[name])/1e3)
  prev=before.get('SDA');value=levels.get('SDA')
  if prev is not None and value!=prev and before.get('SCL',1)==1 and levels.get('SCL',1)==1:
   if value==0:
    if current:current['end_ns']=t;current['end']='RESTART';frames.append(current)
    current={'start_ns':t,'bytes':[]};bits=[];byte_rises=[]
   elif current:
    current['end_ns']=t;current['end']='STOP';frames.append(current);current=None;bits=[];byte_rises=[]
  if before.get('SCL')==0 and levels.get('SCL')==1 and current:
   bits.append(levels.get('SDA',1));byte_rises.append(t)
   if len(bits)==9:
    current['bytes'].append({'value':int(''.join(map(str,bits[:8])),2),'bits_msb':bits[:8],'ack':bits[8]==0,'rising_ns':byte_rises[:]})
    scl_period.extend((b-a)/1e3 for a,b in zip(byte_rises,byte_rises[1:]));bits=[];byte_rises=[]
 return {'i2c':frames,'scl_period_us':stats(scl_period),'period_ms':{n:stats(v) for n,v in intervals.items()},'high_width_us':{n:stats(v) for n,v in widths.items()}}
if __name__=='__main__':
 result=analyze(read_vcd(sys.argv[1]));Path(sys.argv[2]).write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k!='i2c'},indent=2));print('I2C frames',len(result['i2c']))
