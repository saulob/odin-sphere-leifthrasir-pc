/* Focused tests of the runtime's locks and of the guest memory table: no game, no GPU, no host
 * mappings. The memory table's internals are tested directly (this file includes runtime_memory.c);
 * guest rwlocks and mutexes through their libkernel entry points. A deadlock fails the test after a
 * timeout and ends the process. */
#include "../src/runtime_memory.c"
#include <malloc.h>

static int failures;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL line %d: %s\n", __LINE__, #cond);              \
            __atomic_add_fetch(&failures, 1, __ATOMIC_RELAXED);         \
        }                                                               \
    } while (0)

/* runtime.c is not linked: tables are looked up by symbol name instead of NID. */
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) {
    for (size_t i=0; i<count; ++i) if (!strcmp(table[i].name,nid)) return (uintptr_t)table[i].function;
    return 0;
}
int32_t *runtime_errno(void) {
    static __thread int32_t value;
    return &value;
}
#ifdef _WIN32
void runtime_log_flush(void) {}
#endif

static void sleep_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec t={ms/1000,(long)(ms%1000)*1000000};
    nanosleep(&t,NULL);
#endif
}
/* 1 once *flag is set, 0 after ms milliseconds. */
static int wait_for(const int *flag, unsigned ms) {
    for (unsigned waited=0; !__atomic_load_n(flag,__ATOMIC_ACQUIRE); waited+=2) {
        if (waited>=ms) return 0;
        sleep_ms(2);
    }
    return 1;
}
static void set_flag(int *flag) { __atomic_store_n(flag,1,__ATOMIC_RELEASE); }
static void deadlock(const char *what) {
    printf("FAIL: deadlock: %s\n",what);
    fflush(stdout);
    _exit(1);
}
static pthread_t start(void *(*body)(void *), void *argument) {
    pthread_t thread;
    if (pthread_create(&thread,NULL,body,argument)) { puts("FAIL: pthread_create"); _exit(1); }
    return thread;
}

/* ---- guest memory table ---- */
#define BASE UINT64_C(0x2000000000)

static void test_split_keeps_backing_offsets(void) {
    const int kinds[]={KIND_DIRECT,KIND_FLEXIBLE};
    for (int k=0; k<2; ++k) {
        vma_count=0;
        CHECK(!vma_insert(0,(Vma){BASE,BASE+4*PAGE,kinds[k],3,0,8*PAGE}));
        CHECK(!split_at(BASE+PAGE));
        CHECK(vma_count==2 && vmas[0].phys==8*PAGE && vmas[1].phys==9*PAGE);
        int error;
        size_t i=carve(BASE+2*PAGE,BASE+3*PAGE,&error);
        CHECK(!error && vma_count==4 && vmas[i].start==BASE+2*PAGE);
        CHECK(vmas[i].phys==10*PAGE && vmas[i+1].phys==11*PAGE);
    }
    vma_count=0;
    CHECK(!vma_insert(0,(Vma){BASE,BASE+2*PAGE,KIND_RESERVED,0,0,0}));
    CHECK(!split_at(BASE+PAGE) && vma_count==2 && vmas[1].phys==0);
    vma_count=0;
}

static void ignore_range(uintptr_t address, uint64_t size) { (void)address; (void)size; }
static void test_unchanged_protection(void) {
    vma_count=0;
    CHECK(!vma_insert(0,(Vma){BASE,BASE+2*PAGE,KIND_DIRECT,0x33,3,0}));
    CHECK(!vma_insert(1,(Vma){BASE+2*PAGE,BASE+3*PAGE,KIND_FLEXIBLE,0x33,0,POOL_SIZE}));
    CHECK(unchanged(BASE,BASE+3*PAGE,0x33,-1));
    CHECK(unchanged(BASE,BASE+2*PAGE,0x33,3));
    CHECK(!unchanged(BASE,BASE+3*PAGE,0x33,3));
    CHECK(!unchanged(BASE+PAGE,BASE+3*PAGE,0x13,-1));
#ifdef _WIN32
    /* No host view exists here, so only the GPU notification shows whether the protect was applied. */
    hook_invalidate=ignore_range;
    write_lock();
    CHECK(protect_locked(BASE,3*PAGE,0x33,-1)==0 && pending_count==0);
    CHECK(protect_locked(BASE,PAGE,0x13,-1)==0 && pending_count==1);
    CHECK(protect_locked(BASE,PAGE,0x13,-1)==0 && pending_count==1);
    pending_count=0;
    write_unlock();
    hook_invalidate=NULL;
#else
    (void)ignore_range;
#endif
    vma_count=0;
}

