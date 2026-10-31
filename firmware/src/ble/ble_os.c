/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* A cooperative OS for the JieLi BT libraries. Tasks switch only inside the blocking calls below; the core's main
 * loop calls ble_os_service() to run them, so nothing ever preempts the core. Interrupt handlers may post to
 * queues and semaphores (they only mark the waiter ready). */
#include "ble_os.h"
#ifdef BLE_OS_HOST
#include <ucontext.h>
#define BLE_POOL
#else
#include "fm1_ctx.h"
#define BLE_POOL __attribute__((section(".pool")))
#endif

#define NTASK 3                                    /* app_core, btctrler, btstack: the libraries create no other
                                                    * (SDK demo_ble task_info_table); a 4th slot's 2.5 KB went to the heap */
#define QWORDS 96                                  /* message words per task queue */
#define NTIMER 16
#define STACK_WORDS BLE_STACK_WORDS

typedef struct {
    const char *name;
    void (*fn)(void *);
    void *arg;
    uint32_t q[QWORDS], qh, qt;                    /* ring of message words: [len, type, args...] */
    void *wait_obj;                                /* semaphore it waits on, or 0 */
    uint32_t wake_ms, waiting_q, sleeping, ready, started;
#ifdef BLE_OS_HOST
    ucontext_t ctx;
#else
    uint32_t sp;
#endif
    uint32_t stack[STACK_WORDS];
} task_t;

typedef struct { uint32_t used, periodic, period, due; void (*fn)(void *); void *priv; const char *task; } tmr_t;
typedef struct { int16_t count; uint8_t magic, owner; } sem_t;   /* lives inside the SDK's OS_SEM buffer */
#define SEM_MAGIC 0x5Eu

static task_t tasks[NTASK] BLE_POOL __attribute__((aligned(8)));
static tmr_t timers[NTIMER];
static int cur = -1;                               /* running task, -1 = the core */
#if !defined(BLE_CONSOLE) || BLE_CONSOLE
static uint32_t run_max;                           /* the longest single task run, ticks (ble_os_run_max_us) */
#endif
#ifdef BLE_OS_HOST
static ucontext_t core_ctx;
#else
static uint32_t core_sp;
#endif
_Static_assert(sizeof(sem_t) <= BLE_SDK_OS_SEM_SIZE, "sem_t fits in OS_SEM");

/* ---- heap: first fit, 8-byte aligned, headers with a guard word ---- */
typedef struct blk { uint32_t size, used, guard; struct blk *next; } blk_t;
static blk_t *heap0;
static uint32_t heap_use, heap_high;              /* bytes in use (blocks + headers), the most seen */
static uint32_t alloc_fails;                       /* requests refused (ble_os_alloc_fails) */
#define GUARD 0xB1E0B1E0u

void ble_os_init(uint8_t *heap, uint32_t heap_len)
{
    uint32_t i;
    heap0 = (blk_t *)(void *)(((uintptr_t)heap + 7u) & ~(uintptr_t)7u);
    heap0->size = (uint32_t)(heap + heap_len - (uint8_t *)heap0) - sizeof(blk_t);
    heap0->used = 0;
    heap0->guard = GUARD;
    heap0->next = 0;
    heap_use = heap_high = alloc_fails = 0;
    for (i = 0; i < NTASK; i++)
        tasks[i].name = 0;
    for (i = 0; i < NTIMER; i++)
        timers[i].used = 0;
    cur = -1;
}

void *ble_malloc(unsigned int n)
{
    blk_t *b;
    n = (n + 7u) & ~7u;
    for (b = heap0; b; b = b->next) {
        if (b->guard != GUARD) {
            alloc_fails++;
            return 0;                              /* heap overrun detected: refuse, never crash */
        }
        if (!b->used && b->size >= n) {
            if (b->size >= n + sizeof(blk_t) + 16u) {
                blk_t *r = (blk_t *)(void *)((uint8_t *)(b + 1) + n);
                r->size = b->size - n - sizeof(blk_t);
                r->used = 0;
                r->guard = GUARD;
                r->next = b->next;
                b->next = r;
                b->size = n;
            }
            b->used = 1;
            heap_use += b->size + sizeof(blk_t);
            if (heap_use > heap_high)
                heap_high = heap_use;
            return b + 1;
        }
    }
    alloc_fails++;
    return 0;
}

