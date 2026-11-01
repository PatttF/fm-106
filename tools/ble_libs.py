#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# BLE MIDI test: 2026 DEADACTIVE (from fm1-lsdj 548ce73, Hortator)
"""The JieLi AC79 SDK libraries the BLE build links (Apache-2.0, JieLi; prebuilt LLVM bitcode), pinned by sha256.

  ble_libs.py libs     the library paths (fails on a hash mismatch)
  ble_libs.py pin      print the current hashes (to fill PINS the first time)
  ble_libs.py abi      write firmware/src/ble/ble_sdk_abi.h (sizes from the SDK headers, via the JieLi clang)
Env: AC79_BT_SDK = a full SDK copy (default ~/Downloads/fw-AC79_AIoT_SDK-release-AC79NN_SDK_V1.2.0)."""
import hashlib
import os
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent
SDK = Path(os.environ.get("AC79_BT_SDK") or os.environ.get("AC79_SDK") or (Path.home() / "fw-AC79_AIoT_SDK")).expanduser()
LIBS = ["cpu/wl82/liba/btctrler.a", "cpu/wl82/liba/btstack.a", "cpu/wl82/liba/wl_rf_common.a",
        "cpu/wl82/liba/crypto_toolbox_Osize.a",
        "cpu/wl82/liba/lib_ccm_aes.a", "include_lib/newlib/pi32v2-lib/libcompiler_rt.a"]   # AES-CCM, soft float
# single members of archives that are too entangled to link whole (system.a brings the SDK's own OS, cpu.a its
# start-up): extracted after the archive's hash is checked
MEMBERS = {"cpu/wl82/liba/system.a": ["lbuf.c.o", "circular_buf.c.o"],
           "cpu/wl82/liba/cpu.a": ["wlc.c.o", "encryption.c.o"]}
# The SDK's BT config sources (Apache-2.0), compiled from the SDK (mounted at /sdk) against our
# firmware/src/ble/sdkcfg/app_config.h (BLE central only): the libraries' config_* / CONFIG_* / log_tag_const_*
# constants with the SDK's own types and values. As bitcode (-flto, as the SDK builds them): LTO folds the constants
# into the libraries (BLE only, logs off) and drops what they switch off.
LTO_STUBS = "firmware/src/ble/ble_lto_stubs.c"     # the log functions, empty, also bitcode
LOG_FUNCS = ["printf", "puts", "putchar", "put_buf", "printf_buf", "log_print"]   # -dont-used-symbol-list
SDK_CONFIG_SOURCES = ["apps/common/config/log_config/lib_btctrler_config.c", "apps/common/config/bt_profile_config.c"]
SDK_CFLAGS = ["-mcpu=r3", "-Os", "-flto", "-DSUPPORT_MS_EXTENSIONS", "-DCONFIG_CPU_WL82", "-DCONFIG_FREE_RTOS_ENABLE",
              "-DCONFIG_THREAD_ENABLE", "-D__GCC_PI32V2__", "-DCONFIG_NO_SDRAM_ENABLE", "-DCONFIG_BT_ENABLE=1",
              "-Ifirmware/src/ble/sdkcfg",
              # demo_ble's include list (apps/demo/demo_ble/board/wl82/Makefile INCLUDES), minus its app_config.h dir
              "-I/sdk/include_lib/c++",
              "-I/sdk/include_lib/c++/include",
              "-I/sdk/include_lib/newlib/include",
              "-I/sdk/include_lib/c++/simple_pthread",
              "-I/sdk/include_lib",
              "-I/sdk/include_lib/driver",
              "-I/sdk/include_lib/driver/device",
              "-I/sdk/include_lib/driver/cpu/wl82",
              "-I/sdk/include_lib/system",
              "-I/sdk/include_lib/system/generic",
              "-I/sdk/include_lib/btctrler",
              "-I/sdk/include_lib/btctrler/port/wl82",
              "-I/sdk/include_lib/update",
              "-I/sdk/include_lib/btstack/third_party/common",
              "-I/sdk/include_lib/btstack/third_party/rcsp",
              "-I/sdk/include_lib/utils",
              "-I/sdk/include_lib/utils/syscfg",
              "-I/sdk/include_lib/utils/event",
              "-I/sdk/include_lib/media",
              "-I/sdk/include_lib/media/cpu/wl82",
              "-I/sdk/apps",
              "-I/sdk/apps/common",
              "-I/sdk/apps/common/include",
              "-I/sdk/apps/common/config/include",
              "-I/sdk/include_lib/btstack",
              "-I/sdk/include_lib/net",
              "-I/sdk/include_lib/net/lwip_2_2_0/lwip/src/include",
              "-I/sdk/include_lib/net/lwip_2_2_0/lwip/src/include/lwip",
              "-I/sdk/include_lib/net/lwip_2_2_0/lwip/port",
              "-I/sdk/include_lib/net/lwip_2_2_0/lwip/app",
              "-I/sdk/apps/common/ble/include",
              "-I/sdk/include_lib/utils/btmesh",
              "-I/sdk/include_lib/utils/btmesh/adaptation"]

