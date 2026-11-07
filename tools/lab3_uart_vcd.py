"""Decode 115200 8N1 by sampling captured GPIO levels at bit centers."""
import json,sys
from bisect import bisect_right
from collections import deque
from pathlib import Path
from lab3_analyze import summary

def channels(p):
    names={};data={};t=0
    for line in p.open():
        if line.startswith('$var'):
            f=line.split();names[f[3]]=f[4]
        elif line.startswith('#'): t=int(line[1:])/1000
        elif line[:1] in ('0','1'):
            name=names.get(line[1:].strip())
            if name in ('UART_RX','UART_TX'):data.setdefault(name,[]).append((t,int(line[0])))
    return data

def decode(events):
    bits=1e6/115200
    time=[t for t,_ in events]
    lev=[v for _,v in events]
    old=1;bound=-1;cur=[];out=[];errors=0
    for t,v in events:
        if t>=bound and old==1 and v==0:
            bound=t+9.75*bits
            sample=lambda x:lev[bisect_right(time,t+x*bits)-1]
            b=sum(sample(1.5+i)<<i for i in range(8))
            if sample(9.5)!=1:
                errors+=1;old=v;continue
            cur.append((t,b))
            if b==10:
                text=bytes(b for _,b in cur)
                out.append((cur[0][0],t+9.5*bits,text))
                cur=[]
        old=v
    return out,errors

if __name__=='__main__':
    folder=Path(sys.argv[1])
    ds=channels(folder/'capture.vcd')
    rx,rx_err=decode(ds.get('UART_RX',[]));tx,tx_err=decode(ds.get('UART_TX',[]))
    rx_bytes=b''.join(l for _,_,l in rx);tx_bytes=b''.join(l for _,_,l in tx)
    (folder/'wire-rx.log').write_bytes(rx_bytes);(folder/'wire-tx.log').write_bytes(tx_bytes)
    inputs=deque(t for _,t,l in rx if l.strip()==b'status')
    intervals=[b-a for a,b in zip(inputs,list(inputs)[1:])]
    input_count=len(inputs)
    statuses=[(t,end,l) for t,end,l in tx if l.startswith(b'ACK cmd=status ')]
    pair=[]
    for t,end,l in statuses:
        if inputs and inputs[0]<=t:
            pair.append(t-inputs.popleft())
    result={
      'rx_framing_errors':rx_err,'tx_framing_errors':tx_err,
      'rx_bytes':len(rx_bytes),'tx_bytes':len(tx_bytes),
      'tx_matches_uart_log':tx_bytes==(folder/'uart.log').read_bytes(),
      'status_commands_at_pin':input_count,'status_acks_on_wire':len(statuses),
      'status_interarrival_us':summary(intervals),
      'status_latency_us_when_fifo_pairs_complete':summary(pair) if not inputs and len(pair)==len(statuses)==input_count else None,
      'example_ack':{'bytes':len(statuses[0][2]),'wire_us_to_stop_center':round(statuses[0][1]-statuses[0][0],3),
        'expected_us_to_stop_center':round((10*len(statuses[0][2])-.5)/.1152,3)} if statuses else None
    }
    (folder/'uart-wire.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
