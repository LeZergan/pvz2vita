"""Replay the exact game's program cleanup against the compiled Vita bridge.

Only bounded ARM routines run, with GPU completion and frees mocked. No game
boot, UI, graphics context, saves or physical device. Requires Unicorn and
pyelftools, the exact locally supplied game library and unstripped loader ELF.
"""
from pathlib import Path
import argparse, hashlib, re, struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--loader-elf',required=True,type=Path)
p.add_argument('--game-lib',required=True,type=Path)
p.add_argument('--old-loader-elf',type=Path)
a=p.parse_args()
assert hashlib.sha256(a.game_lib.read_bytes()).hexdigest()=='eb96a61de9c00538b251420eb2674423eda1865b03a276b03e9cbc9d63eeee00'
root=Path(__file__).resolve().parents[1]
header=(root/'vita/direct/source/utils/glutil.h').read_text()
LIMIT=int(re.search(r'#define PVZ2_VGL_PROGRAM_LIMIT (\d+)u',header)[1])
BASE,RAM,SP,STOP=0x98000000,0x83000000,0x83018000,0x8301f000
STRIDE=404  # This pinned SDK's program structure; native boundary tested below.

class Replay:
    def __init__(self,path):
        u=self.u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
        u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000)
        u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
        for at,n in [(BASE,0x1200000),(0x81000000,0x800000),(RAM,0x20000)]:u.mem_map(at,n)
        for source,base in [(a.game_lib,BASE),(path,0)]:
            with source.open('rb') as f:
                elf=ELFFile(f)
                for seg in elf.iter_segments():
                    if seg['p_type']=='PT_LOAD':u.mem_write(base+seg['p_vaddr'],seg.data())
                if not base:
                    self.syms={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
                    self.sizes={s.name:s['st_size'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        assert self.sizes['progs']==LIMIT*STRIDE,'Update/check the pinned vitaGL program limit and ABI'
        self.progs=self.syms['progs'];self.deletes=[];self.queries=[];self.outside=[];self.finishes=0
        self.imports={}
        for table in ('default_dynlib','pvz2_gap_dynlib'):
            for off in range(0,self.sizes[table],8):
                name,fn=struct.unpack('<II',u.mem_read(self.syms[table]+off,8))
                name=bytes(u.mem_read(name,220)).split(b'\0')[0].decode()
                self.imports.setdefault(name,fn)
        # Use actual ARM/Thumb interworking branches for the game PLT calls.
        for at,name in ((BASE+0xd93c8,'glIsProgram'),(BASE+0xd920c,'glDeleteProgram')):
            u.mem_write(at,struct.pack('<II',0xe51ff004,self.imports[name]))
        for name in ('sceGxmFinish','vgl_free','telemetry_log'):
            fn=self.syms[name]
            u.mem_write(fn&~1,bytes.fromhex('00207047') if fn&1 else struct.pack('<II',0xe3a00000,0xe12fff1e))
        self.entries={self.syms[n]&~1:n for n in ('glIsProgram','glDeleteProgram','sceGxmFinish','vgl_free','telemetry_log')}
        u.hook_add(UC_HOOK_CODE,self.hook)
        u.hook_add(UC_HOOK_MEM_WRITE,self.write)
    def hook(self,u,addr,size,data):
        name=self.entries.get(addr)
        if name=='glIsProgram':self.queries.append(u.reg_read(UC_ARM_REG_R0))
        elif name=='glDeleteProgram':self.deletes.append(u.reg_read(UC_ARM_REG_R0))
        elif name in ('sceGxmFinish','vgl_free','telemetry_log'):
            if name=='sceGxmFinish':self.finishes+=1
            if name=='vgl_free':assert not u.reg_read(UC_ARM_REG_R0),'Only unlinked programs in this fixture'
    def write(self,u,access,at,n,value,data):
        # Capture the exact underflow slot targeted by glDeleteProgram(0).
        if at==self.progs-STRIDE+8:self.outside.append(at)
    def call(self,fn,value=0,end=STOP):
        self.u.reg_write(UC_ARM_REG_CPSR,0x10)
        self.u.reg_write(UC_ARM_REG_SP,SP);self.u.reg_write(UC_ARM_REG_LR,STOP)
        self.u.reg_write(UC_ARM_REG_R0,value)
        self.u.emu_start(fn,end,timeout=2000000,count=200000)
        assert self.u.reg_read(UC_ARM_REG_PC)==end,'Routine did not return within its bound'
        return self.u.reg_read(UC_ARM_REG_R0)
    def cleanup(self,handle,which=0):
        start,end,field=[(0xdb2ae4,0xdb2b08,0x1d8),(0xdb2b34,0xdb2b54,0x1e4),
                         (0xdb2b7c,0xdb2b9c,0x20c)][which]
        obj=RAM+0x1000;self.u.mem_write(obj,bytes(0x300))
        self.u.mem_write(obj+field,struct.pack('<I',handle))
        self.u.reg_write(UC_ARM_REG_R4,obj)
        self.call(BASE+start,obj,BASE+end)

if a.old_loader_elf:
    old=Replay(a.old_loader_elf)
    old.call(old.syms['glDeleteProgram'],0)
    assert old.deletes==[0] and old.outside==[old.progs-STRIDE+8]
    old.deletes.clear();old.outside.clear()
    for which in range(3):old.cleanup(0,which)
    assert old.deletes==[0,0,0],old.deletes
    print('REPRODUCED: all three RC3 game cleanup branches reach native glDeleteProgram(0); a separate direct execution confirms its write before the program table')

r=Replay(a.loader_elf)
assert r.imports['glIsProgram']==r.syms['glIsProgram_soloader']
assert r.imports['glDeleteProgram']==r.syms['glDeleteProgram_soloader']
for bad in (0,LIMIT+1,0xffffffff):
    assert r.call(r.imports['glIsProgram'],bad)==0
    for which in range(3):r.cleanup(bad,which)
    r.call(r.imports['glDeleteProgram'],bad)
assert not r.deletes and not r.queries and not r.outside

# Exercise native allocation, genuine validity, deletion, repeated cleanup and
# numeric handle reuse; GPU completion is observed but never waits on hardware.
for which in range(3):
    handle=r.call(r.syms['glCreateProgram']);assert handle==1
    assert r.call(r.imports['glIsProgram'],handle)==1
    before=len(r.deletes);r.cleanup(handle,which)
    assert len(r.deletes)==before+1 and r.deletes[-1]==handle
    assert r.call(r.imports['glIsProgram'],handle)==0
    r.cleanup(handle,which);r.call(r.imports['glDeleteProgram'],handle)
    assert len(r.deletes)==before+1
assert r.finishes==3 and not r.outside

# Native table geometry: reserve every slot except its last, then create using
# the SDK function itself. This tests the upper bound without 1024 allocations.
for slot in range(LIMIT-1):r.u.mem_write(r.progs+slot*STRIDE+8,b'\x01')
assert r.call(r.syms['glCreateProgram'])==LIMIT
assert r.call(r.imports['glIsProgram'],LIMIT)==1
r.call(r.imports['glDeleteProgram'],LIMIT)
assert r.call(r.imports['glIsProgram'],LIMIT)==0 and not r.outside
print('PASS: actual game cleanup and compiled imports reject zero/out-of-range/deleted IDs; valid deletion, handle reuse and pinned native table boundary preserved')