PINS = {           # AC79NN_SDK_V1.2.0 and V1.2.1
    "cpu/wl82/liba/btctrler.a": {
        "885cd53747753488b380f761318bf30d691f75541dd28adc72457690aa51506a",
        "9ef4262444b49171c4a5eab906e3939b1d4943350ac4993c18d8474d01d773de",
    },
    "cpu/wl82/liba/btstack.a": {
        "d4c02f42e1be97a5288ecbcb5d42e6d0727e2ca4ffa2e0e2e6bb2ae61fede4bc",
        "c3f4ebfade12823c0b160f202bbe626b74bb2dfc5e509a034af965c5da852a2b",
    },
    "cpu/wl82/liba/wl_rf_common.a": {
        "1e6f7cdf0f0956282d251cdf23dffc2dae39c4ca498cb314801dde35b00e1769",
        "ec50bb31af02ee3e2db9d87726c1b18b317f2acd72f233c9ce7b79a89ea324b2",
    },
    "cpu/wl82/liba/crypto_toolbox_Osize.a": {
        "373251c39f8de0851204b623601b348254023e98b3d0b062ecab743b0c3aa35e",
    },
    "cpu/wl82/liba/system.a": {
        "a71bdc75e5afbcffaf7d79bc2b27f2b65fec63d94d2261b584744d3e9b9098ea",
        "b561a1a216097509f62a58c931227b2897826eca75309737cf4b9bace98fb5a5",
    },
    "cpu/wl82/liba/cpu.a": {
        "fe18129bfee7758074c486b2d33b2ed59a8c365600362d982992677c47821b2a",
        "00cea21d3c8d99487821f2c2b203f2c5a04b2cc8421bc05e9b875a0d70e42c62",
    },
    "cpu/wl82/liba/lib_ccm_aes.a": {
        "bd3dd110197ae0084826538bfb5e432a6fadb3a001acadfdbd5f3a409fb5da2c",
    },
    "include_lib/newlib/pi32v2-lib/libcompiler_rt.a": {
        "141997ab6977c0c4abfb857bc32a897eb345f8d593e27e74c33bdebcf3fd5009",
    },
}

SOURCE_PINS = {    # the SDK config sources compiled into the BLE build (bitcode: they decide what LTO keeps)
    "apps/common/config/log_config/lib_btctrler_config.c": {
        "7323d979d70af02110ada6d3a6eaf76ad5a1292ccbbb7ff30a51e9c667d28fe8",
        "2db8f147b138059644a9bbf046cf985f759fd8cc89b1213a7b29d49dfea215a0",
    },
    "apps/common/config/bt_profile_config.c": {
        "1d385cbd6b07e1b8d87d7336debe51b34b414c1feae4366973b18468fac6f7f2",
        "141480d8affe5e14d893b3adb3254ec32978e34137443dfb03ec58216c1be877",
    },
}


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def pin_matches(rel, actual_sha, pins_map):
    expected = pins_map.get(rel)
    if isinstance(expected, set):
        return actual_sha in expected
    return actual_sha == expected


