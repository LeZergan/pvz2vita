"""Actual ARM cache checksum vs independent zlib, tails/alignment/chunking."""
from pathlib import Path
import argparse,struct,zlib,json,random
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser();p.add_argument('--loader-elf',type=Path,required=True);a=p.parse_args()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x81000000,0x800000);u.mem_map(0x83000000,0x40000)
with a.loader_elf.open('rb') as f:
 e=ELFFile(f)
 for s in e.iter_segments():
  if s['p_type']=='PT_LOAD':u.mem_write(s['p_vaddr'],s.data())
 syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
fast=next(v for k,v in syms.items() if 'pvz2_cache_crc32' in k);slow=syms['mz_crc32']
stop=0x8303f000;data=random.Random(452).randbytes(16400);u.mem_write(0x83000000,data)
count=0
def hook(*args):
 global count
 count+=1
u.hook_add(UC_HOOK_CODE,hook)
def call(fn,crc,offset,n):
 global count
 count=0;u.reg_write(UC_ARM_REG_SP,0x8303e000);u.reg_write(UC_ARM_REG_LR,stop)
 for reg,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2],[crc,0x83000000+offset,n]):u.reg_write(reg,v)
 u.emu_start(fn,stop,count=1000000);assert u.reg_read(UC_ARM_REG_PC)==stop
 return u.reg_read(UC_ARM_REG_R0),count
tests=0
for off in range(4):
 for n in list(range(17))+[31,63,255,1023,4096,16384]:
  for seed in [0,0x12345678]:
   assert call(fast,seed,off,n)[0]==zlib.crc32(data[off:off+n],seed);tests+=1
crc=0
for at in range(0,16384,137):crc=call(fast,crc,at,min(137,16384-at))[0]
assert crc==zlib.crc32(data[:16384])
before=call(slow,0,0,16384);after=call(fast,0,0,16384);assert before[0]==after[0] and after[1]<before[1]
print(json.dumps({'cases':tests,'chunked':True,'bytes':16384,'baseline_instructions':before[1],'candidate_instructions':after[1],'scope':'Linked ARM instructions, not Vita timing'},indent=2))
