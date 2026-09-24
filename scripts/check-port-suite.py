"""Run the port's local checks only. Never starts a game, emulator or device session."""
from pathlib import Path
import argparse,subprocess,sys,json,time
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--only',help='Comma-separated check names; update just these results');a=p.parse_args()
names=sorted(f.stem.removeprefix('check-') for f in (r/'scripts').glob('check-*.py') if f.name!='check-port-suite.py')
if a.only:
 selected=a.only.split(',');assert all(n in names for n in selected);names=selected
a.output.mkdir(parents=True,exist_ok=True);manifest=a.output/'checks.json'
results=json.loads(manifest.read_text()) if a.only and manifest.exists() else []
for name in names:
 cmd=[sys.executable,str(r/'scripts'/f'check-{name}.py')]
 if name.endswith('-arm'):cmd+=['--loader-elf',str(r/'build-vita-direct/pvz2_loader')]
 if name in ['time-arm','placement-arm','program-lifetime-arm','imports-arm','heap-fallback-arm']:cmd+=['--game-lib',str(r/'game/libPVZ2.so')]
 if name=='runtime-support':cmd+=[str(r/'game/main.147.com.ea.game.pvz2_row.obb')]
 if name=='stall-overhead-arm':cmd+=['--expected-lookups','1']
 begin=time.monotonic()
 try:
  run=subprocess.run(cmd,capture_output=True,text=True,timeout=180,cwd=r)
  (a.output/f'{name}.log').write_text(run.stdout+run.stderr,encoding='utf-8')
  result={'name':name,'passed':run.returncode==0,'seconds':round(time.monotonic()-begin,3)}
 except subprocess.TimeoutExpired as e:
  result={'name':name,'passed':False,'error':'180 second timeout'}
  (a.output/f'{name}.log').write_bytes((e.stdout or b'')+(e.stderr or b''))
 results=[x for x in results if x['name']!=name]+[result]
 manifest.write_text(json.dumps(sorted(results,key=lambda x:x['name']),indent=2)+'\n')
 print(result,flush=True)
sys.exit(not all(x['passed'] for x in results))
