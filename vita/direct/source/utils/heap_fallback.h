#ifndef PVZ2_HEAP_FALLBACK_H
#define PVZ2_HEAP_FALLBACK_H
#include <stddef.h>

/* Game allocations only. Every returned pointer must return through these
 * functions (the Android malloc/calloc/realloc/memalign/valloc/free imports). */
void *pvz2_heap_malloc(size_t size);
void *pvz2_heap_calloc(size_t count, size_t size);
void *pvz2_heap_realloc(void *ptr, size_t size);
void *pvz2_heap_memalign(size_t alignment, size_t size);
void pvz2_heap_free(void *ptr);
#endif
