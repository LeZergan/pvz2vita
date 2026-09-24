"""Reproduce the native collector/GL-buffer race with controlled ARM scheduling.
No game boot, GPU, emulator frontend or device is used.
"""
from pathlib import Path
import argparse,struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser();p.add_argument('--loader-elf',type=Path,required=True);a=p.parse_args()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x81000000,0x1800000);u.mem_map(0x83000000,0x200000)
with a.loader_elf.open('rb') as f:
    e=ELFFile(f)
    for seg in e.iter_segments():
        if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
    sy={s.name:(s['st_value'],s['st_size']) for s in e.get_section_by_name('.symtab').iter_symbols()}
def addr(n):return sy[n][0]
def get(at):return struct.unpack('<I',u.mem_read(at,4))[0]
def put(at,*v):u.mem_write(at,struct.pack('<'+'I'*len(v),*v))
STOP=0x831ff000;heap=0x83010000
def alloc(n):
    global heap
    at=heap;heap+=(max(n,16)+15)&~15;assert heap<0x83100000;return at
def ret(v=0):u.reg_write(UC_ARM_REG_R0,v);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
gc=addr('garbage_collector')&~1;buf=addr('glBufferData')&~1
assert sy['garbage_collector'][1]==0xc8 and sy['glBufferData'][1]==0xec
assert bytes(u.mem_read(buf+0x7a,2))==bytes.fromhex('1f68') # ldr r7,[r3]: producer reads purge index
entries={addr(n)&~1:n for n in ['gpu_alloc_mapped_aligned','vgl_free','sceKernelSignalSema','__wrap_sceKernelWaitSema','sceGxmDestroyRenderTarget','sceKernelWaitSema','sceKernelGetSemaInfo','memset','vgl_memalign','__wrap_sceGxmFinish','sceKernelDelayThread']}
collecting=False;finished=False;freed=[]
tokens=4;waiting=False;requests=0
recovery_test=False;sleep_calls=[]
def hook(uc,at,size,_):
    global finished,tokens,waiting,requests
    name=entries.get(at)
    if name=='memset':
        x,y,z=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)]
        u.mem_write(x,bytes([y&255])*z);ret(x)
    elif name=='gpu_alloc_mapped_aligned':ret(alloc(u.reg_read(UC_ARM_REG_R1)))
    elif name in ('vgl_free','sceGxmDestroyRenderTarget'):freed.append(u.reg_read(UC_ARM_REG_R0));ret()
    elif name=='sceKernelSignalSema':
        id,n=u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R1)
        if id==11:requests+=n
        if id==12:tokens+=n
        ret()
        if collecting:finished=True;uc.emu_stop()
    elif name=='__wrap_sceKernelWaitSema':
        if recovery_test and u.reg_read(UC_ARM_REG_R0)==12:tokens-=u.reg_read(UC_ARM_REG_R1)
        ret()
    elif name=='__wrap_sceGxmFinish':ret()
    elif name=='sceKernelDelayThread':sleep_calls.append(u.reg_read(UC_ARM_REG_R0));ret()
    elif name=='vgl_memalign':
        assert requests==1 and tokens==4
        ret(alloc(u.reg_read(UC_ARM_REG_R1)))
    elif name=='sceKernelGetSemaInfo':
        # Vita SceKernelSemaInfo maxCount is at +52 (60-byte struct).
        assert u.reg_read(UC_ARM_REG_R0)==12
        put(u.reg_read(UC_ARM_REG_R1)+52,4);ret()
    elif name=='sceKernelWaitSema':
        assert u.reg_read(UC_ARM_REG_R0)==12 and u.reg_read(UC_ARM_REG_R1)==4
        assert get(u.reg_read(UC_ARM_REG_R2))==5000000
        if tokens<4:waiting=True;uc.emu_stop()
        else:tokens-=4;ret()
