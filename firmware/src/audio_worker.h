/* SPDX-License-Identifier: GPL-3.0-only */
/* Single producer (CPU0 audio ISR), single consumer (CPU1). One outstanding
 * job. Completion publishes the result AND returns ownership of voice state.
 * CPU0 must join before events, engine changes, post processing or flash IO.
 * Platform hooks provide acquire/release accesses, time and fault handling. */
#pragma once
#include <stdint.h>
#ifndef AW_IDLE
#define AW_IDLE() ((void)0)
#endif
typedef void (*audio_worker_fn)(void *);
#define AW_CHECK_HASH(fn, ctx, req) \
    ((uint32_t)(uintptr_t)(fn) ^ (uint32_t)(uintptr_t)(ctx) ^ ((req) * 0x9E3779B1u))
/* Producer (CPU0) and consumer (CPU1) fields sit on separate 32-byte cache
 * lines to avoid false sharing while CPU1 renders or spins. */
static struct {
    AW_WORD request;
    audio_worker_fn fn;
    void *context;
    uint32_t check;
    uint32_t max_wait_ticks, timeouts;
    AW_WORD ready __attribute__((aligned(32)));
    AW_WORD complete;
    uint32_t jobs, max_job_ticks;
    uint32_t last_job_ticks;
} audio_worker __attribute__((aligned(32)));
static int audio_worker_online;

static int audio_worker_submit(audio_worker_fn fn, void *context)
{
    uint32_t req = AW_LOAD(&audio_worker.request);
    if (!audio_worker_online || req != AW_LOAD(&audio_worker.complete))
        return 0;
    req++;
    audio_worker.fn = fn;
    audio_worker.context = context;
    audio_worker.check = AW_CHECK_HASH(fn, context, req);
    AW_STORE(&audio_worker.request, req);
    return 1;
}

static void audio_worker_join(void)
{
    uint32_t target = AW_LOAD(&audio_worker.request);
    uint32_t start = AW_TICKS();
    while (AW_LOAD(&audio_worker.complete) != target) {
        if ((uint32_t)(AW_TICKS() - start) >= AW_TIMEOUT_TICKS) {
            audio_worker.timeouts++;
            /* A partly executed voice cannot be rerun on CPU0: retire CPU1 rather
             * than reuse a buffer or race the worker. */
            AW_FAULT();
            break;
        }
    }
    uint32_t elapsed = AW_TICKS() - start;
    if (elapsed > audio_worker.max_wait_ticks) audio_worker.max_wait_ticks = elapsed;
}

/* This function and its polling helpers must stay in internal RAM on target.
 * It publishes completion only after a callback has returned from XIP/RAMTEXT.
 * Keeping `done` in a CPU1 register halves idle SRAM bus reads, and verifying
 * `request == done + 1` + `check` guards against transient shared-SRAM glitches. */
AW_RAM_LOOP void audio_worker_loop(void)
{
    uint32_t done = AW_LOAD(&audio_worker.complete);
    AW_STORE(&audio_worker.ready, 1u);
    for (;;) {
        uint32_t request = AW_LOAD(&audio_worker.request);
        if (request == done) { AW_IDLE(); continue; }
        if (request == done + 1u) {
            audio_worker_fn fn = audio_worker.fn;
            void *ctx = audio_worker.context;
            if (audio_worker.check == AW_CHECK_HASH(fn, ctx, request)) {
                uint32_t start = AW_TICKS();
                fn(ctx);
                uint32_t elapsed = AW_TICKS() - start;
                audio_worker.jobs++;
                audio_worker.last_job_ticks = elapsed;
                if (elapsed > audio_worker.max_job_ticks) audio_worker.max_job_ticks = elapsed;
                done = request;
                AW_STORE(&audio_worker.complete, request);
                continue;
            }
        }
        AW_IDLE();
    }
}
