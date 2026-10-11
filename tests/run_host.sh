#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/dinghy.c on this computer (tests/host.c): build/host/dinghy.wav and the screens as PNG.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/src -Ifirmware/gen -o build/host/fm106 firmware/src/fm106.c
cc -O2 -Wall -o build/host/audio_worker_test tests/audio_worker_test.c
build/host/audio_worker_test
cc -O2 -Wall -pthread -o build/host/audio_worker_thread_test tests/audio_worker_thread_test.c
build/host/audio_worker_thread_test
build/host/fm106 build/host/fm106
python3 - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("fm106-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PY
