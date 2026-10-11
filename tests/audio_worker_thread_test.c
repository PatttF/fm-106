/* SPDX-License-Identifier: GPL-3.0-only */
/* The real audio_worker_loop on a host thread: concurrent submit/join, one
 * outstanding job, completion before join returns, offline refusal. */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
static uint32_t worker_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000u);
}
#define AW_WORD _Atomic uint32_t
#define AW_LOAD(p) atomic_load_explicit(p, memory_order_acquire)
#define AW_STORE(p,v) atomic_store_explicit(p,v,memory_order_release)
#define AW_TICKS() worker_clock()
#define AW_TIMEOUT_TICKS 2000000u
#define AW_FAULT() abort()
#define AW_RAM_LOOP static
#define AW_IDLE() do { sched_yield(); pthread_testcancel(); } while(0)
#include "../firmware/src/audio_worker.h"
static _Atomic int work_done;
static void job(void *context) { (void)context; atomic_fetch_add(&work_done, 1); }
static void *worker_thread(void *unused)
{
    (void)unused;
    audio_worker_loop();
    return 0;
}
int main(void)
{
    pthread_t thread;
    assert(!pthread_create(&thread, 0, worker_thread, 0));
    while (!AW_LOAD(&audio_worker.ready)) sched_yield();
    audio_worker_online = 1;
    for (int i = 0; i < 1000; i++) {
        assert(audio_worker_submit(job, 0));
        assert(!audio_worker_submit(job, 0));   /* one job outstanding */
        audio_worker_join();                    /* complete == request on return */
        assert(AW_LOAD(&audio_worker.request) == AW_LOAD(&audio_worker.complete));
    }
    assert(atomic_load(&work_done) == 1000);
    assert(audio_worker.jobs == 1000);
    assert(audio_worker.timeouts == 0);
    audio_worker_online = 0;
    assert(!audio_worker_submit(job, 0));
    assert(!pthread_cancel(thread));
    assert(!pthread_join(thread, 0));
    puts("audio worker thread: 1000 concurrent submit/join pairs, offline refusal ok");
    return 0;
}
