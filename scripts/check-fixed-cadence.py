"""Seven days of simulated 30 Hz uptime; actual production pacer/power helpers."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];w=Path(tempfile.mkdtemp(prefix='fixed-cadence-',dir=r/'out'))
(w/'check.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include "utils/frame_pacer.h"
#include "utils/power_policy.h"
int main(void){
 Pvz2FramePacer p={0};uint64_t start=UINT64_C(4200000000),now=start;
 unsigned frames=30u*86400u*7u;
 for(unsigned i=0;i<frames;i++){
  unsigned delay=pvz2_frame_delay(&p,now);
  assert(delay<=33334);
  now+=delay+(i%251)+3000+(i%17000);
 }
 assert(p.deadline==start+UINT64_C(604800000000));
 for(unsigned i=0;i<10000;i++){
  now+=5000000;assert(pvz2_frame_delay(&p,now)==0);
  assert(pvz2_frame_delay(&p,now+1000)>=32333);
 }
 Pvz2PowerPolicy power={.mhz=444,.next_reduce=now+10000000};
 for(unsigned i=0;i<1000;i++){now+=33334;pvz2_power_step(&power,now,8000,33334);}
 assert(power.mhz==222);
 now+=40000;assert(pvz2_power_step(&power,now,26000,40000)==444);
 for(unsigned i=0;i<299;i++){now+=33334;assert(pvz2_power_step(&power,now,8000,33334)==444);}
 now+=33334;assert(pvz2_power_step(&power,now,8000,33334)==333);
 for(unsigned i=0;i<1000000;i++){now+=33334;assert(pvz2_power_step(&power,now,18000,33334)==333);}
 assert(pvz2_power_step(&power,now+=70000,6000,70000)==444);
 puts("PASS: 18,144,000 simulated frames over seven days, exact fractional cadence across 32-bit time wrap, 10,000 long-stall recoveries; sustained light-load reduction and immediate boost/cooldown");
}
''')
subprocess.run(['gcc','-O2','-I'+str(r/'vita/direct/source'),str(w/'check.c'),'-o',str(w/'check.exe')],check=True)
subprocess.run([str(w/'check.exe')],check=True,timeout=15)
print(w)
