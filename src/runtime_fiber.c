/* libSceFiber on Win32 fibers (adapted from HayatoG/gr2_pc) or, on Linux, on stacks from runtime_stack_acquire switched by
 * osl_fiber_switch. Fibers share their thread's TEB / GS base, so the guest TCB stays per thread as on the PS4. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define FIBER_MAGIC UINT64_C(0x5245424946434e44) /* "OSLFIBER" */
#define FIBER_OPT_MAGIC UINT32_C(0xbb40e64d)
#define FIBER_ERR_NULL ((int32_t)0x80590001)
#define FIBER_ERR_ALIGNMENT ((int32_t)0x80590002)
#define FIBER_ERR_INVALID ((int32_t)0x80590004)
#define FIBER_ERR_PERMISSION ((int32_t)0x80590005)
#define FIBER_ERR_STATE ((int32_t)0x80590006)
/* Guest code has no stack probes, so the whole stack is committed up front; host runtime calls
 * made from a fiber need more room than the game's own context size. */
#define FIBER_MIN_STACK (256u * 1024u)

typedef void (ABI *FiberEntry)(uint64_t arg_on_initialize, uint64_t arg_on_run);
typedef struct HostFiber {
#ifdef _WIN32
    void *fiber;
#else
    void *stack, *sp; /* sp: saved stack pointer while the fiber is not running */
    size_t stack_size;
#endif
    void *guest;
    FiberEntry entry;
    uint64_t arg_on_initialize, arg_to_deliver;
    uint64_t *resume_out; /* where this fiber's own Switch/ReturnToThread stores its next argOnRun */
    int started, running;
    char name[32];
} HostFiber;
/* The guest SceFiber is opaque to the game; its first 16 bytes hold the magic and the host object. */
typedef struct { uint64_t magic; HostFiber *host; } GuestFiber;
static __thread struct {
#ifdef _WIN32
    void *thread_fiber;
#else
    void *thread_sp;
#endif
    HostFiber *current;
    uint64_t *thread_return_out;
} fiber_tls;
static unsigned long fibers_created, fiber_runs, fiber_switches;

static HostFiber *fiber_of(void *guest) {
    GuestFiber *g = guest;
    return g && g->magic == FIBER_MAGIC ? g->host : NULL;
}
static void fiber_returned(HostFiber *f) {
    fprintf(stderr, "STOP: fiber %s returned from its entry function\n", f->name);
    fflush(NULL);
    _exit(22);
}

#ifdef _WIN32
static void WINAPI fiber_trampoline(void *param) {
    HostFiber *f = param;
    f->entry(f->arg_on_initialize, f->arg_to_deliver);
    fiber_returned(f);
}
static int host_fiber_create(HostFiber *f, size_t size) {
    f->fiber = CreateFiberEx(size, size, 0, fiber_trampoline, f);
    return f->fiber != NULL;
}
static void host_fiber_destroy(HostFiber *f) { DeleteFiber(f->fiber); }
static int host_thread_prepare(void) {
    if (!fiber_tls.thread_fiber) {
        fiber_tls.thread_fiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(NULL);
    }
    return fiber_tls.thread_fiber != NULL;
}
static void host_run_fiber(HostFiber *to) { SwitchToFiber(to->fiber); }
static void host_switch_fiber(HostFiber *from, HostFiber *to) { (void)from; SwitchToFiber(to->fiber); }
static void host_return_to_thread(HostFiber *from) { (void)from; SwitchToFiber(fiber_tls.thread_fiber); }
#else
/* osl_fiber_switch(&save, to): pushes the callee-saved registers and the SSE/x87 control words on the current stack,
 * stores its pointer in *save and continues on `to`, a stack prepared the same way. */
void osl_fiber_switch(void **save, void *to);
void osl_fiber_start(void);
__asm__(".text\n.globl osl_fiber_switch\n.type osl_fiber_switch,@function\nosl_fiber_switch:\n"
        " push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " sub $8,%rsp\n stmxcsr (%rsp)\n fnstcw 4(%rsp)\n"
        " mov %rsp,(%rdi)\n mov %rsi,%rsp\n"
        " ldmxcsr (%rsp)\n fldcw 4(%rsp)\n add $8,%rsp\n"
        " pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n"
        ".size osl_fiber_switch,.-osl_fiber_switch\n"
        ".globl osl_fiber_start\n.type osl_fiber_start,@function\nosl_fiber_start:\n"
        " mov %rbx,%rdi\n call *%r12\n ud2\n"
        ".size osl_fiber_start,.-osl_fiber_start\n");

static void fiber_main(HostFiber *f) {
    f->entry(f->arg_on_initialize, f->arg_to_deliver);
    fiber_returned(f);
}
static int host_fiber_create(HostFiber *f, size_t size) {
    f->stack_size = (size + 16383) & ~(size_t)16383;
    f->stack = runtime_stack_acquire(f->stack_size);
    if (!f->stack) return 0;
    /* The first switch pops the control words, r15-r12 (r12: fiber_main), rbx (the fiber) and rbp (0 ends frame-pointer
     * walks), then returns into osl_fiber_start. */
    uint64_t *top = (uint64_t *)((char *)f->stack + f->stack_size);
    top -= 2; /* rsp stays 16-byte aligned when osl_fiber_start calls fiber_main */
    *--top = (uint64_t)(uintptr_t)osl_fiber_start;
    *--top = 0;
    *--top = (uint64_t)(uintptr_t)f;
    *--top = (uint64_t)(uintptr_t)fiber_main;
    for (int zero = 0; zero < 3; ++zero) *--top = 0;
    const uint32_t control[2] = {0x1f80, 0x37f}; /* default MXCSR and x87 control word */
    memcpy(--top, control, sizeof(control));
    f->sp = top;
    return 1;
}
static void host_fiber_destroy(HostFiber *f) { runtime_stack_release(f->stack, f->stack_size); }
static int host_thread_prepare(void) { return 1; }
static void host_run_fiber(HostFiber *to) { osl_fiber_switch(&fiber_tls.thread_sp, to->sp); }
static void host_switch_fiber(HostFiber *from, HostFiber *to) { osl_fiber_switch(&from->sp, to->sp); }
static void host_return_to_thread(HostFiber *from) { osl_fiber_switch(&from->sp, fiber_tls.thread_sp); }
#endif

