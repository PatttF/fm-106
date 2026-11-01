#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Convert splash.png into compressed 3-strip RGB565 data for FM-106 LCD."""

import sys
from pathlib import Path
from PIL import Image

SRC = Path(__file__).resolve().parents[1]
sys.path.append(str(SRC / "tools"))
import lz4blk

def gen_splash():
    png_path = SRC / "splash.png"
    out_path = SRC / "firmware" / "src" / "splash_data.h"

    if not png_path.exists():
        print(f"gen_splash: {png_path} not found")
        return False

    im = Image.open(png_path).convert("RGB")
    im240 = im.resize((240, 240), Image.Resampling.LANCZOS)

    strips = []
    for s in range(3):
        raw = bytearray()
        for y in range(s * 80, (s + 1) * 80):
            for x in range(240):
                r, g, b = im240.getpixel((x, y))
                c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                sw = ((c >> 8) & 0xFF) | ((c & 0xFF) << 8)
                raw.extend([sw & 0xFF, (sw >> 8) & 0xFF])
        cblk = lz4blk.compress(bytes(raw))
        strips.append(cblk)
        print(f"gen_splash: Strip {s} (y={s*80}..{(s+1)*80-1}): raw {len(raw)} -> LZ4 {len(cblk)} bytes")

    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* Auto-generated from splash.png by tools/gen_splash.py */",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "/* Minimal LZ4 block decompressor for splash screen */",
        "static inline void splash_lz4_decompress(const uint8_t *src, uint8_t *dst, uint32_t dst_len)",
        "{",
        "    uint32_t si = 0, di = 0;",
        "    while (di < dst_len) {",
        "        uint8_t tok = src[si++];",
        "        uint32_t lit_len = (uint32_t)(tok >> 4);",
        "        if (lit_len == 15u) {",
        "            uint8_t b;",
        "            do { b = src[si++]; lit_len += b; } while (b == 255u);",
        "        }",
        "        while (lit_len--) dst[di++] = src[si++];",
        "        if (di >= dst_len) break;",
        "        uint32_t off = (uint32_t)src[si] | ((uint32_t)src[si + 1] << 8);",
        "        si += 2;",
        "        uint32_t match_len = (uint32_t)(tok & 15u) + 4u;",
        "        if ((tok & 15u) == 15u) {",
        "            uint8_t b;",
        "            do { b = src[si++]; match_len += b; } while (b == 255u);",
        "        }",
        "        while (match_len--) {",
        "            dst[di] = dst[di - off];",
        "            di++;",
        "        }",
        "    }",
        "}",
        "",
    ]

    for s, cblk in enumerate(strips):
        lines.append(f"static const uint8_t splash_strip{s}[{len(cblk)}] = {{")
        for i in range(0, len(cblk), 16):
            chunk = cblk[i:i + 16]
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
        lines.append("};\n")

    lines.append("static const uint8_t *const splash_strips[3] = { splash_strip0, splash_strip1, splash_strip2 };")
    lines.append(f"static const uint32_t splash_strip_lens[3] = {{ {len(strips[0])}, {len(strips[1])}, {len(strips[2])} }};")
    lines.append("")

    out_path.write_text("\n".join(lines))
    total = sum(len(s) for s in strips)
    print(f"gen_splash: wrote {out_path} ({total} bytes compressed)")
    return True

if __name__ == "__main__":
    gen_splash()

