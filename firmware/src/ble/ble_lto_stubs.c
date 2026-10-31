/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The JieLi BT libraries' logging, off: compiled to LLVM bitcode (-flto, tools/build.py) and linked through LTO with
 * the libraries, so the optimizer sees these functions are empty and the tags 0, and drops the calls with their
 * format strings (the SDK's own link does the same: apps/demo/demo_ble/board/wl82/Makefile, -flto and
 * -dont-used-symbol-list). The libraries' other log tags are 0 too (sdkcfg/app_config.h LIB_DEBUG, the SDK config
 * sources, also bitcode). Was ble_port.c's (native: the calls and strings stayed). */
#include <stdint.h>
int printf(const char *fmt, ...) { (void)fmt; return 0; }
int puts(const char *s) { (void)s; return 0; }
int putchar(int c) { return c; }
void put_buf(const unsigned char *buf, int len) { (void)buf; (void)len; }
void printf_buf(uint8_t *buf, uint32_t len) { (void)buf; (void)len; }
void log_print(int level, const char *tag, const char *fmt, ...) { (void)level; (void)tag; (void)fmt; }
const char log_tag_const_d_TWS = 0, log_tag_const_i_TWS = 0, log_tag_const_e_LBUF = 0, log_tag_const_i_LBUF = 0,
           log_tag_const_i_WLC = 0;
