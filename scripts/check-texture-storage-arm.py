"""Check compiled texture storage/update agreement with GPU queries and allocation mocked.
No game boot, device, graphics output or FPS measurement.
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
assert symbols['texture_slots'][1]==8192*112
heap=0x83010000
texture_format=0
def alloc(n):
    global heap
    at=heap;heap+=max(16,(n+15)&~15);assert heap<0x831e0000
    u.mem_write(at,bytes(n));return at
def ret(value=0):
    u.reg_write(UC_ARM_REG_R0,value&0xffffffff);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
names=['gpu_alloc_texture','sceGxmTextureGetFormat','sceGxmTextureGetWidth','sceGxmTextureGetHeight',
       'sceGxmTextureGetData','vgl_memalign','sceClibMemcpy','sceClibMemset','sceKernelLockMutex',
       'sceKernelUnlockMutex','sceKernelWaitSema','sceKernelSignalSema','sceKernelGetThreadId',
       'malloc','free','vgl_free','sceKernelGetSystemTimeWide','sceGxmFinish',
       'pvz2_stall_native_wait','pvz2_stall_native_done','telemetry_log','fopen','file_exists','_log_print','vglMemFree']
entries={addr(n)&~1:n for n in names}
gpu_data=0;gpu_size=0
history=[]
by_address={v[0]&~1:n for n,v in symbols.items() if v[1]}
def hook(uc,at,size,data):
    if at in by_address:
        history.append((by_address[at],hex(u.reg_read(UC_ARM_REG_R0)),hex(u.reg_read(UC_ARM_REG_LR))))
        if len(history)>30:history.pop(0)
    global texture_format,gpu_data,gpu_size
    name=entries.get(at)
    x,y,z=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)]
    if name=='gpu_alloc_texture':texture_format=z
    elif name=='sceGxmTextureGetFormat':ret(texture_format)
    elif name in ('sceGxmTextureGetWidth','sceGxmTextureGetHeight'):ret(4)
    elif name=='sceGxmTextureGetData':ret(gpu_data)
    elif name=='vgl_memalign':
        gpu_data=alloc(y);gpu_size=y;ret(gpu_data)
    elif name=='malloc':ret(alloc(x))
    elif name=='sceClibMemcpy':u.mem_write(x,bytes(u.mem_read(y,z)));ret(x)
    elif name=='sceClibMemset':u.mem_write(x,bytes([y&255])*z);ret(x)
    elif name=='sceKernelGetSystemTimeWide':u.reg_write(UC_ARM_REG_R1,0);ret(1)
    elif name=='vglMemFree':ret(128*1024*1024)
    elif name:ret()
u.hook_add(UC_HOOK_CODE,hook)
STOP=0x831ff000
def call(name,*args):
    u.reg_write(UC_ARM_REG_CPSR,0x10);u.reg_write(UC_ARM_REG_SP,0x831fe000);u.reg_write(UC_ARM_REG_LR,STOP)
    for reg,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(reg,value)
    for i,value in enumerate(args[4:]):put(0x831fe000+i*4,value)
    try:u.emu_start(addr(name),STOP,timeout=3000000,count=600000)
    except Exception:
        print('while calling',name,'PC',hex(u.reg_read(UC_ARM_REG_PC)),history);raise
    assert u.reg_read(UC_ARM_REG_PC)==STOP,(name,hex(u.reg_read(UC_ARM_REG_PC)))
    return u.reg_read(UC_ARM_REG_R0)


handle=alloc(4)
def texture(upload,typ):
    call('glGenTextures',1,handle);call('glBindTexture',0xde1,get(handle))
    call(upload,0xde1,0,0x1908,4,4,0,0x1908,typ,0)
    assert call('glGetError')==0
    return call('vglGetTexDataPointer',0xde1)
# The pinned driver's raw packed allocation followed by RGBA8 fill does not
# perform format conversion. This is a color/layout regression, not proof of
# the user's latest crash or a hardware heap overwrite.
ptr=texture('glTexImage2D',0x8033)
assert gpu_size==64 # 8-pixel aligned pitch * 4 rows * 2 bytes
pixels=alloc(64);u.mem_write(pixels,bytes([255,0,0,255])*16)
call('glTexSubImage2D',0xde1,0,0,0,4,4,0x1908,0x1401,pixels)
assert call('glGetError')==0 and bytes(u.mem_read(ptr,8))!=bytes.fromhex('0ff00ff00ff00ff0')
print('REPRODUCED: native packed storage does not translate a later RGBA8 fill')
# The production loader normalizes empty storage and packed/full-byte fills.
ptr=texture('glTexImage2D_pvz2',0x8033)
assert gpu_size==128
call('glTexSubImage2D_soloader',0xde1,0,0,0,4,4,0x1908,0x1401,pixels)
assert call('glGetError')==0
for row in range(4): assert bytes(u.mem_read(ptr+row*32,16))==bytes([255,0,0,255])*4
packed=alloc(32);u.mem_write(packed,struct.pack('<16H',*([0x0f0f]*16)))
call('glTexSubImage2D_soloader',0xde1,0,0,0,4,4,0x1908,0x8033,packed)
assert call('glGetError')==0
for row in range(4): assert bytes(u.mem_read(ptr+row*32,16))==bytes([0,255,0,255])*4
print('PASS: compiled loader and native VitaGL agree on RGBA8 storage, byte fills and converted packed fills; row pixels checked')
