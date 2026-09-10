"""Execute linked native wait wrappers, including 64-bit positioned-read ABI.

Only isolated ARM functions with kernel calls mocked. No device or game boot.
"""
from pathlib import Path
import argparse, struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--loader-elf', type=Path, required=True)
p.add_argument('--stress-read-kib', type=int, default=0)
p.add_argument('--stress-read-latency-us', type=int, default=0)
a = p.parse_args()
u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
u.mem_map(0x81000000, 0x800000)
u.mem_map(0x83000000, 0x20000)
with a.loader_elf.open('rb') as f:
    elf = ELFFile(f)
    for seg in elf.iter_segments():
        if seg['p_type'] == 'PT_LOAD': u.mem_write(seg['p_vaddr'], seg.data())
    syms = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
SP, STOP, TIMEOUT = 0x83010000, 0x8301f000, 0x83001000
real = ['sceKernelWaitSema', 'sceKernelLockMutex', 'sceGxmFinish', 'sceIoRead', 'sceIoPread']
hooks = {syms[n] & ~1: n for n in real + ['pvz2_stall_native_wait', 'pvz2_stall_native_done', 'sceKernelDelayThread']}
events = []
result = 0
def word(at): return struct.unpack('<I', u.mem_read(at, 4))[0]
def hook(u, at, size, ctx):
    name = hooks.get(at)
    if name is None: return
    regs = [u.reg_read(r) for r in [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]]
    if name == 'pvz2_stall_native_wait': events.append(('enter', *regs[:3])); ret = 17
    elif name == 'pvz2_stall_native_done':
        assert regs[0] == 17, 'wrapper must retain and return the observer slot token'
        events.append(('done',)); ret = 0
    elif name == 'sceKernelDelayThread': events.append(('delay', regs[0])); ret = 0
    else:
        offset = bytes(u.mem_read(u.reg_read(UC_ARM_REG_SP), 8)) if name == 'sceIoPread' else None
        events.append((name, regs, offset))
        if name in real[:2] and regs[2]: u.mem_write(regs[2], struct.pack('<I', 123))
        ret = result
    u.reg_write(UC_ARM_REG_R0, ret & 0xffffffff)
    u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE, hook)
def call(name, args, offset=None):
    events.clear()
    u.reg_write(UC_ARM_REG_SP, SP); u.reg_write(UC_ARM_REG_LR, STOP)
    for r, value in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3], args):
        u.reg_write(r, value)
    if offset is not None: u.mem_write(SP, struct.pack('<q', offset))
    u.emu_start(syms[name], STOP, count=100000)
    assert u.reg_read(UC_ARM_REG_PC) == STOP
    return u.reg_read(UC_ARM_REG_R0)
for n, kind, args in [('sceKernelWaitSema',1,[77,2,TIMEOUT]),
                      ('sceKernelLockMutex',2,[78,1,TIMEOUT]),
                      ('sceGxmFinish',3,[0x83002000]),
                      ('sceIoRead',4,[79,0x83003000,4096]),
                      ('sceIoPread',5,[80,0x83003000,4096])]:
    for result in [0, 17, 0x80028005]:
        off = 0x123456789 if n == 'sceIoPread' else None
        value = call('__wrap_' + n, args, off)
        if n != 'sceGxmFinish': assert value == result
        assert events[0] == ('enter',kind,args[0],STOP)
        assert events[1][0] == n and events[1][1][:len(args)] == args
        delays = [e[1] for e in events if e[0] == 'delay']
        expected = 0
        if n in ['sceIoRead', 'sceIoPread'] and 0 < result < 0x80000000:
            expected = a.stress_read_latency_us
            if a.stress_read_kib: expected += result * 1000000 // (a.stress_read_kib * 1024)
        assert sum(delays) == expected and all(0 < us <= 1000000 for us in delays)
        assert events[-1] == ('done',) and len(events) == 3 + len(delays)
        if n in real[:2]: assert word(TIMEOUT) == 123
        if off is not None: assert struct.unpack('<q',events[1][2])[0] == off
# Follow a real SDK primitive through the linked wrapper, not just its symbol.
result = 0
assert call('pte_osSemaphorePend', [77,0]) == 0
assert events[0][:3] == ('enter',1,77)
assert events[1][0] == 'sceKernelWaitSema' and events[1][1][:3] == [77,1,0]
assert events[-1] == ('done',)
# Large successful transfers exercise delay chunking while preserving the return.
result = 5 * 1024 * 1024
assert call('__wrap_sceIoPread', [80, 0x83003000, result], 0x123456789) == result
expected = a.stress_read_latency_us + (result * 1000000 // (a.stress_read_kib * 1024) if a.stress_read_kib else 0)
delays = [e[1] for e in events if e[0] == 'delay']
assert sum(delays) == expected and all(0 < us <= 1000000 for us in delays)
print('PASS: linked SDK wait interception; 15 ARM success/error cases; timeout pointer and 64-bit pread offset preserved')
print('PASS: read-stress delays match actual returned bytes; EOF/errors do not sleep; large reads use bounded chunks')
