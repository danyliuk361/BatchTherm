from pathlib import Path
import json
result={}
for f in Path('tests/build').glob('*.c.gcov'):
 text=f.read_text();first=text.splitlines()[0]
 if '/Devices/' not in first and '/App/' not in first:continue
 total=hit=0
 for line in text.splitlines():
  parts=line.split(':',2)
  if len(parts)!=3:continue
  count,lineno=parts[0].strip(),parts[1].strip()
  if not lineno.isdigit() or int(lineno)==0 or count=='-':continue
  total+=1
  if count not in ('#####','====='):hit+=1
 pct=100*hit/total
 result[f.stem]={'covered':hit,'lines':total,'percent':round(pct,2)}
 assert pct>=90,(f,pct)
assert len(result)==4,result
Path('tests/build/coverage.json').write_text(json.dumps(result,indent=2))
print('Coverage gate >=90% per Devices/App file: PASS',result)
