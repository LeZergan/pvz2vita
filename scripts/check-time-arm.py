"""Replay the faulting game instructions and compiled Vita time bridge only.

No game/Vita boot, graphics, audio, UI or saves. RTC syscalls are injected;
all calendar code, the Android import bridge and optional old newlib failure
path execute their real ARM instructions. Needs Unicorn and pyelftools.
"""
from pathlib import Path
import argparse, datetime as D, hashlib, struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--game-lib',required=True,type=Path)
p.add_argument('--loader-elf',required=True,type=Path)
p.add_argument('--old-loader-elf',type=Path)
a=p.parse_args()
assert hashlib.sha256(a.game_lib.read_bytes()).hexdigest()=='eb96a61de9c00538b251420eb2674423eda1865b03a276b03e9cbc9d63eeee00'
BASE,RAM,SP,STOP=0x98000000,0x83000000,0x83008000,0x8301f000
EPOCH=62135596800000000
epoch=D.datetime(1970,1,1)
def put(uc,at,*words):uc.mem_write(at,struct.pack('<'+'I'*len(words),*(w&0xffffffff for w in words)))
def word(uc,at):return struct.unpack('<I',uc.mem_read(at,4))[0]
def cstr(uc,at):
    out=b''
    for i in range(4096):
        c=bytes(uc.mem_read(at+i,1))
        if c==b'\0':return out
        out+=c
    raise AssertionError('unterminated string')

class Replay:
    def __init__(self,path):
        self.u=uc=Uc(UC_ARCH_ARM,UC_MODE_ARM);self.zone=-14400;self.fail=0;self.tls={};self.next_tls=RAM+0x10000
        self.heap=RAM+0x18000;self.allocations={};self.alloc_fail=False;self.free_bytes=128<<30;self.storage_fail=False
        uc.mem_map(BASE,0x1200000);uc.mem_map(0x81000000,0x680000);uc.mem_map(RAM,0x20000)
        for file,base in ((a.game_lib,BASE),(path,0)):
            with file.open('rb') as stream:
                elf=ELFFile(stream)
                for seg in elf.iter_segments():
                    if seg['p_type']=='PT_LOAD':uc.mem_write(base+seg['p_vaddr'],seg.data())
                if not base:
                    symbols=list(elf.get_section_by_name('.symtab').iter_symbols())
                    self.syms={s.name:s['st_value'] for s in symbols}
                    self.sizes={s.name:s['st_size'] for s in symbols}
        self.imports={}
        for table in ('default_dynlib','pvz2_gap_dynlib'):
            for off in range(0,self.sizes[table],8):
                name,fn=struct.unpack('<II',uc.mem_read(self.syms[table]+off,8))
                self.imports.setdefault(cstr(uc,name).decode(),fn)
        self.at={self.syms[n]&~1:n for n in ('sceRtcGetCurrentTick','sceRtcConvertUtcToLocalTime',
            'sceRtcSetTime_t','sceRtcGetTime_t','sceRtcSetTick','sceRtcGetTick','__errno','__getreent',
            '__vita_sce_errno_to_errno','__emutls_get_address','_log_print','pthread_mutex_lock','pthread_mutex_unlock',
            'malloc','free','sceIoDevctl') if n in self.syms}
        uc.reg_write(UC_ARM_REG_CPSR,0x10)
        uc.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);uc.reg_write(UC_ARM_REG_FPEXC,0x40000000)
        uc.hook_add(UC_HOOK_CODE,self.hook)
    def hook(self,u,addr,size,context):
        name=self.at.get(addr)
        if not name:return
        r0=u.reg_read(UC_ARM_REG_R0);r1=u.reg_read(UC_ARM_REG_R1);result=0
        def date(at):
            vals=struct.unpack('<6HI',u.mem_read(at,16));return D.datetime(*vals[:6],vals[6])
        def dateput(at,d):u.mem_write(at,struct.pack('<6HI',d.year,d.month,d.day,d.hour,d.minute,d.second,d.microsecond))
        def tick(at):return struct.unpack('<Q',u.mem_read(at,8))[0]
        if name=='sceRtcGetCurrentTick':
            if self.fail==1:result=-1
            else:u.mem_write(r0,struct.pack('<Q',EPOCH+1788800000*1000000))
        elif name=='sceRtcConvertUtcToLocalTime':
            if self.fail==2:result=-1
            else:u.mem_write(r1,struct.pack('<Q',tick(r0)+self.zone*1000000))
        elif name=='sceRtcSetTime_t':dateput(r0,epoch+D.timedelta(seconds=r1))
        elif name=='sceRtcGetTick':u.mem_write(r1,struct.pack('<Q',EPOCH+int((date(r0)-epoch).total_seconds()*1000000)))
        elif name=='sceRtcSetTick':dateput(r0,epoch+D.timedelta(microseconds=tick(r1)-EPOCH))
        elif name=='sceRtcGetTime_t':
            seconds=int((date(r0)-epoch).total_seconds())
            if seconds<0 or seconds>0xffffffff:result=-1
            else:put(u,r1,seconds)
        elif name=='__errno':result=RAM+0x1f100
        elif name=='__vita_sce_errno_to_errno':result=84
        elif name=='__getreent':result=word(u,self.syms['_impure_ptr'])
        elif name=='malloc':
            if self.alloc_fail:result=0
            else:
                result=self.heap;self.heap+=(r0+15)&~15;assert self.heap<RAM+0x1e000
                self.allocations[result]=r0
        elif name=='free':
            if r0:assert self.allocations.pop(r0,None) is not None
        elif name=='sceIoDevctl':
            assert cstr(u,r0)==b'ux0:' and r1==0x3001
            if self.storage_fail:result=-1
            else:
                sp=u.reg_read(UC_ARM_REG_SP);out=word(u,sp)
                assert word(u,sp+4)==24
                u.mem_write(out,struct.pack('<qqII',256<<30,self.free_bytes,32768,0))
        elif name=='__emutls_get_address':
            if r0 not in self.tls:
                size=word(u,r0);template=word(u,r0+12)
                result=self.next_tls;self.next_tls+=(size+15)&~15
                assert self.next_tls<RAM+0x18000
                self.tls[r0]=result
                if template:u.mem_write(result,bytes(u.mem_read(template,size)))
            result=self.tls[r0]
        u.reg_write(UC_ARM_REG_R0,result&0xffffffff);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    def run(self,start,end=STOP):
        self.u.emu_start(start,end,timeout=2000000,count=200000)
        assert self.u.reg_read(UC_ARM_REG_PC)==end,hex(self.u.reg_read(UC_ARM_REG_PC))
    def call(self,name,*args):
        u=self.u;u.reg_write(UC_ARM_REG_SP,SP);u.reg_write(UC_ARM_REG_LR,STOP)
        for reg,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(reg,v&0xffffffff)
        self.run(self.syms[name]);return u.reg_read(UC_ARM_REG_R0)
    def native_fault_site(self,target,seconds=6):
        u=self.u
        assert self.imports['localtime']==self.syms[target], 'actual localtime import differs from tested function'
        put(u,BASE+0xd87d4,0xe51ff004,self.imports['localtime']) # actual linked import
        u.reg_write(UC_ARM_REG_SP,SP);u.reg_write(UC_ARM_REG_R4,seconds)
        self.run(BASE+0x81e9d4,BASE+0x81e9e8)
        return u.reg_read(UC_ARM_REG_R0)

