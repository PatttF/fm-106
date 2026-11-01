/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The core-reference build (FELUCCA_CORE_REF=1): the core's unit compiled exactly as in a BLE build (its wrappers,
 * its main-loop calls), linked with these empty entry points instead of the BLE unit and the JieLi libraries. */
#include <stdint.h>
#include "ble/ble_core.h"
void ble_service(void) {}
void ble_console(const char *args) { (void)args; }
void ble_user_start(void) {}
void ble_status(void) {}
int ble_started(void) { return 0; }
int ble_midi_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 0; }
uint32_t ble_midi_room(void) { return 0; }
int ble_fml_send(const uint8_t *p, uint32_t len) { (void)p; (void)len; return 0; }
int ble_fml_ready(void) { return 0; }
uint32_t ble_ui_state(void) { return BLE_ST_OFF; }
const char *ble_ui_err(uint32_t *code) { *code = 0; return ""; }
uint32_t ble_ui_devs(void) { return 0; }
int ble_ui_dev(uint32_t i, ble_ui_dev_t *out) { (void)i; (void)out; return 0; }
void ble_ui_slot_target(uint32_t slot, const uint8_t addr[6], uint32_t type) { (void)slot; (void)addr; (void)type; }
int ble_ui_slot(uint32_t slot, ble_ui_slot_t *out) { (void)slot; (void)out; return 0; }
void ble_ui_scan_want(int on) { (void)on; }
void ble_ui_want(int on) { (void)on; }
int ble_ui_conn_name(char out[24]) { (void)out; return 0; }
void ble_ui_role(int device) { (void)device; }
uint32_t ble_ui_role_now(void) { return 0; }
void ble_ui_get_diag(uint32_t out[16]) { uint32_t i; for (i = 0; i < 16; i++) out[i] = 0; }

