"""Production resolver: unresolved imports fail, weak symbols, addends and overrides."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];w=Path(tempfile.mkdtemp(prefix='import-resolver-',dir=r/'out'))
s=(r/'vita/direct/lib/so_util/so_util.c').read_text()
c=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define R_ARM_ABS32 2
#define R_ARM_GLOB_DAT 21
#define R_ARM_JUMP_SLOT 22
#define SHN_UNDEF 0
#define STB_WEAK 2
#define ELF32_R_TYPE(i) ((i)&255)
#define ELF32_R_SYM(i) ((i)>>8)
#define ELF32_ST_BIND(i) ((i)>>4)
typedef struct { uint32_t r_offset,r_info; } Elf32_Rel;
typedef struct { uint32_t st_name; unsigned char st_info; unsigned short st_shndx; } Elf32_Sym;
typedef struct { const char *symbol; uintptr_t func; } so_default_dynlib;
typedef struct { int num_reldyn,num_relplt; Elf32_Rel *reldyn,*relplt; Elf32_Sym *dynsym;
    char *dynstr;uintptr_t text_base; } so_module;
static unsigned warnings;
static void telemetry_log(const char *tag,const char *fmt,const char *name) {
    assert(!strcmp(tag,"IMPORT_MISSING"));assert(*name);++warnings;
}
static void sceClibPrintf(const char *fmt,...) {}
static uintptr_t so_resolve_link(so_module *m,const char *name) {
    return !strcmp(name,"dep") ? 0x123400 : 0;
}
static void kuKernelCpuUnrestrictedMemcpy(void *d,const void *s,size_t n) { memcpy(d,s,n); }
'''
c+=s[s.index('int so_resolve('):s.index('\nint __ret0()',s.index('int so_resolve('))]
c+=r'''
int main(void) {
    uintptr_t slots[4]={0xaaaa,0xbbbb,0xcccc,0xdddd};
    char names[]="real\0missing\0weak\0dep\0";
    Elf32_Sym symbols[]={{0,16,0},{5,16,0},{13,32,0},{18,16,0}};
    Elf32_Rel rels[4];
    for(int i=0;i<4;++i) {rels[i].r_offset=i*sizeof(uintptr_t);rels[i].r_info=(i<<8)|R_ARM_JUMP_SLOT;}
    so_module m={0,4,NULL,rels,symbols,names,(uintptr_t)slots};
    so_default_dynlib table[]={{"real",0x567800},{"dep",0x876500}};
    assert(so_resolve(&m,table,sizeof(table),0)==1 && warnings==1);
    assert(slots[0]==0x567800 && slots[1]==0xbbbb && slots[2]==0 && slots[3]==0x876500);
    /* The local table overrides dependency symbols once, with the original addend. */
    m.relplt=&rels[3];m.num_relplt=1;rels[3].r_info=(3<<8)|R_ARM_ABS32;slots[3]=16;
    assert(!so_resolve(&m,table,sizeof(table),0) && slots[3]==0x876510);
    slots[3]=16;assert(!so_resolve(&m,table,sizeof(table[0]),0) && slots[3]==0x123410);
    slots[3]=16;assert(so_resolve(&m,table,sizeof(table[0]),1)==1 && slots[3]==16);
    m.relplt=&rels[2];rels[2].r_info=(2<<8)|R_ARM_ABS32;slots[2]=24;
    assert(!so_resolve(&m,table,sizeof(table),0) && slots[2]==24);
    puts("PASS: missing strong imports report failure without fake success; weak imports, dependency overrides and ABS32 addends");
}
'''
(w/'check.c').write_text(c);e=w/'check.exe'
subprocess.run(['gcc','-O2','-static',str(w/'check.c'),'-o',str(e)],check=True,timeout=30)
subprocess.run([str(e)],check=True,timeout=3)
