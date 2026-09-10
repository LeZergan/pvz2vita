"""Exercise production JNI strings with exact UTF-16 / modified UTF-8 fixtures."""
from pathlib import Path
import argparse, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
lib=root/'vita/direct/lib/falso_jni'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,default=lib/'FalsoJNI.c')
p.add_argument('--bridge',type=Path,default=lib/'FalsoJNI_ImplBridge.c')
a=p.parse_args()
jni=a.source.read_text(encoding='utf-8'); bridge=a.bridge.read_text(encoding='utf-8')
def function(text, signature):
    start=text.index(signature)
    return text[start:text.index('\n}',start)+3]
work=Path(tempfile.mkdtemp(prefix='jni-unicode-',dir=root/'out'))
# The Vita-only varargs bridge returns va_list, which is an array on Linux
# x86_64. This fixture only exercises strings, so omit that unrelated prototype
# from a local header copy; all tested implementations and types stay intact.
header=(lib/'FalsoJNI_ImplBridge.h').read_text(encoding='utf-8')
prototype='va_list _AtoV(int dummy, ...);'
assert prototype in header
(work/'FalsoJNI_ImplBridge.h').write_text(header.replace(prototype,''),encoding='utf-8')
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <assert.h>
#include <pthread.h>
#include <limits.h>
#include <stddef.h>
#define FALSOJNI_DEBUGLEVEL 3
#include "FalsoJNI.h"
#include "FalsoJNI_ImplBridge.h"
#include "FalsoJNI_Logger.h"
#include "converter.h"
void _fjni_log_debug(const char*a,int b,const char*c,const char*d,...) {}
void _fjni_log_info(const char*a,int b,const char*c,const char*d,...) {}
void _fjni_log_warn(const char*a,int b,const char*c,const char*d,...) {}
void _fjni_log_error(const char*a,int b,const char*c,const char*d,...) {}
typedef union { max_align_t alignment; struct { size_t bytes; unsigned magic; } h; } Guard;
static atomic_int allocations;
static int fail_ordinal, allocation_ordinal;
static void *checked_malloc(size_t n) {
    if(fail_ordinal && ++allocation_ordinal==fail_ordinal) return NULL;
    Guard *g=malloc(sizeof(*g)+n+32); assert(g);
    g->h.bytes=n; g->h.magic=0xabc123;
    memset((unsigned char*)(g+1)+n,0xa5,32);
    atomic_fetch_add(&allocations,1); return g+1;
}
static void checked_free(void *p) {
    if(!p) return;
    Guard *g=(Guard*)p-1; assert(g->h.magic==0xabc123);
    for(int i=0;i<32;i++) assert(((unsigned char*)p)[g->h.bytes+i]==0xa5);
    g->h.magic=0; atomic_fetch_sub(&allocations,1); free(g);
}
static void *checked_calloc(size_t n,size_t width) {
    assert(!width || n<=SIZE_MAX/width);
    void *p=checked_malloc(n*width); if(p)memset(p,0,n*width); return p;
}
static void *checked_realloc(void *p,size_t n) {
    if(!p)return checked_malloc(n);
    Guard *g=(Guard*)p-1; size_t old=g->h.bytes;
    void *q=checked_malloc(n); if(!q)return NULL;
    memcpy(q,p,old<n?old:n); checked_free(p); return q;
}
#define malloc checked_malloc
#define calloc checked_calloc
#define realloc checked_realloc
#define free checked_free
'''
if (lib/'jni_string_codec.h').exists(): prefix+='\n#include "jni_string_codec.h"\n'
parts=[prefix,function(bridge,'jsize getFieldTypeSize('),
    bridge[bridge.index('JavaDynArray * jda_alloc('):bridge.index('va_list _AtoV(')],
    jni[jni.index('typedef enum TrackedObjKind'):jni.index('/*\n * JavaVM Methods')],
    jni[jni.index('jobject NewGlobalRef('):jni.index('jint EnsureLocalCapacity(')],
    jni[jni.index('jstring NewString('):jni.index('jsize GetArrayLength(')],
    function(jni,'void GetStringRegion('),function(jni,'void GetStringUTFRegion('),
    jni[jni.index('jobjectArray NewObjectArray('):jni.index('jbooleanArray NewBooleanArray(')],
    function(jni,'jbyteArray NewByteArray(')]
tests=r'''
static int failures;
#define CHECK(expr, message) do { if(!(expr)) { printf("FAIL: %s\n",message); failures++; } } while(0)
static const jchar ru[]={0x041c,0x0430,0x043a,0x0441};
static const char ru8[]="\xd0\x9c\xd0\xb0\xd0\xba\xd1\x81";
static const jchar mixed[]={0x41,0,0x4e2d,0xd83c,0xdf3b};
static const char mixed8[]="A\xc0\x80\xe4\xb8\xad\xed\xa0\xbc\xed\xbc\xbb";
static void *read_shared(void *value) {
    jstring s=value;
    for(int i=0;i<10000;i++) {
        jboolean copied=JNI_FALSE;
        char *bytes=(char*)GetStringUTFChars(NULL,s,&copied);
        assert(bytes && copied && !strcmp(bytes,mixed8));
        assert(GetStringLength(NULL,s)==5 && GetStringUTFLength(NULL,s)==12);
        ReleaseStringUTFChars(NULL,s,bytes);
    }
    return NULL;
}
static void check_string(const char *name,const jchar *units,int count,const char *bytes) {
    jstring s=NewString(NULL,units,count); assert(s);
    CHECK(GetStringLength(NULL,s)==count,"NewString UTF-16 length");
    char *copy=(char*)GetStringUTFChars(NULL,s,NULL); assert(copy);
    if(strcmp(copy,bytes)) { printf("FAIL: %s UTF-16 constructor roundtrip (got %zu bytes, expected %zu)\n",name,strlen(copy),strlen(bytes)); failures++; }
    CHECK(GetStringUTFLength(NULL,s)==(jsize)strlen(bytes),"UTF length must count encoded bytes");
    ReleaseStringUTFChars(NULL,s,copy); DeleteLocalRef(NULL,s);
    s=NewStringUTF(NULL,bytes); assert(s);
    if(GetStringLength(NULL,s)!=count) { printf("FAIL: %s UTF constructor reports %d units instead of %d\n",name,GetStringLength(NULL,s),count); failures++; }
    jchar *wide=(jchar*)GetStringChars(NULL,s,NULL); assert(wide);
    CHECK(!memcmp(wide,units,count*sizeof(jchar)),"UTF constructor decoded content");
    ReleaseStringChars(NULL,s,wide); DeleteLocalRef(NULL,s);
}
int main(void) {
    static const jchar ascii[]={0x61,0x62,0x63};
    check_string("ASCII",ascii,3,"abc");
    check_string("Cyrillic",ru,4,ru8);
    check_string("NUL/CJK/surrogate pair",mixed,5,mixed8);
    if(!failures) {
        check_string("empty",NULL,0,"");
        jstring s=NewString(NULL,mixed,5); assert(s);
        char region[12]; memset(region,0x55,sizeof(region));
        GetStringUTFRegion(NULL,s,1,2,region);
        CHECK(!memcmp(region,mixed8+1,5) && region[5]==0x55,"region indices are UTF-16, writes bounded encoded bytes");
        GetStringUTFRegion(NULL,s,3,1,region);
        CHECK(!memcmp(region,"\xed\xa0\xbc",3),"region preserves lone high surrogate");
        memset(region,0x55,sizeof(region));
        GetStringUTFRegion(NULL,s,-1,1,region);
        GetStringUTFRegion(NULL,s,1,INT_MAX,region);
        GetStringUTFRegion(NULL,s,0,-1,region);
        GetStringUTFRegion(NULL,s,5,0,region);
        for(int i=0;i<12;i++) CHECK(region[i]==0x55,"invalid and empty UTF regions leave output alone");
        jchar wide[8]; for(int i=0;i<8;i++)wide[i]=0x5555;
        GetStringRegion(NULL,s,-1,1,wide); GetStringRegion(NULL,s,1,INT_MAX,wide);
        for(int i=0;i<8;i++)CHECK(wide[i]==0x5555,"invalid UTF-16 regions leave output alone");
        GetStringRegion(NULL,s,2,3,wide);
        CHECK(!memcmp(wide,mixed+2,6) && wide[3]==0x5555,"UTF-16 region content and bounds");
        JavaString *data=s; void *u8=data->utf8->array, *u16=data->utf16->array;
        pthread_t readers[4];
        for(int i=0;i<4;i++)assert(!pthread_create(&readers[i],NULL,read_shared,s));
        for(int i=0;i<4;i++)assert(!pthread_join(readers[i],NULL));
        CHECK(data->utf8->array==u8 && data->utf16->array==u16,"concurrent getters preserve immutable backing");
        DeleteLocalRef(NULL,s);
        s=NewStringUTF(NULL,"\xf0\x9f\x8c\xbb"); assert(s);
        char *normalized=(char*)GetStringUTFChars(NULL,s,NULL);
        CHECK(!strcmp(normalized,"\xed\xa0\xbc\xed\xbc\xbb"),"native four-byte UTF-8 compatibility");
        ReleaseStringUTFChars(NULL,s,normalized); DeleteLocalRef(NULL,s);
        /* Constructors must release partial allocations if any allocation fails. */
        for(int i=1;i<=7;i++) {
            fail_ordinal=i; allocation_ordinal=0;
            s=NewStringUTF(NULL,ru8); fail_ordinal=0;
            if(s) DeleteLocalRef(NULL,s);
            CHECK(atomic_load(&allocations)==0,"UTF constructor allocation failure cleanup");
            fail_ordinal=i; allocation_ordinal=0;
            s=NewString(NULL,mixed,5); fail_ordinal=0;
            if(s) DeleteLocalRef(NULL,s);
            CHECK(atomic_load(&allocations)==0,"UTF-16 constructor allocation failure cleanup");
        }
        for(int i=1;i<=3;i++) {
            fail_ordinal=i; allocation_ordinal=0;
            jobject arr=NewByteArray(NULL,0); fail_ordinal=0;
            if(arr)DeleteLocalRef(NULL,arr);
            CHECK(atomic_load(&allocations)==0,"empty primitive array failure cleanup");
            s=NewStringUTF(NULL,"child"); assert(s);
            fail_ordinal=i; allocation_ordinal=0;
            arr=NewObjectArray(NULL,3,NULL,s); fail_ordinal=0;
            if(arr)DeleteLocalRef(NULL,arr);
            CHECK(GetStringLength(NULL,s)==5,"object-array failure retains caller reference");
            DeleteLocalRef(NULL,s);
            CHECK(atomic_load(&allocations)==0,"object-array tracking failure releases retained children");
        }
        if(!failures) puts("PASS: guarded Unicode copies/regions, 40000 concurrent reads, immutable backing, string/array allocation-failure cleanup");
    }
    uint32_t live,created,freed; fjni_ref_stats(&live,&created,&freed);
    CHECK(live==0 && created==freed,"all string allocations released");
    CHECK(atomic_load(&allocations)==0,"guarded allocations all released");
    if(!failures) puts("PASS: production JNI UTF-16 and modified UTF-8 constructors, lengths, copies and lifetime");
    return failures ? 1 : 0;
}
'''
(work/'check.c').write_text('\n'.join(parts)+tests,encoding='utf-8')
subprocess.run(['gcc','-std=gnu11','-O2','-static','-pthread','-Wno-pointer-to-int-cast','-I',str(lib),str(work/'check.c'),str(lib/'converter.c'),'-o',str(work/'check.exe')],check=True,timeout=30)
r=subprocess.run([str(work/'check.exe')],capture_output=True,text=True,timeout=20)
(work/'result.txt').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr,end=''); print(f'Evidence: {work}')
raise SystemExit(r.returncode)
