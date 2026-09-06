"""Small production shader-cache lifetime check, no graphics/device control."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
w=Path(tempfile.mkdtemp(prefix='program-cache-',dir=r/'out'))
s=(r/'vita/direct/source/utils/glutil.c').read_text(encoding='utf-8')
def part(a,b): return s[s.index(a):s.index(b)]
c='''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned GLuint;
typedef int GLint;
typedef float GLfloat;
static unsigned deleted,linked,pvz2_program_link_us;
static int g_last_mat4_have;
static void glDeleteProgram(GLuint p) { deleted=p; }
static void glLinkProgram(GLuint p) { linked=p; }
static void log_program_link_failure(GLuint p) {}
static uint64_t sceKernelGetSystemTimeWide(void) { return 0; }
#define launch_state_mark_gl_phase(...) ((void)0)
'''
c+=part('#define PROGRAM_CACHE_CAP','static GLint sole_mat4_location(')
c+=part('static program_uniform_cache *program_cache_get(', 'static void program_cache_add_uniform(')
c+=part('void glDeleteProgram_soloader(', 'void glUseProgram_soloader(')
c+=part('void glLinkProgram_soloader(', '#if defined(USE_GLSL_SHADERS) && defined(DUMP_COMPILED_SHADERS)')
c+='''
int main(void) {
    mat4_program_state *m=mat4_state_get(7);
    m->sole_location=123; m->location_cached=1; m->have_last_finite=1;
    program_uniform_cache *u=program_cache_get(7,1); u->valid=1; u->uniform_count=8;
    assert(mat4_state_get(7)==m && program_cache_get(7,0)==u);
    g_uniform_current_program=7; g_last_mat4_have=1;
    glDeleteProgram_soloader(7);
    assert(deleted==7 && !g_uniform_current_program && !g_last_mat4_have);
    assert(!program_cache_get(7,0));
    m=mat4_state_get(7);
    assert(m->sole_location==-1 && !m->location_cached && !m->have_last_finite);
    m->location_cached=1; m->have_last_finite=1;
    program_cache_get(7,1)->valid=1;
    g_uniform_current_program=7; g_last_mat4_have=1;
    glLinkProgram_soloader(7);
    assert(linked==7 && !g_last_mat4_have && !program_cache_get(7,0));
    assert(!mat4_state_get(7)->location_cached);
    for(unsigned i=1;i<2048;++i) {
        assert(mat4_state_get(i)->program==i);
        assert(program_cache_get(i,1)->program==i);
        assert(mat4_state_get(i)->program==i && program_cache_get(i,0)->program==i);
    }
    glDeleteProgram_soloader(0);
    assert(!program_cache_get(0,1));
    puts("PASS: program deletion/relink clears uniform locations and matrix recovery; ID reuse and cache eviction; repeated lookup fast paths");
}
'''
(w/'check.c').write_text(c,encoding='utf-8'); e=w/'check.exe'
subprocess.run(['gcc','-O2','-static',str(w/'check.c'),'-o',str(e)],check=True,timeout=30)
run=subprocess.run([str(e)],check=True,capture_output=True,text=True,timeout=3)
(w/'result.txt').write_text(run.stdout,encoding='utf-8')
print(run.stdout.strip());print(w)
