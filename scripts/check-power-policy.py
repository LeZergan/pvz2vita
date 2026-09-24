"""Check the production CPU policy with deterministic frame traces; no device."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix='power-policy-', dir=root/'out'))
(work/'check.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include "utils/power_policy.h"
#include "utils/frame_pacer.h"
#define pvz2_power_step(p,n,w,f) pvz2_power_step_budget(p,n,w,f,16667)
int main(void) {
    Pvz2PowerPolicy p = {.mhz=444, .next_reduce=10000000};
    uint64_t now=0;
    /* Boot cannot downclock before cooldown; sustained light menu can. */
    for(unsigned i=0;i<599;i++) {
        now+=16667;
        assert(pvz2_power_step(&p,now,8000,16667)==444);
    }
    now+=16667; assert(pvz2_power_step(&p,now,8000,16667)==333);
    for(unsigned i=0;i<1000;i++) {
        now+=16667; assert(pvz2_power_step(&p,now,11000,16667)==333);
    }
    /* First expensive frame boosts; no slow moving average. */
    now+=20000; assert(pvz2_power_step(&p,now,13000,20000)==444);
    for(unsigned i=0;i<599;i++) {
        now+=16667; assert(pvz2_power_step(&p,now,6000,16667)==444);
    }
    now+=16667; assert(pvz2_power_step(&p,now,6000,16667)==333);
    /* A GPU/present stall also prevents frequency reduction. */
    now+=50000; assert(pvz2_power_step(&p,now,7000,50000)==444);
    for(unsigned i=0;i<10000;i++) {
        now+=50000; assert(pvz2_power_step(&p,now,40000,50000)==444);
    }
    /* Mixed frames cannot accumulate disconnected headroom samples. */
    for(unsigned i=0;i<10000;i++) {
        now+=16667; assert(pvz2_power_step(&p,now,i%100 ? 7000 : 10000,16667)==444);
    }
    /* 64-bit monotonic time across the 32-bit microsecond wrap. */
    now=UINT64_C(0xffffffff)+100000000;
    for(unsigned i=0;i<180;i++) pvz2_power_step(&p,now+i*16667,8000,16667);
    assert(p.mhz==333);
    /* Intentional 30 FPS waiting must not be mistaken for overload. RC14
     * boosted every capped frame above 19 ms back to 444 MHz. */
    p=(Pvz2PowerPolicy){.mhz=444};now=100000000;
    for(unsigned i=0;i<600;i++) {
        now+=33334; pvz2_power_step_budget(&p,now,16000,33334,33334);
    }
    assert(p.mhz==333);
    assert(pvz2_power_step_budget(&p,now+40000,26000,40000,33334)==444);
    p=(Pvz2PowerPolicy){.mhz=333};
    for(unsigned i=0;i<179;i++) {now+=33334;assert(pvz2_power_step_budget(&p,now,9000,33334,33334)==333);}
    now+=33334;assert(pvz2_power_step_budget(&p,now,9000,33334,33334)==222);
    /* Returning to 60 immediately restores performance, before another frame. */
    assert(pvz2_power_step_budget(&p,now+16667,9000,16667,16667)==444);
    p=(Pvz2PowerPolicy){.mhz=333};
    assert(pvz2_power_step(&p,now,7000,20000)==333);
    assert(pvz2_power_step(&p,now+16667,7000,16667)==333);
    assert(pvz2_power_step(&p,now+36667,7000,20000)==333);
    assert(pvz2_power_step(&p,now+56667,7000,20000)==333);
    assert(pvz2_power_step(&p,now+76667,7000,20000)==444);
    /* Shipping performance policy: light scenes cannot fall below 444;
     * supported 500 MHz resumes on the first expensive frame. */
    p=(Pvz2PowerPolicy){.mhz=500,.ceiling_mhz=500,.floor_mhz=444};
    for(unsigned i=0;i<10000;i++) { now+=33334; pvz2_power_step_budget(&p,now,8000,33334,33334); }
    assert(p.mhz==444);
    assert(pvz2_power_step_budget(&p,now+40000,26000,40000,33334)==500);
    p=(Pvz2PowerPolicy){.mhz=444,.ceiling_mhz=444,.floor_mhz=444};
    for(unsigned i=0;i<10000;i++) {now+=33334;assert(pvz2_power_step_budget(&p,now,1000,33334,33334)==444);}
    puts("PASS: shipping 444 MHz floor, accepted 500 MHz boost and stock 444 MHz ceiling");
    puts("PASS: light-scene reduction, immediate load boost, cooldown, mixed/heavy frames and long uptime");
}
''', encoding='utf-8')
exe=work/'check.exe'
subprocess.run(['gcc','-std=c11','-O2','-I'+str(root/'vita/direct/source'),str(work/'check.c'),'-o',str(exe)],check=True,timeout=30)
run=subprocess.run([str(exe)],text=True,capture_output=True,check=True,timeout=5)
(work/'result.txt').write_text(run.stdout)
print(run.stdout.strip()); print(work)
