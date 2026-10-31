/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
/* Cooperative context switch (pi32v2). A context is a saved stack pointer; callee-saved r4-r15 and rets are on
 * its stack (the compiler's own prologue set: [--sp] = {rets, r15-r4}).
 *   fm1_ctx_switch(&from_sp, to_sp)       save here, resume `to`
 *   fm1_ctx_start(&from_sp, stack_top, entry, arg)   save here, run entry(arg) on a new stack (entry never returns) */
#pragma once
#include <stdint.h>
void fm1_ctx_switch(uint32_t *save_sp, uint32_t to_sp);
void fm1_ctx_start(uint32_t *save_sp, uint32_t stack_top, void (*entry)(void *), void *arg);
