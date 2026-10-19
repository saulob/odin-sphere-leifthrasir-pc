/* Kernel event flags (sceKernel*EventFlag), adapted from gr2_pc's runtime_tlg.c. */
#define _GNU_SOURCE
#include "runtime.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define KERR_ESRCH KERR(3)
#define KERR_EPERM KERR(1)
#define KERR_EBUSY KERR(16)
#define KERR_EINVAL KERR(22)
#define KERR_ENOMEM KERR(12)
#define KERR_ETIMEDOUT KERR(60)
#define KERR_ECANCELED KERR(85)

#define EF_MAGIC UINT32_C(0x45564654)
#define EF_ATTR_MULTI 0x20
#define EF_WAIT_AND 1
#define EF_WAIT_OR 2
#define EF_CLEAR_ALL 0x10
#define EF_CLEAR_PAT 0x20
typedef struct {
    uint32_t magic, attr;
    uint64_t bits;
    int waiters, deleted, cancel_generation;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    char name[32];
} EventFlag;

static ABI int32_t ef_create(EventFlag **out,const char *name,uint32_t attr,uint64_t init,const void *param) {
    (void)param;
    if (!out || (attr & ~UINT32_C(0x33))) return KERR_EINVAL;
    EventFlag *ef=calloc(1,sizeof(*ef));
    if (!ef) return KERR_ENOMEM;
    ef->magic=EF_MAGIC; ef->attr=attr; ef->bits=init;
    pthread_mutex_init(&ef->lock,NULL);
    pthread_cond_init(&ef->cond,NULL);
    snprintf(ef->name,sizeof(ef->name),"%s",name ? name : "");
    *out=ef; return 0;
}
static int ef_valid(const EventFlag *ef) { return ef && ef->magic==EF_MAGIC && !ef->deleted; }
static ABI int32_t ef_delete(EventFlag *ef) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    ef->deleted=1;
    pthread_cond_broadcast(&ef->cond);
    int busy=ef->waiters;
    pthread_mutex_unlock(&ef->lock);
    /* Waiters return ESRCH; the object is leaked if any were blocked, since they still read it. */
    if (!busy) { ef->magic=0; pthread_cond_destroy(&ef->cond); pthread_mutex_destroy(&ef->lock); free(ef); }
    return 0;
}
static ABI int32_t ef_set(EventFlag *ef,uint64_t bits) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    ef->bits|=bits;
    pthread_cond_broadcast(&ef->cond);
    pthread_mutex_unlock(&ef->lock);
    return 0;
}
static ABI int32_t ef_clear(EventFlag *ef,uint64_t mask) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    ef->bits&=mask;
    pthread_mutex_unlock(&ef->lock);
    return 0;
}
/* sceKernelCancelEventFlag: the waiters return ECANCELED, the bits become `pattern`. */
static ABI int32_t ef_cancel(EventFlag *ef,uint64_t pattern,int32_t *waiters) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    if (waiters) *waiters=ef->waiters;
    ef->bits=pattern;
    ++ef->cancel_generation;
    pthread_cond_broadcast(&ef->cond);
    pthread_mutex_unlock(&ef->lock);
    return 0;
}
static int ef_match(uint64_t bits,uint64_t pattern,uint32_t mode) {
    return (mode & EF_WAIT_AND) ? (bits & pattern)==pattern : (bits & pattern)!=0;
}
static void ef_consume(EventFlag *ef,uint64_t pattern,uint32_t mode,uint64_t *result) {
    if (result) *result=ef->bits;
    if (mode & EF_CLEAR_ALL) ef->bits=0;
    else if (mode & EF_CLEAR_PAT) ef->bits&=~pattern;
}
static int ef_mode_ok(uint32_t mode) {
    uint32_t wait=mode & 3, clear=mode & 0x30;
    return (wait==EF_WAIT_AND || wait==EF_WAIT_OR) && clear!=0x30 && !(mode & ~UINT32_C(0x33));
}
static ABI int32_t ef_poll(EventFlag *ef,uint64_t pattern,uint32_t mode,uint64_t *result) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    if (!pattern || !ef_mode_ok(mode)) return KERR_EINVAL;
    pthread_mutex_lock(&ef->lock);
    int32_t r=KERR_EBUSY;
    if (!(ef->waiters && !(ef->attr & EF_ATTR_MULTI)) && ef_match(ef->bits,pattern,mode)) {
        ef_consume(ef,pattern,mode,result); r=0;
    } else if (result) *result=ef->bits;
    pthread_mutex_unlock(&ef->lock);
    return r;
}
static ABI int32_t ef_wait(EventFlag *ef,uint64_t pattern,uint32_t mode,uint64_t *result,uint32_t *timeout_us) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    if (!pattern || !ef_mode_ok(mode)) return KERR_EINVAL;
    pthread_mutex_lock(&ef->lock);
    if (ef->waiters && !(ef->attr & EF_ATTR_MULTI)) { pthread_mutex_unlock(&ef->lock); return KERR_EPERM; }
    struct timespec start, deadline;
    clock_gettime(CLOCK_REALTIME,&start);
    if (timeout_us) {
        uint64_t ns=(uint64_t)start.tv_nsec+(uint64_t)*timeout_us*1000;
        deadline.tv_sec=start.tv_sec+(time_t)(ns/1000000000); deadline.tv_nsec=(long)(ns%1000000000);
    }
    int32_t r=0;
    const int generation=ef->cancel_generation;
    ++ef->waiters;
    while (!ef->deleted && ef->cancel_generation==generation && !ef_match(ef->bits,pattern,mode)) {
        int e=timeout_us ? pthread_cond_timedwait(&ef->cond,&ef->lock,&deadline) : pthread_cond_wait(&ef->cond,&ef->lock);
        if (e==ETIMEDOUT) { r=KERR_ETIMEDOUT; break; }
    }
    --ef->waiters;
    if (ef->deleted) r=KERR_ESRCH;
    else if (ef->cancel_generation!=generation) { r=KERR_ECANCELED; if (result) *result=ef->bits; }
    else if (!r) ef_consume(ef,pattern,mode,result);
    else if (result) *result=ef->bits;
    pthread_mutex_unlock(&ef->lock);
    if (timeout_us) {
        if (r==KERR_ETIMEDOUT) *timeout_us=0;
        else {
            struct timespec now; clock_gettime(CLOCK_REALTIME,&now);
            int64_t spent=(int64_t)(now.tv_sec-start.tv_sec)*1000000+(now.tv_nsec-start.tv_nsec)/1000;
            *timeout_us=spent>=(int64_t)*timeout_us ? 0 : (uint32_t)(*timeout_us-spent);
        }
    }
    return r;
}

static const RuntimeExport exports[] = {
    {"sceKernelCreateEventFlag", ef_create}, {"sceKernelDeleteEventFlag", ef_delete},
    {"sceKernelSetEventFlag", ef_set}, {"sceKernelClearEventFlag", ef_clear},
    {"sceKernelPollEventFlag", ef_poll}, {"sceKernelWaitEventFlag", ef_wait},
    {"sceKernelCancelEventFlag", ef_cancel},
};
uintptr_t runtime_eventflag_resolve(const char *name) { return RUNTIME_LOOKUP(exports, name); }
