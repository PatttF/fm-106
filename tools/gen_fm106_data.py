#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate tables and presets for FM-106 Juno-106 synthesis engine."""

import math
import re
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware" / "src"

FS = 44100

def gen_tables():
    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* Auto-generated DSP lookup tables for FM-106 (Roland Juno-106) */",
        "#pragma once",
        "#include <stdint.h>",
        "",
    ]

    # 1. Sine table: 1024 entries Q15 (-32767..32767)
    sine = [round(32767.0 * math.sin(2.0 * math.pi * i / 1024.0)) for i in range(1024)]
    lines.append("static const int16_t FM106_SINE[1024] = {")
    for i in range(0, 1024, 8):
        lines.append("    " + ", ".join(f"{v:6d}" for v in sine[i:i+8]) + ",")
    lines.append("};\n")

    # 2. TANH saturation table: 257 entries Q15 (input 0..3 in Q8)
    tanh = [round(32767.0 * math.tanh((i / 256.0) * 3.0)) for i in range(257)]
    lines.append("static const int16_t FM106_TANH[257] = {")
    for i in range(0, 257, 8):
        lines.append("    " + ", ".join(f"{v:6d}" for v in tanh[i:i+8]) + ",")
    lines.append("};\n")

    # 3. VCF Cutoff G1 table: 4096 entries (Q15: 0..32767)
    # Extracted directly from kV4Hz[4096] in J106DACHzTable.h
    dac_table_path = Path("/tmp/juno_ref/ultramaster/Source/DSP/J106DACHzTable.h")
    if not dac_table_path.exists():
        raise SystemExit(f"Error: {dac_table_path} not found")
    
    text = dac_table_path.read_text()
    raw_nums = re.findall(r"[-+]?\d*\.\d+|\d+", text.split("kV4Hz[kV4HzSize] = {")[1].split("};")[0])
    kv4_hz = [float(x) for x in raw_nums]
    if len(kv4_hz) != 4096:
        raise SystemExit(f"Expected 4096 kV4Hz entries, got {len(kv4_hz)}")

    g1_tbl = []
    for hz in kv4_hz:
        cps = min(hz / float(FS), 0.425)
        # Bilinear transform pre-warping: g = tan(pi * fc / fs)
        g = math.tan(cps * math.pi)
        g1 = g / (1.0 + g)
        g1_q15 = max(1, min(32767, round(g1 * 32767.0)))
        g1_tbl.append(g1_q15)

    lines.append("static const uint16_t FM106_VCF_G1[4096] = {")
    for i in range(0, 4096, 8):
        lines.append("    " + ", ".join(f"{v:5d}" for v in g1_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 4. Resonance feedback gain k table: 128 entries (Q12: 0..4096)
    # From ResK_J106(res) in KR106VCF_OPTIMIZED.h
    k_tbl = []
    for step in range(128):
        r = step / 127.0
        r2 = r * r
        r3 = r2 * r
        r4 = r2 * r2
        k = 4.6265 * r + 4.3866 * r2 - 8.9869 * r3 + 3.3902 * r4
        if r > 0.7:
            knee = (r - 0.7) / 0.3
            k += min(knee * knee * 1.2, 0.35)
        boost = max(0.0, (r - 0.898) * (0.17 / 0.102))
        k = k * (1.0 + boost)
        k_q12 = max(0, min(32767, round(k * 4096.0)))
        k_tbl.append(k_q12)

    lines.append("static const uint16_t FM106_RES_K[128] = {")
    for i in range(0, 128, 8):
        lines.append("    " + ", ".join(f"{v:5d}" for v in k_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 4b. BA662 OTA Feedback scale & inverse scale tables: 128 entries (Q12)
    # Matching KR106VCF_OPTIMIZED.h lines 318-320:
    # baseFbScale = 4.20 * clamp((k - 2.5) * 1.0, 0.3, 1.0)
    fb_scale_tbl = []
    inv_fb_scale_tbl = []
    for step in range(128):
        k_val = k_tbl[step] / 4096.0
        fb_s = 4.20 * min(max((k_val - 2.5) * 1.0, 0.3), 1.0)
        fb_scale_tbl.append(round(fb_s * 4096.0))
        inv_fb_scale_tbl.append(round((1.0 / fb_s) * 4096.0))

    lines.append("static const uint16_t FM106_FB_SCALE[128] = {")
    for i in range(0, 128, 8):
        lines.append("    " + ", ".join(f"{v:5d}" for v in fb_scale_tbl[i:i+8]) + ",")
    lines.append("};\n")

    lines.append("static const uint16_t FM106_INV_FB_SCALE[128] = {")
    for i in range(0, 128, 8):
        lines.append("    " + ", ".join(f"{v:5d}" for v in inv_fb_scale_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 4c. Peak-pull compensation table from KR106VCF_OPTIMIZED.h (128 entries Q12)
    # Compensates for 4-pole TPT cascade resonant peak pulling below nominal cutoff
    peak_pull_tbl = []
    for step in range(128):
        k_val = k_tbl[step] / 4096.0
        kFit = min(5.0, max(0.0, k_val))
        logM = 0.20646 + kFit * (-0.04140 + kFit * (0.00602 - kFit * 0.00127))
        if k_val < 1.0:
            logM *= max(k_val, 0.0)
        if k_val > 3.7:
            fade = min(1.0, max(0.0, (4.4 - k_val) / 0.7))
            logM *= fade
        mult = math.exp(logM)
        peak_pull_tbl.append(round(mult * 4096.0))

    lines.append("static const uint16_t FM106_PEAK_PULL[128] = {")
    for i in range(0, 128, 8):
        lines.append("    " + ", ".join(f"{v:5d}" for v in peak_pull_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 4c. OTASat Padé approximant table: 257 entries Q15 (0..3.0 input, step=384 in Q15)
    # Matches KR106VCF_OPTIMIZED.h OTASat(x) = x * (27 + x^2) / (27 + 9*x^2)
    ota_sat_tbl = []
    for i in range(257):
        x = (i / 256.0) * 3.0
        x2 = x * x
        sat = x * (27.0 + x2) / (27.0 + 9.0 * x2) if x < 3.0 else 1.0
        ota_sat_tbl.append(round(sat * 32767.0))

    lines.append("static const int16_t FM106_OTA_SAT[257] = {")
    for i in range(0, 257, 8):
        lines.append("    " + ", ".join(f"{v:6d}" for v in ota_sat_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 5. J106 Sub-oscillator level table from hardware measurement (128 entries Q15)
    # Matches KR106Voice.h dcoSubLevel_j106()
    def dco_sub_level_j106(t):
        kTable = [
            0.00154, 0.01251, 0.10875, 0.22125, 0.33576, 0.45206,
            0.55750, 0.67178, 0.78447, 0.91132, 1.00000
        ]
        idx = t * 10.0
        i0 = min(int(idx), 9)
        frac = idx - i0
        return kTable[i0] + frac * (kTable[i0 + 1] - kTable[i0])

    sub_tbl = [round(dco_sub_level_j106(s / 127.0) * 32767.0) for s in range(128)]
    lines.append("static const uint16_t FM106_SUB_LEVEL_J106[128] = {")
    for i in range(0, 128, 8):
        lines.append("    " + ", ".join(f"{v:5d}" for v in sub_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 5b. J106 Noise level table from hardware measurement (128 entries Q15)
    # Matches KR106Voice.h dcoNoiseLevel_j106()
    def dco_noise_level_j106(t):
        if t <= 0.0: return 0.0
        kA, kB, kC = 1.0632, 0.0146, 0.0594
        d = t - kC
        return kA * (math.sqrt(d * d + kB * kB) + d) * 0.5

    noise_tbl = [min(32767, round(dco_noise_level_j106(s / 127.0) * 32767.0)) for s in range(128)]
    lines.append("static const uint16_t FM106_NOISE_LEVEL_J106[128] = {")
    for i in range(0, 128, 8):
        lines.append("    " + ", ".join(f"{v:5d}" for v in noise_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 6. VCA Level law: 128 entries Q12 (-10 dB to +10 dB)
    vca_tbl = []
    for s in range(128):
        db = -10.0 + 20.0 * (s / 127.0)
        gain = 10.0 ** (db / 20.0)
        vca_tbl.append(round(gain * 4096.0))
    lines.append("static const uint16_t FM106_VCA_LAW[128] = {")
    for i in range(0, 128, 8):
        lines.append("    " + ", ".join(f"{v:5d}" for v in vca_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 6b. Hardware VCA transfer curve kVCATableHW: 256 entries Q15
    # Matches KR106VCA.h kVCATableHW (2SA1015 PNP + BA662 current mirror)
    vca_header = Path("/tmp/juno_ref/ultramaster/Source/DSP/KR106VCA.h")
    vca_hw_tbl = []
    if vca_header.exists():
        raw_nums = re.findall(r"[-+]?\d*\.\d+|\d+", vca_header.read_text().split("kVCATableHW[256] = {")[1].split("};")[0])
        vca_hw_tbl = [round(float(x) * 32767.0) for x in raw_nums]
    else:
        vca_hw_tbl = [round((i / 255.0) * 32767.0) for i in range(256)]

    lines.append("static const uint16_t FM106_VCA_HW[256] = {")
    for i in range(0, 256, 8):
        lines.append("    " + ", ".join(f"{v:5d}" for v in vca_hw_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 7. Juno-106 D7811G ADSR Decay/Release multiplier table (128 entries)
    counts = [4, 1, 10, 28, 22, 58, 4]
    steps = [0x2000, 0x1000, 0x0800, 0x0080, 0x000C, 0x0004, 0x0001]
    dec_tbl = []
    val = 0x1000
    dec_tbl.append(val)
    for seg in range(7):
        for _ in range(counts[seg]):
            val = (val + steps[seg]) & 0xFFFF
            dec_tbl.append(val)
    dec_tbl = dec_tbl[:128]

    lines.append("static const uint16_t FM106_ENV_DEC_REL[128] = {")
    for i in range(0, 128, 8):
        lines.append("    " + ", ".join(f"0x{v:04X}" for v in dec_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 8. Juno-106 D7811G Attack increment table (128 entries)
    atk_tbl = []
    for idx in range(128):
        s = idx / 127.0
        if s < 0.003937:
            inc = 0x3FFF
        elif s <= 0.500000:
            inc = round(8192.0 / (s * 127.0))
        elif s <= 0.681102:
            inc = round(305.03 - 352.26 * s)
        elif s <= 0.846457:
            inc = round(194.74 - 190.50 * s)
        elif s <= 0.956693:
            inc = round(86.37 - 62.52 * s)
        else:
            inc = max(round(148.0 - 127.0 * s), 1)
        atk_tbl.append(max(1, min(0x3FFF, inc)))

    lines.append("static const uint16_t FM106_ENV_ATK[128] = {")
    for i in range(0, 128, 8):
        lines.append("    " + ", ".join(f"0x{v:04X}" for v in atk_tbl[i:i+8]) + ",")
    lines.append("};\n")

    # 9. Note frequency phase step increments (12 semitones C9..B9)
    inc_c9 = [
        round((440.0 * (2.0 ** ((midi - 69) / 12.0)) * (2.0 ** 32)) / FS)
        for midi in range(120, 132)
    ]
    lines.append("static const uint32_t FM106_INC_C9[12] = {")
    lines.append("    " + ", ".join(f"{v}u" for v in inc_c9))
    lines.append("};\n")

    out_file = FW / "fm106_tables.h"
    out_file.write_text("\n".join(lines))
    print(f"Generated {out_file} ({len(lines)} lines)")


def gen_presets():
    ref_path = Path("/tmp/juno_ref/ultramaster/Source/KR106_Presets_JUCE.h")
    if not ref_path.exists():
        print("Warning: /tmp/juno_ref/ultramaster/Source/KR106_Presets_JUCE.h not found")
        return

    content = ref_path.read_text()
    pattern = re.compile(r'\{\s*"([^"]+)"\s*,\s*\{([^}]+)\}\s*\}')
    matches = pattern.findall(content)
    # Presets 128..255 are Juno-106 bank
    j106_presets = matches[128:256] if len(matches) >= 256 else matches

    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* 128 Factory Presets for FM-106 (decoded from Juno-106 SysEx library) */",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    const char name[16];",
        "    uint8_t p[44];",
        "} fm106_preset_t;",
        "",
        "#define FM106_NUM_PRESETS 128u",
        "static const fm106_preset_t FM106_PRESETS[FM106_NUM_PRESETS] = {",
    ]

    for name, vals_str in j106_presets:
        vals = [int(v.strip()) for v in vals_str.split(",") if v.strip()]
        if len(vals) < 44:
            vals += [0] * (44 - len(vals))
        vals = vals[:44]
        v_str = ", ".join(str(v) for v in vals)
        clean_name = name.strip()[:15]
        lines.append(f'    {{"{clean_name}", {{{v_str}}}}},')

    lines.append("};\n")
    out_file = FW / "fm106_presets.h"
    out_file.write_text("\n".join(lines))
    print(f"Generated {out_file} with {len(j106_presets)} presets")


if __name__ == "__main__":
    gen_tables()
    gen_presets()
