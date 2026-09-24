"""Fixed release cadence ignores all legacy configuration without file access."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];w=Path(tempfile.mkdtemp(prefix='fixed-fps-',dir=r/'out'))
header=(r/'vita/direct/source/utils/fps_preference.h').read_text()
assert not any(s in header for s in ['fopen','fread','fscanf','rename','pvz2_fps_save','pvz2_fps_load'])
for name in ['fps_preference.txt','fps_preference.previous','fps_preference.pending','fps_cap.txt','novsync.txt']:
 (w/name).write_text('60\n')
(w/'check.c').write_text('#include <assert.h>\n#include "utils/fps_preference.h"\nint main(void){assert(PVZ2_TARGET_FPS==30 && PVZ2_FRAME_BUDGET_US==33334);}')
subprocess.run(['gcc','-O2','-I'+str(r/'vita/direct/source'),str(w/'check.c'),'-o',str(w/'check.exe')],check=True)
subprocess.run([str(w/'check.exe')],cwd=w,check=True)
for p in w.glob('*.txt'):assert p.read_text()=='60\n'
print('PASS: fixed 30 FPS; no configuration reads/writes; old files left intact')