uint32_t ble_os_heap_high(void) { return heap_high; }
uint32_t ble_os_alloc_fails(void) { return alloc_fails; }

void *ble_zalloc(unsigned int n)
{
    uint8_t *p = ble_malloc(n);
    uint32_t i;
    if (p)
        for (i = 0; i < n; i++)
            p[i] = 0;
    return p;
}

void ble_free(void *p)
{
    blk_t *b, *f;
    if (!p)
        return;
    f = (blk_t *)p - 1;
    if (f->guard != GUARD)
        return;
    if (!f->used)
        return;
    f->used = 0;
    heap_use -= f->size + sizeof(blk_t);
    for (b = heap0; b; b = b->next)               /* merge free neighbours */
        while (!b->used && b->next && !b->next->used) {
            b->size += sizeof(blk_t) + b->next->size;
            b->next = b->next->next;
        }
}

/* ---- tasks ---- */
static int find(const char *name)
{
    int i;
    for (i = 0; i < NTASK; i++) {
        const char *a = tasks[i].name, *b = name;
        if (!a)
            continue;
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == *b)
            return i;
    }
    return -1;
}

static void trampoline(void *arg)
{
    task_t *t = arg;
    t->fn(t->arg);
    for (;;)
        os_time_dly(1000);                         /* a task body that returns parks here */
}

#ifdef BLE_OS_HOST
static void host_entry(void) { trampoline(&tasks[cur]); }
#endif

#define STACK_PAINT 0xA5A5A5A5u
int ble_os_stack_intact(uint32_t i)               /* 0: task i's stack reached its lowest words (an overflow) */
{
    if (i >= NTASK || !tasks[i].name)
        return 1;
    return tasks[i].stack[0] == STACK_PAINT && tasks[i].stack[1] == STACK_PAINT;
}
int task_create(void (*task)(void *p), void *p, const char *name)
{
    int i;
    uint32_t k;
    for (i = 0; i < NTASK; i++)
        if (!tasks[i].name) {
            task_t *t = &tasks[i];
            t->name = name;
            t->fn = task;
            t->arg = p;
            t->qh = t->qt = 0;
            t->wait_obj = 0;
            t->waiting_q = t->sleeping = t->started = 0;
            t->ready = 1;
            for (k = 0; k < STACK_WORDS; k++)       /* painted: ble_os_stack_free reads the high-water mark */
                t->stack[k] = STACK_PAINT;
            return 0;
        }
    return -1;
}

#if !defined(BLE_CONSOLE) || BLE_CONSOLE
uint32_t ble_os_stack_free(uint32_t i, const char **name)   /* bytes of task i's stack never touched */
{
    uint32_t k = 0;
    if (i >= NTASK || !tasks[i].name) {
        *name = 0;
        return 0;
    }
    *name = tasks[i].name;
    while (k < STACK_WORDS && tasks[i].stack[k] == STACK_PAINT)
        k++;
    return k * 4u;
}
#endif
const char *os_current_task(void) { return cur >= 0 ? tasks[cur].name : "app_core"; }
const char *os_current_task_rom(void) { return os_current_task(); }

static void yield(void)                            /* from a task back to the scheduler (the core) */
{
    int me = cur;
#ifdef BLE_OS_HOST
    swapcontext(&tasks[me].ctx, &core_ctx);
#else
    fm1_ctx_switch(&tasks[me].sp, core_sp);
#endif
}

volatile uint32_t *ble_os_trace;                   /* if set: the running task + 1, 0 in the core (diagnostics) */
void (*ble_os_after_run)(int task);                /* if set: called each time task hands back to the core */
static void run(int i)                             /* from the scheduler into task i */
{
    task_t *t = &tasks[i];
    cur = i;
    if (ble_os_trace)
        *ble_os_trace = (uint32_t)i + 1u;
    t->ready = 0;
#ifdef BLE_OS_HOST
    if (!t->started) {
        t->started = 1;
        getcontext(&t->ctx);
        t->ctx.uc_stack.ss_sp = t->stack;
        t->ctx.uc_stack.ss_size = sizeof t->stack;
        t->ctx.uc_link = &core_ctx;
        makecontext(&t->ctx, host_entry, 0);
    }
    swapcontext(&core_ctx, &t->ctx);
#else
    if (!t->started) {
        t->started = 1;
        fm1_ctx_start(&core_sp, (uint32_t)(uintptr_t)&t->stack[STACK_WORDS], trampoline, t);
    } else {
        fm1_ctx_switch(&core_sp, t->sp);
    }
#endif
    cur = -1;
    if (ble_os_after_run)                          /* (before the trace is cleared: a fatal there names task i) */
        ble_os_after_run(i);
    if (ble_os_trace)
        *ble_os_trace = 0;
}

