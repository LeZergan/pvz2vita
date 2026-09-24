"""Replay compiled Vita condition destruction with injected kernel errors.

No game, emulator session, UI, or device. Requires Unicorn and pyelftools.
The optional old ELF must reproduce its global-lock leak.
"""
from pathlib import Path
import argparse, struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--loader-elf',type=Path,required=True)
p.add_argument('--old-loader-elf',type=Path)
a=p.parse_args()
RAM,SP,STOP=0x83000000,0x83008000,0x8301f000
CV,HANDLE=RAM+0x1000,RAM+0x2000
def put(u,at,*v):u.mem_write(at,struct.pack('<'+'I'*len(v),*(x&0xffffffff for x in v)))
def word(u,at):return struct.unpack('<I',u.mem_read(at,4))[0]

class Replay:
    def __init__(self,path):
        self.u=u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
        u.mem_map(0x81000000,0x800000);u.mem_map(RAM,0x20000)
        with path.open('rb') as f:
            e=ELFFile(f)
            for seg in e.iter_segments():
                if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
            self.syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
        names=('pte_osMutexLock','pte_osMutexUnlock','sem_wait','sem_post','sem_destroy',
               'pthread_mutex_trylock','pthread_mutex_unlock','pthread_mutex_destroy','free','__errno',
               'calloc','pthread_mutex_lock','pthread_join','pthread_cond_timedwait',
               'pvz2_stall_wait','pvz2_stall_wait_done',
               'pvz2_stall_sync','pvz2_stall_sync_done')
        self.hooks={self.syms[n]&~1:n for n in names if n in self.syms}
        u.hook_add(UC_HOOK_CODE,self.hook)
        self.held=set();self.freed=0;self.gate=1;self.mode='ok';self.trylock=0
        self.bridge=False;self.wait_result=0;self.allocated=0;self.oom=False
        put(u,self.syms['pte_cond_list_lock'],123)
        put(u,self.syms['pte_cond_test_init_lock'],456)
    def setup(self,mode):
        self.mode=mode;self.held=set();self.freed=0;self.gate=1;self.trylock=0
        # SDK ABI: blocked/gone/to-unblock, semqueue, semgate, mutex, next/prev.
        put(self.u,CV,1 if mode=='waiters' else 0,0,0,0x101,0x102,0x103,0,0)
        put(self.u,HANDLE,CV)
        put(self.u,self.syms['pte_cond_list_head'],CV)
        put(self.u,self.syms['pte_cond_list_tail'],CV)
    def hook(self,u,addr,size,data):
        n=self.hooks.get(addr)
        if not n:return
        x=u.reg_read(UC_ARM_REG_R0);ret=0
        if n=='pte_osMutexLock':
            assert x not in self.held,'second operation deadlocks on leaked global lock'
            self.held.add(x)
        elif n=='pte_osMutexUnlock':assert x in self.held;self.held.remove(x)
        elif n=='sem_wait':
            if self.mode=='gate-error':put(u,RAM+0x3000,4);ret=-1
            else:assert self.gate==1;self.gate=0
        elif n=='sem_post':self.gate+=1
        elif n=='pthread_mutex_trylock':
            ret=16 if self.mode=='busy' else 0
            self.trylock=not ret
        elif n=='pthread_mutex_unlock':
            if not self.bridge:assert self.trylock
            self.trylock=0
        elif n=='free':assert x in (CV,RAM+0x4000);self.freed+=1
        elif n=='__errno':ret=RAM+0x3000
        elif n=='calloc':
            assert x*u.reg_read(UC_ARM_REG_R1)==8
            if not self.oom:self.allocated+=1;ret=RAM+0x4000;put(u,ret,0,0)
        elif n in ('pthread_join','pthread_cond_timedwait'):ret=self.wait_result
        u.reg_write(UC_ARM_REG_R0,ret&0xffffffff);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    def call(self,name,*args):
        u=self.u;u.reg_write(UC_ARM_REG_SP,SP);u.reg_write(UC_ARM_REG_LR,STOP)
        for reg,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args or (HANDLE,)):
            u.reg_write(reg,v)
        u.emu_start(self.syms[name],STOP,count=100000)
        assert u.reg_read(UC_ARM_REG_PC)==STOP,'compiled destructor did not return'
        return u.reg_read(UC_ARM_REG_R0)

