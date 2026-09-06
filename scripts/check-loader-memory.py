"""Exercise the actual SO loader with a bounded host allocator and failed I/O.

This is a loader regression check, not a simulation of Vita GPU or gameplay.
The fixture uses the exact 4.5.2 file/segment sizes but inert section contents.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix="loader-memory-check-", dir=ROOT / "out"))
lib = ROOT / "vita/direct/lib/so_util"
(work / "psp2").mkdir()
(work / "psp2/types.h").write_text("#include <stdint.h>\n#include <stddef.h>\ntypedef int SceUID;\n")
source = (lib / "so_util.c").read_text()
loader = source[source.index("int _so_load("):source.index("int so_relocate(")]
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "so_util.h"
typedef int64_t SceOff;
typedef uint32_t SceUInt32;
typedef struct {size_t size; unsigned attr, field_C;} SceKernelAllocMemBlockKernelOpt;
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW 1
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX 2
#define SCE_O_RDONLY 0
#define SCE_SEEK_SET 0
#define SCE_SEEK_END 2
#define PATCH_SZ 0x10000
#define OOM ((int)0x80024302u)
#define FILE_BYTES 18198492u
#define sceClibMemcpy memcpy
#define sceClibPrintf(...) ((void)0)
static so_module *head, *tail;
static void telemetry_log(const char *tag, const char *fmt, ...) {}
static struct {void *ptr; size_t size;} blocks[32];
static size_t budget, used, peak, position, chunk;
static unsigned allocation_calls, fail_at, live_blocks, live_fds;
static int base_fail, read_error;
static size_t eof_at;
static SceOff reported_size;
static unsigned char fixture[4096];
static int alloc_block(size_t size) {
    ++allocation_calls;
    if (allocation_calls == fail_at || size > budget - used) return OOM;
    unsigned id = allocation_calls;
    assert(id < 32);
    blocks[id].ptr = calloc(1, size);
    assert(blocks[id].ptr);
    blocks[id].size = size;
    used += size; if (used > peak) peak = used; ++live_blocks;
    return (int)id;
}
static int sceKernelAllocMemBlock(const char *name, int type, size_t size, void *opt) {return alloc_block(size);}
static int kuKernelAllocMemBlock(const char *name, int type, size_t size, void *opt) {return alloc_block(size);}
static int sceKernelGetMemBlockBase(int id, void **out) {
    if (base_fail) return -7;
    assert(id > 0 && blocks[id].ptr); *out = blocks[id].ptr; return 0;
}
static int sceKernelFreeMemBlock(int id) {
    assert(id > 0 && id < 32 && blocks[id].ptr);
    free(blocks[id].ptr); blocks[id].ptr = NULL;
    used -= blocks[id].size; --live_blocks; return 0;
}
/* No execution/relocation is tested: only allocations, reads, and ownership. */
static void kuKernelCpuUnrestrictedMemcpy(void *dst, const void *src, size_t n) {}
static int sceIoOpen(const char *path, int flags, int mode) {++live_fds; position = 0; return 99;}
static int sceIoClose(int fd) {assert(fd == 99 && live_fds == 1); --live_fds; return 0;}
static SceOff sceIoLseek(int fd, SceOff offset, int whence) {
    if (whence == SCE_SEEK_END) return reported_size;
    position = 0; return 0;
}
static int sceIoRead(int fd, void *dst, size_t bytes) {
    if (read_error) return -8;
    if (position >= eof_at) return 0;
    if (bytes > eof_at-position) bytes = eof_at-position;
    if (bytes > chunk) bytes = chunk;
    memset(dst, 0, bytes);
    if (position < sizeof(fixture)) {
        size_t take = sizeof(fixture)-position;
        if (take > bytes) take = bytes;
        memcpy(dst, fixture+position, take);
    }
    position += bytes; return (int)bytes;
}
'''
tests = r'''
static void reset(size_t cap) {
    assert(live_blocks == 0 && live_fds == 0 && used == 0);
    budget=cap; peak=0; allocation_calls=fail_at=0;
    base_fail=read_error=0; reported_size=FILE_BYTES; eof_at=FILE_BYTES;
    chunk=65536; head=tail=NULL;
}
static void fixture_init(void) {
    Elf32_Ehdr *e=(void *)fixture;
    memcpy(e->e_ident, ELFMAG, SELFMAG);
    e->e_phoff=sizeof(*e); e->e_phnum=2;
    Elf32_Phdr *p=(void *)(fixture+e->e_phoff);
    p[0]=(Elf32_Phdr){.p_type=PT_LOAD,.p_flags=PF_R|PF_X,.p_vaddr=0,.p_filesz=0x109d868,.p_memsz=0x109d868,.p_align=4096};
    p[1]=(Elf32_Phdr){.p_type=PT_LOAD,.p_flags=PF_R|PF_W,.p_vaddr=0x109f170,.p_filesz=0xbc930,.p_memsz=0x136284,.p_align=4096};
    e->e_shoff=512; e->e_shnum=6; e->e_shstrndx=0;
    Elf32_Shdr *s=(void *)(fixture+512);
    s[0].sh_offset=1024;
    char *names=(void *)(fixture+1024); size_t n=1;
    const char *required[]={".dynamic",".dynstr",".dynsym",".rel.dyn",".rel.plt"};
    for(int i=1;i<6;i++) {
        s[i].sh_name=n; s[i].sh_addr=256;
        strcpy(names+n,required[i-1]); n+=strlen(required[i-1])+1;
    }
}
static void free_loaded(so_module *m) {
    for(int i=0;i<m->n_data;i++) sceKernelFreeMemBlock(m->data_blockid[i]);
    sceKernelFreeMemBlock(m->text_blockid); sceKernelFreeMemBlock(m->patch_blockid);
}
int main(void) {
    so_module m; fixture_init();
    /* Old order: graphics leaves 32 MiB before the file/map overlap. */
    reset(32u*1024*1024);
    assert(so_file_load(&m,"fixture",0x98000000u)==OOM);
    assert(live_blocks==0 && live_fds==0);
    puts("PASS: 32 MiB post-graphics budget reproduces physical-page exhaustion; no leaked blocks/fd");
    /* New order: map while memory is available, then reserve graphics. */
    reset(64u*1024*1024);
    assert(so_file_load(&m,"fixture",0x98000000u)==0);
    assert(live_blocks==3 && live_fds==0 && position==FILE_BYTES);
    printf("PASS: load-before-graphics peak=%zu resident=%zu staging released\n",peak,used);
    assert(peak==36966400u && used==18767872u);
    free_loaded(&m);
    for(unsigned failure=1;failure<=4;failure++) {
        reset(64u*1024*1024); fail_at=failure;
        assert(so_file_load(&m,"fixture",0x98000000u)==OOM);
        assert(live_blocks==0 && live_fds==0);
    }
    puts("PASS: injected staging/patch/text/data allocation failures release all resources");
    reset(64u*1024*1024); eof_at=100;
    assert(so_file_load(&m,"fixture",0x98000000u)==-1);
    reset(64u*1024*1024); read_error=1;
    assert(so_file_load(&m,"fixture",0x98000000u)==-8);
    reset(64u*1024*1024); base_fail=1;
    assert(so_file_load(&m,"fixture",0x98000000u)==-7);
    reset(64u*1024*1024); reported_size=-5;
    assert(so_file_load(&m,"fixture",0x98000000u)==-1);
    reset(64u*1024*1024); reported_size=INT64_C(1)<<33;
    assert(so_file_load(&m,"fixture",0x98000000u)==-1);
    reset(64u*1024*1024);
    puts("PASS: partial reads complete; EOF/read/base/size failures release all resources");
}
'''
(work / "check.c").write_text(harness + loader + tests)
subprocess.run(["gcc", "-std=gnu11", "-O1", "-static", "-I", str(work), "-I", str(lib),
                str(work / "check.c"), "-o", str(work / "check.exe")], check=True)
result = subprocess.run([str(work / "check.exe")], text=True, capture_output=True)
(work / "result.txt").write_text(result.stdout + result.stderr)
print(result.stdout, end="")
print(result.stderr, end="")
result.check_returncode()
print(f"Evidence: {work}")
