/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
	.section .text.fm1_ctx_switch,"ax",@progbits
	.globl fm1_ctx_switch
fm1_ctx_switch:                     /* r0 = &save_sp, r1 = to_sp */
	[--sp] = {rets, r15-r4}
	r2 = sp
	[r0 + 0] = r2
	sp = r1
	{pc, r15-r4} = [sp++]

	.section .text.fm1_ctx_start,"ax",@progbits
	.globl fm1_ctx_start
fm1_ctx_start:                      /* r0 = &save_sp, r1 = stack_top, r2 = entry, r3 = arg */
	[--sp] = {rets, r15-r4}
	r4 = sp
	[r0 + 0] = r4
	sp = r1
	r0 = r3
	call r2                         /* entry(arg); a task body never returns */
1:	goto 1b
