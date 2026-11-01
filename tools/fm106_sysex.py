#!/usr/bin/env python3
"""
FM-106 CLI SysEx Tool
Librarian, patch dumper, and flash manager for the FM-106 synth running on M-VAVE FM-1.
Supports Roland Juno-106 SysEx, Jarvik7 Librarian (.106), and web editor JSON files.
"""
import argparse
import io
import json
import struct
import sys
import time
import os
import mido

PORT_NAME = "FM-106"

def open_fm1_port():
    names = mido.get_ioport_names()
    target = None
    for name in names:
        if "fm-106" in name.lower() or "felucca" in name.lower() or "fm-1" in name.lower():
            target = name
            break
    if not target:
        if PORT_NAME in names:
            target = PORT_NAME
        else:
            print(f"Error: FM-106 MIDI port not found. Available ports: {names}", file=sys.stderr)
            sys.exit(1)
    return mido.open_ioport(target)

def index_to_slot(idx):
    bank_idx = (idx // 64) % 4
    sub = idx % 64
    grp = (sub // 8) + 1
    ptc = (sub % 8) + 1
    prefix = ["A", "B", "UA", "UB"][bank_idx]
    return f"{prefix}{grp}{ptc}"

def dump_patch(port, slot_idx):
    bank_idx = (slot_idx // 64) % 4
    local_idx = slot_idx % 64
    bank_opt = bank_idx + 1  # 1=Fact A, 2=Fact B, 3=User A, 4=User B
    ch_byte = (bank_opt << 4) & 0x70
    req = mido.Message("sysex", data=[0x41, 0x34, ch_byte, local_idx & 0x7F])
    port.send(req)
    t0 = time.time()
    while time.time() - t0 < 0.5:
        for msg in port.iter_pending():
            if msg.type == "sysex" and len(msg.data) >= 22 and msg.data[0] == 0x41 and msg.data[1] == 0x30:
                return list(msg.data)
        time.sleep(0.01)
    return None

def parse_j106_file(filepath):
    """Parse Jarvik7 .106 binary file format."""
    with open(filepath, "rb") as f:
        raw = f.read()
    if len(raw) < 30 or raw[:6] != b"!j106\\":
        return None
    s = io.BytesIO(raw[6:])
    def read_utf():
        l = struct.unpack(">I", s.read(4))[0]
        return s.read(l).decode("utf-8", errors="replace")

    header = read_utf()
    if header != "Library":
        return None
    major, minor = struct.unpack(">II", s.read(8))
    lib_name = read_utf()
    num_patches, flags = struct.unpack(">qq", s.read(16))
    crc_header = struct.unpack(">q", s.read(8))[0]

    key = [34, 68, 40, 103, 61, 43, 49, 45, 71, 124, 6, 50, 46, 50, 32, 104]
    patches = []
    for idx in range(num_patches):
        pname = read_utf()
        if minor == 1:
            read_utf() # uuid
        pos_tag = struct.unpack(">q", s.read(8))[0]
        salt = ((idx + 79) * 137 - 31)
        params = []
        for k in range(18):
            s_val = struct.unpack(">h", s.read(2))[0]
            k_val = key[(k + salt) % 16]
            params.append(s_val // k_val)
        crc_p = struct.unpack(">q", s.read(8))[0]
        patches.append((pname, params))
    return lib_name, patches

def send_preset_to_slot(port, slot_idx, name, sliders, sw1, sw2):
    # Map to user preset space: User A is 0..63, User B is 64..127
    u_idx = (slot_idx - 128) if slot_idx >= 128 else (slot_idx % 128)
    bank_opt = 3 if u_idx < 64 else 4
    local_slot = u_idx % 64
    ch_byte = (bank_opt << 4) & 0x70
    name_bytes = [ord(c) for c in (name or f"Slot {index_to_slot(128 + u_idx)}").ljust(16)[:16]]
    # 0x30 APR store: 41 30 ch_byte slot [16 sliders] sw1 sw2 [16 name bytes]
    msg = [0x41, 0x30, ch_byte, local_slot & 0x7F] + sliders + [sw1 & 0x7F, sw2 & 0x7F] + name_bytes
    port.send(mido.Message("sysex", data=msg))

def main():
    parser = argparse.ArgumentParser(description="FM-106 Roland Juno-106 / Jarvik7 SysEx Librarian CLI")
    parser.add_argument("--dump", type=int, metavar="SLOT", help="Dump patch by slot index (0..255)")
    parser.add_argument("--dump-all", action="store_true", help="Dump all 256 presets (Factory A/B + User A/B)")
    parser.add_argument("--dump-user", action="store_true", help="Dump 128 user presets (User A & User B)")
    parser.add_argument("--burn-flash", action="store_true", help="Commit RAM user presets to SPI flash")
    parser.add_argument("--restore-factory", action="store_true", help="Restore hardware user presets to factory defaults")
    parser.add_argument("--save-syx", metavar="FILE", help="Save dumped patches to .syx file")
    parser.add_argument("--load-syx", metavar="FILE", help="Send standard .syx patch or bank file to FM-1")
    parser.add_argument("--load-106", metavar="FILE", help="Load Jarvik7 .106 library file to FM-1 hardware")
    parser.add_argument("--load-json", metavar="FILE", help="Load Jarvik7 lib.json or backup JSON to FM-1 hardware")
    parser.add_argument("--crash", action="store_true", help="Query crash registers, exception status and BLE diag")
    parser.add_argument("--ble-on", action="store_true", help="Turn Bluetooth ON via SysEx")
    parser.add_argument("--ble-off", action="store_true", help="Turn Bluetooth OFF via SysEx")
    parser.add_argument("--ble-scan", action="store_true", help="Start Bluetooth scan via SysEx")
    args = parser.parse_args()

    actions = [args.dump is not None, args.dump_all, args.dump_user, args.burn_flash, args.restore_factory,
               args.load_syx, args.load_106, args.load_json, args.crash, args.ble_on, args.ble_off, args.ble_scan]
    if not any(actions):
        parser.print_help()
        sys.exit(0)

    with open_fm1_port() as port:
        if args.restore_factory:
            print("Sending factory reset command (0x36)...")
            port.send(mido.Message("sysex", data=[0x41, 0x36, 0x00, 0x01]))
            time.sleep(0.05)
            print("Committing factory presets to SPI flash...")
            port.send(mido.Message("sysex", data=[0x41, 0x35, 0x00, 0x01]))
            time.sleep(0.3)
            print("Hardware successfully restored to factory defaults!")

        elif args.burn_flash:
            print("Committing RAM user presets to SPI flash...")
            port.send(mido.Message("sysex", data=[0x41, 0x35, 0x00, 0x01]))
            t0 = time.time()
            ack = False
            while time.time() - t0 < 1.0:
                for msg in port.iter_pending():
                    if msg.type == "sysex" and len(msg.data) >= 4 and msg.data[1] == 0x35 and msg.data[3] == 0x00:
                        ack = True
                        break
                if ack: break
                time.sleep(0.02)
            if ack:
                print("Commit confirmed! User presets saved to SPI flash.")
            else:
                print("Flash commit sent.")

        elif args.dump is not None:
            idx = args.dump
            slot = index_to_slot(idx)
            data = dump_patch(port, idx)
            if data:
                print(f"Slot {slot} (index {idx})")
                print(f"Sliders (16): {data[4:20]}")
                print(f"SW1: 0x{data[20]:02X}, SW2: 0x{data[21]:02X}")
                if args.save_syx:
                    syx_bytes = bytes([0xF0] + data + [0xF7])
                    with open(args.save_syx, "wb") as f:
                        f.write(syx_bytes)
                    print(f"Saved to {args.save_syx}")
            else:
                print(f"Failed to dump patch {idx} from FM-106", file=sys.stderr)

        elif args.dump_all or args.dump_user:
            start_i = 128 if args.dump_user else 0
            end_i = 256
            count = end_i - start_i
            print(f"Dumping {count} presets from FM-106 (slots {start_i}..{end_i-1})...")
            all_syx = bytearray()
            for i in range(start_i, end_i):
                data = dump_patch(port, i)
                if data:
                    print(f"[{index_to_slot(i)}] Dumped")
                    all_syx.extend([0xF0] + data + [0xF7])
                else:
                    print(f"[{index_to_slot(i)}] Error reading")
                time.sleep(0.02)
            if args.save_syx:
                with open(args.save_syx, "wb") as f:
                    f.write(all_syx)
                print(f"\nSaved {count} presets ({len(all_syx)} bytes) to {args.save_syx}")

        elif args.load_106:
            res = parse_j106_file(args.load_106)
            if not res:
                print(f"Error: {args.load_106} is not a valid Jarvik7 .106 library file.", file=sys.stderr)
                sys.exit(1)
            lib_name, patches = res
            print(f"Loading Jarvik7 library '{lib_name}' ({len(patches)} patches)...")
            for i, (name, params) in enumerate(patches[:128]):
                send_preset_to_slot(port, i, name, params[:16], params[16], params[17])
                print(f"  [{index_to_slot(i)}] {name}")
                time.sleep(0.02)
            print(f"Transferred {min(len(patches), 128)} patches to FM-1 hardware RAM.")
            print("Hint: Run with --burn-flash to persist to SPI flash.")

        elif args.load_json:
            with open(args.load_json, "r") as f:
                data = json.load(f)
            count = 0
            for i, p in enumerate(data[:128]):
                if "data" in p and len(p["data"]) == 18:
                    # Jarvik7 lib.json
                    send_preset_to_slot(port, i, p.get("name"), p["data"][:16], p["data"][16], p["data"][17])
                    count += 1
                elif "sliders" in p and len(p["sliders"]) == 16:
                    send_preset_to_slot(port, i, p.get("name"), p["sliders"], p.get("sw1", 0), p.get("sw2", 0))
                    count += 1
                time.sleep(0.02)
            print(f"Transferred {count} JSON patches to FM-1 hardware RAM.")

        elif args.load_syx:
            with open(args.load_syx, "rb") as f:
                content = f.read()
            pos = 0
            count = 0
            while pos < len(content):
                if content[pos] == 0xF0 and pos + 2 < len(content) and content[pos+1] == 0x41:
                    end = content.find(b"\xF7", pos)
                    if end != -1:
                        msg_data = list(content[pos+1:end])
                        port.send(mido.Message("sysex", data=msg_data))
                        count += 1
                        pos = end + 1
                        time.sleep(0.02)
                        continue
                pos += 1
            print(f"Transferred {count} SysEx messages to FM-1 hardware.")

        elif args.ble_on:
            print("Sending Bluetooth ON (41 3B 00 01)...")
            port.send(mido.Message("sysex", data=[0x41, 0x3B, 0x00, 0x01]))
            time.sleep(0.1)
            print("Command sent.")

        elif args.ble_off:
            print("Sending Bluetooth OFF (41 3B 00 02)...")
            port.send(mido.Message("sysex", data=[0x41, 0x3B, 0x00, 0x02]))
            time.sleep(0.1)
            print("Command sent.")

        elif args.ble_scan:
            print("Sending Bluetooth Scan Start (41 3B 00 03)...")
            port.send(mido.Message("sysex", data=[0x41, 0x3B, 0x00, 0x03]))
            time.sleep(0.1)
            print("Command sent.")

        elif args.crash:
            # Send crash query 41 3A 00
            for _ in port.iter_pending(): pass
            port.send(mido.Message("sysex", data=[0x41, 0x3A, 0x00]))
            t0 = time.time()
            data = None
            while time.time() - t0 < 1.0 and not data:
                for msg in port.iter_pending():
                    if msg.type == "sysex" and len(msg.data) >= 4 and msg.data[0] == 0x41 and msg.data[1] == 0x3A:
                        data = list(msg.data[3:]) # raw 7-bit bytes after cmd/ch
                        break
                time.sleep(0.01)

            if not data:
                print("No crash response received from FM-106.")
                return

            words = []
            for i in range(0, len(data) - 4, 5):
                chunk = data[i:i+5]
                w = chunk[0] | (chunk[1] << 7) | (chunk[2] << 14) | (chunk[3] << 21) | (chunk[4] << 28)
                words.append(w)

            # Load symbol table if available
            syms = {}
            if os.path.exists("build/fm106.elf"):
                try:
                    import subprocess
                    from pathlib import Path
                    tc = os.environ.get("JIELI_TOOLCHAIN") or str(Path.home() / ".jieli" / "toolchain")
                    out = subprocess.check_output([
                        "docker", "run", "--rm", "--platform", "linux/amd64",
                        "-v", f"{os.getcwd()}:/work",
                        "-v", f"{tc}:/opt/jieli:ro",
                        "-w", "/work",
                        "debian:bookworm-slim",
                        "/opt/jieli/pi32v2/bin/nm", "-n", "build/fm106.elf"
                    ], text=True)
                    for l in out.splitlines():
                        p = l.split()
                        if len(p) >= 3 and p[1].upper() in ('T', 'W'):
                            syms[int(p[0], 16)] = p[2]
                except Exception:
                    pass

            def resolve(addr):
                if not addr or not syms: return ""
                match = None
                m_addr = 0
                for a, name in sorted(syms.items()):
                    if a <= addr:
                        match = name
                        m_addr = a
                    else:
                        break
                if match:
                    return f"({match} + 0x{addr - m_addr:X})"
                return ""

            print(f"=== CRASH RECORD ({len(words)} words decoded) ===")
            if len(words) >= 16:
                magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime = words[0:11]
                etm = words[11:15]
                early = words[15]
                print(f"Magic : 0x{magic:08X} {'(VALID CRASH)' if magic == 0x43525348 else '(NONE)'}")
                print(f"Count : {count}")
                print(f"VEC   : 0x{vec:08X} ({vec})")
                print(f"PC    : 0x{pc:08X} {resolve(pc)}")
                print(f"RETS  : 0x{rets:08X} {resolve(rets)}")
                print(f"EMU   : 0x{emu:08X}")
                print(f"DBG   : 0x{dbg:08X}")
                print(f"SP    : 0x{sp:08X}")
                print(f"PSR   : 0x{psr:08X}")
                print(f"ICFG  : 0x{icfg:08X}")
                print(f"Uptime: {uptime} ms")
                print(f"Early : {early}")
                etm_strs = [f"0x{x:08X} {resolve(x)}".strip() for x in etm]
                print(f"ETM   : {etm_strs}")

            if len(words) >= 36:
                ctx = words[16:36]
                c_count, c_pc = ctx[0], ctx[1]
                r = ctx[2:18]
                usp, ssp = ctx[18], ctx[19]
                print(f"\n=== REGISTERS AT CRASH (count {c_count}) ===")
                print(f"PC : 0x{c_pc:08X} {resolve(c_pc)}")
                for row in range(4):
                    line = "  ".join(f"R{row*4+col:02d}: 0x{r[row*4+col]:08X}" for col in range(4))
                    print(line)
                print(f"USP: 0x{usp:08X}   SSP: 0x{ssp:08X}")

            if len(words) >= 52:
                bd = words[36:52]
                st_names = ["OFF", "STARTING", "IDLE", "SCANNING", "CONNECTING", "DISCOVERING", "SUBSCRIBED", "RECEIVING", "FAILED", "ADVERTISING"]
                st_str = st_names[bd[13]] if bd[13] < len(st_names) else str(bd[13])
                print(f"\n=== BLE DIAGNOSTICS ===")
                print(f"Magic       : 0x{bd[0]:08X}")
                print(f"Stage       : {bd[1]}")
                print(f"Task        : {bd[2]}")
                print(f"Starts      : {bd[3]}")
                print(f"Trim        : {bd[4]}")
                print(f"Delays      : {bd[5]} (last {bd[6]} us, T4 stalls {bd[7]})")
                print(f"IRQ leaks   : {bd[8]}")
                print(f"Fatal       : {bd[9]}")
                print(f"Heap high   : {bd[10]}")
                print(f"Alloc fails : {bd[11]}")
                print(f"IRQ lowered : {bd[12]}")
                print(f"BLE State   : {st_str}")
                print(f"Scan On     : {bd[14]}")
                print(f"Scan Want   : {bd[15]}")

            if len(words) >= 60:
                sd = words[52:60]
                p3_rst = sd[0]
                rst_reasons = []
                if p3_rst & (1 << 0): rst_reasons.append("POWER_ON")
                if p3_rst & (1 << 1): rst_reasons.append("VDDIO_LOW")
                if p3_rst & (1 << 2): rst_reasons.append("WDT_RESET")
                if p3_rst & (1 << 3): rst_reasons.append("VCM")
                if p3_rst & (1 << 4): rst_reasons.append("LONG_PRESS")
                if p3_rst & (1 << 5): rst_reasons.append("1.2V_LOW")
                if p3_rst & (1 << 6): rst_reasons.append("SOFT_P33")
                rst_str = "|".join(rst_reasons) if rst_reasons else f"0x{p3_rst:02X}"
                where_step = sd[1]
                bt_st = sd[2]
                st_cur = bt_st & 0xFF
                st_fails = (bt_st >> 8) & 0xFF
                st_dirty = (bt_st >> 16) & 0xFF
                st_loaded = (bt_st >> 24) & 0xFF
                print(f"\n=== SYSTEM DIAGNOSTICS ===")
                print(f"Reset Cause : 0x{p3_rst:02X} ({rst_str})")
                print(f"Last Step   : 0x{where_step:02X} ({where_step})")
                print(f"BT Store    : cur={st_cur} fails={st_fails} dirty={st_dirty} loaded={st_loaded}")
                print(f"AW Timeouts : {sd[3]}")
                print(f"AW Max Wait : {sd[4]} ticks ({sd[4] // 240} us)")
                print(f"Audio Lates : {sd[5]}")
                print(f"Uptime ms   : {sd[6]}")
                print(f"AW Jobs     : {sd[7]}")

if __name__ == "__main__":
    main()

