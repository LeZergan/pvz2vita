"""A failed error-dialog initialization must not become an endless render loop."""
from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=r/'vita/direct/source/utils/dialog.c');a=p.parse_args()
s=a.source.read_text();s=s[s.index('void fatal_error('):]
w=Path(tempfile.mkdtemp(prefix='fatal-dialog-',dir=r/'out'))
c=r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
static int graphics_ready=1,failed=1,swaps,polls;
#define GL_TRUE 1
#define sceClibVsnprintf vsnprintf
#define telemetry_log(...) ((void)0)
static void pvz2_boot_screen(const char *s) { assert(0); }
static int init_msg_dialog(const char *s) { return failed ? -1 : 0; }
static int get_msg_dialog_result(void) { assert(!failed);return ++polls==3; }
static void vglSwapBuffers(int b) { assert(!failed);++swaps; }
static void sceKernelExitProcess(int n) { assert(n==1 && swaps==(failed ? 0 : 2));puts("PASS: error-dialog failure exits without rendering; normal dialog remains dismissible");exit(0); }
'''+s+r'''
int main(int argc,char **argv) { if(argc>1)failed=0;fatal_error("test %d",5); }
'''
f=w/'test.c';f.write_text(c);exe=w/'test.exe'
subprocess.run(['gcc','-O2',str(f),'-o',str(exe)],check=True)
for options in [[],['normal']]:subprocess.run([str(exe),*options],check=True,timeout=3)
