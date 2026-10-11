# Make it yours

Dinghy is meant to be copied. This is how a copy becomes your firmware, gets released, and gets into the
[FM-1 Firmware Hub](https://fm1.designburgapps.com) next to the others.

## 1. Copy

On GitHub: **Use this template** (or fork). Then build once before changing anything, so you know your setup
works: `./build.sh` and `tests/run_host.sh` (see the [README](../README.md)).

## 2. Name it

- `dinghy.c` is yours: rename it if you like (and the one line in `tools/build.py` that compiles it).
- At its top, before `#include "platform.c"`:

  ```c
  #define APP_NAME "MYSYNTH"        /* the rescue and the crash screen say this */
  ```

- The package file names (`dinghy.fwsc`) are in `tools/build.py`: search for `dinghy`.
- Make your own logo; `assets/logo.svg` is Dinghy's.

## 3. Your package identity

Every FM-1 package carries an identity like `FM-1_990`. The FM-1 reports the one it runs, and installers and the
hub use it to tell firmwares apart: two firmwares with the same identity look like one.

- Dinghy is `FM-1_99N` (N = the minor version). **Pick your own** in `tools/build.py` (`PRODUCT` and the
  `--release` line under it).
- Keep the form `FM-1_9` + two digits if you can: the web installers then also finish an install that was cut
  off halfway (they resume only the update loaders they know, `FM-1_9NN`).
- Taken so far: `900` SLOOP, `91N` Felucca, `93N` sloopDX, `97N` zp12, `98N` (a Doom port), `99N` Dinghy, and others
  in the hub's list. There is no registry yet: look at the hub, and ask there if you are unsure.

## 4. Release

```sh
./build.sh --release 0.2        # -> build/dinghy-0.2.fwsc, identity FM-1_992, the version "0.2" on the screen
```

On GitHub: a release with a tag (`v0.2`) and the `.fwsc` attached. Say what changed in its notes: the hub links
them.

## 5. Into the hub

The [FM-1 Firmware Hub](https://fm1.designburgapps.com) installs open-source FM-1 firmwares with one click,
and checks every night for new releases. To be listed:

- the source is public and GPL-3.0 (Dinghy already is: keep it so);
- the releases are on GitHub, each with its `.fwsc`;
- a screenshot of your firmware's screen: `tests/run_host.sh` makes one (`build/host/*.png`), or a photo;
- you ask: the hub's *"Made an open-source firmware?"* card says how.

After that every release you make shows up there by itself.

## 6. Keep the credits

Your firmware stands on Felucca (Leo Kuroshita, Hügelton Instruments) and SLOOP (isod89). The GPL asks you to
keep their copyright lines and to give your users your source; it also is just fair. Keep `LICENSING.md` and
the headers, and add yourself.

## Ideas to start from

- A second oscillator, detuned: two phases, one `inc` a little bigger.
- An envelope per key with the four knobs (`ENC_KNOB1..4`) as attack, decay, sustain, release.
- A one-pole low-pass: `y += (x - y) * k >> 15` per frame; `k` from a knob.
- A step sequencer on the white keys, `BTN_PLAY` to start, the tempo on `ENC_SELECT`.
- Look at how zp12 or sloopDX do it: same platform, much bigger apps.