void os_time_dly(int ticks)
{
    if (cur < 0)
        return;                                    /* never sleep the core */
    tasks[cur].sleeping = 1;
    tasks[cur].wake_ms = ble_os_now_ms() + (uint32_t)ticks * 10u;
    yield();
}

/* ---- queues: words [len, type, args...] ---- */
static uint32_t q_used(const task_t *t) { return t->qt - t->qh; }

int os_taskq_post_type(const char *name, int type, int argc, int *argv)
{
    int i = find(name), k;
    task_t *t;
    if (i < 0)
        return 14;                                 /* OS_TASK_NOT_EXIST, as the SDK's __os_taskq_post */
    if (argc > 18)
        argc = 18;                                 /* (the SDK caps argc at 18) */
    t = &tasks[i];
    if (q_used(t) + 2u + (uint32_t)argc > QWORDS)
        return BLE_SDK_OS_Q_FULL;
    t->q[t->qt++ % QWORDS] = (uint32_t)argc;
    t->q[t->qt++ % QWORDS] = (uint32_t)type;
    for (k = 0; k < argc; k++)
        t->q[t->qt++ % QWORDS] = (uint32_t)argv[k];
    if (t->waiting_q)
        t->ready = 1;
    return BLE_SDK_OS_NO_ERR;
}

/* Q_CALLBACK (fm1-lsdj docs/ble/LINK_NOTES.md): w[0] func, w[1] flags F, w[2..] the arguments; F & 0xFF arity
 * (1: f(a), 2: f(a, b) unless F & 0x400, else f(a, arity - 1, &rest)); F & 0x100: *(int *)next = ret;
 * F & 0x200: os_sem_post(next). Run here, never returned to the caller, as the SDK's pend does. */
#ifdef BLE_OS_HOST
void *ble_os_host_ptr(uint32_t w);                 /* host test: a 32-bit word -> pointer (pointers are 64-bit there) */
#define PTR_OF(w) ble_os_host_ptr(w)
static void *host_words[64];                       /* our own posts of function pointers: handles 0x1000 + i */
static uint32_t host_nwords;
static uint32_t WORD_OF(void *p)
{
    uint32_t i;
    for (i = 0; i < host_nwords; i++)
        if (host_words[i] == p)
            return 0x1000u + i;
    host_words[host_nwords] = p;
    return 0x1000u + host_nwords++;
}
void *ble_os_host_word_ptr(uint32_t w) { return w >= 0x1000u && w < 0x1000u + host_nwords ? host_words[w - 0x1000u] : 0; }
#else
#define PTR_OF(w) ((void *)(uintptr_t)(w))         /* pi32v2: pointers are the 32-bit words the libraries post */
#define WORD_OF(p) ((uint32_t)(uintptr_t)(p))
#endif
static void run_callback(const uint32_t *w, uint32_t n)
{
    uint32_t f = n > 1u ? w[1] : 0u, ar = f & 0xFFu, k = 2u + ar;
    int ret;
    if (n < 3u)
        return;
    if (ar == 1u)
        ret = ((int (*)(int))PTR_OF(w[0]))((int)w[2]);
    else if (ar == 2u && !(f & 0x400u))
        ret = ((int (*)(int, int))PTR_OF(w[0]))((int)w[2], (int)w[3]);
    else
        ret = ((int (*)(int, int, int *))PTR_OF(w[0]))((int)w[2], (int)ar - 1, (int *)(uintptr_t)&w[3]);
    if ((f & 0x100u) && k < n) {
        *(int *)PTR_OF(w[k]) = ret;
        k++;
    }
    if ((f & 0x200u) && k < n)
        os_sem_post(PTR_OF(w[k]));
}

