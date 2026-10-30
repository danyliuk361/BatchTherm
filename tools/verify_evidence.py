from pathlib import Path
import json,hashlib,re
root=Path('.')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
elf=sha('docs/evidence/lab2/firmware.elf');checks={};manifest={}
for name in ['integration-final','functional-final','lid-final','persistent-read-final']:
 p=Path('docs/metrics/lab2')/name;r=json.loads((p/'run.json').read_text());assert r['elf_sha256']==elf,(name,r['elf_sha256']);manifest[name]={'run':r,'files':{str(f):sha(f) for f in p.iterdir() if f.is_file()}}
s=Path('docs/metrics/lab2/integration-final/uart.log').read_text();assert 'status=NACK' in s and 'status=OK' in s;assert 'commands=6 rx_overflow=0 rx_errors=0' in s;assert 'mismatch=0' in s and 'ERR ' not in s;checks['integration']=True
s=Path('docs/metrics/lab2/functional-final/uart.log').read_text();
for pattern in ['adc=2048 sample_dC=250','adc=857 sample_dC=580','adc=764 sample_dC=620','adc=742 sample_dC=630','sample_valid=0']:
 assert pattern in s,pattern
checks['temperatures']=True
s=Path('docs/metrics/lab2/lid-final/uart.log').read_text();assert re.search(r'LID t=\d+ closed=1',s) and re.search(r'LID t=\d+ closed=0',s);assert 'usable=1 band=1' in s;checks['lid']=True
s=Path('docs/metrics/lab2/persistent-read-final/uart.log').read_text();assert 'STORAGE op=READ_ONLY sector=32 result=0 mismatch=0' in s and 'WRITE_READ' not in s;checks['persistence_new_process']=True
expected=bytes((n*37+19)&255 for n in range(512))
for name in ['integration-final','persistent-read-final']:
 b=(Path('docs/metrics/lab2')/name/'sd-after.img').read_bytes();assert b[32*512:33*512]==expected
raw=Path('docs/metrics/lab2/integration-final/uart.log').read_bytes();decoded=Path('docs/captures/lab2/uart-decoded-ascii.txt').read_bytes();assert raw==decoded;checks['uart_independent_decode']=True
text=Path('docs/metrics/lab2/test-capture-final.log').read_text();assert 'ALL THERMAL TESTS PASS' in text and 'Coverage gate >=90%' in text;checks['host_tests']=True
text=Path('docs/metrics/lab2/build-final.log').read_text();assert 'warning:' not in text and 'error:' not in text;checks['arm_build_no_warnings']=True
Path('docs/metrics/lab2/verification.json').write_text(json.dumps({'elf_sha256':elf,'checks':checks},indent=2));Path('docs/metrics/lab2/manifest.json').write_text(json.dumps(manifest,indent=2));print(json.dumps(checks,indent=2))
