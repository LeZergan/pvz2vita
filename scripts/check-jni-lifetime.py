"""Run the actual JNI lifetime functions and texture mark cleanup on the host."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
lib = root / "vita/direct/lib/falso_jni"
work = Path(tempfile.mkdtemp(prefix="jni-lifetime-", dir=root / "out"))
jni = (lib / "FalsoJNI.c").read_text(encoding="utf-8")
bridge = (lib / "FalsoJNI_ImplBridge.c").read_text(encoding="utf-8")


def function(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}", start) + 3]


prefix = r'''
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <assert.h>
#include <pthread.h>
#define FALSOJNI_DEBUGLEVEL 3
#include "FalsoJNI.h"
#include "FalsoJNI_ImplBridge.h"
#include "FalsoJNI_Logger.h"
#include "converter.h"
#include "jni_string_codec.h"
#include "utils/texture_marks.h"
void _fjni_log_debug(const char*a,int b,const char*c,const char*d,...) {}
void _fjni_log_info(const char*a,int b,const char*c,const char*d,...) {}
void _fjni_log_warn(const char*a,int b,const char*c,const char*d,...) {}
void _fjni_log_error(const char*a,int b,const char*c,const char*d,...) {}
'''
parts = [prefix, function(bridge, "jsize getFieldTypeSize("),
         bridge[bridge.index("JavaDynArray * jda_alloc("):bridge.index("va_list _AtoV(")],
         jni[jni.index("typedef enum TrackedObjKind"):jni.index("/*\n * JavaVM Methods")],
         jni[jni.index("jobject NewGlobalRef("):jni.index("jint EnsureLocalCapacity(")],
         jni[jni.index("jstring NewStringUTF("):jni.index("jsize GetArrayLength(")],
         jni[jni.index("jobjectArray NewObjectArray("):jni.index("jbooleanArray NewBooleanArray(")],
         function(jni, "jbyteArray NewByteArray(")]
tests = r'''
static void expect_live(unsigned expected) {
    uint32_t live,created,freed; fjni_ref_stats(&live,&created,&freed);
    assert(live==expected && created-freed==live);
}
static void *churn(void *unused) {
    for(unsigned i=0;i<50000;i++) {
        jobject s=NewStringUTF(NULL,"wctgc");
        jobject a=NewByteArray(NULL,32);
        DeleteLocalRef(NULL,s); DeleteLocalRef(NULL,a);
    }
    return NULL;
}
static void put(uint32_t *t,unsigned id) {
    unsigned i=(id*2654435761u)&7;
    while(t[i]) i=(i+1)&7;
    t[i]=id;
}
static int has(uint32_t *t,unsigned id) {
    unsigned i=(id*2654435761u)&7;
    for(unsigned n=0;n<8 && t[i];n++,i=(i+1)&7) if(t[i]==id)return 1;
    return 0;
}
int main(void) {
    jobject s=NewStringUTF(NULL,"context"); expect_live(1);
    NewGlobalRef(NULL,s); DeleteLocalRef(NULL,s); expect_live(1);
    jobject alias=NewLocalRef(NULL,s); DeleteGlobalRef(NULL,s); expect_live(1);
    char *copy=(char*)GetStringUTFChars(NULL,alias,NULL);
    assert(strcmp(copy,"context")==0); ReleaseStringUTFChars(NULL,alias,copy);
    DeleteLocalRef(NULL,alias); expect_live(0);
    puts("PASS: locals and globals keep objects alive until their final release");
    s=NewStringUTF(NULL,"child");
    jobject a=NewObjectArray(NULL,2,NULL,s); DeleteLocalRef(NULL,s); expect_live(2);
    jobject child=GetObjectArrayElement(NULL,a,0);
    SetObjectArrayElement(NULL,a,0,NULL); expect_live(2);
    DeleteLocalRef(NULL,a); expect_live(1);
    copy=(char*)GetStringUTFChars(NULL,child,NULL); assert(strcmp(copy,"child")==0);
    ReleaseStringUTFChars(NULL,child,copy); DeleteLocalRef(NULL,child); expect_live(0);
    puts("PASS: object-array retention, replacement, duplicate children and returned local aliases");
    DeleteLocalRef(NULL,(jobject)(uintptr_t)0x42424242); expect_live(0);
    pthread_t threads[4];
    for(int i=0;i<4;i++) assert(pthread_create(&threads[i],NULL,churn,NULL)==0);
    for(int i=0;i<4;i++) pthread_join(threads[i],NULL);
    expect_live(0);
    printf("PASS: 400000 threaded transient allocations; live=%u created=%u freed=%u\n",g_tracked_live,g_tracked_created,g_tracked_freed);
    jobject held[20000];
    for(int i=0;i<20000;i++) held[i]=NewByteArray(NULL,1);
    expect_live(20000);
    for(int i=19999;i>=0;i--) DeleteLocalRef(NULL,held[i]);
    expect_live(0); puts("PASS: 20000 simultaneous refs; bucket collision cleanup");
    uint32_t t[8]={0}; put(t,7);put(t,15);put(t,23);
    texture_marks_remove(t,8,7); assert(!has(t,7)&&has(t,15)&&has(t,23));
    put(t,7); texture_marks_remove(t,8,15); assert(has(t,7)&&!has(t,15)&&has(t,23));
    memset(t,0,sizeof(t)); for(unsigned i=1;i<=8;i++)put(t,i);
    texture_marks_remove(t,8,4); assert(!has(t,4));
    for(unsigned i=1;i<=8;i++) if(i!=4)assert(has(t,i));
    texture_marks_remove(t,8,100); put(t,4); assert(has(t,4));
    puts("PASS: recycled texture IDs, wrapped collision chains, full table and absent IDs");
}
'''
(work / "check.c").write_text("\n".join(parts) + tests, encoding="utf-8")
cmd = ["gcc", "-std=gnu11", "-O2", "-static", "-pthread", "-Wno-pointer-to-int-cast",
       "-I", str(lib), "-I", str(root / "vita/direct/source"), str(work / "check.c"),
       str(lib / "converter.c"), "-o", str(work / "check.exe")]
subprocess.run(cmd, check=True)
r = subprocess.run([str(work / "check.exe")], capture_output=True, text=True)
(work / "result.txt").write_text(r.stdout + r.stderr)
print(r.stdout, end="")
print(r.stderr, end="")
r.check_returncode()
print(f"Evidence: {work}")