u.hook_add(UC_HOOK_CODE,hook)
def start(at,*args,stack=0x831fe000):
    u.reg_write(UC_ARM_REG_CPSR,0x10);u.reg_write(UC_ARM_REG_SP,stack);u.reg_write(UC_ARM_REG_LR,STOP)
    for r,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(r,v)
    return at
def run(at,end=STOP):
    u.emu_start(at,end,timeout=2000000,count=100000)
    assert u.reg_read(UC_ARM_REG_PC)==end or (collecting and finished) or waiting,hex(u.reg_read(UC_ARM_REG_PC))
def collect():
    global collecting,finished
    saved=u.context_save();collecting=True;finished=False
    run(start(addr('garbage_collector'),0,0,stack=0x831fc000))
    assert finished;collecting=False;u.context_restore(saved)
def reset():
    u.mem_write(addr('frame_purge_list'),bytes(sy['frame_purge_list'][1]))
    put(addr('frame_purge_idx'),0);put(addr('frame_elem_purge_idx'),0);put(addr('frame_purge_clean_idx'),3)
    put(addr('frame_rt_purge_idx'),0);put(addr('vgl_framecount'),1)
def replace(handle,old,end=STOP):
    put(handle,old,32,0,0,0)
    run(start(addr('glBindBuffer'),0x8892,handle))
    run(start(addr('glBufferData'),0x8892,32,0,0x88e4),end)
handle=alloc(20);old1=alloc(32);old2=alloc(32);table=addr('frame_purge_list')
reset()
# Suspend producer after reading its list index. The independent collector
# then advances that index and zeros the shared element cursor.
replace(handle,old1,buf+0x7c);producer=u.context_save();collect();u.context_restore(producer)
run(buf+0x7c|1)
replace(handle,old2)
assert get(table)==old1 and get(table+0x10000)==0 and get(table+0x10004)==old2
assert get(addr('frame_elem_purge_idx'))==2
for _ in range(4):collect()
assert old1 in freed and old2 not in freed
print('REPRODUCED: native GL buffer retirement races collector cursor reset; a live allocation is hidden after an empty list slot and never freed')
# Completion fencing means the producer cannot overlap the collector reset.
reset();freed.clear();collect()
replace(handle,old1);replace(handle,old2)
assert get(table+0x10000)==old1 and get(table+0x10004)==old2
for _ in range(4):collect()
assert old1 in freed and old2 in freed
print('PASS: waiting for native collector completion preserves both retirement entries and their eventual reclamation')
if '__wrap_sceKernelSignalSema' in sy:
    reset();freed.clear();tokens=3;requests=0
    put(addr('gc_mutex'),11,12)
    run(start(addr('__wrap_sceKernelSignalSema'),11,1))
    assert waiting and requests==1 and tokens==3
    waiting=False;collect();assert tokens==4
    # Resume the real compiled barrier after the worker returns its token.
    run(u.reg_read(UC_ARM_REG_PC)|1)
    assert tokens==4 and not waiting
    replace(handle,old1);replace(handle,old2)
    assert get(table+0x10000)==old1 and get(table+0x10004)==old2
    print('PASS: actual compiled completion wrapper blocks until native GC finishes, restores all four tokens, then permits intact buffer retirement')
    assert sy['gpu_alloc_mapped_aligned_unsafe'][1]==0xc8
    reset();tokens=4;requests=0;recovery_test=True;sleep_calls.clear()
    run(start(addr('gpu_alloc_mapped_aligned_unsafe'),16,128,0))
    assert waiting and requests==1 and tokens==3
    waiting=False;collect();run(u.reg_read(UC_ARM_REG_PC)|1)
    assert not sleep_calls and u.reg_read(UC_ARM_REG_R0)>=0x83010000
    run(start(addr('__wrap_sceKernelDelayThread'),1000000))
    assert sleep_calls==[1000000]
    print('PASS: native allocation recovery waits for completed GC and retries without its fixed 1-second sleep; unrelated 1-second sleeps preserved')
