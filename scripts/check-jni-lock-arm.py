"""Run compiled JNI lock under a delayed owner; no device or game boot."""
from pathlib import Path
import argparse
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser();p.add_argument('--loader-elf',required=True,type=Path);a=p.parse_args()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x81000000,0x1800000);u.mem_map(0x83000000,0x200000)
with a.loader_elf.open('rb') as f:
 e=ELFFile(f)
 for seg in e.iter_segments():
  if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
 sy={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
stop=0x831ff000;delays=[]
def hook(uc,at,size,_):
 if at in [sy['__wrap_sceKernelDelayThread']&~1,sy['sceKernelDelayThread']&~1]:
  delays.append(uc.reg_read(UC_ARM_REG_R0));assert delays[-1]==50
  if len(delays)==3:uc.mem_write(sy['g_tracked_refs_lock'],b'\0')
  uc.reg_write(UC_ARM_REG_R0,0);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
 if at==stop:uc.emu_stop()
u.hook_add(UC_HOOK_CODE,hook)
for held in [0,1]:
 delays.clear();u.mem_write(sy['g_tracked_refs_lock'],bytes([held]))
 u.reg_write(UC_ARM_REG_SP,0x831f0000);u.reg_write(UC_ARM_REG_LR,stop|1)
 for i,reg in enumerate([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2]):u.reg_write(reg,0x83010000+4*i)
 u.emu_start(sy['fjni_ref_stats'],0,count=50000)
 assert u.reg_read(UC_ARM_REG_PC)==stop
 assert len(delays)==(3 if held else 0) and u.mem_read(sy['g_tracked_refs_lock'],1)==b'\0'
print('PASS: compiled JNI uncontended path never sleeps; contended path yields 50us and completes after owner release')
