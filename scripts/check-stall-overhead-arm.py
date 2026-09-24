"""Count actual ARM wait-observer work with kernel calls mocked; not an FPS test."""
from pathlib import Path
import argparse, json, struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--loader-elf',required=True,type=Path)
p.add_argument('--expected-lookups',type=int,required=True)
a=p.parse_args()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
u.mem_map(0x81000000,0x800000);u.mem_map(0x83000000,0x20000)
with a.loader_elf.open('rb') as f:
    e=ELFFile(f)
    for seg in e.iter_segments():
        if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
    symbols={s.name:(s['st_value'],s['st_size']) for s in e.get_section_by_name('.symtab').iter_symbols()}
u.mem_write(symbols['pvz2_logging_enabled'][0],struct.pack('<I',1))
slots,slot_bytes=symbols['slots'];assert slot_bytes==40*28
STOP=0x8301f000
hooks={symbols[n][0]&~1:n for n in ['sceKernelGetThreadId','sceKernelGetThreadInfo','sceKernelWaitSema','__emutls_get_address']}
instructions=lookups=waits=barriers=0;at_slot=0
def words(at,n):return struct.unpack('<'+'I'*n,u.mem_read(at,4*n))
def hook(uc,at,size,ctx):
    global instructions,lookups,waits,barriers
    instructions+=1
    if bytes(uc.mem_read(at,size)) in [bytes.fromhex('5bf07ff5'),bytes.fromhex('bff35b8f')]:barriers+=1
    name=hooks.get(at)
    if name is None:return
    if name=='sceKernelGetThreadId':lookups+=1;value=123
    elif name=='sceKernelWaitSema':
        assert [uc.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2]]==[77,1,0]
        state=words(slots+at_slot*28,7)
        assert state[0]==123 and state[4:]==((1,77,STOP) if enabled else (0,0,0)),state
        waits+=1;value=0x80028005
    else:raise AssertionError('Unexpected kernel/TLS call: '+name)
    uc.reg_write(UC_ARM_REG_R0,value);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
results=[]
for enabled in [0,1]:
    u.mem_write(symbols['pvz2_logging_enabled'][0],struct.pack('<I',enabled))
    for at_slot in [0,5,15,39]:
        u.mem_write(slots,b''.join(struct.pack('<7I',123 if i==at_slot else i+1,0,0,0,0,0,0) for i in range(40)))
        instructions=lookups=waits=barriers=0
        u.reg_write(UC_ARM_REG_SP,0x83018000);u.reg_write(UC_ARM_REG_LR,STOP)
        for r,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2],[77,1,0]):u.reg_write(r,v)
        u.emu_start(symbols['__wrap_sceKernelWaitSema'][0],STOP,count=10000)
        assert u.reg_read(UC_ARM_REG_PC)==STOP and u.reg_read(UC_ARM_REG_R0)==0x80028005
        assert words(slots+at_slot*28,7)[4]==0 and waits==1 and lookups==(a.expected_lookups if enabled else 0)
        results.append(dict(logging=enabled,slot=at_slot,instructions=instructions,thread_id_calls=lookups,memory_barriers=barriers))
print(json.dumps({'elf':str(a.loader_elf),'scope':'actual compiled wrapper/observer; kernel mocked; no device timing', 'samples':results},indent=2))
