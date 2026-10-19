"""Recheck preserved Lab 1 artifacts; does not replace the saved clean build runs."""
from pathlib import Path
from datetime import datetime,timezone
import json,hashlib,subprocess
r=json.loads(Path('docs/metrics/lab1/reproducibility.json').read_text())
print('BatchTherm / Lab 1 / preserved build and signal evidence',flush=True)
print('Artifact recheck UTC:',datetime.now(timezone.utc).isoformat(),flush=True)
assert r['first']==r['second'] and r['identical']
for ext,expected in r['second'].items():
 p=Path('docs/evidence/lab1')/('firmware'+ext)
 actual=hashlib.sha256(p.read_bytes()).hexdigest();assert actual==expected
 print(ext,actual,flush=True)
print('Two recorded clean Docker builds: ELF / HEX / BIN identical: PASS',flush=True)
s=json.loads(Path('docs/metrics/lab1/signals.json').read_text());assert s['decoded_matches_raw']
print('Independent VCD UART decode equals raw UART: PASS',flush=True)
print('LED regular intervals (ms):',round(s['min_ms'],3),'..',round(s['max_ms'],3),flush=True)
subprocess.run(['docker','run','--rm','-v',str(Path.cwd())+':/workspace','-w','/workspace','batchtherm-build:lab1','arm-none-eabi-size','docs/evidence/lab1/firmware.elf'],check=True)