int os_taskq_pend(const char *fmt, int *argv, int argc)
{
    task_t *t;
    (void)fmt;
    if (cur < 0)
        return BLE_SDK_OS_TIMEOUT;
    t = &tasks[cur];
    for (;;) {
        uint32_t n, k, type, w[18];
        while (!q_used(t)) {
            t->waiting_q = 1;
            yield();
        }
        t->waiting_q = 0;
        n = t->q[t->qh++ % QWORDS];
        type = t->q[t->qh++ % QWORDS];
        for (k = 0; k < n; k++)
            w[k < 18u ? k : 17u] = t->q[t->qh++ % QWORDS];
        if ((type & 0xF00000u) == (uint32_t)BLE_SDK_Q_CALLBACK) {
            run_callback(w, n < 18u ? n : 18u);
            continue;
        }
        argv[0] = (int)type;                       /* Q_MSG / Q_EVENT / Q_USER (| code) */
        for (k = 0; k < n && k + 1u < (uint32_t)argc; k++)
            argv[k + 1u] = (int)w[k];
        return BLE_SDK_OS_TASKQ;
    }
}

/* ---- semaphores / mutexes ---- */
int os_sem_create(void *s, int cnt)
{
    sem_t *x = s;
    x->count = (int16_t)cnt;
    x->magic = SEM_MAGIC;
    x->owner = 0;
    return BLE_SDK_OS_NO_ERR;
}

int os_sem_post(void *s)
{
    sem_t *x = s;
    int i;
    x->count++;
    for (i = 0; i < NTASK; i++)
        if (tasks[i].name && tasks[i].wait_obj == s)
            tasks[i].ready = 1;
    return BLE_SDK_OS_NO_ERR;
}

int os_sem_set(void *s, unsigned short cnt) { ((sem_t *)s)->count = (int16_t)cnt; return BLE_SDK_OS_NO_ERR; }

int os_sem_pend(void *s, int timeout)
{
    sem_t *x = s;
    uint32_t until = ble_os_now_ms() + (uint32_t)timeout * 10u;
    while (x->count <= 0) {
        if (cur < 0)
            return BLE_SDK_OS_TIMEOUT;             /* the core never blocks */
        if (timeout && (int32_t)(ble_os_now_ms() - until) >= 0) {
            tasks[cur].wait_obj = 0;
            return BLE_SDK_OS_TIMEOUT;
        }
        tasks[cur].wait_obj = s;
        tasks[cur].sleeping = timeout != 0;
        tasks[cur].wake_ms = until;
        yield();
    }
    if (cur >= 0)
        tasks[cur].wait_obj = 0;
    x->count--;
    return BLE_SDK_OS_NO_ERR;
}

int os_mutex_create(void *m) { return os_sem_create(m, 1); }
int os_mutex_pend(void *m, int timeout) { return os_sem_pend(m, timeout); }
int os_mutex_post(void *m) { return os_sem_post(m); }
int os_mutex_del(void *m, int force) { (void)m; (void)force; return BLE_SDK_OS_NO_ERR; }

/* ---- timers ---- */
static unsigned short tmr_add(void *priv, void (*fn)(void *), unsigned int ms, uint32_t periodic)
{
    unsigned short i;
    for (i = 0; i < NTIMER; i++)
        if (!timers[i].used) {
            timers[i].used = 1;
            timers[i].task = 0;
            timers[i].periodic = periodic;
            timers[i].period = ms;
            timers[i].due = ble_os_now_ms() + ms;
            timers[i].fn = fn;
            timers[i].priv = priv;
            return (unsigned short)(i + 1u);
        }
    return 0;
}
unsigned short sys_timer_add(void *priv, void (*func)(void *priv), unsigned int msec) { return tmr_add(priv, func, msec, 1); }
unsigned short sys_timeout_add(void *priv, void (*func)(void *priv), unsigned int msec) { return tmr_add(priv, func, msec, 0); }
void sys_timer_del(unsigned short id) { if (id && id <= NTIMER) timers[id - 1u].used = 0; }
void sys_timeout_del(unsigned short id) { sys_timer_del(id); }
/* bound to a task: the callback runs inside that task, as a Q_CALLBACK message (the SDK's sys_timer_add_to_task) */
unsigned short sys_timer_add_to_task(const char *task_name, void *priv, void (*func)(void *priv), unsigned int msec)
{
    unsigned short id = tmr_add(priv, func, msec, 1);
    if (id)
        timers[id - 1u].task = task_name;
    return id;
}
unsigned short sys_timeout_add_to_task(const char *task_name, void *priv, void (*func)(void *priv), unsigned int msec)
{
    unsigned short id = tmr_add(priv, func, msec, 0);
    if (id)
        timers[id - 1u].task = task_name;
    return id;
}
void *sys_timer_get_user_data(unsigned short id) { return id && id <= NTIMER ? timers[id - 1u].priv : 0; }
void sys_timer_set_user_data(unsigned short id, void *priv) { if (id && id <= NTIMER) timers[id - 1u].priv = priv; }
void sys_timer_re_run(unsigned short id)
{
    if (id && id <= NTIMER && timers[id - 1u].used)
        timers[id - 1u].due = ble_os_now_ms() + timers[id - 1u].period;
}
unsigned short usr_timeout_add(void *priv, void (*func)(void *priv), unsigned int msec, uint8_t priority)
{
    (void)priority;
    return tmr_add(priv, func, msec, 0);
}
void usr_timeout_del(unsigned short id) { sys_timer_del(id); }
void usr_timer_del(unsigned short id) { sys_timer_del(id); }

