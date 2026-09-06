"""Compile the real rotation/RSB code against small host I/O adapters.

Requires the matching user-supplied 4.5.2 OBB and a native GCC toolchain.
No emulator, save data, or original game data is modified.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("obb", type=Path)
args = parser.parse_args()
work = Path(tempfile.mkdtemp(prefix="runtime-check-", dir=ROOT / "out"))
source = ROOT / "vita/direct/source"
stubs = work / "psp2"
(stubs / "io").mkdir(parents=True)
(stubs / "kernel").mkdir()
for name in ("io/fcntl.h", "io/stat.h", "kernel/clib.h"):
    (stubs / name).write_text("")

rotation = work / "rotation.c"
rotation.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef int SceUID;
typedef int64_t SceOff;
#define SCE_O_WRONLY 1
#define SCE_O_CREAT 2
#define SCE_O_APPEND 4
#define SCE_O_TRUNC 8
#define SCE_SEEK_END 2
#define sceClibSnprintf snprintf
static int64_t current, previous = -1;
static int rotations, writes;
static int sceIoOpen(const char *p, int f, int m) {
    if (f & SCE_O_TRUNC) current = 0;
    return 1;
}
static int sceIoClose(int fd) { return 0; }
static SceOff sceIoLseek(int fd, SceOff p, int w) { return current; }
static int sceIoRemove(const char *p) { previous = -1; return 0; }
static int sceIoRename(const char *a, const char *b) {
    previous = current; current = -1; ++rotations; return 0;
}
static int sceIoWrite(int fd, const void *data, size_t n) {
    current += n; ++writes; return n;
}
#include "utils/bounded_log.h"
int main(void) {
    int fd = 1;
    char data[4] = {0};
    current = PVZ2_LOG_LIMIT - 4;
    bounded_log_write(&fd, "log", data, 4);
    assert(current == PVZ2_LOG_LIMIT && rotations == 0);
    bounded_log_write(&fd, "log", data, 1);
    assert(current == 1 && previous == PVZ2_LOG_LIMIT && rotations == 1);
    current = PVZ2_LOG_LIMIT - 1;
    bounded_log_write(&fd, "log", data, 4);
    assert(current == 4 && previous == PVZ2_LOG_LIMIT - 1 && rotations == 2);
    /* Simulate an old >4 GiB file without creating one on disk. */
    current = (INT64_C(1) << 33) + 7;
    bounded_log_write(&fd, "log", data, 1);
    assert(current == 1 && previous == -1 && rotations == 2);
    int before = writes;
    bounded_log_write(&fd, "log", data, PVZ2_LOG_LIMIT + 1);
    bounded_log_write(&fd, "log", data, 0);
    current = -1;
    bounded_log_write(&fd, "log", data, 1);
    assert(writes == before);
    puts("PASS: log boundaries, rotation, oversized old log, failed seek");
}
''')

def run(command):
    subprocess.run([str(x) for x in command], check=True, cwd=work, timeout=20)

run([shutil.which("gcc"), "-std=c11", "-static", "-I", work, "-I", source,
     rotation, "-o", work / "rotation.exe"])
run([work / "rotation.exe"])

# A hard link lets the actual parser read the original OBB without copying it.
linked_obb = work / "main.147.com.ea.game.pvz2_row.obb"
os.link(args.obb.resolve(), linked_obb)
try:
    shutil.copy2(ROOT / "vita/direct/extras/rsb452.idx", work / "rsb452.idx")
    (stubs / "kernel/processmgr.h").write_text('#include <stdint.h>\nuint64_t sceKernelGetSystemTimeWide(void);\n')
    (stubs / "io/stat.h").write_text(
        '#include <direct.h>\n#define sceIoMkdir(path, mode) _mkdir(path)\n')
    harness = work / "rsb.cpp"
    harness.write_text(r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <pthread.h>
#include <atomic>
#include <thread>
#include "reimpl/rsb_index_vita.h"
static std::atomic<bool> finished{false};
extern "C" void _log_print(int, const char *, ...) {}
extern "C" void telemetry_log(const char *, const char *fmt, ...) {
    if (strstr(fmt,"background index finished")) finished=true;
}
uint64_t sceKernelGetSystemTimeWide(void) {return 0;}
extern "C" const char *pvz2_obb_path(void) {return DATA_PATH "main.147.com.ea.game.pvz2_row.obb";}
extern "C" int pthread_create_soloader(pthread_t *t,const void *,void *(*fn)(void *),void *p) {return pthread_create(t,nullptr,fn,p);}
int main(void) {
    vita_rsb_start_preload();
    uint64_t offset = 0; uint32_t size = 0;
    const char *front = vita_rsb_locate(
        "ASSET:images/768/initial/effects/load_icon_front/load_icon_front.pam", &offset, &size);
    assert(front && offset == 0x5000 && size == 322);
    const char *back = vita_rsb_locate(
        "images/768/initial/effects/load_icon_back/load_icon_back.pam", &offset, &size);
    assert(back && offset == 0x2000 && size == 10949 && !strcmp(front, back));
    assert(!vita_rsb_locate("this_resource_does_not_exist", &offset, &size));
    while(!finished) std::this_thread::yield();
    puts("PASS: compressed RSB locator, byte ranges, shared block, missing resource");
}
''')
    miniz = ROOT / "vita/direct/third_party/miniz"
    run([shutil.which("g++"), "-std=c++17", "-O2", "-pthread", "-static",
         "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_STDIO",
         '-DDATA_PATH="' + work.as_posix() + '/"',
         '-DPVZ2_INDEX_PATH="' + (work / "rsb452.idx").as_posix() + '"',
         "-I", work, "-I", source, harness,
         source / "reimpl/rsb_index_vita.cpp", miniz / "miniz.c",
         miniz / "miniz_tinfl.c", miniz / "miniz_tdef.c",
         "-o", work / "rsb.exe"])
    run([work / "rsb.exe"])
    with args.obb.open("rb") as stream:
        stream.seek(0x273000)
        expected = zlib.decompress(stream.read(0x3000))
    actual = (work / "cache/rsb452/00273000.bin").read_bytes()
    assert actual == expected and len(actual) == 32768
    print("PASS: all 32768 cached bytes match independent zlib decompression")
finally:
    linked_obb.unlink()
print("Evidence:", work)