# September 9 batch: three UTC-7 cores (57, 6, 6 seconds) and two
# UTC-3 cores (6, 6). RTC structures survive at the stopped SP minus 32.
# Include the earlier UTC-4 report as well; duplicates need one replay.
crash_cases=((-14400,6),(-25200,57),(-25200,6),(-10800,6))
if a.old_loader_elf:
    old=Replay(a.old_loader_elf)
    for zone,seconds in crash_cases:
        old.zone=zone
        try:old.native_fault_site('localtime',seconds);raise AssertionError('Expected old NULL dereference')
        except UcError:
            assert old.u.reg_read(UC_ARM_REG_PC)==BASE+0x81e9e0
            assert old.u.reg_read(UC_ARM_REG_R0)==0
        print(f'PASS: old linked import reproduces NULL read at libPVZ2+0x81e9e0: offset={zone}, timestamp={seconds}.')
    old.u.mem_write(RAM+0x600,b'\xa5'*160);old.u.mem_write(RAM+0x300,b'ux0:data/pvz2\0')
    old.call('statfs_soloader',RAM+0x300,RAM+0x600)
    assert bytes(old.u.mem_read(RAM+0x658,40))==bytes(40)
    print('PASS: old ARM storage query reproduces 40-byte overwrite beyond Android statfs.')

