/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The OS calls the JieLi BT libraries make (SDK signatures), on a cooperative scheduler run from the core's main
 * loop (ble_os_service). Semantics follow the SDK's os_api.c (fm1-lsdj 548ce73 docs/ble/LINK_NOTES.md). */
#ifndef BLE_OS_H
#define BLE_OS_H
#include <stdint.h>
#include "ble_sdk_abi.h"
#define BLE_STACK_WORDS 1024                       /* 4 KiB per task (ample headroom for active BLE scan parsing), in .pool */
int task_create(void (*task)(void *p), void *p, const char *name);
const char *os_current_task(void);
const char *os_current_task_rom(void);
void os_time_dly(int ticks);                                             /* 1 tick = 10 ms */
int os_taskq_post_type(const char *name, int type, int argc, int *argv); /* 0, 14 no task, 21 full */
int os_taskq_pend(const char *fmt, int *argv, int argc);                 /* blocks; returns OS_TASKQ */
int os_sem_create(void *sem, int cnt);
int os_sem_pend(void *sem, int timeout_ticks);                           /* 0 = forever */
int os_sem_post(void *sem);
int os_sem_set(void *sem, unsigned short cnt);
int os_mutex_create(void *m);
int os_mutex_pend(void *m, int timeout_ticks);
int os_mutex_post(void *m);
int os_mutex_del(void *m, int force);
unsigned short sys_timer_add(void *priv, void (*func)(void *priv), unsigned int msec);
unsigned short sys_timeout_add(void *priv, void (*func)(void *priv), unsigned int msec);
void sys_timer_del(unsigned short id);
void sys_timeout_del(unsigned short id);
int sys_timer_modify(unsigned short id, unsigned int msec);
unsigned short sys_timer_add_to_task(const char *task_name, void *priv, void (*func)(void *priv), unsigned int msec);
unsigned short sys_timeout_add_to_task(const char *task_name, void *priv, void (*func)(void *priv), unsigned int msec);
void *sys_timer_get_user_data(unsigned short id);
void sys_timer_set_user_data(unsigned short id, void *priv);
void sys_timer_re_run(unsigned short id);
unsigned short usr_timeout_add(void *priv, void (*func)(void *priv), unsigned int msec, uint8_t priority);
void usr_timeout_del(unsigned short id);
void usr_timer_del(unsigned short id);
#ifdef BLE_OS_HOST
void *ble_os_host_word_ptr(uint32_t w);
#endif
void *ble_malloc(unsigned int n);                 /* the target also exports malloc / zalloc / free */
void *ble_zalloc(unsigned int n);
void ble_free(void *p);
void ble_os_init(uint8_t *heap, uint32_t heap_len);
uint32_t ble_os_service(uint32_t budget_us);
extern void (*ble_os_after_run)(int task);      /* called after each task run (the core's checks) */
extern volatile uint32_t *ble_os_trace;          /* diagnostics: the running task + 1 is written here */
uint32_t ble_os_heap_high(void);                  /* console: the most heap in use (blocks + headers), bytes */
#if !defined(BLE_CONSOLE) || BLE_CONSOLE
uint32_t ble_os_stack_free(uint32_t i, const char **name);   /* console: a task's unused stack, bytes */
uint32_t ble_os_run_max_us(uint32_t reset);       /* console: the longest task run (the slice is checked between runs) */
#endif
uint32_t ble_os_alloc_fails(void);                /* console: heap requests refused */
int ble_os_stack_intact(uint32_t i);              /* 0: task i overflowed its stack (its lowest words) */
uint32_t ble_os_now_ms(void);
#endif
