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
    args = parser.parse_args()

    actions = [args.dump is not None, args.dump_all, args.dump_user, args.burn_flash, args.restore_factory,
               args.load_syx, args.load_106, args.load_json]
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

if __name__ == "__main__":
    main()