int sys_timer_modify(unsigned short id, unsigned int msec)
{
    if (!id || id > NTIMER || !timers[id - 1u].used)
        return -1;
    timers[id - 1u].period = msec;
    timers[id - 1u].due = ble_os_now_ms() + msec;
    return 0;
}

static uint32_t run_timers(void)
{
    uint32_t i, n = 0, now = ble_os_now_ms();
    for (i = 0; i < NTIMER; i++) {
        tmr_t *t = &timers[i];
        if (t->used && (int32_t)(now - t->due) >= 0) {
            void (*fn)(void *) = t->fn;
            void *priv = t->priv;
            const char *task = t->task;
            if (t->periodic)
                t->due += t->period ? t->period : 1u;
            else
                t->used = 0;
            if (task) {                            /* run in its task: Q_CALLBACK {fn, 1 argument, priv} */
                int w[3];
                w[0] = (int)WORD_OF((void *)fn);
                w[1] = 1;
                w[2] = (int)(uint32_t)(uintptr_t)priv;
                if (os_taskq_post_type(task, BLE_SDK_Q_CALLBACK, 3, w) == 14)
                    fn(priv);                      /* (no such task: run here) */
            } else {
                fn(priv);
            }
            n++;
        }
    }
    return n;
}

/* ---- the scheduler, called from the core's main loop ---- */
#ifdef BLE_OS_HOST
#define BLE_OS_HOST_SWITCHES 64
#endif
uint32_t ble_os_service(uint32_t budget_us)
{
    uint32_t ran = 0, progress = 1;
#ifdef BLE_OS_HOST
    uint32_t switches = 0;
    (void)budget_us;
#else
    uint32_t t0 = fm1_ticks();
#endif
    run_timers();
    while (progress) {
        int i;
        progress = 0;
        for (i = 0; i < NTASK; i++) {
            task_t *t = &tasks[i];
            uint32_t now = ble_os_now_ms();
            if (!t->name)
                continue;
            if (t->sleeping && (int32_t)(now - t->wake_ms) >= 0) {
                t->sleeping = 0;
                t->ready = 1;
            }
            if (!t->ready && !(t->waiting_q && q_used(t)))
                continue;
#if defined(BLE_OS_HOST) || (defined(BLE_CONSOLE) && !BLE_CONSOLE)
            run(i);
#else
            {
                uint32_t t1 = fm1_ticks();
                run(i);
                t1 = fm1_ticks() - t1;
                if (t1 > run_max)
                    run_max = t1;
            }
#endif
            ran++;
            progress = 1;
#ifdef BLE_OS_HOST
            if (++switches >= BLE_OS_HOST_SWITCHES)
                return ran;
#else
            if (fm1_ticks() - t0 >= budget_us * FM1_TICKS_PER_US)
                return ran;
#endif
        }
    }
    return ran;
}

#if !defined(BLE_CONSOLE) || BLE_CONSOLE
uint32_t ble_os_run_max_us(uint32_t reset)        /* the longest single task run since the last reset, us */
{
#ifdef BLE_OS_HOST
    (void)reset;
    return 0;
#else
    uint32_t us = run_max / FM1_TICKS_PER_US;
    if (reset)
        run_max = 0;
    return us;
#endif
}
#endif

#ifndef BLE_OS_HOST
/* the names the JieLi libraries call */
void *malloc(unsigned int n) { return ble_malloc(n); }
void *zalloc(unsigned int n) { return ble_zalloc(n); }
void free(void *p) { ble_free(p); }
#endif
