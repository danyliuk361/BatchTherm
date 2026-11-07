"""Analyze the captured MCU records, GPIO timing and independent SD CRC."""
from __future__ import annotations
import argparse
import json
import math
import re
import statistics
import struct
import zlib
from pathlib import Path
from bisect import bisect_right

def fields(line):
    return {k:int(v) if re.fullmatch(r'-?\d+',v) else v
            for k,v in re.findall(r'(\w+)=([^\s]+)',line)}

def summary(values):
    if not values:return {'n':0}
    a=sorted(values)
    return {'n':len(a),'min':a[0],'avg':round(statistics.mean(a),3),
            'max':a[-1],'p99':a[math.ceil(.99*len(a))-1]}

def journal(path):
    data=path.read_bytes()
    records=[];damage=[]
    for i in range(512):
        block=data[(128+i)*512:(129+i)*512]
        if not any(block):continue
        if len(block)<512:
            damage.append(i+1);continue
        magic,seq,bid,time,held=struct.unpack_from('<5I',block)
        crc=zlib.crc32(block[:60])
        seal=bytes(1+((crc>>(j*8))%255) for j in range(4))
        if (magic,seq,block[508:])!=(0x31544842,i+1,seal) or crc!=struct.unpack_from('<I',block,60)[0]:
            damage.append(i+1);continue
        p=list(struct.unpack_from('<hhhH',block,20))
        ntc,bmp,low,high=struct.unpack_from('<4h',block,28)
        records.append({'seq':seq,'id':bid,'t':time,'hold':held,'profile':p,'ntc':ntc,'bmp':bmp,
                        'min':low,'max':high,'phase':block[36],'why':block[37]})
    return {'records':records,'crc_or_seal_errors':damage}

def signals(path,first_ms=0):
    names={};level={};t=0;rises={};fall={};begins={}
    for ln in path.open():
        if ln.startswith('$var'):
            f=ln.split();names[f[3]]=f[4]
        elif ln.startswith('#'):t=int(ln[1:])/1000 # 1 ns -> us
        elif ln[:1] in ('0','1'):
            name=names.get(ln[1:].strip())
            if name is None:continue
            v=int(ln[0]);old=level.get(name);level[name]=v
            if old==0 and v: rises.setdefault(name,[]).append(t)
            if old==1 and not v:fall.setdefault(name,[]).append(t)
    adc=[x for x in rises.get('ADC_MARK',[]) if x>=first_ms*1000]
    adc_low=fall.get('ADC_MARK',[])
    widths=[round(adc_low[j]-x,3) for x in adc if (j:=bisect_right(adc_low,x))<len(adc_low)]
    raw=rises.get('LID_RAW',[]);raw_closed=fall.get('LID_RAW',[])
    sd_hi=rises.get('SD_BUSY',[]);sd_lo=fall.get('SD_BUSY',[])
    reactions=[]
    for end in fall.get('BATCH_ACTIVE',[]):
        i=bisect_right(raw,end)-1
        if i<0:continue
        edge=raw[i];closed=bisect_right(raw_closed,edge)
        if closed<len(raw_closed) and raw_closed[closed]<end:continue
        if end-edge<10000:continue
        on_sd=bisect_right(sd_hi,edge)>bisect_right(sd_lo,edge)
        reactions.append({'edge_us':edge,'response_us':end,
                          'latency_us':round(end-edge,3),'during_sd':on_sd})
    sd_intervals=[round(sd_lo[j]-x,3) for x in sd_hi if (j:=bisect_right(sd_lo,x))<len(sd_lo)]
    soak_lo=fall.get('SOAK_LED',[])
    active_hi=rises.get('BATCH_ACTIVE',[]);active_lo=fall.get('BATCH_ACTIVE',[])
    soak=[];excluded_soak=[]
    for start in rises.get('SOAK_LED',[]):
        j=bisect_right(soak_lo,start)
        if j==len(soak_lo):continue
        end=soak_lo[j]
        active=bisect_right(active_hi,start)>bisect_right(active_lo,start)
        leaves_active=bisect_right(active_lo,end)>bisect_right(active_lo,start)
        pulse={'start_us':start,'end_us':end,'width_us':round(end-start,3)}
        if start>=first_ms*1000 and active and not leaves_active:
            soak.append(pulse)
        else:
            excluded_soak.append({**pulse,'starts_before_window':start<first_ms*1000,
                                  'starts_with_active_batch':active,'crosses_end_of_batch':leaves_active})
    soak_widths=[x['width_us'] for x in soak]
    return {'soak_width_us':summary(soak_widths),'soak_samples_us':soak_widths,
            'soak_pulses':soak,'excluded_soak_pulses':excluded_soak,
            'adc_width_us':summary(widths),'adc_period_us':summary([round(y-x,3) for x,y in zip(adc,adc[1:])]),
            'sd_busy_us':summary(sd_intervals),'lid_latency_us':summary([r['latency_us'] for r in reactions]),
            'lid_during_sd':sum(r['during_sd'] for r in reactions),'reaction_points':reactions,'vcd_end_us':t}