/* ---- memory table lock ---- */
static int reader_holds, reader_done, writer_done;
static void *nested_reader(void *unused) {
    (void)unused;
    read_lock();
    set_flag(&reader_holds);
    while (!(__atomic_load_n(&rw_state,__ATOMIC_ACQUIRE)&RW_WAITING)) sleep_ms(1);
    /* As under a GPU page fault taken while holding the table: a writer is queued. */
    read_lock();
    read_unlock();
    read_unlock();
    set_flag(&reader_done);
    return NULL;
}
static void *queued_writer(void *unused) {
    (void)unused;
    write_lock();
    write_unlock();
    set_flag(&writer_done);
    return NULL;
}
static void test_table_lock_nested_reader(void) {
    pthread_t reader=start(nested_reader,NULL);
    if (!wait_for(&reader_holds,2000)) deadlock("first read lock");
    pthread_t writer=start(queued_writer,NULL);
    if (!wait_for(&reader_done,2000)) deadlock("nested read lock behind a queued writer");
    if (!wait_for(&writer_done,2000)) deadlock("writer after the readers left");
    pthread_join(reader,NULL);
    pthread_join(writer,NULL);
}

static int second_writer_done;
static void *second_writer(void *unused) {
    (void)unused;
    write_lock();
    write_unlock();
    set_flag(&second_writer_done);
    return NULL;
}
static void test_table_lock_writer_after_writer(void) {
    write_lock();
    pthread_t writer=start(second_writer,NULL);
    while (!(__atomic_load_n(&rw_state,__ATOMIC_ACQUIRE)&RW_WAITING)) sleep_ms(1);
    write_unlock();
    if (!wait_for(&second_writer_done,2000)) deadlock("a writer waiting for another writer");
    pthread_join(writer,NULL);
}

static int inside_readers, inside_writers, stress_errors;
static void *stress(void *seed) {
    unsigned x=(unsigned)(uintptr_t)seed;
    for (int i=0; i<200000; ++i) {
        x=x*1103515245u+12345u;
        if ((x>>16)%8==0) {
            write_lock();
            if (__atomic_add_fetch(&inside_writers,1,__ATOMIC_ACQ_REL)!=1 || __atomic_load_n(&inside_readers,__ATOMIC_ACQUIRE))
                __atomic_add_fetch(&stress_errors,1,__ATOMIC_RELAXED);
            __atomic_sub_fetch(&inside_writers,1,__ATOMIC_ACQ_REL);
            write_unlock();
        } else {
            read_lock();
            __atomic_add_fetch(&inside_readers,1,__ATOMIC_ACQ_REL);
            if (__atomic_load_n(&inside_writers,__ATOMIC_ACQUIRE)) __atomic_add_fetch(&stress_errors,1,__ATOMIC_RELAXED);
            __atomic_sub_fetch(&inside_readers,1,__ATOMIC_ACQ_REL);
            read_unlock();
        }
    }
    return NULL;
}
static int stress_done;
static void *stress_all(void *unused) {
    (void)unused;
    pthread_t threads[6];
    for (int i=0; i<6; ++i) threads[i]=start(stress,(void *)(uintptr_t)(i+1));
    for (int i=0; i<6; ++i) pthread_join(threads[i],NULL);
    set_flag(&stress_done);
    return NULL;
}
static void test_table_lock_stress(void) {
    pthread_t runner=start(stress_all,NULL);
    if (!wait_for(&stress_done,60000)) deadlock("readers and writers under load");
    pthread_join(runner,NULL);
    CHECK(stress_errors==0 && rw_state==0);
}

