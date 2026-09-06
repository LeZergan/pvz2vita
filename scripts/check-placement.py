"""Bounded placement regression checks; no game, emulator or desktop UI."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
(ROOT / 'out').mkdir(exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='placement-', dir=ROOT / 'out'))
code = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "utils/placement_452.h"
struct Frame { int before[4], grid[90], after[24]; };
static void init(struct Frame *p) {
 for (int i=0;i<4;++i) p->before[i]=0x12345678;
 for (int i=0;i<90;++i) p->grid[i]=i%6;
 for (int i=0;i<24;++i) p->after[i]=0x12345678;
}
static void guards(struct Frame *p) {
 for (int i=0;i<4;++i) assert(p->before[i]==0x12345678);
 for (int i=0;i<24;++i) assert(p->after[i]==0x12345678);
}
int main(void) {
 struct Frame a,b;
 /* The native fallback can fit minimum=1x1 at (8,3), then mark full=2x2.
    Its last write is grid[94], exactly the caller's vector begin pointer. */
 int old_frame[114]={0};
 old_frame[94]=0x100000;
 for(int x=8;x<10;++x) for(int y=3;y<5;++y)
   old_frame[x*10+y]=(x==8 && y==3)?3:1;
 assert(old_frame[94]==1);
 init(&a); assert(placement452_mark_cells(a.grid,8,3,2,2,1,1));
 guards(&a); assert(a.grid[83]==5 && a.grid[84]==1);
 unsigned cases=0;
 for(int x=-3;x<=12;++x) for(int y=-3;y<=13;++y)
 for(int w=-1;w<=12;++w) for(int h=-1;h<=13;++h) {
  init(&a); b=a;
  /* Independent reference: evaluate each of the 90 legal cells. */
  for(int col=0;col<9;++col) for(int row=0;row<10;++row) {
   if(w>0 && h>0 && col>=x && row>=y && col-x<w && row-y<h) {
    int v=(col-x<2 && row-y<3)?3:1;
    if(b.grid[col*10+row]<v) b.grid[col*10+row]=v;
   }
  }
  placement452_mark_cells(a.grid,x,y,w,h,2,3);
  assert(memcmp(&a,&b,sizeof(a))==0); guards(&a); ++cases;
 }
 const int extreme[]={INT_MIN,-1,0,1,INT_MAX};
 for(int x=0;x<5;++x) for(int y=0;y<5;++y)
 for(int w=0;w<5;++w) for(int h=0;h<5;++h) {
  init(&a); placement452_mark_cells(a.grid,extreme[x],extreme[y],
    extreme[w],extreme[h],INT_MAX,INT_MIN); guards(&a); ++cases;
 }
 printf("PASS: reproduced original grid[94]=1 pointer corruption; "
        "production marker preserves guards and cell rules (%u cases).\n",cases);
}
'''
source = work / 'placement.c'
source.write_text(code)
exe = work / 'placement.exe'
subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I'+str(ROOT/'vita/direct/source'), str(source), '-o', str(exe)],
               check=True, timeout=30)
result = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=15)
(work/'result.txt').write_text(result.stdout)
print(result.stdout.strip())
print(work)
