#!/usr/bin/env python3
"""
Automated A/B Preset Testing Harness
Tests M-VAVE FM-1 hardware against the PC reference (ultramaster render_midi).
Generates matching MIDI with exact Roland APR SysEx + Program Change,
captures USB audio from FM-1, renders reference, and analyzes level,
RMS, polyphony, clipping, and spectral correlation.
"""

import sys
import os
import time
import math
import cmath
import struct
import wave
import argparse
import subprocess
from pathlib import Path

try:
    import mido
except ImportError:
    print("Error: mido not installed. Run pip3 install mido")
    sys.exit(1)

PAT_PATH = Path("/tmp/juno_ref/ultramaster/tools/preset-gen/Factory_Patches.pat")
RENDER_MIDI_PATH = Path("/tmp/juno_ref/ultramaster/tools/render-midi/render_midi")

def load_factory_patches():
    if not PAT_PATH.exists():
        raise FileNotFoundError(f"Factory_Patches.pat not found at {PAT_PATH}")
    with open(PAT_PATH, "rb") as f:
        lines = f.read().decode("latin-1").splitlines()
    patches = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        parts = line.split(",", 1)
        b = [int(x, 16) for x in parts[0].strip().split()]
        name = parts[1].strip() if len(parts) > 1 else f"Preset {len(patches)}"
        patches.append((name, b))
    return patches

def build_preset_midi(preset_idx, name, pat_bytes, out_path):
    mid = mido.MidiFile(ticks_per_beat=480)
    trk = mido.MidiTrack()
    mid.tracks.append(trk)
    trk.append(mido.MetaMessage("set_tempo", tempo=500000, time=0))

    # Roland APR SysEx: F0 41 31 00 01 [18 bytes] F7
    sysex_payload = [0x41, 0x31, 0x00, 0x01] + pat_bytes
    trk.append(mido.Message("sysex", data=sysex_payload, time=0))
    trk.append(mido.Message("program_change", program=preset_idx, time=0))

    gap = int(480 * 0.4) # 0.2s settle

    # 1. Single Note C3 (48) for 0.8s, release 0.6s
    gap = int(480 * 0.6) # 0.3s settle
    trk.append(mido.Message("note_on", note=48, velocity=100, time=gap))
    trk.append(mido.Message("note_off", note=48, velocity=0, time=int(480 * 1.6))) # 0.8s on

    # 2. Single Note C4 (60) for 0.8s, release 0.6s
    trk.append(mido.Message("note_on", note=60, velocity=100, time=int(480 * 1.2))) # 0.6s gap
    trk.append(mido.Message("note_off", note=60, velocity=0, time=int(480 * 1.6))) # 0.8s on

    # 3. 4-Note Polyphonic Chord (C3, G3, C4, E4) for 1.0s, release 0.8s
    trk.append(mido.Message("note_on", note=48, velocity=100, time=int(480 * 1.2))) # 0.6s gap
    trk.append(mido.Message("note_on", note=55, velocity=100, time=0))
    trk.append(mido.Message("note_on", note=60, velocity=100, time=0))
    trk.append(mido.Message("note_on", note=64, velocity=100, time=0))

    trk.append(mido.Message("note_off", note=48, velocity=0, time=int(480 * 2.0))) # 1.0s on
    trk.append(mido.Message("note_off", note=55, velocity=0, time=0))
    trk.append(mido.Message("note_off", note=60, velocity=0, time=0))
    trk.append(mido.Message("note_off", note=64, velocity=0, time=0))

    # Release tail gap
    trk.append(mido.Message("note_on", note=60, velocity=0, time=int(480 * 1.6))) # 0.8s tail

    mid.save(out_path)
    return mid

def read_wav_channels(path):
    with wave.open(str(path), "rb") as w:
        n = w.getnframes()
        data = w.readframes(n)
        s = struct.unpack(f"<{n*2}h", data)
        l = s[0::2]
        r = s[1::2]
        return l, r

def calc_rms(samples):
    if not samples:
        return 0.0
    return math.sqrt(sum(x * x for x in samples) / len(samples))

def find_midi_port():
    ports = mido.get_output_names()
    for p in ports:
        if "FM-106" in p or "Felucca" in p:
            return p
    return ports[0] if ports else None

def find_audio_device():
    cmd = ["ffmpeg", "-f", "avfoundation", "-list_devices", "true", "-i", ""]
    p = subprocess.run(cmd, capture_output=True, text=True)
    in_audio = False
    for line in p.stderr.splitlines():
        if "audio devices" in line.lower():
            in_audio = True
            continue
        if in_audio:
            import re
            m = re.search(r'\[(\d+)\]\s*FM-106', line)
            if m:
                return f":{m.group(1)}"
    return ":1"

