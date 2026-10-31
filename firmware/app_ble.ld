/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-106 BLE memory map.
 * app.ld + JieLi BT libraries' sections.
 * Code + constants execute in place from XIP (0x02001000).
 * 0x01C00000..0x01C06000 holds .ram_text (flash driver + volatile RF trim code).
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases, CPU1 stacks), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02001000, LENGTH = 0x8C000
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C020, LENGTH = 0x3D30
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0)) *(.text*) *(.rodata*)
        . = ALIGN(4);
        a2dp_source_media_codec_begin = .; KEEP(*(.a2dp_source_media_codec)) a2dp_source_media_codec_end = .;
        a2dp_sink_media_probe_begin = .; KEEP(*(.a2dp_sink_media_probe)) a2dp_sink_media_probe_end = .;
        a2dp_sink_media_codec_begin = .; KEEP(*(.a2dp_sink_media_codec)) a2dp_sink_media_codec_end = .;
        a2dp_event_handler_begin = .; KEEP(*(.a2dp_event_handler)) a2dp_event_handler_end = .;
        sdp_record_item_begin = .; KEEP(*(.sdp_record_item)) sdp_record_item_end = .;
        bt_sleep_begin = .; KEEP(*(.bt_sleep)) bt_sleep_end = .;
        tws_func_stub_begin = .; KEEP(*(.tws_func_stub)) tws_func_stub_end = .;
        tws_sync_channel_begin = .; KEEP(*(.tws_sync_channel)) tws_sync_channel_end = .;
        _sys_event_handler_begin = .; PROVIDE(sys_event_handler_begin = .); KEEP(*(.sys_event.*))
        _sys_event_handler_end = .; PROVIDE(sys_event_handler_end = .);
        lp_target_begin = .; KEEP(*(.lp_target)) lp_target_end = .;
        *(.[a-uw-z]*_code) *(.v[a-np-z]*_code) *(.*_const) *(.LOG_TAG_CONST)
        . = ALIGN(4); _etext = .;
    } > XIP
    .ram_text : {
        _rt_start = .; KEEP(*(.ram_text*))
        *(.volatile_ram_code) *(*.rf.text.cache.L2.rf_trim)
        . = ALIGN(4); _rt_end = .;
    } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .data : {
        . = ALIGN(4); _data_start = .;
        *(.data*) *(.sdata*) *(.*_data)
        . = ALIGN(4); _data_end = .;
    } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        _bss_start = .; *(.bss*) *(.sbss*) *(COMMON)
        *(.bd_base) *(.*_bulk) *(.*_bss) *(.btstack_pool)
        acl_tx_pool = .; acl_tx_pool_end = .; acl_rx_pool = .; acl_rx_pool_end = .;
        tws_bulk_pool = .; tws_bulk_pool_end = .;
        . = ALIGN(4); _bss_end = .;
    } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .cpu1_stacks (NOLOAD) : {
        . = ALIGN(32); _cpu1_stacks_lo = .;
        KEEP(*(.cpu1_stacks))
        . = ALIGN(32); _cpu1_stacks_hi = .;
    } > POOL
    _cpu1_ustack_top = _cpu1_stacks_lo + 4096;
    _cpu1_sstack_top = _cpu1_stacks_lo + 8192;
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) *(.debug*) }
}
