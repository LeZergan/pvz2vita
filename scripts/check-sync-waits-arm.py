"""Run linked ARM condition wrappers with SDK primitives mocked, no game/device."""
from pathlib import Path
import argparse, struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--loader-elf', type=Path, required=True)
a = p.parse_args()
u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
u.mem_map(0x81000000, 0x800000)
u.mem_map(0x83000000, 0x20000)
with a.loader_elf.open('rb') as f:
    elf = ELFFile(f)
    for seg in elf.iter_segments():
        if seg['p_type'] == 'PT_LOAD': u.mem_write(seg['p_vaddr'], seg.data())
    syms = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
STOP, SP = 0x8301f000, 0x83018000
names = ['pthread_cond_wait', 'pthread_cond_timedwait', 'pthread_cond_signal', 'pthread_cond_broadcast']
hooks = {syms[n] & ~1: n for n in names + ['sceKernelGetThreadId', 'sceKernelGetThreadInfo', '__emutls_get_address']}
expected, result, calls = None, 0, []
def words(at, count): return struct.unpack('<'+'I'*count, u.mem_read(at, 4*count))
def hook(uc, at, size, data):
    name = hooks.get(at)
    if name is None: return
    if name == 'sceKernelGetThreadId': value = 123
    elif name in names:
        kind, args = expected
        assert name == names[kind-1]
        assert words(syms['sync_slots'],4) == (kind,args[0],args[1] if len(args)>1 else 0,STOP)
        assert [uc.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2]][:len(args)] == args
        # The native context must not erase an enclosing Android wait record.
        assert words(syms['slots'],4) == (123,1,0x99112233,0x98004567)
        calls.append(name);value=result
    else: raise AssertionError('Unexpected kernel/TLS call: '+name)
    uc.reg_write(UC_ARM_REG_R0,value)
    uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
for kind,args in [(1,[0x83001000,0x83002000]),(2,[0x83001000,0x83002000,0x83003000]),
                  (3,[0x83001000]),(4,[0x83001000])]:
    for result in [0,12,16,116,0x80028005]:
        expected=kind,args;calls.clear()
        u.mem_write(syms['slots'],struct.pack('<7I',123,1,0x99112233,0x98004567,0,0,0))
        u.mem_write(syms['sync_slots'],b'\0'*16)
        u.reg_write(UC_ARM_REG_SP,SP);u.reg_write(UC_ARM_REG_LR,STOP)
        for r,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2],args):u.reg_write(r,v)
        u.emu_start(syms['__wrap_'+names[kind-1]],STOP,count=10000)
        assert u.reg_read(UC_ARM_REG_PC)==STOP and u.reg_read(UC_ARM_REG_R0)==result
        assert words(syms['sync_slots'],1)==(0,) and calls==[names[kind-1]]
        assert words(syms['slots'],4)==(123,1,0x99112233,0x98004567)
print('PASS: 20 ARM condition wait/timedwait/signal/broadcast calls; original arguments, deadline pointer, errors and outer Android context preserved; no TLS allocation')