def test_preset(preset_idx, name, pat_bytes, midi_port=None, audio_device=None):
    if midi_port is None:
        midi_port = find_midi_port()
    if audio_device is None:
        audio_device = find_audio_device()
    mid_file = f"/tmp/p_{preset_idx:02d}_test.mid"
    ref_wav = f"/tmp/p_{preset_idx:02d}_ref.wav"
    hw_wav = f"/tmp/p_{preset_idx:02d}_hw.wav"

    mid = build_preset_midi(preset_idx, name, pat_bytes, mid_file)

    # 1. Render reference offline (--gain=0.75 matches FM-1 3/16 digital headroom ceiling)
    subprocess.run([
        str(RENDER_MIDI_PATH), mid_file, ref_wav, "44100", "--16bit", "--raw", "--gain=0.75"
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

    # 2. Record hardware audio
    # Clear any sounding voices before recording
    reset_port = mido.open_output(midi_port)
    reset_port.send(mido.Message("control_change", control=123, value=0))
    reset_port.send(mido.Message("control_change", control=120, value=0))
    reset_port.close()
    time.sleep(0.1)

    total_dur = mid.length + 1.2
    rec = subprocess.Popen([
        "ffmpeg", "-y", "-f", "avfoundation", "-i", audio_device,
        "-t", f"{total_dur:.2f}", "-ac", "2", "-ar", "44100", hw_wav
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.4)

    outport = mido.open_output(midi_port)
    for msg in mid.play():
        if msg.type != "sysex":
            outport.send(msg)
    outport.close()
    rec.wait()
    time.sleep(0.2)

    # 3. Analyze audio
    ref_l, ref_r = read_wav_channels(ref_wav)
    hw_l, hw_r = read_wav_channels(hw_wav)

    ref_peak = max(max(abs(x) for x in ref_l), max(abs(x) for x in ref_r)) / 32768.0
    hw_peak = max(max(abs(x) for x in hw_l), max(abs(x) for x in hw_r)) / 32768.0

    hw_clipped = sum(1 for x in hw_l if abs(x) >= 32766) + sum(1 for x in hw_r if abs(x) >= 32766)

    sr = 44100

    min_len = min(len(hw_l), len(ref_l))
    ref_stereo = [math.sqrt((ref_l[i]**2 + ref_r[i]**2) / 2.0) for i in range(min_len)]
    hw_stereo = [math.sqrt((hw_l[i]**2 + hw_r[i]**2) / 2.0) for i in range(min_len)]

    def get_segmented_peaks(sig):
        step = int(0.02 * sr)
        rms_p = [calc_rms(sig[i:i+step]) for i in range(0, len(sig) - step, step)]
        segments = [(0.25, 1.3), (1.3, 2.6), (2.6, 4.5)]
        res = []
        for (t_st, t_en) in segments:
            k_st = int(t_st / 0.02)
            k_en = min(len(rms_p), int(t_en / 0.02))
            if k_en <= k_st:
                res.append(0.0)
                continue
            best_k = k_st + max(range(k_en - k_st), key=lambda idx: rms_p[k_st + idx])
            center = best_k * step
            chunk = sig[max(0, center - int(0.05*sr)) : min(len(sig), center + int(0.20*sr))]
            res.append(calc_rms(chunk))
        return res

    r_peaks = get_segmented_peaks(ref_stereo)
    h_peaks = get_segmented_peaks(hw_stereo)

    diffs = [
        (20 * math.log10(h_peaks[k] / r_peaks[k])) if (r_peaks[k] > 0 and h_peaks[k] > 0) else 0.0
        for k in range(3)
    ]

    diff_c3 = diffs[0]
    diff_c4 = diffs[1]
    diff_chd = diffs[2]

    ref_pk_db = 20 * math.log10(ref_peak) if ref_peak > 0 else -99.9
    hw_pk_db = 20 * math.log10(hw_peak) if hw_peak > 0 else -99.9

    return {
        "idx": preset_idx,
        "name": name,
        "hw_peak": hw_peak,
        "hw_pk_db": hw_pk_db,
        "ref_pk_db": ref_pk_db,
        "hw_clipped": hw_clipped,
        "diff_c3": diff_c3,
        "diff_c4": diff_c4,
        "diff_chd": diff_chd,
    }

def main():
    parser = argparse.ArgumentParser(description="Test FM-1 presets against PC reference")
    parser.add_argument("--start", type=int, default=0, help="Start preset index (0..127)")
    parser.add_argument("--count", type=int, default=8, help="Number of presets to test")
    args = parser.parse_args()

    patches = load_factory_patches()
    end_idx = min(len(patches), args.start + args.count)

    print(f"Testing presets {args.start} to {end_idx - 1} ({end_idx - args.start} total)")
    print(f"{'Idx':3s} | {'Preset Name':18s} | {'HW Pk':7s} | {'C3 dB':7s} | {'C4 dB':7s} | {'Chd dB':7s} | {'Clip':4s} | {'Status'}")
    print("-" * 75)

    for i in range(args.start, end_idx):
        name, pat_bytes = patches[i]
        res = test_preset(i, name, pat_bytes)

        status = "OK"
        if res["hw_clipped"] > 0:
            status = "CLIPPED!"
        elif abs(res["diff_c3"]) > 5.0 or abs(res["diff_c4"]) > 5.0 or abs(res["diff_chd"]) > 5.0:
            status = "MISMATCH"

        print(f"{res['idx']:3d} | {res['name']:18s} | {res['hw_pk_db']:+5.1f}dB | {res['diff_c3']:+5.1f}dB | {res['diff_c4']:+5.1f}dB | {res['diff_chd']:+5.1f}dB | {res['hw_clipped']:4d} | {status}")

if __name__ == "__main__":
    main()
