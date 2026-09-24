"""Replay exact 4.5.2 operator new through the built loader's memory bridge.

Only bounded ARM instructions and fake memory syscalls; no Vita/game session.
The proprietary input is local only and never included in outputs.
"""
from pathlib import Path
import argparse
import hashlib
import struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--loader-elf',required=True,type=Path)
p.add_argument('--game-lib',required=True,type=Path)
a=p.parse_args()
assert hashlib.sha256(a.game_lib.read_bytes()).hexdigest()=='eb96a61de9c00538b251420eb2674423eda1865b03a276b03e9cbc9d63eeee00'
BASE,RAM,SP,STOP,BLOCK=0x98000000,0x83000000,0x83010000,0x8301f000,0x85000000

def setup(recovery,available):
    uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    uc.mem_map(BASE,0x1200000);uc.mem_map(0x81000000,0x700000)
    uc.mem_map(RAM,0x20000);uc.mem_map(BLOCK,0x800000)
    symbols={}
    for path,base in [(a.game_lib,BASE),(a.loader_elf,0)]:
        with path.open('rb') as f:
            e=ELFFile(f)
            for seg in e.iter_segments():
                if seg['p_type']=='PT_LOAD':uc.mem_write(base+seg['p_vaddr'],seg.data())
            if not base:symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
    state={'threw':False,'requested':[],'allocated':[],'released':[],'native_free':0}
    hooks={v&~1:k for k,v in symbols.items() if k in {
        'malloc','free','telemetry_log','sceKernelGetFreeMemorySize',
        'sceKernelAllocMemBlock','sceKernelGetMemBlockBase','sceKernelFreeMemBlock'}}
    def ret(value=0):
        uc.reg_write(UC_ARM_REG_R0,value);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    def adapter(uc,address,size,context):
        if address==BASE+0xd890c: # Actual malloc import called by operator new.
            if recovery:uc.reg_write(UC_ARM_REG_PC,symbols['malloc_soloader'])
            else:state['requested'].append(uc.reg_read(UC_ARM_REG_R0));ret()
        elif address==BASE+0xd9944:ret() # No C++ new handler installed.
        elif address==BASE+0xd974c:ret(RAM+0x1000)
        elif address==BASE+0xd9764:
            state['threw']=True;uc.reg_write(UC_ARM_REG_PC,STOP)
        elif address in hooks:
            name=hooks[address]
            if name=='malloc':state['requested'].append(uc.reg_read(UC_ARM_REG_R0));ret()
            elif name=='free':state['native_free']+=1;ret()
            elif name=='telemetry_log':ret()
            elif name=='sceKernelGetFreeMemorySize':
                at=uc.reg_read(UC_ARM_REG_R0)
                uc.mem_write(at,struct.pack('<4I',16,available,0,0));ret()
            elif name=='sceKernelAllocMemBlock':
                state['allocated'].append(uc.reg_read(UC_ARM_REG_R2));ret(17)
            elif name=='sceKernelGetMemBlockBase':
                assert uc.reg_read(UC_ARM_REG_R0)==17
                uc.mem_write(uc.reg_read(UC_ARM_REG_R1),struct.pack('<I',BLOCK));ret()
            elif name=='sceKernelFreeMemBlock':state['released'].append(uc.reg_read(UC_ARM_REG_R0));ret()
    uc.hook_add(UC_HOOK_CODE,adapter)
    return uc,symbols,state

def call(uc,start,arg):
    uc.reg_write(UC_ARM_REG_CPSR,0x10)
    uc.reg_write(UC_ARM_REG_SP,SP);uc.reg_write(UC_ARM_REG_LR,STOP)
    uc.reg_write(UC_ARM_REG_R0,arg)
    uc.emu_start(start,STOP,timeout=2000000,count=30000)
    assert uc.reg_read(UC_ARM_REG_PC)==STOP,hex(uc.reg_read(UC_ARM_REG_PC))
    return uc.reg_read(UC_ARM_REG_R0)

for size in [0x480000,0x600000]:
    uc,syms,state=setup(False,32*1024*1024)
    call(uc,BASE+0xe5f175,size)
    assert state['threw'] and state['requested']==[size]
    uc,syms,state=setup(True,32*1024*1024)
    assert call(uc,BASE+0xe5f175,size)==BLOCK
    assert not state['threw'] and state['requested']==[size] and state['allocated']==[size]
    call(uc,syms['free_soloader'],BLOCK)
    assert state['released']==[17] and not state['native_free']
    print(f'PASS: exact game new({size}) throws on original NULL path; compiled bridge recovers and frees through the correct allocator')
print('PASS: real ARM game-to-loader ABI, fallback page allocation and ownership; syscalls are mocked, not hardware validation')
