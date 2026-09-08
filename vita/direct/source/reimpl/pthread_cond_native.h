/* Pthreads-embedded condition ABI; LGPL-2.0-or-later, see accompanying source
 * and third_party/pthread-embedded/LICENSE. Only the definition needed by the
 * corrected SDK destructor is exposed here. */
#ifndef PVZ2_PTHREAD_COND_NATIVE_H
#define PVZ2_PTHREAD_COND_NATIVE_H
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
struct pthread_cond_t_
  {
    long nWaitersBlocked;		/* Number of threads blocked            */
    long nWaitersGone;		/* Number of threads timed out          */
    long nWaitersToUnblock;	/* Number of threads to unblock         */
    sem_t semBlockQueue;		/* Queue up threads waiting for the     */
    /*   condition to become signalled      */
    sem_t semBlockLock;		/* Semaphore that guards access to      */
    /* | waiters blocked count/block queue  */
    /* +-> Mandatory Sync.LEVEL-1           */
    pthread_mutex_t mtxUnblockLock;	/* Mutex that guards access to          */
    /* | waiters (to)unblock(ed) counts     */
    /* +-> Optional* Sync.LEVEL-2           */
    pthread_cond_t next;		/* Doubly linked list                   */
    pthread_cond_t prev;
  };



#if defined(__vita__)
_Static_assert(sizeof(struct pthread_cond_t_) == 32, "SDK condition ABI changed");
_Static_assert(offsetof(struct pthread_cond_t_, semBlockLock) == 16, "SDK gate offset");
_Static_assert(offsetof(struct pthread_cond_t_, mtxUnblockLock) == 20, "SDK unblock offset");
#endif
extern pthread_cond_t pte_cond_list_head, pte_cond_list_tail;
extern int pte_cond_list_lock, pte_cond_test_init_lock;
extern int pte_osMutexLock(int handle);
extern int pte_osMutexUnlock(int handle);
#endif
