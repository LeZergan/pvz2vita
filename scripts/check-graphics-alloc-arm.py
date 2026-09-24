"""Exercise the actual native draw allocation/copy callsite under forced OOM.
Only bounded ARM instructions are executed; no Vita or game boot.
"""
from pathlib import Path
import argparse, struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser();p.add_argument('--loader-elf',type=Path,required=True);a=p.parse_args()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x81000000,0x1800000);u.mem_map(0x83000000,0x200000)
with a.loader_elf.open('rb') as f:
    e=ELFFile(f)
    for seg in e.iter_segments():
        if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
    sy={s.name:(s['st_value'],s['st_size']) for s in e.get_section_by_name('.symtab').iter_symbols()}
def addr(n):return sy[n][0]
ranges={'glDrawArrays':0x206,'glDrawElements':0x10e8,
 '_glDrawArrays_CustomShadersIMPL':0x10ac,'_glDrawElements_CustomShadersIMPL':0x1546,
 'glDrawArraysInstanced':0x270,'glDrawElementsInstanced':0xaf0}
for n,size in ranges.items():assert sy[n][1]==size,(n,sy[n])
STOP=0x831ff000;PTR=0x83010000
copy=None;exited=None;failure=True;seen=[]
def ret(v=0):
    u.reg_write(UC_ARM_REG_R0,v);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
def hook(uc,at,size,_):
    global copy,exited
    if at==addr('gpu_alloc_mapped_aligned')&~1:
        args=tuple(u.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2])
        seen.append(args);ret(0 if failure else PTR)
    elif at==addr('sceClibPrintf')&~1:ret()
    elif at==addr('sceKernelExitProcess')&~1:
        exited=u.reg_read(UC_ARM_REG_R0);u.emu_stop()
    elif at==addr('sceClibMemcpy')&~1:
        copy=u.reg_read(UC_ARM_REG_R0);u.emu_stop()
    elif at==STOP:u.emu_stop()
u.hook_add(UC_HOOK_CODE,hook)
def run(pc,lr=STOP|1):
    global copy,exited,seen
    copy=exited=None;seen=[]
    u.reg_write(UC_ARM_REG_SP,0x831f0000);u.reg_write(UC_ARM_REG_LR,lr)
    for r,v in [(UC_ARM_REG_R0,16),(UC_ARM_REG_R1,128),(UC_ARM_REG_R2,0),
                (UC_ARM_REG_R4,128),(UC_ARM_REG_R5,4),(UC_ARM_REG_R9,124)]:u.reg_write(r,v)
    u.emu_start(pc|1,STOP,count=10000)
    assert seen==[(16,128,0)],seen
# Exact native glDrawArrays instruction that calls the allocator before memcpy.
site=(addr('glDrawArrays')&~1)+0x18c
run(site)
guarded='__wrap_gpu_alloc_mapped_aligned' in sy
if guarded:
    assert exited==7 and copy is None,(exited,copy)
    print('PASS: native draw OOM exits before NULL reaches memcpy')
else:
    assert copy==0 and exited is None,(copy,exited)
    print('REPRODUCED: original native draw passes NULL allocation to memcpy')
failure=False;run(site);assert copy==PTR and exited is None
if guarded:
    failure=True
    run(addr('__wrap_gpu_alloc_mapped_aligned'))
    assert exited is None and u.reg_read(UC_ARM_REG_R0)==0
    for n in ranges:
        run(addr('__wrap_gpu_alloc_mapped_aligned'),(addr(n)&~1)+8|1)
        assert exited==7 and copy is None,n
    print('PASS: six pinned draw ranges, successful allocation ABI and recoverable non-draw OOM')
