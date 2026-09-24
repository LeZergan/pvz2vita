"""Check the built ARM pixel kernel and count instructions; no device/game run."""
from pathlib import Path
import argparse,random,struct,json
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser();p.add_argument('--loader-elf',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x81000000,0x1800000);u.mem_map(0x83000000,0x200000)
u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
with a.loader_elf.open('rb') as f:
 e=ELFFile(f)
 for seg in e.iter_segments():
  if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
 sy={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
stop=0x831ff000;instructions=0;copies=0
copy_addresses={sy[n]&~1 for n in ['memcpy','sceClibMemcpy','memcpy_soloader'] if n in sy}
def hook(uc,at,size,_):
 global instructions,copies
 instructions+=1
 if at in copy_addresses:
  dst,src,n=[uc.reg_read(reg) for reg in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2]]
  uc.mem_write(dst,bytes(uc.mem_read(src,n)));copies+=1;uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
 if at==stop:uc.emu_stop()
u.hook_add(UC_HOOK_CODE,hook)
mods=[[2,8,-2,-8],[5,17,-5,-17],[9,29,-9,-29],[13,42,-13,-42],[18,60,-18,-60],[24,80,-24,-80],[33,106,-33,-106],[47,183,-47,-183]]
def block(b):
 hi=int.from_bytes(b[:4],'big');pix=int.from_bytes(b[4:],'big');bases=[[],[]]
 for shift in [24,16,8]:
  if hi&2:
   v=(hi>>(shift+3))&31;d=(hi>>shift)&7;v2=v+(d-8 if d>3 else d)
   bases[0].append(v*8|(v>>2));bases[1].append(v2*8|(v2>>2))
  else:
   bases[0].append(((hi>>(shift+4))&15)*17);bases[1].append(((hi>>shift)&15)*17)
 out=bytearray(64)
 for y in range(4):
  for x in range(4):
   i=x*4+y;sub=y//2 if hi&1 else x//2;code=((pix>>(i+16))&1)*2+((pix>>i)&1)
   m=mods[(hi>>(2 if sub else 5))&7][code]
   out[(y*4+x)*4:(y*4+x+1)*4]=bytes([max(0,min(255,v+m)) for v in bases[sub]]+[255])
 return out
rows=[];rng=random.Random(32191)
for w,h in [(17,19),(64,64)]:
 for mode in range(5):
  half=mode not in [1,3];dw=w//2 if half else w;dh=h//2 if half else h
  data=rng.randbytes(((w+3)//4)*((h+3)//4)*8 if mode>=3 else w*h*(4 if mode==0 else 1))
  src=0x83000001;dst=0x83020001;job=0x83040000;n=dw*dh*4
  first=dh//3;end=dh-1;expected=bytearray([0xcd])*n
  decoded={}
  for y in range(first,end):
   for x in range(dw):
    if mode>=3:
     sx=x*(2 if half else 1);syy=y*(2 if half else 1);bx,by=sx//4,syy//4
     if (bx,by) not in decoded:
      offset=(by*((w+3)//4)+bx)*8;decoded[bx,by]=block(data[offset:offset+8])
     b=decoded[bx,by];i=((syy%4)*4+sx%4)*4
     val=[sum(b[i+o+c] for o in [0,4,16,20])//4 for c in range(4)] if half else b[i:i+4]
    elif mode==0:
     i=(y*2*w+x*2)*4;val=[sum(data[i+o+c] for o in [0,4,w*4,w*4+4])//4 for c in range(4)]
    else:
     i=y*(2 if half else 1)*w+x*(2 if half else 1)
     alpha=sum(data[i+o] for o in [0,1,w,w+1])//4 if half else data[i];val=[255,255,255,alpha]
    expected[(y*dw+x)*4:(y*dw+x+1)*4]=bytes(val)
  u.mem_write(src,data);u.mem_write(dst-1,b'\xcd'*(n+2));u.mem_write(job,struct.pack('<6I',src,dst,w,dw,dh,mode))
  u.reg_write(UC_ARM_REG_SP,0x831f0000);u.reg_write(UC_ARM_REG_LR,stop|1)
  for reg,value in [(UC_ARM_REG_R0,job),(UC_ARM_REG_R1,first),(UC_ARM_REG_R2,end)]:u.reg_write(reg,value)
  instructions=copies=0;u.emu_start(sy['pixel_rows'],0,count=2000000)
  assert u.reg_read(UC_ARM_REG_PC)==stop
  assert bytes(u.mem_read(dst-1,n+2))==b'\xcd'+expected+b'\xcd',(w,h,mode)
  rows.append({'size':f'{w}x{h}','mode':mode,'instructions':instructions,'mocked_memcpy_calls':copies})
print('PASS: built ARM pixel kernel matches independent reference for five modes, odd/full blocks, unaligned buffers and partial worker rows')
if a.output:a.output.write_text(json.dumps({'scope':'instruction counts, not hardware cycles; memcpy imports mocked','results':rows},indent=2)+'\n')
