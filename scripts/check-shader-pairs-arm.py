"""Run compiled cache and pinned VitaGL link/ownership routines. Mock GPU only.

No game boot, device, emulator UI or rendered-output claim. Synthetic GXP has
no parameters; compiler entry is forbidden on the cached path. The first pair
starts compiled, a second pair starts uncompiled with identical tracked GLSL.
"""
from pathlib import Path
import argparse, struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--loader-elf',type=Path,required=True)
a=p.parse_args()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
u.mem_map(0x81000000,0x1000000);u.mem_map(0x83000000,0x200000)
u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
with a.loader_elf.open('rb') as f:
    elf=ELFFile(f)
    for seg in elf.iter_segments():
        if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
    symbols={s.name:(s['st_value'],s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()}
def addr(name):return symbols[name][0]
def put(at,value):u.mem_write(at,struct.pack('<I',value))
def get(at):return struct.unpack('<I',u.mem_read(at,4))[0]
assert symbols['shaders'][1]==2048*820 and symbols['progs'][1]==1024*404
assert symbols['g_shader_diag'][1]==512*552
heap=0x83010000
allocations=[];native_links=0;finishes=0;releases=[]
def alloc(n):
    global heap
    at=heap;heap+=max(16,(n+15)&~15);assert heap<0x831e0000
    u.mem_write(at,bytes(n));return at
def string(s):
    b=s.encode()+b'\0';at=alloc(len(b));u.mem_write(at,b);return at
def ret(value=0):
    u.reg_write(UC_ARM_REG_R0,value&0xffffffff);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
names=['malloc','free','vglMalloc','vglCalloc','vgl_free','sceKernelGetSystemTimeWide',
       'sceGxmProgramGetParameterCount','sceGxmFinish','sceGxmShaderPatcherForceUnregisterProgram',
       'glsl_translator_set_process','glGetProgramiv','glLinkProgram',
       'pvz2_stall_native_wait','pvz2_stall_native_done','telemetry_log',
       'sceGxmProgramFindParameterByName','sceGxmProgramParameterGetCategory','sceGxmProgramParameterGetResourceIndex']
entries={addr(n)&~1:n for n in names}
def hook(uc,at,size,data):
    global native_links,finishes
    name=entries.get(at)
    x,y,z=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)]
    if name=='glsl_translator_set_process':raise AssertionError('Cache miss entered GLSL compiler')
    if name=='glLinkProgram':native_links+=1
    elif name in ('malloc','vglMalloc','vglCalloc'):
        n=x*y if name=='vglCalloc' else x
        out=alloc(n);allocations.append((name,n,out));ret(out)
    elif name in ('free','vgl_free'):releases.append(x);ret()
    elif name=='sceKernelGetSystemTimeWide':u.reg_write(UC_ARM_REG_R1,0);ret(1)
    elif name=='sceGxmProgramFindParameterByName':ret(0x83000000)
    elif name=='sceGxmProgramParameterGetCategory':ret(0)
    elif name=='sceGxmProgramParameterGetResourceIndex':ret(4)
    elif name=='sceGxmFinish':finishes+=1;ret()
    elif name in ('sceGxmProgramGetParameterCount','sceGxmShaderPatcherForceUnregisterProgram',
                  'pvz2_stall_native_wait','pvz2_stall_native_done','telemetry_log'):ret()
u.hook_add(UC_HOOK_CODE,hook)
STOP=0x831ff000
def call(name,*args):
    u.reg_write(UC_ARM_REG_CPSR,0x10);u.reg_write(UC_ARM_REG_SP,0x831fe000);u.reg_write(UC_ARM_REG_LR,STOP)
    for reg,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(reg,value)
    try:u.emu_start(addr(name),STOP,timeout=3000000,count=600000)
    except Exception:
        print('while calling',name,'PC',hex(u.reg_read(UC_ARM_REG_PC)));raise
    assert u.reg_read(UC_ARM_REG_PC)==STOP,(name,hex(u.reg_read(UC_ARM_REG_PC)))
    return u.reg_read(UC_ARM_REG_R0)
def shader(kind,source,compiled):
    sh=call('glCreateShader',kind);base=addr('shaders')+(sh-1)*820
    u.mem_write(base+6,b'\x01') # native is_glsl
    put(base+796,alloc(256) if compiled else 0) # GXP program
    put(base+804,0) # synthetic GXP contains no uniforms
    put(base+808,string(source) if not compiled else 0)
    diag=addr('g_shader_diag')+(sh-1)*552
    put(diag,sh);put(diag+544,string(source));put(diag+548,len(source))
    return sh
def pair(compiled):
    prog=call('glCreateProgram');v=shader(0x8b31,'vertex fixture',compiled);f=shader(0x8b30,'fragment fixture',compiled)
    call('glAttachShader',prog,v);call('glAttachShader',prog,f)
    return prog,v,f
def refs(sh):return struct.unpack('<h',u.mem_read(addr('shaders')+(sh-1)*820+788,2))[0]
assert get(addr('glsl_sema_mode'))==2
first,v,f=pair(True)
attribute_name=string('position')
call('glBindAttribLocation',first,3,attribute_name)
call('glLinkProgram_soloader',first)
assert get(addr('shader_pair_count'))==1 and refs(v)==refs(f)==2
assert call('glGetAttribLocation',first,attribute_name)==3
call('glDeleteShader',v);call('glDeleteShader',f);call('glDeleteProgram_soloader',first)
assert refs(v)==refs(f)==1 and finishes==1
second,newv,newf=pair(False)
assert second==first and newv!=v and newf!=f
call('glBindAttribLocation',second,6,attribute_name)
call('glDeleteShader',newv);call('glDeleteShader',newf)
call('glLinkProgram_soloader',second)
assert get(addr('pvz2_pair_hits'))==1 and get(addr('shader_pair_count'))==1
assert refs(v)==refs(f)==2 and refs(newv)==refs(newf)==0 and native_links==2
assert call('glGetAttribLocation',second,attribute_name)==6
out=alloc(12);call('glGetAttachedShaders',second,2,out,out+4)
assert get(out)==2 and get(out+4)==v and get(out+8)==f
call('glDeleteProgram_soloader',second)
assert refs(v)==refs(f)==1 and finishes==2
print('PASS: compiled VitaGL retains deleted shaders via holder, replaces uncompiled pair, skips translator, preserves different attribute bindings, links normally, and releases game references without an extra GPU finish')