/* ---- guest rwlocks (libkernel scePthreadRwlock*) ---- */
typedef struct { int64_t seconds, nanoseconds; } GuestTime;
typedef int32_t (ABI *RwInit)(void **, void *const *, const char *);
typedef int32_t (ABI *RwOp)(void **);
typedef int32_t (ABI *RwTimed)(void **, const GuestTime *);
#define EBUSY_CODE ((int32_t)UINT32_C(0x80020010))
#define ETIMEDOUT_CODE ((int32_t)UINT32_C(0x8002003c))
static RwOp rw_rdlock, rw_wrlock, rw_tryrdlock, rw_unlock;
static RwTimed rw_timedwrlock;
static void *guest_lock;
static int a_holds, a_go, a_nested, a_release, a_done, w_done;
static void *recursive_reader(void *unused) {
    (void)unused;
    CHECK(rw_rdlock(&guest_lock)==0);
    set_flag(&a_holds);
    wait_for(&a_go,10000);
    CHECK(rw_rdlock(&guest_lock)==0); /* FreeBSD: a thread holding a read lock passes a waiting writer */
    set_flag(&a_nested);
    wait_for(&a_release,10000);
    CHECK(rw_unlock(&guest_lock)==0 && rw_unlock(&guest_lock)==0);
    set_flag(&a_done);
    return NULL;
}
static void *waiting_writer(void *unused) {
    (void)unused;
    CHECK(rw_wrlock(&guest_lock)==0);
    CHECK(rw_unlock(&guest_lock)==0);
    set_flag(&w_done);
    return NULL;
}
static int32_t try_result;
static void *other_reader(void *unused) {
    (void)unused;
    try_result=rw_tryrdlock(&guest_lock);
    if (!try_result) rw_unlock(&guest_lock);
    return NULL;
}
static void *timed_writer(void *unused) {
    (void)unused;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME,&now);
    int64_t ns=(int64_t)now.tv_nsec+100000000;
    const GuestTime deadline={(int64_t)now.tv_sec+ns/1000000000,ns%1000000000};
    CHECK(rw_timedwrlock(&guest_lock,&deadline)==ETIMEDOUT_CODE);
    return NULL;
}
static void test_guest_rwlock(void) {
    RwInit init=(RwInit)runtime_rwlock_resolve("6ULAa0fq4jA#libkernel");
    rw_rdlock=(RwOp)runtime_rwlock_resolve("Ox9i0c7L5w0#libkernel");
    rw_wrlock=(RwOp)runtime_rwlock_resolve("mqdNorrB+gI#libkernel");
    rw_tryrdlock=(RwOp)runtime_rwlock_resolve("XD3mDeybCnk#libkernel");
    rw_unlock=(RwOp)runtime_rwlock_resolve("+L98PIbGttk#libkernel");
    rw_timedwrlock=(RwTimed)runtime_rwlock_resolve("adh--6nIqTk#libkernel");
    CHECK(init && rw_rdlock && rw_wrlock && rw_tryrdlock && rw_unlock && rw_timedwrlock);
    if (!init || init(&guest_lock,NULL,"test")) return;

    pthread_t a=start(recursive_reader,NULL);
    if (!wait_for(&a_holds,2000)) deadlock("guest read lock");
    pthread_t w=start(waiting_writer,NULL);
    /* Once the writer queues, it keeps out readers that hold nothing. */
    for (unsigned waited=0; waited<2000; waited+=10) {
        pthread_join(start(other_reader,NULL),NULL);
        if (try_result==EBUSY_CODE) break;
        sleep_ms(10);
    }
    CHECK(try_result==EBUSY_CODE);
    set_flag(&a_go);
    if (!wait_for(&a_nested,2000)) deadlock("recursive guest read lock behind a waiting writer");
    set_flag(&a_release);
    if (!wait_for(&a_done,2000) || !wait_for(&w_done,2000)) deadlock("guest writer after the readers");
    pthread_join(a,NULL);
    pthread_join(w,NULL);

    /* A writer that times out no longer keeps readers out. */
    CHECK(rw_rdlock(&guest_lock)==0);
    pthread_join(start(timed_writer,NULL),NULL);
    pthread_join(start(other_reader,NULL),NULL);
    CHECK(try_result==0);
    CHECK(rw_unlock(&guest_lock)==0);
}

/* ---- guest mutexes ---- */
static void test_guest_mutex_object(void) {
    typedef int32_t (ABI *MutexInit)(void **, void **, const char *);
    MutexInit init=(MutexInit)runtime_mutex_resolve("cmo1RIYva9o#libkernel");
    RwOp lock=(RwOp)runtime_mutex_resolve("9UK1vLZQft4#libkernel");
    RwOp unlock=(RwOp)runtime_mutex_resolve("tn3VlD0hG60#libkernel");
    RwOp destroy=(RwOp)runtime_mutex_resolve("2Of0f+3mhhE#libkernel");
    CHECK(init && lock && unlock && destroy);
    if (!init) return;
    void *mutex=NULL;
    CHECK(init(&mutex,NULL,"test")==0 && mutex);
#ifdef _WIN32
    CHECK(_msize(mutex)>=0x100);
#else
    CHECK(malloc_usable_size(mutex)>=0x100);
#endif
    /* What the game's libc does right after scePthreadMutexInit. */
    *(volatile uint32_t *)((char *)mutex+0x20)|=0x100;
    CHECK(lock(&mutex)==0 && unlock(&mutex)==0 && destroy(&mutex)==0);
}

