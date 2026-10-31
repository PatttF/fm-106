/* SPDX-License-Identifier: GPL-3.0-only */
/* Boot-stub request layer (our recovery stub; not part of felucca-bsp).
 * The stub (firmware/stub/) reads FM1_STUB_RAM[0] after a core reset and
 * jumps to rescue/test entry instead of the app. */
#pragma once
#include <stdint.h>
#include "fm1_sys.h"

#define FM1_STUB_RAM ((volatile uint32_t *)0x01C7C000u)   /* 8 words below NOINIT (app.ld) */
#define FM1_HAVE_STUB 1
#define STUB_REQ_RESCUE 0x52514552u
#define STUB_REQ_TEST   0x54514552u

FM1_INLINE uint32_t fm1_stub_word(uint32_t i) { return FM1_STUB_RAM[i & 7u]; }

/* as fm1_enter_update, with the record cleared instead of written (the loader's own restart after its write: the
 * SPL then boots the app's slot) and the request in the stub's word; the caller detaches USB first, as there */
FM1_INLINE void fm1_stub_request(uint32_t word)
{
    __asm__ volatile("cli");
    if (!(*(volatile uint32_t *)0x1EEE240u & 1u))       /* drop CPU0 write limits, as fm1_enter_uboot */
        *(volatile uint32_t *)0x1EEE240u = 0xE7u;
    *(volatile uint32_t *)0x1EEE348u = 0;
    fm1_updata_parm_clear();
    FM1_STUB_RAM[0] = word;
    fm1_core_reset();
}
