/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The boot stub (docs/BOOT-SAFETY.md "As built"): app.bin's first 0xEE0 bytes, flash 0x4120..0x4FFF, executed in
 * place. Code and constants only: nothing may land in RAM (tools/build.py fails on any other section with a size).
 * The stacks and the entry it leaves through are the app's (firmware/app.ld). */
MEMORY
{
    STUB (rx) : ORIGIN = 0x02000120, LENGTH = 0xEE0
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _stub_end = .; } > STUB
    .ram (NOLOAD) : { *(.data*) *(.sdata*) *(.bss*) *(.sbss*) *(COMMON) *(.ram_text*) *(.noinit*) *(.pool*) } > STUB
    _app_entry = 0x02001000;        /* the body's _start (app.ld XIP ORIGIN) */
    _stub_sp = 0x01C7A000;          /* app.ld _ustack_top */
    _stub_ssp = 0x01C7C000;         /* app.ld _sstack_top */
    /DISCARD/ : { *(.comment) *(.note*) }
}