/* ---- event flags (libkernel sceKernel*EventFlag) ---- */
typedef int32_t (ABI *EfCreate)(void **, const char *, uint32_t, uint64_t, const void *);
typedef int32_t (ABI *EfHandleOp)(void *);
typedef int32_t (ABI *EfSet)(void *, uint64_t);
typedef int32_t (ABI *EfPoll)(void *, uint64_t, uint32_t, uint64_t *);
typedef int32_t (ABI *EfWait)(void *, uint64_t, uint32_t, uint64_t *, uint32_t *);
typedef int32_t (ABI *EfCancel)(void *, uint64_t, int32_t *);
#define EPERM_CODE ((int32_t)UINT32_C(0x80020001))
#define ECANCELED_CODE ((int32_t)UINT32_C(0x80020055))
#define EF_SINGLE 0x10
#define EF_MULTI 0x20
#define EF_AND 0x01
#define EF_CLEAR_ALL 0x10
static EfCreate ef_create_fn;
static EfHandleOp ef_delete_fn;
static EfSet ef_set_fn;
static EfPoll ef_poll_fn;
static EfWait ef_wait_fn;
static EfCancel ef_cancel_fn;
static int resolve_event_flags(void) {
    ef_create_fn=(EfCreate)runtime_eventflag_resolve("sceKernelCreateEventFlag");
    ef_delete_fn=(EfHandleOp)runtime_eventflag_resolve("sceKernelDeleteEventFlag");
    ef_set_fn=(EfSet)runtime_eventflag_resolve("sceKernelSetEventFlag");
    ef_poll_fn=(EfPoll)runtime_eventflag_resolve("sceKernelPollEventFlag");
    ef_wait_fn=(EfWait)runtime_eventflag_resolve("sceKernelWaitEventFlag");
    ef_cancel_fn=(EfCancel)runtime_eventflag_resolve("sceKernelCancelEventFlag");
    return ef_create_fn && ef_delete_fn && ef_set_fn && ef_poll_fn && ef_wait_fn && ef_cancel_fn;
}
/* Polls until a single-waiter flag reports its waiter, as the game does. */
static int32_t poll_until_parked(void *flag, uint64_t pattern, uint32_t mode) {
    int32_t r=0;
    for (unsigned waited=0; waited<2000; ++waited) {
        if ((r=ef_poll_fn(flag,pattern,mode,NULL))==EPERM_CODE) break;
        sleep_ms(1);
    }
    return r;
}
typedef struct {
    void *flag;
    uint64_t pattern, result;
    uint32_t mode, timeout;
    int use_timeout;
    int32_t status;
} EfWaitCall;
static void *ef_waiter(void *p) {
    EfWaitCall *call=p;
    call->status=ef_wait_fn(call->flag,call->pattern,call->mode,&call->result,call->use_timeout ? &call->timeout : NULL);
    return NULL;
}
static void test_event_flag_poll_reports_parked_waiter(void) {
    void *flag=NULL;
    CHECK(ef_create_fn(&flag,"single",EF_SINGLE,1,NULL)==0);
    uint64_t result=0;
    CHECK(ef_poll_fn(flag,1,EF_AND,&result)==0 && result==1);

    EfWaitCall call={.flag=flag,.pattern=2,.mode=EF_AND|EF_CLEAR_ALL};
    pthread_t waiter=start(ef_waiter,&call);
    CHECK(poll_until_parked(flag,1,EF_AND)==EPERM_CODE);
    result=0xdead;
    CHECK(ef_poll_fn(flag,1,EF_AND,&result)==EPERM_CODE && result==0xdead);
    CHECK(ef_wait_fn(flag,1,EF_AND,NULL,NULL)==EPERM_CODE);

    /* The setter releases the waiter and applies its clear before the waiter runs. */
    CHECK(ef_set_fn(flag,2)==0);
    CHECK(ef_poll_fn(flag,1,EF_AND,&result)==EBUSY_CODE && result==0);
    pthread_join(waiter,NULL);
    CHECK(call.status==0 && call.result==3);
    CHECK(ef_delete_fn(flag)==0);

    CHECK(ef_create_fn(&flag,"multi",EF_MULTI,1,NULL)==0);
    EfWaitCall other={.flag=flag,.pattern=4,.mode=EF_AND};
    waiter=start(ef_waiter,&other);
    sleep_ms(50);
    CHECK(ef_poll_fn(flag,1,EF_AND,NULL)==0);
    CHECK(ef_set_fn(flag,4)==0);
    pthread_join(waiter,NULL);
    CHECK(other.status==0 && other.result==5);
    CHECK(ef_delete_fn(flag)==0);
}
/* The game's LIGHTCALC handshake: the main thread waits for its worker to park, then hands it a job. */
static void *light_flag;
static int light_quit, light_jobs, light_done, light_exit;
static void *light_worker(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&light_quit,__ATOMIC_ACQUIRE)) {
        CHECK(ef_wait_fn(light_flag,2,EF_AND|EF_CLEAR_ALL,NULL,NULL)==0);
        __atomic_add_fetch(&light_done,1,__ATOMIC_ACQ_REL);
        CHECK(ef_set_fn(light_flag,1)==0);
    }
    set_flag(&light_exit);
    return NULL;
}
static void test_event_flag_worker_handshake(void) {
    CHECK(ef_create_fn(&light_flag,"LIGHTCALC",EF_SINGLE,1,NULL)==0);
    pthread_t worker=start(light_worker,NULL);
    for (int frame=0; frame<500; ++frame) {
        int32_t r;
        unsigned spins=0;
        while ((r=ef_poll_fn(light_flag,1,EF_AND,NULL))==EBUSY_CODE || r==0) {
            if (++spins%1024) continue;
            if (spins>1024u*2000) deadlock("LIGHTCALC worker never reported parked");
            sleep_ms(1);
        }
        CHECK(r==EPERM_CODE);
        CHECK(__atomic_load_n(&light_done,__ATOMIC_ACQUIRE)==light_jobs);
        ++light_jobs;
        CHECK(ef_set_fn(light_flag,2)==0);
    }
    __atomic_store_n(&light_quit,1,__ATOMIC_RELEASE);
    for (unsigned waited=0; !__atomic_load_n(&light_exit,__ATOMIC_ACQUIRE); ++waited) {
        if (waited>2000) deadlock("LIGHTCALC worker teardown");
        if (ef_poll_fn(light_flag,1,EF_AND,NULL)==EPERM_CODE) ef_set_fn(light_flag,2);
        sleep_ms(1);
    }
    pthread_join(worker,NULL);
    CHECK(light_done>=light_jobs);
    CHECK(ef_delete_fn(light_flag)==0);
}
static void test_event_flag_timeout_and_cancel(void) {
    void *flag=NULL;
    CHECK(ef_create_fn(&flag,"single",EF_SINGLE,0,NULL)==0);
    EfWaitCall timed={.flag=flag,.pattern=1,.mode=EF_AND,.timeout=50000,.use_timeout=1};
    pthread_join(start(ef_waiter,&timed),NULL);
    CHECK(timed.status==ETIMEDOUT_CODE && timed.timeout==0);
    CHECK(ef_poll_fn(flag,1,EF_AND,NULL)==EBUSY_CODE);

    EfWaitCall canceled={.flag=flag,.pattern=1,.mode=EF_AND};
    pthread_t waiter=start(ef_waiter,&canceled);
    CHECK(poll_until_parked(flag,1,EF_AND)==EPERM_CODE);
    int32_t count=-1;
    CHECK(ef_cancel_fn(flag,8,&count)==0 && count==1);
    pthread_join(waiter,NULL);
    CHECK(canceled.status==ECANCELED_CODE && canceled.result==8);
    CHECK(ef_poll_fn(flag,8,EF_AND,NULL)==0);
    CHECK(ef_delete_fn(flag)==0);
}

int main(void) {
    test_split_keeps_backing_offsets();
    test_unchanged_protection();
    test_table_lock_nested_reader();
    test_table_lock_writer_after_writer();
    test_table_lock_stress();
    test_guest_rwlock();
    test_guest_mutex_object();
    if (!resolve_event_flags()) { puts("FAIL: event flag entry points"); return 1; }
    test_event_flag_poll_reports_parked_waiter();
    test_event_flag_worker_handshake();
    test_event_flag_timeout_and_cancel();
    if (failures) {
        printf("%d check(s) failed\n",failures);
        return 1;
    }
    puts("runtime sync tests: all passed");
    return 0;
}
