/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * FM-Link: 2026 DEADACTIVE */
/* The JieLi SDK BLE API the central uses (AC79 SDK V1.2.0 include_lib/btstack/le/ble_api.h, att.h, sm.h, gatt.h;
 * btstack/btstack_typedef.h). Only what ble_central.c needs. The numbers and struct layouts are checked against
 * ble_sdk_abi.h, which tools/ble_libs.py abi reads from the SDK headers by compiling them. */
#ifndef BLE_SDK_H
#define BLE_SDK_H
#include <stdint.h>
#include "ble/ble_sdk_abi.h"

typedef void (*btstack_packet_handler_t)(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

/* ble_api.h:119 (varargs: the ble_op_* macros pass ints and pointers) */
int ble_user_cmd_prepare(int cmd, int argc, ...);
/* ble_api.h:146 */
struct create_conn_param_t {
    uint16_t conn_interval, conn_latency, supervision_timeout;
    uint8_t peer_address_type;
    uint8_t peer_address[6];
} __attribute__((packed));
/* ble_api.h:173 */
typedef struct {
    uint8_t event_type, address_type, address[6];
    int8_t rssi;
    uint8_t length;
    uint8_t data[];
} adv_report_t;
/* att.h:54 service_report_t, :61 charact_report_t, :70 att_data_report_t, :81 search_result_t */
typedef struct { uint16_t start_group_handle, end_group_handle, uuid16; uint8_t uuid128[16]; } service_report_t;
typedef struct {
    uint16_t start_handle, value_handle, end_handle, properties, uuid16;
    uint8_t uuid128[16];
} charact_report_t;
typedef struct {
    uint16_t packet_type, value_handle, value_offset, blob_length;
    uint8_t *blob;
    uint16_t conn_handle;
} att_data_report_t;
typedef struct {
    service_report_t services;
    charact_report_t characteristic;
    uint16_t service_index, characteristic_index;
} search_result_t;
/* utils/event/bt_event.h:19 */
struct bt_event { uint8_t event; uint8_t args[7]; uint32_t value; };

_Static_assert(sizeof(struct create_conn_param_t) == BLE_SDK_SZ_CONN_PARAM, "create_conn_param_t");
_Static_assert(__builtin_offsetof(struct create_conn_param_t, supervision_timeout) == BLE_SDK_OFF_CONN_TIMEOUT, "");
_Static_assert(__builtin_offsetof(struct create_conn_param_t, peer_address_type) == BLE_SDK_OFF_CONN_ADDR_TYPE, "");
_Static_assert(__builtin_offsetof(struct create_conn_param_t, peer_address) == BLE_SDK_OFF_CONN_ADDR, "");
_Static_assert(sizeof(adv_report_t) == BLE_SDK_SZ_ADV_REPORT, "adv_report_t");
_Static_assert(__builtin_offsetof(adv_report_t, address_type) == BLE_SDK_OFF_ADV_ADDR_TYPE, "");
_Static_assert(__builtin_offsetof(adv_report_t, address) == BLE_SDK_OFF_ADV_ADDR, "");
_Static_assert(__builtin_offsetof(adv_report_t, rssi) == BLE_SDK_OFF_ADV_RSSI, "");
_Static_assert(__builtin_offsetof(adv_report_t, length) == BLE_SDK_OFF_ADV_LEN, "");
_Static_assert(__builtin_offsetof(adv_report_t, data) == BLE_SDK_OFF_ADV_DATA, "");
_Static_assert(sizeof(service_report_t) == BLE_SDK_SZ_SERVICE, "service_report_t");
_Static_assert(sizeof(charact_report_t) == BLE_SDK_SZ_CHARACT, "charact_report_t");
_Static_assert(__builtin_offsetof(charact_report_t, value_handle) == BLE_SDK_OFF_CH_VALUE_HANDLE, "");
_Static_assert(__builtin_offsetof(charact_report_t, properties) == BLE_SDK_OFF_CH_PROPERTIES, "");
_Static_assert(__builtin_offsetof(charact_report_t, uuid16) == BLE_SDK_OFF_CH_UUID16, "");
_Static_assert(__builtin_offsetof(charact_report_t, uuid128) == BLE_SDK_OFF_CH_UUID128, "");
_Static_assert(sizeof(search_result_t) == BLE_SDK_SZ_SEARCH_RESULT, "search_result_t");
_Static_assert(__builtin_offsetof(search_result_t, characteristic) == BLE_SDK_OFF_SR_CHAR, "");
_Static_assert(__builtin_offsetof(search_result_t, service_index) == BLE_SDK_OFF_SR_SVC_INDEX, "");
#if UINTPTR_MAX == 0xFFFFFFFFu                     /* (the target: 32-bit pointers) */
_Static_assert(sizeof(att_data_report_t) == BLE_SDK_SZ_DATA_REPORT, "att_data_report_t");
_Static_assert(__builtin_offsetof(att_data_report_t, value_handle) == BLE_SDK_OFF_DATA_HANDLE, "");
_Static_assert(__builtin_offsetof(att_data_report_t, blob_length) == BLE_SDK_OFF_DATA_LEN, "");
_Static_assert(__builtin_offsetof(att_data_report_t, blob) == BLE_SDK_OFF_DATA_BLOB, "");
_Static_assert(__builtin_offsetof(att_data_report_t, conn_handle) == BLE_SDK_OFF_DATA_CONN, "");
#endif
_Static_assert(sizeof(struct bt_event) == BLE_SDK_SZ_BT_EVENT, "bt_event");

/* the ble_op_* macros (ble_api.h:513, 542, 610, 639, 973) and the scan / connect ones (ble_api.h, same form) */
#define ble_op_disconnect(h) ble_user_cmd_prepare(BLE_SDK_CMD_DISCONNECT, 1, (int)(h))
#define ble_op_att_send_init(h, ram, size, payload) \
    ble_user_cmd_prepare(BLE_SDK_CMD_ATT_SEND_INIT, 4, (int)(h), (ram), (int)(size), (int)(payload))
#define ble_op_att_send_data(handle, data, len, op) \
    ble_user_cmd_prepare(BLE_SDK_CMD_ATT_SEND_DATA, 4, (int)(handle), (data), (int)(len), (int)(op))
#define ble_op_att_get_remain(remain_size_ptr) ble_user_cmd_prepare(BLE_SDK_CMD_ATT_VAILD_LEN, 1, (remain_size_ptr))
#define ble_op_set_scan_param(type, interval, window) \
    ble_user_cmd_prepare(BLE_SDK_CMD_SCAN_PARAM, 3, (int)(type), (int)(interval), (int)(window))
#define ble_op_scan_enable2(en, filter_dup) ble_user_cmd_prepare(BLE_SDK_CMD_SCAN_ENABLE2, 2, (int)(en), (int)(filter_dup))
#define ble_op_create_connection(p) ble_user_cmd_prepare(BLE_SDK_CMD_CREATE_CONN, 1, (void *)(p))
#define ble_op_create_connection_cancel() ble_user_cmd_prepare(BLE_SDK_CMD_CREATE_CONN_CANCEL, 0)
#define ble_op_search_profile_all() ble_user_cmd_prepare(BLE_SDK_CMD_SEARCH_PROFILE, 2, BLE_SDK_PFL_SERVER_ALL, 0)
/* several links at once (ble_api.h:557, 573, 623, 656; multi_demo/le_multi_common.c, le_multi_client.c): one send
 * buffer for all of them, each link given an index per role (0 server, 1 client), each send naming its link */
#define ble_op_multi_att_send_init(ram, size, payload) \
    ble_user_cmd_prepare(BLE_SDK_CMD_MULTI_ATT_SEND_INIT, 3, (ram), (int)(size), (int)(payload))
#define ble_op_multi_att_send_conn_handle(con, index, role) \
    ble_user_cmd_prepare(BLE_SDK_CMD_MULTI_ATT_SET_CONN_HANDLE, 3, (int)(con), (int)(index), (int)(role))
#define ble_op_multi_att_get_remain(con, remain_size_ptr) \
    ble_user_cmd_prepare(BLE_SDK_CMD_MULTI_ATT_VAILD_LEN, 2, (int)(con), (remain_size_ptr))
#define ble_op_multi_att_send_data(con, handle, data, len, op) \
    ble_user_cmd_prepare(BLE_SDK_CMD_MULTI_ATT_SEND_DATA, 5, (int)(con), (int)(handle), (data), (int)(len), (int)(op))

/* stack functions (le_net_central.c 1300-1330; btstack.a / btctrler.a) */
void btstack_init(void);
int le_controller_set_mac(void *addr);
void le_device_db_init(void);
void ble_stack_gatt_role(uint8_t role);
void sm_init(void);
void sm_set_io_capabilities(int io_capability);
void sm_set_authentication_requirements(uint8_t auth_req);
void sm_set_encryption_key_size_range(uint8_t min_size, uint8_t max_size);
void sm_set_master_request_pair(int enable);
void gatt_client_init(void);
void gatt_client_register_packet_handler(btstack_packet_handler_t handler);
void hci_event_callback_set(btstack_packet_handler_t handler);
void le_l2cap_register_packet_handler(btstack_packet_handler_t handler);
uint16_t ble_vendor_set_default_att_mtu(uint16_t mtu);
void user_client_init(uint16_t handle, uint8_t *buffer, uint16_t buffer_size);

/* the device role (le_trans_data.c): the GATT server (att.h:119, 130, 140, 141, 96, 97), advertising (ble_api.h:423,
 * 453, 485, 500), Just Works pairing (sm.h:32, 50, 52) */
typedef uint16_t (*att_read_callback_t)(uint16_t con_handle, uint16_t attribute_handle, uint16_t offset, uint8_t *buffer,
                                        uint16_t buffer_size);
typedef int (*att_write_callback_t)(uint16_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode,
                                    uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
void att_server_init(uint8_t const *db, att_read_callback_t read_callback, att_write_callback_t write_callback);
void att_server_register_packet_handler(btstack_packet_handler_t handler);
void multi_att_set_ccc_config(uint16_t conn_handle, uint16_t att_handle, uint16_t cfg);   /* (att.h:105-109) */
uint16_t multi_att_get_ccc_config(uint16_t conn_handle, uint16_t att_handle);
int multi_att_clear_ccc_config(uint16_t conn_handle);
void sm_just_works_confirm(uint16_t con_handle);
void sm_set_request_security(int enable);
void sm_event_callback_set(void (*cbk_sm_ph)(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size));
#define ble_op_adv_enable(en) ble_user_cmd_prepare(BLE_SDK_CMD_ADV_ENABLE, 1, (int)(en))
#define ble_op_set_adv_param(interval, type, channel) \
    ble_user_cmd_prepare(BLE_SDK_CMD_ADV_PARAM, 3, (int)(interval), (int)(type), (int)(channel))
#define ble_op_set_adv_data(len, data) ble_user_cmd_prepare(BLE_SDK_CMD_ADV_DATA, 2, (int)(len), (void *)(data))
#define ble_op_set_rsp_data(len, data) ble_user_cmd_prepare(BLE_SDK_CMD_RSP_DATA, 2, (int)(len), (void *)(data))
#endif