def ar_members(path):
    """GNU ar: {name: bytes} (the "//" long-name table resolved)"""
    data, out, longnames, i = path.read_bytes(), {}, b"", 8
    if data[:8] != b"!<arch>\n":
        sys.exit(f"ble_libs: {path} is not an ar archive")
    while i + 60 <= len(data):
        name, size = data[i:i + 16].decode().strip(), int(data[i + 48:i + 58].decode().strip())
        body = data[i + 60:i + 60 + size]
        if name == "//":
            longnames = body
        elif name.startswith("/") and name[1:].isdigit():
            off = int(name[1:])
            out[longnames[off:longnames.index(b"/\n", off)].decode()] = body
        elif name not in ("/", "/SYM64/"):
            out[name.rstrip("/")] = body
        i += 60 + size + (size & 1)
    return out


def members(outdir):
    """extract MEMBERS (archive hashes checked) into outdir; their paths"""
    paths = []
    for rel, names in MEMBERS.items():
        p = SDK / rel
        if not p.exists():
            sys.exit(f"ble_libs: {p} missing (set AC79_BT_SDK to a full AC79 SDK copy)")
        if not pin_matches(rel, sha(p), PINS):
            sys.exit(f"ble_libs: {rel} not the pinned archive")
        found = ar_members(p)
        for n in names:
            q = Path(outdir) / n
            q.write_bytes(found[n])
            paths.append(q)
    return paths


def libs():
    out = []
    for rel in LIBS:
        p = SDK / rel
        if not p.exists():
            sys.exit(f"ble_libs: {p} missing (set AC79_BT_SDK to a full AC79 SDK copy)")
        if rel not in PINS:
            sys.exit(f"ble_libs: {rel} not pinned (run `ble_libs.py pin` and fill PINS)")
        if not pin_matches(rel, sha(p), PINS):
            sys.exit(f"ble_libs: {rel} sha256 {sha(p)} is not the pinned {PINS[rel]}")
        out.append(p)
    for rel in SOURCE_PINS:
        p = SDK / rel
        if not p.exists():
            sys.exit(f"ble_libs: {p} missing (set AC79_BT_SDK to a full AC79 SDK copy)")
        if not pin_matches(rel, sha(p), SOURCE_PINS):
            sys.exit(f"ble_libs: {rel} sha256 {sha(p)} is not the pinned {SOURCE_PINS[rel]}")
    return out


# the BLE central's SDK values (ble_central.c, ble_sdk.h), read from the SDK headers by compiling them: command
# codes, event codes, enum values, and the layout of every struct the central shares with the libraries
BLE_HEADERS = ["btstack/bluetooth.h", "btstack/btstack_typedef.h", "btstack/le/le_common_define.h",
               "btstack/le/ble_api.h", "btstack/le/att.h", "btstack/le/le_user.h", "btstack/le/sm.h",
               "btstack/avctp_user.h", "event/bt_event.h"]
