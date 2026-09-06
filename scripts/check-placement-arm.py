"""Replay only placement instructions against the built ARM bridge.

Requires locally supplied exact libPVZ2.so, the unstripped Vita ELF,
pyelftools and Unicorn. Does not boot the game or an emulated Vita, use its
saves, create graphics/audio, or control the desktop. Each call has a bounded
instruction count. The proprietary library is never copied into source.
"""
from pathlib import Path
import argparse
import hashlib
import struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--game-lib', required=True, type=Path)
p.add_argument('--loader-elf', required=True, type=Path)
a = p.parse_args()
assert hashlib.sha256(a.game_lib.read_bytes()).hexdigest() == 'eb96a61de9c00538b251420eb2674423eda1865b03a276b03e9cbc9d63eeee00'
BASE, RAM, SP, STOP = 0x98000000, 0x83000000, 0x83008000, 0x8301f000
MARK, RESUME = BASE+0x1dca90, BASE+0x1dcb20
REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
        UC_ARM_REG_R12, UC_ARM_REG_LR]

def u32(uc, at): return struct.unpack('<I', uc.mem_read(at, 4))[0]
def put(uc, at, *words): uc.mem_write(at, struct.pack('<'+'I'*len(words), *(w&0xffffffff for w in words)))

def setup():
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    # One mapping per image; thousands of single-page calls are needlessly
    # expensive in Unicorn on Windows.
    uc.mem_map(BASE, 0x1200000)
    uc.mem_map(0x81000000, 0x680000)
    symbols = {}
    for path, base in [(a.game_lib, BASE), (a.loader_elf, 0)]:
        with path.open('rb') as f:
            elf = ELFFile(f)
            for seg in elf.iter_segments():
                if seg['p_type'] != 'PT_LOAD': continue
                at = base+seg['p_vaddr']
                uc.mem_write(at, seg.data())
            if base == 0:
                symbols = {s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    uc.mem_map(RAM, 0x20000)
    uc.reg_write(UC_ARM_REG_CPSR, 0x10)
    def adapter(uc, address, size, context):
        if address == symbols['telemetry_log'] & ~1:
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif address == symbols['hook_arm'] & ~1:
            # AAPCS struct return uses r0 for the output object, r1/r2 for args.
            dest = uc.reg_read(UC_ARM_REG_R1)
            target = uc.reg_read(UC_ARM_REG_R2)
            put(uc, dest, 0xe51ff004, target)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_CODE, adapter)
    return uc, symbols

def run(uc, start, end=STOP):
    uc.emu_start(start, end, timeout=2000000, count=100000)
    assert uc.reg_read(UC_ARM_REG_PC) == end, hex(uc.reg_read(UC_ARM_REG_PC))

uc, syms = setup()
# Execute the original candidate enumeration after its RTTI/property reads.
# Only (8,3) is vacant. The 2x2 footprint cannot fit; relaxed 1x1 can.
grid, candidates = RAM+0xa000, RAM+0x9000
for width, height, expected in [(2,2,0),(1,1,1)]:
    put(uc, grid, *[0 if i==83 else 5 for i in range(90)])
    uc.reg_write(UC_ARM_REG_SP, SP)
    put(uc, SP+0xc, candidates); put(uc, SP+0x54, STOP)
    for reg, value in [(UC_ARM_REG_R6,width),(UC_ARM_REG_R4,height),
                       (UC_ARM_REG_R5,grid),(UC_ARM_REG_R7,1)]: uc.reg_write(reg,value)
    run(uc, BASE+0x1dcbf0)
    assert uc.reg_read(UC_ARM_REG_R0) == expected
    if expected: assert tuple(struct.unpack('<III',uc.mem_read(candidates,12))) == (8,3,1)
print('PASS: original ARM fallback enumerates (8,3) with minimum 1x1 after full 2x2 fails.')

def install(uc, syms, mismatch=False):
    module = RAM+0x1000
    put(uc,module+44,BASE)
    uc.reg_write(UC_ARM_REG_SP,SP)
    uc.reg_write(UC_ARM_REG_R0,module)
    uc.reg_write(UC_ARM_REG_LR,STOP)
    if mismatch: put(uc,MARK+16,0)
    run(uc,syms['placement452_install'])
    assert uc.reg_read(UC_ARM_REG_R0) == (0xffffffff if mismatch else 0)
    if not mismatch:
        assert u32(uc,MARK)==0xe51ff004
        assert u32(uc,MARK+4)==syms['placement452_marker_bridge']
        assert u32(uc,syms['placement452_resume'])==RESUME
    else: assert u32(uc,MARK)==0xe1a01009

def marker(uc, x, y, w, h, mw, mh):
    caller = SP+0x4c0
    grid = caller+0x80
    uc.mem_write(SP,bytes(0x1000))
    put(uc,grid,*([0]*90))
    put(uc,caller+0x1f8,0x85000000,0x85000004,0x85000004)
    put(uc,SP+0x470,7,11)
    put(uc,SP+0x480,mw,mh)
    put(uc,SP+0x490,w,h)
    for i,reg in enumerate(REGS): uc.reg_write(reg,0x10000+i)
    for reg,value in [(UC_ARM_REG_R8,x),(UC_ARM_REG_R9,grid),(UC_ARM_REG_R11,y),
                       (UC_ARM_REG_SP,SP)]: uc.reg_write(reg,value&0xffffffff)
    run(uc,MARK,RESUME)
    assert uc.reg_read(UC_ARM_REG_SP)==SP
    assert uc.reg_read(UC_ARM_REG_R8)==x&0xffffffff
    assert uc.reg_read(UC_ARM_REG_R11)==y&0xffffffff
    assert uc.reg_read(UC_ARM_REG_R10)==0x1000a
    assert u32(uc,SP+12)==0x10006
    assert uc.reg_read(UC_ARM_REG_R12)==7 and uc.reg_read(UC_ARM_REG_LR)==11
    # Run the native vector cleanup until operator delete, capture its argument.
    contents=bytes(uc.mem_read(grid,360))
    uc.reg_write(UC_ARM_REG_SP,caller)
    run(uc,BASE+0x1dc628,BASE+0xd8528)
    return contents,uc.reg_read(UC_ARM_REG_R0)

old, old_syms=setup()
_, free_arg=marker(old,8,3,2,2,1,1)
assert free_arg==1
print('PASS: original ARM marker/cleanup reproduces delete(0x1), matching the Vita dump.')
install(uc,syms)
for args in [(8,3,2,2,1,1),(3,3,2,2,1,1),(0,0,9,10,9,10),
             (8,9,10,10,2,2),(-2,-3,4,5,3,4),(9,10,2,2,1,1),
             (3,3,0,1,1,1),(3,3,0x7fffffff,0x7fffffff,1,1)]:
    cells,free_arg=marker(uc,*args)
    assert free_arg==0x85000000,(args,hex(free_arg))
    x,y,w,h,mw,mh=args
    expected=[]
    for col in range(9):
        for row in range(10):
            expected.append((3 if col-x<mw and row-y<mh else 1)
                            if w>0 and h>0 and x<=col<x+w and y<=row<y+h else 0)
    assert tuple(struct.unpack('<90i',cells))==tuple(expected),args
bad, bad_syms=setup(); install(bad,bad_syms,True)
print('PASS: built ARM/Thumb bridge preserves stack/live registers, clips writes, keeps delete pointer, rejects changed fingerprint.')
