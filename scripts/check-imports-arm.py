"""Audit the built import table and execute the actual Vita math routines.
No game boot, UI or device. Requires pyelftools and Unicorn.
"""
from pathlib import Path
import argparse,math,struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--loader-elf',type=Path,required=True)
p.add_argument('--game-lib',type=Path,required=True)
p.add_argument('--old-loader-elf',type=Path)
a=p.parse_args()
RAM,SP,STOP=0x83000000,0x83010000,0x8301f000
def words(x):return struct.unpack('<II',struct.pack('<d',x))
def double(lo,hi):return struct.unpack('<d',struct.pack('<II',lo,hi))[0]
class Replay:
    def __init__(self,path):
        self.u=u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
        u.mem_map(0x81000000,0x800000);u.mem_map(RAM,0x20000)
        with path.open('rb') as f:
            e=ELFFile(f);self.syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
            self.sizes={s.name:s['st_size'] for s in e.get_section_by_name('.symtab').iter_symbols()}
            for seg in e.iter_segments():
                if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
        u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
        self.hooks={self.syms[n]&~1:n for n in ['__errno','__getreent'] if n in self.syms}
        u.hook_add(UC_HOOK_CODE,self.hook)
        self.imports={}
        for table in ['default_dynlib','pvz2_gap_dynlib']:
            for off in range(0,self.sizes[table],8):
                name,fn=struct.unpack('<II',u.mem_read(self.syms[table]+off,8))
                text=bytes(u.mem_read(name,220)).split(b'\0')[0].decode()
                self.imports.setdefault(text,fn)
    def hook(self,u,addr,size,data):
        n=self.hooks.get(addr)
        if not n:return
        ret=RAM+0xf000 if n=='__errno' else struct.unpack('<I',u.mem_read(self.syms['_impure_ptr'],4))[0]
        u.reg_write(UC_ARM_REG_R0,ret);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    def call(self,fn,*args):
        u=self.u;u.reg_write(UC_ARM_REG_SP,SP);u.reg_write(UC_ARM_REG_LR,STOP)
        for reg,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(reg,v&0xffffffff)
        if len(args)>4:u.mem_write(SP,struct.pack('<'+'I'*(len(args)-4),*args[4:]))
        u.emu_start(fn,STOP,count=1000000)
        assert u.reg_read(UC_ARM_REG_PC)==STOP,'routine failed to return'
        return u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R1)
    def number(self,name,*args):return double(*self.call(self.imports[name],*(w for x in args for w in words(x))))

with a.game_lib.open('rb') as f:
    e=ELFFile(f);required={s.name for s in e.get_section_by_name('.dynsym').iter_symbols()
        if s.name and s['st_shndx']=='SHN_UNDEF' and s['st_info']['bind']!='STB_WEAK'}
r=Replay(a.loader_elf)
assert not required-r.imports.keys(),sorted(required-r.imports.keys())
assert all(r.imports[n] for n in required)
print(f'PASS: all {len(required)} required game imports have non-null entries in the actual linked table')
if a.old_loader_elf:
    old=Replay(a.old_loader_elf);missing=required-old.imports.keys()
    assert len(missing)==32
    assert double(*old.call(old.syms['__ret0'],*words(8.0)))!=2.0
    print('REPRODUCED: RC2 has 32 missing imports; its integer-zero stub returns the wrong double for cbrt(8)')

cases={'acosh':[1,1.5,10],'asinh':[-10,0,0.5],'atanh':[-0.5,0,0.8],
       'cbrt':[-27,8,125],'cosh':[-2,0,3],'erf':[-1,0,2],'erfc':[-1,0,2],
       'expm1':[-1,1e-8,2],'lgamma':[0.5,3,10],'log1p':[-0.5,1e-9,3],
       'logb':[0.125,1,1024],'nearbyint':[-2.5,2.5,3.5],'tgamma':[0.5,3,6]}
checks=0
for name,vals in cases.items():
    for x in vals:
        expected=(math.frexp(abs(x))[1]-1 if name=='logb' else round(x) if name=='nearbyint'
                  else math.gamma(x) if name=='tgamma' else getattr(math,name)(x))
        actual=r.number(name,x)
        assert math.isclose(actual,expected,rel_tol=2e-12,abs_tol=1e-14),(name,x,actual,expected)
        checks+=1
for name,vals in [('hypot',[(3,4),(1e150,1e150)]),('remainder',[(29,3),(-29,3)]),
                  ('nextafter',[(1,2),(1,0),(-1,-2)])]:
    for x,y in vals:
        actual=r.number(name,x,y);expected=getattr(math,name)(x,y)
        assert math.isclose(actual,expected,rel_tol=2e-15,abs_tol=0),(name,actual,expected)
        if name=='nextafter':assert actual==expected
        checks+=1
for x,y,expected in [(1,2,0x3f800001),(1,0,0x3f7fffff),(0,1,1)]:
    f=lambda v:struct.unpack('<I',struct.pack('<f',v))[0]
    assert r.call(r.imports['nextafterf'],f(x),f(y))[0]==expected;checks+=1
for x in [2.5,3.5,-3.5,2**40+0.5]:
    lo,hi=r.call(r.imports['llrint'],*words(x));result=(hi<<32)|lo
    if result>>63:result-=1<<64
    assert result==round(x);checks+=1
for x,y in [(29,3),(-29,3),(29,-3)]:
    actual=double(*r.call(r.imports['remquo'],*words(x),*words(y),RAM+0x8000))
    q=struct.unpack('<i',r.u.mem_read(RAM+0x8000,4))[0]
    assert actual==math.remainder(x,y) and abs(q)&7==abs(round(x/y))&7
    assert (q<0)==(x/y<0);checks+=1
for x,n in [(0.75,10),(3,-5)]:
    assert double(*r.call(r.imports['scalbnl'],*words(x),n))==math.ldexp(x,n);checks+=1
for flush in [0,1<<24]:
    r.u.reg_write(UC_ARM_REG_FPSCR,flush)
    for bits,category in [(0,16),(1<<63,16),(1,8),(0x8000000000000001,8),
        (0x3ff0000000000000,4),(0x7ff0000000000000,1),(0x7ff8000000000000,2)]:
        w=(bits&0xffffffff,bits>>32)
        assert r.call(r.imports['__fpclassifyd'],*w)[0]==category
        assert r.call(r.imports['__isfinite'],*w)[0]==(category not in [1,2])
        assert r.call(r.imports['isnan'],*w)[0]==(category==2)
        assert r.call(r.imports['__signbit'],*w)[0]==bits>>63
        checks+=4
for bits in [0,1<<31,0x3f800000,0xbf800000,0xff800001]:
    assert r.call(r.imports['__signbitf'],bits)[0]==bits>>31;checks+=1
print(f'PASS: {checks} compiled softfp math checks, including double/64-bit returns, output pointers, Android classes and signed/subnormal values')