def analyze(folder):
    r=json.loads((folder/'run.json').read_text())
    lines=(folder/'uart.log').read_text(errors='replace').splitlines()
    get=lambda p:[fields(l) for l in lines if l.startswith(p)]
    hearts=get('HEART ');events=get('EV ');commits=get('COMMIT ');totals=get('TOTAL ')
    metrics={x['name']:{**x,'avg':round(x['sum']/x['n'],3) if x['n'] else None} for x in get('MET ')}
    total=totals[-1] if totals else {}
    first=int(r.get('measure_start_ms') or 0)
    result={'run':r,'first':get('FIRST '),'boot':get('BOOT '),'heart_n':len(hearts),
            'first_heart':hearts[0] if hearts else None,'last_heart':hearts[-1] if hearts else None,
            'largest_heartbeat_gap_ms':max((b['t']-a['t'] for a,b in zip(hearts,hearts[1:])),default=0),
            'events':events,'commits':commits,'counts':get('COUNTS '),'metrics':metrics,'total':total,
            'busy_percent':round(100*total['busy_us']/total['elapsed_us'],3) if total.get('elapsed_us') else None,
            'status_ack_us':summary([x['lat_us'] for x in get('ACK cmd=status ') if x.get('t',0)>=first]),
            'abort_ack_us':summary([x['lat_us'] for x in get('ACK cmd=abort ')]),
            'lid_poll_reaction_us':summary([x['latency_us'] for x in get('REACT ')]),
            'sector_commit_us':summary([x['elapsed_us'] for x in commits]),
            'rejections':get('ERR '),'bmp_status':get('BMP '),'export':get('EXPORT '),
            'recovery':get('RECOVERY ')}
    starts={};durations=[]
    for item in events:
        bid=item['id']
        if item['phase']==2:starts[bid]=item['t']
        if item['phase']==3 and bid in starts:durations.append(item['t']-starts[bid])
    result['hold_mcu_ms']=summary(durations)
    result['hold_samples_mcu_ms']=durations
    if (folder/'capture.vcd').exists():
        result['gpio']=signals(folder/'capture.vcd',first)
    if (folder/'sd-after.img').exists():
        result['journal']=journal(folder/'sd-after.img')
    return result,hearts