r=Replay(a.loader_elf);u=r.u
for zone,seconds in crash_cases:
    r.zone=zone
    assert r.native_fault_site('bionic_localtime',seconds)==(zone+seconds)&0xffffffff
    print(f'PASS: patched linked import runs past the exact crash site: offset={zone}, timestamp={seconds}.')
for zone in (-43200,-25200,-14400,-12600,-10800,0,10800,19800,20700,45900,50400):
    r.zone=zone
    for seconds in (-2147483648,-1,0,6,57,951782400,2147483647):
        put(u,RAM,seconds,0xdeadbeef);u.mem_write(RAM+0x100,b'\xa5'*76)
        assert r.call('bionic_localtime_r',RAM,RAM+0x110)==RAM+0x110
        values=struct.unpack('<11I',u.mem_read(RAM+0x110,44))
        expected=epoch+D.timedelta(seconds=seconds+zone)
        assert values[:6]==(expected.second,expected.minute,expected.hour,expected.day,expected.month-1,expected.year-1900)
        assert values[9]==zone&0xffffffff and cstr(u,values[10]).startswith(b'UTC')
        assert bytes(u.mem_read(RAM+0x100,16))==b'\xa5'*16
        assert bytes(u.mem_read(RAM+0x13c,16))==b'\xa5'*16
        assert r.call('bionic_mktime',RAM+0x110)==seconds&0xffffffff
print('PASS: compiled ARM tm is exactly 44 bytes; 4-byte time_t input; signed epoch/2038 boundaries and 77 timezone round trips.')
r.zone=-14400;put(u,RAM,6)
r.call('bionic_localtime_r',RAM,RAM+0x110)
u.mem_write(RAM+0x300,b'%Y-%m-%d %H:%M:%S %z %Z %s\0')
assert r.call('bionic_strftime',RAM+0x500,128,RAM+0x300,RAM+0x110)
assert cstr(u,RAM+0x500)==b'1969-12-31 20:00:06 -0400 UTC-04:00 6'
u.mem_write(RAM+0x300,b'%Y-%m-%d %H:%M:%S\0');u.mem_write(RAM+0x500,b'2024-02-29 13:14:15 suffix\0')
assert r.call('bionic_strptime',RAM+0x500,RAM+0x300,RAM+0x110)==RAM+0x513
assert struct.unpack('<6i',u.mem_read(RAM+0x110,24))==(15,14,13,29,1,124)
for fmt,value,offset in [(b'%s',b'6',-14400),
    (b'%Y-%m-%d %H:%M:%S %z',b'1969-12-31 20:00:06 -0400',-14400),
    (b'%Y-%m-%d %H:%M:%S %z',b'2024-02-29 13:14:15 +05:45',20700),
    (b'%Y-%m-%d %H:%M:%S %Z',b'2024-02-29 13:14:15 UTC-04:00',-14400)]:
    u.mem_write(RAM+0x300,fmt+b'\0');u.mem_write(RAM+0x500,value+b'\0')
    assert r.call('bionic_strptime',RAM+0x500,RAM+0x300,RAM+0x110)==RAM+0x500+len(value)
    assert word(u,RAM+0x134)==offset&0xffffffff
    assert not r.allocations
u.mem_write(RAM+0x300,b'%s\0');u.mem_write(RAM+0x500,b'6\0');before=bytes(u.mem_read(RAM+0x110,44))
r.alloc_fail=True
assert not r.call('bionic_strptime',RAM+0x500,RAM+0x300,RAM+0x110)
assert bytes(u.mem_read(RAM+0x110,44))==before
r.alloc_fail=False
for failure in (1,2):
    r.fail=failure
    assert r.native_fault_site('bionic_localtime')==(-14394)&0xffffffff
print('PASS: actual linked ARM strftime/strptime, zone formatting, and both RTC failure paths.')
for free,failed in [(128<<30,False),(0,False),(0,True)]:
    r.free_bytes=free;r.storage_fail=failed
    u.mem_write(RAM+0x300,b'ux0:data/pvz2\0');u.mem_write(RAM+0x600,b'\xa5'*160)
    result=r.call('statfs_soloader',RAM+0x300,RAM+0x600)
    assert result==(0xffffffff if failed else 0)
    assert bytes(u.mem_read(RAM+0x658,72))==b'\xa5'*72
    if failed:assert bytes(u.mem_read(RAM+0x600,88))==b'\xa5'*88
    else:assert struct.unpack('<Q',u.mem_read(RAM+0x618,8))[0]*4096==free
print('PASS: compiled ARM storage query preserves caller canaries, including full and unavailable storage.')
