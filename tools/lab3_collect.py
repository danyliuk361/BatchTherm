"""Long-run supervisor: one simulator, stop by MCU clock, validate on exit."""
from pathlib import Path
import argparse
import fcntl
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone

p=argparse.ArgumentParser()
p.add_argument('out',type=Path)
p.add_argument('frozen',type=Path)
a=p.parse_args()
out=a.out.resolve()
out.parent.mkdir(parents=True,exist_ok=True)
frozen=a.frozen.resolve()
identity=json.loads((frozen/'identity.json').read_text())
for ext in ('elf','hex','bin'):
    actual=hashlib.sha256((frozen/f'firmware.{ext}').read_bytes()).hexdigest()
    if actual!=identity[ext]:raise SystemExit('Frozen image mismatch: '+ext)
sd=frozen/'sd-before.img'
if hashlib.sha256(sd.read_bytes()).hexdigest()!=identity['sd_before_sha256']:
    raise SystemExit('Frozen SD image mismatch')
if hashlib.sha256(Path('diagram.json').read_bytes()).hexdigest()!=identity['diagram_sha256']:
    raise SystemExit('Diagram differs from the frozen baseline')
with (out.parent/'batchtherm-s2.lock').open('a+') as lock:
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    started=datetime.now(timezone.utc).isoformat()
    session=out.parent/'S2.session.json'
    state={'pid':os.getpid(),'state':'running','started':started,'run_dir':str(out),'frozen':str(frozen),'elf_sha256':identity['elf']}
    session.write_text(json.dumps(state,indent=2)+'\n')
    with (out.parent/(out.name+'.console.log')).open('x') as log:
        result=subprocess.run(['node','tools/lab3_run.cjs','s2',str(out)],
             env={**os.environ,'FIRMWARE_DIR':str(frozen),'SD_IMAGE':str(sd)},
             stdout=log,stderr=subprocess.STDOUT)
        validator=subprocess.run([sys.executable,'tools/lab3_analyze.py',str(out),'--s2-ms','3600000'],stdout=log,stderr=subprocess.STDOUT)
    state.update(state='succeeded' if result.returncode==validator.returncode==0 else 'failed',
                 ended=datetime.now(timezone.utc).isoformat(),runner_exit=result.returncode,validator_exit=validator.returncode)
    session.write_text(json.dumps(state,indent=2)+'\n')
    if not (out/'S2.result.json').is_file():
        out.mkdir(exist_ok=True)
        (out/'S2.result.json').write_text(json.dumps({'valid':False,'failure':'runner or validator did not produce evidence','session':state},indent=2))
    raise SystemExit(0 if state['state']=='succeeded' else 1)