def validate_s2(result,hearts,minimum):
    run=result['run'];start=run.get('measure_start_ms')
    window=[x for x in hearts if start is not None and x['t']>=start]
    end=window[-1]['t'] if window else None
    elapsed=end-start if end is not None else 0
    records=result.get('journal',{}).get('records',[])
    grouped={}
    for x in records:grouped.setdefault(x['id'],[]).append(x)
    batches=sum([a['phase'] for a in g]==[1,2,3,4] and g[-1]['hold']>=30000
                and all(a['why']==0 for a in g) for g in grouped.values())
    checks={
      'capture_completed':run['status']=='captured',
      'one_boot':len(result['boot'])==1,
      'continuous_mcu_duration':elapsed>=minimum,
      'heartbeat_contiguous':bool(window) and all(b['hb']==a['hb']+1 and 0<b['t']-a['t']<=1500
             and 0<((b['us']-a['us'])&0xffffffff)<=1500000 for a,b in zip(window,window[1:])),
      'timer_wraps_observed':bool(window) and window[-1]['w']-window[0]['w']>=minimum//70,
      'no_wrapped_metric_intervals':all(m['max']<10000000 for m in result['metrics'].values()),
      'complete_task_metrics':all(result['metrics'].get(k,{}).get('n',0)>0 for k in
                                  ('loop','lid','ntc','bmp','rx','sd','heart')),
      'no_rx_errors_or_overflow':bool(window) and all(not x['rx'] and not x['err'] for x in window),
      'both_channels_valid':bool(window) and all(x['nv'] and x['av'] for x in window),
      'no_heap_growth':result['total'].get('alloc_attempts')==0 and result['total'].get('heap')==0,
      'journal_crc_sequence':bool(records) and not result.get('journal',{}).get('crc_or_seal_errors')
              and [x['seq'] for x in records]==list(range(1,len(records)+1))
              and [x['seq'] for x in records]==[x['seq'] for x in result['commits']],
      'all_batches_finished':batches==len(grouped)==minimum//120000,
      'recorded_status_traffic':result['status_ack_us']['n']>=minimum//10000-1,
      'raw_vcd_present':bool(result.get('gpio',{}).get('adc_width_us',{}).get('n')),
    }
    if minimum==3600000:
        gpio=result.get('gpio',{})
        checks['gpio_soak_count']=gpio.get('soak_width_us',{}).get('n')==batches
        checks['gpio_capture_spans_window']=gpio.get('vcd_end_us',0)>=(start+minimum)*1000 if start is not None else False
    return {'valid':all(checks.values()),'checks':checks,'start_ms':start,'end_ms':end,
      'elapsed_mcu_ms':elapsed,'required_ms':minimum,'completed_batches':batches,'journal_records':len(records),
      'elf_sha256':run['elf_sha256'],'hex_sha256':run['hex_sha256'],'timing_and_evidence':'metrics.json'}

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('directory',type=Path)
    p.add_argument('--s2-ms',type=int)
    p.add_argument('--brief',action='store_true',help='Compact S3 evidence for the demonstration')
    a=p.parse_args()
    result,hearts=analyze(a.directory)
    (a.directory/'metrics.json').write_text(json.dumps(result,indent=2)+'\n')
    if a.brief:
        gpio=result['gpio']; lid=gpio['lid_latency_us']; status=result['status_ack_us']
        print(f"BatchTherm {result['run']['mode']} | MCU {result['total']['elapsed_us']/1e6:.3f} s")
        print(f"Lid: {lid['n']} edges, {gpio['lid_during_sd']} during SD, max {lid['max']/1e3:.3f} ms")
        print(f"Status: {status['n']} ACKs, max {status['max']/1e3:.3f} ms")
        print(f"Durable sectors: {len(result['commits'])}; CRC errors: {len(result['journal']['crc_or_seal_errors'])}")
        raise SystemExit(0)
    print('BatchTherm',result['run']['mode'],'first valid frame',result['first'])
    print('MCU totals:',result['total'],'task busy %:',result['busy_percent'])
    print('GPIO lid (us):',result.get('gpio',{}).get('lid_latency_us'),'during SD:',result.get('gpio',{}).get('lid_during_sd'))
    print('UART status/abort ACK (us):',result['status_ack_us'],result['abort_ack_us'])
    print('Durable sectors:',len(result['commits']),'journal image CRC errors:',result.get('journal',{}).get('crc_or_seal_errors'))
    if a.s2_ms:
        final=validate_s2(result,hearts,a.s2_ms)
        (a.directory/'S2.result.json').write_text(json.dumps(final,indent=2)+'\n')
        print(json.dumps(final,indent=2))
        raise SystemExit(0 if final['valid'] else 1)
