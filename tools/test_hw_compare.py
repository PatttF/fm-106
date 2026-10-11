#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Hardware vs Reference A/B test tool for FM-106 (Roland Juno-106 on M-VAVE FM-1).

Features:
- Select presets via standard MIDI Program Change (0..127).
- Play MIDI note sequences to connected FM-1 hardware over USB-MIDI.
- Capture real-time audio from FM-106 USB Audio interface via AVFoundation.
- Render identical MIDI through reference PC ultramaster engine.
- Compare pitch, levels (Peak/RMS), and spectral characteristics.
"""

import argparse
import cmath
import math
import os
import re
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path

# Paths
REPO_ROOT = Path(__file__).resolve().parents[1]
REF_ROOT = Path("/tmp/juno_ref/ultramaster")
RENDER_BIN = REF_ROOT / "tools" / "render-midi" / "render_midi"
GEN_PRESET_BIN = REF_ROOT / "tools" / "preset-midi" / "gen_preset_midi"

PRESETS_HEADER = REPO_ROOT / "firmware" / "src" / "fm106_presets.h"

def load_presets():
    """Load list of 128 presets from fm106_presets.h."""
    presets = []
    if not PRESETS_HEADER.exists():
        return presets
    with open(PRESETS_HEADER, "r") as f:
        for line in f:
            m = re.search(r'\{\s*"([^"]+)"\s*,\s*\{([^}]+)\}\s*\}', line)
            if m:
                presets.append(m.group(1).strip())
    return presets

def find_midi_port():
    """Find Felucca USB-MIDI output port."""
    try:
        import mido
    except ImportError:
        sys.exit("Error: 'mido' module not found. Install via: pip3 install mido python-rtmidi")
    
    ports = mido.get_output_names()
    for p in ports:
        if "Felucca" in p:
            return p
    if ports:
        return ports[0]
    return None

def find_audio_device():
    """Find AVFoundation audio device index for FM-106."""
    cmd = ["ffmpeg", "-f", "avfoundation", "-list_devices", "true", "-i", ""]
    p = subprocess.run(cmd, capture_output=True, text=True)
    out = p.stderr
    
    in_audio = False
    for line in out.splitlines():
        if "audio devices" in line.lower():
            in_audio = True
            continue
        if in_audio:
            m = re.search(r'\[(\d+)\]\s*FM-106', line)
            if m:
                return m.group(1)
            # End of audio section
            if "[" in line and "]" in line and "video" in line.lower():
                break
    return "0"  # default to :0

def read_wav_mono(filepath):
    """Read WAV file and return (samples_normalized, sample_rate)."""
    with wave.open(str(filepath), "rb") as w:
        nframes = w.getnframes()
        sr = w.getframerate()
        nch = w.getnchannels()
        sampwidth = w.getsampwidth()
        raw = w.readframes(nframes)

    if sampwidth == 2:
        fmt = f"<{nframes * nch}h"
        scale = 32768.0
    elif sampwidth == 3:
        # 24-bit
        samples = []
        for i in range(0, len(raw), 3):
            b = raw[i:i+3]
            val = int.from_bytes(b, byteorder="little", signed=True)
            samples.append(val / 8388608.0)
        if nch == 2:
            left = samples[0::2]
            right = samples[1::2]
            return [(l + r) * 0.5 for l, r in zip(left, right)], sr
        return samples, sr
    else:
        raise ValueError(f"Unsupported sample width: {sampwidth}")

    data = struct.unpack(fmt, raw)
    if nch == 2:
        left = [data[i * 2] / scale for i in range(nframes)]
        right = [data[i * 2 + 1] / scale for i in range(nframes)]
        mono = [(l + r) * 0.5 for l, r in zip(left, right)]
    else:
        mono = [s / scale for s in data]
    return mono, sr

def compute_dft_peaks(signal, sr, f_min=50.0, f_max=1200.0, f_step=1.0):
    """Find dominant frequency in signal window using discrete Fourier transform."""
    N = len(signal)
    if N < 64:
        return 0.0, 0.0
    
    # Hann window
    w = [signal[i] * 0.5 * (1.0 - math.cos(2 * math.pi * i / N)) for i in range(N)]
    
    best_mag = -1.0
    best_f = 0.0
    
    f = f_min
    while f <= f_max:
        k = f * N / sr
        acc = sum(w[n] * cmath.exp(-2j * math.pi * k * n / N) for n in range(N))
        mag = abs(acc)
        if mag > best_mag:
            best_mag = mag
            best_f = f
        f += f_step
        
    return best_f, best_mag

def generate_midi_file(preset_idx, quick=False, out_mid=Path("/tmp/test_preset.mid")):
    """Generate MIDI file for preset test using gen_preset_midi or custom short pattern."""
    import mido
    if quick:
        # 3.5-second quick pattern: chord C4/E4/G4, then single A4
        mid = mido.MidiFile(ticks_per_beat=480)
        track = mido.MidiTrack()
        mid.tracks.append(track)
        
        track.append(mido.MetaMessage("set_tempo", tempo=500000, time=0))
        # Program change
        track.append(mido.Message("program_change", program=preset_idx, time=0))
        
        # Settle
        time_settle = int(480 * 0.2)
        # Note 60 (C4)
        track.append(mido.Message("note_on", note=60, velocity=100, time=time_settle))
        track.append(mido.Message("note_off", note=60, velocity=0, time=int(480 * 1.5)))
        
        # Settle
        track.append(mido.Message("note_on", note=69, velocity=100, time=int(480 * 0.3)))
        track.append(mido.Message("note_off", note=69, velocity=0, time=int(480 * 1.5)))
        
        mid.save(str(out_mid))
        return out_mid
    else:
        # Use ultramaster gen_preset_midi
        cmd = [str(GEN_PRESET_BIN), str(preset_idx)]
        subprocess.run(cmd, cwd="/tmp", check=True, stdout=subprocess.DEVNULL)
        
        # Find generated .mid in /tmp
        presets = load_presets()
        name = presets[preset_idx] if preset_idx < len(presets) else f"preset_{preset_idx}"
        clean = re.sub(r"[^a-zA-Z0-9]+", "_", name).strip("_")
        mid_path = Path("/tmp") / f"{clean}.mid"
        if not mid_path.exists():
            # Search newest .mid in /tmp
            mids = sorted(Path("/tmp").glob("*.mid"), key=os.path.getmtime, reverse=True)
            if mids:
                mid_path = mids[0]
        return mid_path

def run_test(preset_idx=0, quick=False, out_dir=Path("/tmp")):
    import mido
    presets = load_presets()
    pname = presets[preset_idx] if preset_idx < len(presets) else f"Preset {preset_idx}"
    print(f"\n========================================================")
    print(f"  FM-106 Hardware vs Ultramaster Reference Test")
    print(f"  Target: Preset {preset_idx:02d} ({pname})")
    print(f"  Mode:   {'Quick (3.5s)' if quick else 'Full Scale (11s)'}")
    print(f"========================================================\n")
    
    midi_port_name = find_midi_port()
    if not midi_port_name:
        sys.exit("Error: Felucca MIDI port not found. Ensure FM-1 is connected.")
    print(f"[1/5] Connected to MIDI port: {midi_port_name}")
    
    audio_dev = find_audio_device()
    print(f"[2/5] Using AVFoundation Audio Input: :{audio_dev} (FM-106)")
    
    # 1. Generate MIDI file
    mid_file = generate_midi_file(preset_idx, quick=quick)
    print(f"[3/5] Test MIDI file: {mid_file}")
    
    # Read duration
    mid_obj = mido.MidiFile(str(mid_file))
    duration = mid_obj.length + 1.2
    
    # 2. Setup audio capture
    hw_wav = out_dir / f"fm106_hw_preset_{preset_idx:02d}.wav"
    ref_wav = out_dir / f"fm106_ref_preset_{preset_idx:02d}.wav"
    
    ffmpeg_cmd = [
        "ffmpeg", "-y", "-f", "avfoundation", "-i", f":{audio_dev}",
        "-t", f"{duration:.2f}", "-ac", "2", "-ar", "44100", str(hw_wav)
    ]
    
    print(f"[4/5] Recording {duration:.1f}s from hardware while streaming MIDI...")
    rec_proc = subprocess.Popen(ffmpeg_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.3)
    
    # Open MIDI port and play
    outport = mido.open_output(midi_port_name)
    # Always send explicit Program Change first
    outport.send(mido.Message("program_change", program=preset_idx))
    time.sleep(0.1)
    
    for msg in mid_obj.play():
        if msg.type not in ("sysex",):  # Hardware handles Program Change
            outport.send(msg)
            
    outport.close()
    rec_proc.wait()
    print(f"      Captured hardware audio: {hw_wav}")
    
    # 3. Render reference
    print(f"[5/5] Rendering PC reference audio via ultramaster...")
    render_cmd = [str(RENDER_BIN), str(mid_file), str(ref_wav), "44100", "--16bit"]
    subprocess.run(render_cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"      Rendered reference audio: {ref_wav}")
    
    # 4. Analyze & Compare
    print("\n--- Audio Analysis & Verification ---")
    hw_samples, sr = read_wav_mono(hw_wav)
    ref_samples, _ = read_wav_mono(ref_wav)
    
    hw_peak = max(abs(s) for s in hw_samples)
    ref_peak = max(abs(s) for s in ref_samples)
    hw_rms = math.sqrt(sum(s*s for s in hw_samples) / max(1, len(hw_samples)))
    ref_rms = math.sqrt(sum(s*s for s in ref_samples) / max(1, len(ref_samples)))
    
    hw_peak_db = 20 * math.log10(hw_peak) if hw_peak > 1e-5 else -99.9
    ref_peak_db = 20 * math.log10(ref_peak) if ref_peak > 1e-5 else -99.9
    hw_rms_db = 20 * math.log10(hw_rms) if hw_rms > 1e-5 else -99.9
    ref_rms_db = 20 * math.log10(ref_rms) if ref_rms > 1e-5 else -99.9
    
    hw_clipped = sum(1 for s in hw_samples if abs(s) >= 0.999)
    ref_clipped = sum(1 for s in ref_samples if abs(s) >= 0.999)

    print(f"Level Comparison:")
    print(f"  Hardware Peak:  {hw_peak_db:6.1f} dBFS  |  RMS: {hw_rms_db:6.1f} dBFS  |  Clipped: {hw_clipped:5d} ({100.0 * hw_clipped / max(1, len(hw_samples)):.2f}%)")
    print(f"  Reference Peak: {ref_peak_db:6.1f} dBFS  |  RMS: {ref_rms_db:6.1f} dBFS  |  Clipped: {ref_clipped:5d} ({100.0 * ref_clipped / max(1, len(ref_samples)):.2f}%)")
    
    # Pitch check
    print(f"\nPitch Verification (Concert Pitch 440 Hz standard):")
    if quick:
        test_points = [
            ("C4 (Note 60)", 0.6, 261.63),
            ("A4 (Note 69)", 1.4, 440.00),
        ]
    else:
        test_points = [
            ("C2 (Note 36)", 2.0, 65.41),
            ("C3 (Note 48)", 3.8, 130.81),
            ("C4 (Note 60)", 5.8, 261.63),
            ("C5 (Note 72)", 7.8, 523.25),
            ("C6 (Note 84)", 9.8, 1046.50),
        ]

    for name, t, expected in test_points:
        idx = int(t * sr)
        N = 4096
        if idx + N <= len(hw_samples):
            win = hw_samples[idx:idx+N]
            w = [win[i] * 0.5 * (1.0 - math.cos(2 * math.pi * i / N)) for i in range(N)]
            f_min = max(20.0, expected - 25.0)
            f_max = expected + 25.0
            f_step = 0.2
            best_f, best_m = 0, -1
            f = f_min
            while f <= f_max:
                k = f * N / sr
                acc = sum(w[n] * cmath.exp(-2j * math.pi * k * n / N) for n in range(N))
                mag = abs(acc)
                if mag > best_m:
                    best_m = mag
                    best_f = f
                f += f_step
            err = abs(best_f - expected)
            status = "[PASS]" if err < 2.0 else "[WARN]"
            print(f"  {name:14s}: Expected {expected:6.1f} Hz -> Measured {best_f:6.1f} Hz (err: {err:4.1f} Hz) {status}")

    print(f"\nFiles saved for listening / inspection:")
    print(f"  Hardware:  {hw_wav}")
    print(f"  Reference: {ref_wav}")
    print(f"========================================================\n")

def run_debug_monitor():
    """Listen for real-time USB-MIDI telemetry broadcast from FM-1."""
    import mido
    in_port = None
    for p in mido.get_input_names():
        if "Felucca" in p:
            in_port = p
            break
    if not in_port:
        sys.exit("Error: Felucca MIDI port not found. Ensure FM-1 is connected via USB.")

    print(f"========================================================")
    print(f"  FM-106 Live USB-MIDI Telemetry Monitor")
    print(f"  Port: {in_port}")
    print(f"  Press Ctrl+C to exit")
    print(f"========================================================\n")
    print(f"{'Time':8s} | {'CPU0 Load':10s} | {'CPU1 Load':10s} | {'Active Voices':14s} | {'Worker Timeouts':16s}")
    print("-" * 72)

    c0, c1, act_v, timeouts = 0, 0, 0, 0
    t_start = time.time()
    t_last_print = 0

    with mido.open_input(in_port) as port:
        try:
            while True:
                for msg in port.iter_pending():
                    if msg.type == "control_change":
                        if msg.control == 102:
                            c0 = msg.value
                        elif msg.control == 103:
                            c1 = msg.value
                        elif msg.control == 104:
                            act_v = msg.value
                        elif msg.control == 105:
                            timeouts = msg.value
                now = time.time()
                if now - t_last_print >= 0.25:
                    t_last_print = now
                    elapsed = now - t_start
                    sys.stdout.write(f"\r{elapsed:6.1f}s  |   {c0:3d}%      |   {c1:3d}%      |      {act_v:1d} / 6       |        {timeouts:3d}        ")
                    sys.stdout.flush()
                time.sleep(0.02)
        except KeyboardInterrupt:
            print("\nStopped.")

def main():
    parser = argparse.ArgumentParser(description="FM-106 Hardware vs Reference A/B test tool")
    parser.add_argument("--preset", default="0", help="Preset number (0..127) or name (e.g. 'A11 Brass', 'A18 Piano I')")
    parser.add_argument("--quick", action="store_true", help="Run short 3.5s chord/note test instead of full 11s pattern")
    parser.add_argument("--list", action="store_true", help="List all 128 factory presets with Program Change indices")
    parser.add_argument("--set-preset", type=int, default=None, help="Set preset on hardware via Program Change and exit")
    parser.add_argument("--debug", action="store_true", help="Monitor live CPU load and voice telemetry via USB-MIDI")
    parser.add_argument("--out-dir", default="/tmp", help="Output directory for recorded and reference WAV files")
    
    args = parser.parse_args()
    presets = load_presets()
    
    if args.debug:
        run_debug_monitor()
        return

    if args.list:
        print("FM-106 Factory Presets (MIDI Program Change 0..127):")
        for i, p in enumerate(presets):
            print(f"  [{i:3d}] {p}")
        return
        
    if args.set_preset is not None:
        import mido
        port = find_midi_port()
        if not port:
            sys.exit("Error: Felucca MIDI port not found.")
        out = mido.open_output(port)
        out.send(mido.Message("program_change", program=args.set_preset))
        out.close()
        pname = presets[args.set_preset] if args.set_preset < len(presets) else "Unknown"
        print(f"Switched FM-106 to Preset {args.set_preset}: {pname}")
        return
        
    # Resolve preset arg
    preset_idx = 0
    if args.preset.isdigit():
        preset_idx = int(args.preset)
    else:
        target = args.preset.lower()
        for i, p in enumerate(presets):
            if target in p.lower():
                preset_idx = i
                break
                
    run_test(preset_idx=preset_idx, quick=args.quick, out_dir=Path(args.out_dir))

if __name__ == "__main__":
    main()
