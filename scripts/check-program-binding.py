"""Small production GL binding check with driver state mocked; no rendering."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix='program-binding-', dir=root / 'out'))
source = (root / 'vita/direct/source/utils/glutil.c').read_text(encoding='utf-8')
start = source.index('void glUseProgram_soloader(')
end = source.index('\nvoid glVertexAttribPointer_soloader(', start)
test = '''
#include <assert.h>
#include <stdio.h>
typedef unsigned GLuint;
typedef int GLint;
#define MCSM_FAST_FINAL_RUNTIME 1
#define GL_CURRENT_PROGRAM 1
static GLuint g_uniform_current_program, current, calls;
static unsigned pvz2_program_binds, pvz2_program_binds_skipped;
static void glGetIntegerv(int name, GLint *p) { *p=current; }
static void glUseProgram(GLuint program) { current=program; ++calls; }
'''+source[start:end]+'''
int main(void) {
    glUseProgram_soloader(2); assert(calls==1 && current==2);
    glUseProgram_soloader(2); assert(calls==2 && pvz2_program_binds_skipped==0);
    glUseProgram(3); /* Loader-owned rendering bypasses the wrapper. */
    glUseProgram_soloader(2); assert(calls==4 && current==2);
    current=0; /* Driver changes current program after deletion/context reset. */
    glUseProgram_soloader(2); assert(calls==5 && current==2);
    glUseProgram_soloader(0); assert(calls==6 && !current && !g_uniform_current_program);
    glUseProgram_soloader(2); assert(calls==7 && current==2);
    assert(pvz2_program_binds==6 && pvz2_program_binds_skipped==0);
    puts("PASS: every requested binding reaches driver; external driver state, deletion/reset and unbind respected");
}
'''
(work / 'check.c').write_text(test, encoding='utf-8')
exe = work / 'check.exe'
subprocess.run(['gcc', '-O2', '-static', str(work/'check.c'), '-o', str(exe)], check=True, timeout=30)
run = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=3)
(work / 'result.txt').write_text(run.stdout, encoding='utf-8')
print(run.stdout.strip())
print(work)