static ABI int32_t opt_param_initialize(uint32_t *param) {
    if (!param) return FIBER_ERR_NULL;
    if ((uintptr_t)param & 7) return FIBER_ERR_ALIGNMENT;
    *param = FIBER_OPT_MAGIC;
    return 0;
}
static ABI int32_t fiber_initialize(void *guest, const char *name, FiberEntry entry, uint64_t arg_on_initialize,
                                    void *context, uint64_t context_size, const void *option, uint32_t build_version) {
    (void)context; (void)option; (void)build_version;
    if (!guest || !entry) return FIBER_ERR_NULL;
    if ((uintptr_t)guest & 7) return FIBER_ERR_ALIGNMENT;
    HostFiber *f = calloc(1, sizeof(*f));
    if (!f) return FIBER_ERR_INVALID;
    f->guest = guest; f->entry = entry; f->arg_on_initialize = arg_on_initialize;
    snprintf(f->name, sizeof(f->name), "%s", name ? name : "");
    size_t size = context_size < FIBER_MIN_STACK ? FIBER_MIN_STACK : (size_t)context_size;
    if (!host_fiber_create(f, size)) { free(f); return FIBER_ERR_INVALID; }
    ((GuestFiber *)guest)->magic = FIBER_MAGIC;
    ((GuestFiber *)guest)->host = f;
    if (__atomic_fetch_add(&fibers_created, 1, __ATOMIC_RELAXED) == 0) printf("Runtime: first fiber '%s', context %llu bytes\n", f->name, (unsigned long long)context_size);
    return 0;
}
static ABI int32_t fiber_finalize(void *guest) {
    HostFiber *f = fiber_of(guest);
    if (!guest) return FIBER_ERR_NULL;
    if (!f) return FIBER_ERR_INVALID;
    if (f->running) return FIBER_ERR_STATE;
    ((GuestFiber *)guest)->magic = 0;
    host_fiber_destroy(f);
    free(f);
    return 0;
}
static void fiber_enter(HostFiber *f, uint64_t arg) {
    if (f->started && f->resume_out) *f->resume_out = arg;
    f->arg_to_deliver = arg;
    f->started = 1; f->running = 1;
    fiber_tls.current = f;
}
static ABI int32_t fiber_run(void *guest, uint64_t arg_on_run_to, uint64_t *arg_on_return) {
    HostFiber *f = fiber_of(guest);
    if (!guest) return FIBER_ERR_NULL;
    if (!f) return FIBER_ERR_INVALID;
    if (fiber_tls.current) return FIBER_ERR_PERMISSION; /* only a thread, not a fiber, runs a fiber */
    if (f->running) return FIBER_ERR_STATE;
    if (!host_thread_prepare()) return FIBER_ERR_INVALID;
    fiber_tls.thread_return_out = arg_on_return;
    __atomic_add_fetch(&fiber_runs, 1, __ATOMIC_RELAXED);
    fiber_enter(f, arg_on_run_to);
    host_run_fiber(f);
    fiber_tls.current = NULL;
    return 0;
}
static ABI int32_t fiber_switch(void *guest, uint64_t arg_on_run_to, uint64_t *arg_on_run) {
    HostFiber *to = fiber_of(guest), *from = fiber_tls.current;
    if (!guest) return FIBER_ERR_NULL;
    if (!to) return FIBER_ERR_INVALID;
    if (!from) return FIBER_ERR_PERMISSION;
    if (to->running) return FIBER_ERR_STATE;
    from->resume_out = arg_on_run; from->running = 0;
    __atomic_add_fetch(&fiber_switches, 1, __ATOMIC_RELAXED);
    fiber_enter(to, arg_on_run_to);
    host_switch_fiber(from, to);
    return 0;
}
static ABI int32_t fiber_return_to_thread(uint64_t arg_on_return, uint64_t *arg_on_run) {
    HostFiber *from = fiber_tls.current;
    if (!from) return FIBER_ERR_PERMISSION;
    if (fiber_tls.thread_return_out) *fiber_tls.thread_return_out = arg_on_return;
    from->resume_out = arg_on_run; from->running = 0;
    fiber_tls.current = NULL;
    host_return_to_thread(from);
    return 0;
}
static ABI int32_t fiber_get_self(void **out) {
    if (!out) return FIBER_ERR_NULL;
    if (!fiber_tls.current) return FIBER_ERR_PERMISSION;
    *out = fiber_tls.current->guest;
    return 0;
}

static const RuntimeExport exports[] = {
    {"sceFiberOptParamInitialize", opt_param_initialize}, {"_sceFiberInitializeImpl", fiber_initialize},
    {"sceFiberFinalize", fiber_finalize}, {"sceFiberRun", fiber_run}, {"sceFiberSwitch", fiber_switch},
    {"sceFiberReturnToThread", fiber_return_to_thread}, {"sceFiberGetSelf", fiber_get_self},
};
uintptr_t runtime_fiber_resolve(const char *name) { return RUNTIME_LOOKUP(exports, name); }
void runtime_fiber_report(void) {
    printf("Runtime: fibers created=%lu, runs=%lu, switches=%lu\n", fibers_created, fiber_runs, fiber_switches);
}
