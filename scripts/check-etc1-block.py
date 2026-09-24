"""Compare optimized block decode against frozen RC18, including malformed input."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];w=Path(tempfile.mkdtemp(prefix='etc1-block-',dir=r/'out'))
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define pvz2_etc1_block reference_block
#define etc1_clamp reference_clamp
#define k_etc1_mod reference_mod
#include "etc1-block-reference.h"
#undef PVZ2_ETC1_BLOCK_H
#undef pvz2_etc1_block
#undef etc1_clamp
#undef k_etc1_mod
#include "utils/etc1_block.h"
int main(){
 unsigned state=0x94b12;unsigned char in[10],old[66],now[66];
 for(unsigned n=0;n<200000;n++){
  for(unsigned i=0;i<10;i++){state^=state<<13;state^=state>>17;state^=state<<5;in[i]=(unsigned char)state;}
  /* Half the samples are valid differential/individual blocks. The rest
   * exercise all bit patterns, including out-of-range differential bases. */
  if((n&1)&&(in[4]&2))for(int c=1;c<=3;c++)in[c]=(16<<3)|(in[c]&7);
  memset(old,0xad,sizeof(old));memset(now,0xad,sizeof(now));
  reference_block(in+1,old+1);pvz2_etc1_block(in+1,now+1);
  assert(!memcmp(old,now,sizeof(old)));assert(now[0]==0xad&&now[65]==0xad);
 }
 puts("PASS: 200000 mixed ETC1 blocks byte-identical to RC18, both orientations/modes, clamp extremes, unaligned input/output and guard bytes");
}
'''
(w/'check.c').write_text(code)
subprocess.run(['gcc','-std=c11','-O2','-I'+str(r/'scripts/fixtures'),'-I'+str(r/'vita/direct/source'),str(w/'check.c'),'-o',str(w/'check.exe')],check=True)
subprocess.run([str(w/'check.exe')],check=True,timeout=10)
print(w)