if a.old_loader_elf:
    old=Replay(a.old_loader_elf)
    for mode,expected in [('busy',16),('gate-error',4)]:
        old.setup(mode);assert old.call('pthread_cond_destroy')==expected
        assert old.held=={123},'old ELF does not exhibit expected leak'
    print('REPRODUCED: old compiled Vita destructor leaks its global lock on busy/error exits')

r=Replay(a.loader_elf)
for mode,expected in [('busy',16),('gate-error',4),('waiters',16),('ok',0)]:
    r.setup(mode);assert r.call('__wrap_pthread_cond_destroy')==expected
    assert not r.held and not r.trylock
    assert word(r.u,HANDLE)==(0 if mode=='ok' else CV)
    assert r.freed==(mode=='ok')
    if mode!='ok':
        if mode!='gate-error':assert r.gate==1
        # The same live object can be retried when the transient error ends.
        r.mode='ok';put(r.u,CV,0)
        assert r.call('__wrap_pthread_cond_destroy')==0 and not r.held
r.setup('ok');put(r.u,HANDLE,0xffffffff)
assert r.call('__wrap_pthread_cond_destroy')==0 and not r.held
assert word(r.u,HANDLE)==0
print('PASS: compiled fix balances both global locks, preserves busy objects, permits retry, destroys/static-cleans safely')

r=Replay(a.loader_elf);r.bridge=True
put(r.u,HANDLE,0xcccccccc,0xfeedface)
assert r.call('pthread_condattr_init_soloader')==0
assert word(r.u,HANDLE)==RAM+0x4000 and word(r.u,HANDLE+4)==0xfeedface
assert r.call('pthread_condattr_destroy_soloader')==0 and r.freed==1
r.oom=True;assert r.call('pthread_condattr_init_soloader')==12 and not word(r.u,HANDLE)
assert r.call('pthread_condattr_init_soloader',0)==22
assert r.call('pthread_condattr_destroy_soloader',0)==22
# Publish already-initialized bridge objects using the actual compiled registry.
put(r.u,HANDLE,RAM+0x4100,RAM+0x4200)
assert r.call('rememberObject',HANDLE)==1 and r.call('rememberObject',HANDLE+4)==1
put(r.u,RAM+0x5000,1788800001,123456000)
for native,android in [(0,0),(116,110),(45,35),(134,95),(22,22)]:
    r.wait_result=native
    assert r.call('pthread_cond_timedwait_soloader',HANDLE,HANDLE+4,RAM+0x5000)==android
    assert r.call('pthread_join_soloader',123,0)==android
print('PASS: compiled condition attributes initialize/free without overwrite, handle OOM/null, and waits return Android error codes')

# Follow the actual Android bridge call through the link wrapper as shipped.
r=Replay(a.loader_elf);r.bridge=True;r.setup('busy')
put(r.u,HANDLE,RAM+0x4000);put(r.u,RAM+0x4000,CV)
assert r.call('rememberObject',HANDLE)==1
assert r.call('pthread_cond_destroy_soloader',HANDLE)==16 and not r.held
assert r.call('isObjectInitialized',HANDLE)==1
r.mode='ok'
assert r.call('pthread_cond_destroy_soloader',HANDLE)==0 and not r.held
assert r.call('isObjectInitialized',HANDLE)==0 and not word(r.u,HANDLE)
assert r.freed==2
print('PASS: shipped Android bridge reaches corrected SDK destructor and removes its registry entry only after success')
