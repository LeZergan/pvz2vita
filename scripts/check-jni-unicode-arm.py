"""Run actual linked ARM JNI string routines with guarded allocator boundaries."""
import argparse, struct, json
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--loader-elf',type=Path,required=True)
p.add_argument('--benchmark-ascii',action='store_true',help='Count compiled instructions for identical ASCII getters; not a device FPS benchmark')
a=p.parse_args()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
for base,size in [(0x81000000,0x800000),(0x83000000,0x20000),(0x85000000,0x1000000)]:u.mem_map(base,size)
with a.loader_elf.open('rb') as f:
    elf=ELFFile(f)
    for seg in elf.iter_segments():
        if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
    symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s['st_value']}
allocations={}; cursor=0x85000000
def alloc(n):
    global cursor
    assert n<0x100000
    result=cursor+32; cursor+=(n+95)&~31
    assert cursor<0x86000000
    u.mem_write(result-32,b'\xa5'*32+bytes(n)+b'\xa5'*32)
    allocations[result]=n
    return result
def free(ptr):
    if not ptr:return
    n=allocations.pop(ptr)
    assert bytes(u.mem_read(ptr-32,32))==b'\xa5'*32,'allocation underwrite'
    assert bytes(u.mem_read(ptr+n,32))==b'\xa5'*32,'allocation overwrite'
def cstring(ptr):
    out=bytearray()
    while True:
        c=u.mem_read(ptr+len(out),1)[0]
        if not c:return bytes(out)
        out.append(c)
        assert len(out)<0x10000
entries={}
for name in ['malloc','calloc','realloc','free','_fjni_log_error','_fjni_log_debug','_fjni_log_info','_fjni_log_warn']:
    if name not in symbols:continue
    addr=symbols[name]; entries[addr&~1]=name
    u.mem_write(addr&~1,b'\x70\x47' if addr&1 else struct.pack('<I',0xe12fff1e))
instructions=0
def hook(uc,addr,size,data):
    global instructions
    instructions+=1
    name=entries.get(addr)
    if not name:return
    x=uc.reg_read(UC_ARM_REG_R0); y=uc.reg_read(UC_ARM_REG_R1)
    result=0
    if name=='malloc':result=alloc(x)
    elif name=='calloc':result=alloc(x*y)
    elif name=='free':free(x)
    elif name=='realloc':
        result=alloc(y)
        if x:uc.mem_write(result,bytes(uc.mem_read(x,min(y,allocations[x]))));free(x)
    uc.reg_write(UC_ARM_REG_R0,result)
u.hook_add(UC_HOOK_CODE,hook)
u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
STOP=0x8301f000
def call(name,*args):
    u.reg_write(UC_ARM_REG_CPSR,0x10)
    u.reg_write(UC_ARM_REG_SP,0x83018000);u.reg_write(UC_ARM_REG_LR,STOP)
    for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):u.reg_write(reg,value&0xffffffff)
    for i,value in enumerate(args[4:]):u.mem_write(0x83018000+i*4,struct.pack('<I',value&0xffffffff))
    u.emu_start(symbols[name],STOP,timeout=2000000,count=200000)
    assert u.reg_read(UC_ARM_REG_PC)==STOP,f'{name} did not return'
    return u.reg_read(UC_ARM_REG_R0)

if a.benchmark_ascii:
    rows=[]
    for n in [5,64,255]:
        encoded=b'x'*n; u.mem_write(0x83004000,encoded+b'\0')
        before_constructor=instructions
        s=call('NewStringUTF',0,0x83004000); assert(s)
        constructor_instructions=instructions-before_constructor
        counts=[]
        for i in range(8):
            before=instructions
            copy=call('GetStringUTFChars',0,s,0)
            counts.append(instructions-before)
            assert cstring(copy)==encoded
            call('ReleaseStringUTFChars',0,s,copy)
        call('DeleteLocalRef',0,s); assert not allocations
        rows.append({'ascii_bytes':n,'constructor_instructions':constructor_instructions,
                     'getter_instructions':counts,
                     'construct_plus_one_get_instructions':constructor_instructions+counts[0]})
    print(json.dumps({'elf':str(a.loader_elf),'allocator':'mocked; no Vita syscall timing','samples':rows},indent=2))
    raise SystemExit(0)

cases=[('ASCII',[65,66],b'AB'),('Cyrillic',[0x41c,0x430,0x43a,0x441],'Макс'.encode()),
       ('CJK/NUL/emoji',[0x4e2d,0,0xd83c,0xdf3b],bytes.fromhex('e4b8ad c080 eda0bc edbcbb')),
       ('empty',[],b'')]
for label,units,encoded in cases:
    u.mem_write(0x83002000,struct.pack('<'+'H'*len(units),*units)+b'\x55'*32)
    for constructor in ['NewString','NewStringUTF']:
        if constructor=='NewString':s=call(constructor,0,0x83002000,len(units))
        else:
            u.mem_write(0x83004000,encoded+b'\0')
            s=call(constructor,0,0x83004000)
        assert s
        assert call('GetStringLength',0,s)==len(units),f'{label}: wrong UTF-16 length'
        assert call('GetStringUTFLength',0,s)==len(encoded),f'{label}: wrong UTF byte length'
        copy=call('GetStringUTFChars',0,s,0)
        assert cstring(copy)==encoded,f'{label}: corrupted encoded copy'
        call('ReleaseStringUTFChars',0,s,copy)
        copy=call('GetStringChars',0,s,0)
        assert bytes(u.mem_read(copy,len(units)*2))==struct.pack('<'+'H'*len(units),*units)
        call('ReleaseStringChars',0,s,copy)
        call('DeleteLocalRef',0,s)
        assert not allocations,f'{label}: leaked allocations'
print('PASS: compiled ARM JNI constructors/getters/release; ASCII, Cyrillic, CJK, NUL, surrogate pair and empty strings; guarded allocator and no leaks')