BLE_VALUES = {
    "CMD_DISCONNECT": "BLE_CMD_DISCONNECT", "CMD_ATT_SEND_INIT": "BLE_CMD_ATT_SEND_INIT",
    "CMD_ATT_SEND_DATA": "BLE_CMD_ATT_SEND_DATA", "CMD_ATT_VAILD_LEN": "BLE_CMD_ATT_VAILD_LEN",
    "CMD_SCAN_PARAM": "BLE_CMD_SCAN_PARAM",
    "CMD_CREATE_CONN": "BLE_CMD_CREATE_CONN", "CMD_CREATE_CONN_CANCEL": "BLE_CMD_CREATE_CONN_CANCEL",
    "CMD_SCAN_ENABLE2": "BLE_CMD_SCAN_ENABLE2", "CMD_SEARCH_PROFILE": "BLE_CMD_SEARCH_PROFILE",
    "PFL_SERVER_ALL": "PFL_SERVER_ALL", "SCAN_ACTIVE": "SCAN_ACTIVE", "ATT_OP_WRITE": "ATT_OP_WRITE",
    "ATT_OP_WRITE_WITHOUT_RESPOND": "ATT_OP_WRITE_WITHOUT_RESPOND",
    "ATT_CTRL_BLOCK_SIZE": "ATT_CTRL_BLOCK_SIZE", "HCI_EVENT_PACKET": "HCI_EVENT_PACKET",
    "HCI_EVENT_LE_META": "HCI_EVENT_LE_META", "HCI_SUBEVENT_LE_CONNECTION_COMPLETE": "HCI_SUBEVENT_LE_CONNECTION_COMPLETE",
    "HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE": "HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE",
    "HCI_EVENT_DISCONNECTION_COMPLETE": "HCI_EVENT_DISCONNECTION_COMPLETE",
    "GAP_EVENT_ADVERTISING_REPORT": "GAP_EVENT_ADVERTISING_REPORT", "GATT_EVENT_NOTIFICATION": "GATT_EVENT_NOTIFICATION",
    "IO_CAPABILITY_NO_INPUT_NO_OUTPUT": "IO_CAPABILITY_NO_INPUT_NO_OUTPUT", "BT_STATUS_INIT_OK": "BT_STATUS_INIT_OK",
    "BT_EVENT_FROM_CON": "BT_EVENT_FROM_CON",
    "SZ_BT_EVENT": "sizeof(struct bt_event)", "SZ_ADV_REPORT": "sizeof(adv_report_t)",
    "OFF_ADV_ADDR_TYPE": "offsetof(adv_report_t, address_type)", "OFF_ADV_ADDR": "offsetof(adv_report_t, address)",
    "OFF_ADV_RSSI": "offsetof(adv_report_t, rssi)", "OFF_ADV_LEN": "offsetof(adv_report_t, length)",
    "OFF_ADV_DATA": "offsetof(adv_report_t, data)",
    "SZ_CONN_PARAM": "sizeof(struct create_conn_param_t)",
    "OFF_CONN_TIMEOUT": "offsetof(struct create_conn_param_t, supervision_timeout)",
    "OFF_CONN_ADDR_TYPE": "offsetof(struct create_conn_param_t, peer_address_type)",
    "OFF_CONN_ADDR": "offsetof(struct create_conn_param_t, peer_address)",
    "SZ_DATA_REPORT": "sizeof(att_data_report_t)", "OFF_DATA_HANDLE": "offsetof(att_data_report_t, value_handle)",
    "OFF_DATA_LEN": "offsetof(att_data_report_t, blob_length)", "OFF_DATA_BLOB": "offsetof(att_data_report_t, blob)",
    "OFF_DATA_CONN": "offsetof(att_data_report_t, conn_handle)",
    "SZ_SEARCH_RESULT": "sizeof(search_result_t)", "OFF_SR_CHAR": "offsetof(search_result_t, characteristic)",
    "OFF_SR_SVC_INDEX": "offsetof(search_result_t, service_index)",
    "OFF_CH_VALUE_HANDLE": "offsetof(charact_report_t, value_handle)",
    "OFF_CH_PROPERTIES": "offsetof(charact_report_t, properties)",
    "OFF_CH_UUID16": "offsetof(charact_report_t, uuid16)", "OFF_CH_UUID128": "offsetof(charact_report_t, uuid128)",
    "SZ_CHARACT": "sizeof(charact_report_t)", "SZ_SERVICE": "sizeof(service_report_t)",
    # the device role (GATT server, advertising; le_trans_data.c)
    "CMD_ADV_ENABLE": "BLE_CMD_ADV_ENABLE", "CMD_ADV_PARAM": "BLE_CMD_ADV_PARAM", "CMD_ADV_DATA": "BLE_CMD_ADV_DATA",
    "CMD_RSP_DATA": "BLE_CMD_RSP_DATA", "ADV_IND": "ADV_IND", "ADV_CHANNEL_ALL": "ADV_CHANNEL_ALL",
    "ATT_OP_AUTO_READ_CCC": "ATT_OP_AUTO_READ_CCC", "SM_EVENT_JUST_WORKS_REQUEST": "SM_EVENT_JUST_WORKS_REQUEST",
    # FM-Link (the device role notifies without the CCCD) and two links at once (ble_api.h BLE_CMD_MULTI_ATT_*,
    # multi_demo/le_multi_client.c)
    "ATT_OP_NOTIFY": "ATT_OP_NOTIFY",
    "CMD_MULTI_ATT_SEND_INIT": "BLE_CMD_MULTI_ATT_SEND_INIT",
    "CMD_MULTI_ATT_SET_CONN_HANDLE": "BLE_CMD_MULTI_ATT_SET_CONN_HANDLE",
    "CMD_MULTI_ATT_SEND_DATA": "BLE_CMD_MULTI_ATT_SEND_DATA", "CMD_MULTI_ATT_VAILD_LEN": "BLE_CMD_MULTI_ATT_VAILD_LEN",
}


