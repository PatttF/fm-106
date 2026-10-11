/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 app entry. The boot stub (firmware/stub, app.bin's first 0xEE0 bytes at XIP 0x02000120, where the SPL
 * jumps) checks the commit marker and enters here, the body's first byte at XIP 0x02001000, with interrupts off and
 * the watchdog armed (docs/BOOT-SAFETY.md). The core state the SPL left (icfg, psr, r0, sp, ssp) is in STATUS
 * (0x01C07F20..): the stub's crt0 saved it. The first instruction keeps the SPL's `goto +4; rts` form. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	cli
	sp = _ustack_top
	ssp = _sstack_top
	call fm1_cstart
	goto .

/* CPU1 entry follows cpu.a/startup.S.o: install separate supervisor/user
 * stacks, then return from the startup interrupt into the RAM polling loop.
 * No application interrupt is enabled on this core. */
#ifdef DINGHY_DUAL_CORE
#if DINGHY_DUAL_CORE
    .section .text.core1,"ax",@progbits
    .globl fm1_core1_start
fm1_core1_start:
    cli
    icfg = 0
    usp = _cpu1_ustack_top
    sp = _cpu1_sstack_top
    ssp = _cpu1_sstack_top
    reti = fm1_core1_main
    rti
#endif
#endif