def abi():
    probe = SRC / "build" / "ble_abi_probe.c"
    probe.parent.mkdir(exist_ok=True)
    probe.write_text('#include "generic/typedef.h"\n#include "os/os_api.h"\n'
                     "char sz_sem[sizeof(OS_SEM)]; char sz_mutex[sizeof(OS_MUTEX)]; char sz_queue[sizeof(OS_QUEUE)];\n"
                     "char v_taskq[OS_TASKQ]; char v_timeout[OS_TIMEOUT]; char v_qfull[OS_Q_FULL];\n"
                     + "".join(f'#include "{h}"\n' for h in BLE_HEADERS) + "#include <stddef.h>\n"
                     + "".join(f"char b_{n}[({e}) + 1];\n" for n, e in BLE_VALUES.items()))
    tc = Path(os.environ.get("JIELI_TOOLCHAIN", "~/.jieli/toolchain")).expanduser()
    # the flags the SDK's own demo_ble Makefile compiles with (apps/demo/demo_ble/board/wl82/Makefile)
    inc = ["-mcpu=r3", "-DSUPPORT_MS_EXTENSIONS", "-DCONFIG_CPU_WL82", "-DCONFIG_FREE_RTOS_ENABLE",
           "-DCONFIG_THREAD_ENABLE", "-D__GCC_PI32V2__", "-DCONFIG_NO_SDRAM_ENABLE", "-DCONFIG_BT_ENABLE=1"]
    inc += [f"-I/sdk/{d}" for d in ("include_lib/newlib/include", "include_lib", "include_lib/driver",
                                     "include_lib/driver/cpu/wl82", "include_lib/system", "include_lib/system/os",
                                     "include_lib/system/os/FreeRTOS", "include_lib/system/os/FreeRTOS/pi32v2",
                                     "include_lib/utils", "include_lib/btstack", "include_lib/system/generic")]
    cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work", "-v", f"{SDK}:/sdk:ro",
           "-v", f"{tc}:/opt/jieli:ro", "-w", "/work", "debian:bookworm-slim", "/opt/jieli/pi32v2/bin/clang",
           "-target", "pi32v2", "-c", *inc, "build/ble_abi_probe.c", "-o", "build/ble_abi_probe.o"]
    subprocess.run(cmd, check=True)
    nm = subprocess.run(["xcrun", "llvm-nm", "-S", str(SRC / "build/ble_abi_probe.o")], capture_output=True,
                        text=True, check=True).stdout
    size = {ln.split()[3]: int(ln.split()[1], 16) for ln in nm.splitlines() if len(ln.split()) == 4}
    assert size["sz_sem"] == size["sz_mutex"], "OS_SEM and OS_MUTEX differ in size"
    assert (size["v_taskq"], size["v_timeout"], size["v_qfull"]) == (13, 11, 21), "OS return codes changed"
    (SRC / "firmware/src/ble/ble_sdk_abi.h").write_text(f"""/* Generated by tools/ble_libs.py abi from the AC79 SDK headers (do not edit) */
#define BLE_SDK_OS_SEM_SIZE {size['sz_sem']}u
#define BLE_SDK_OS_QUEUE_SIZE {size['sz_queue']}u
#define BLE_SDK_OS_NO_ERR 0
#define BLE_SDK_OS_TIMEOUT 11
#define BLE_SDK_OS_TASKQ 13
#define BLE_SDK_OS_Q_FULL 21
#define BLE_SDK_Q_MSG 0x100000
#define BLE_SDK_Q_EVENT 0x200000
#define BLE_SDK_Q_CALLBACK 0x300000
#define BLE_SDK_Q_USER 0x400000
#define BLE_SDK_IRQ_BLE_RX 29
#define BLE_SDK_IRQ_BLE_EVENT 45
""" + "".join(f"#define BLE_SDK_{n} {size['b_' + n] - 1}\n" for n in BLE_VALUES))
    print("wrote firmware/src/ble/ble_sdk_abi.h", size)


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "libs"
    if what == "pin":
        for rel in LIBS:
            print(f'    "{rel}": "{sha(SDK / rel)}",')
    elif what == "abi":
        abi()
    else:
        print("\n".join(str(p) for p in libs()))
