---
name: run-dutler
description: Build, test, and run the DUTler Pico firmware. Use when asked to run, build, test, compile, or smoke-test DUTler, produce the .uf2, or verify a firmware/console/settings change. Covers the host unit tests (the runnable surface with no board) and the cross-board firmware build.
---

# Run DUTler

DUTler is **Raspberry Pi Pico firmware** (RP2040 / RP2350), not a desktop or web
app — there is nothing to launch and screenshot. With no Pico attached (CI, a
container, a fresh checkout) the two things you *can* do, and the two things
almost every PR needs, are:

- **host unit tests** — the real runnable surface. `command`/`settings`/`console`
  logic compiled with the native C compiler + vendored Unity, hardware seams
  faked. Fast, no toolchain needed. This is what most changes here touch.
- **firmware build** — cross-compile the `.uf2` with the Pico SDK + Arm toolchain
  to prove the firmware still links across the Pico family.

Both are wrapped by the driver and both mirror `.github/workflows/ci.yml`.

**Paths below are relative to the repo root** (the `DUTler/` directory). The
driver is `.claude/skills/run-dutler/driver.sh`.

> Verified on macOS (darwin) with the SDK + Arm toolchain installed as siblings
> of the repo (see Build). CI verifies the same steps on Ubuntu.

## Run (agent path) — the driver

```bash
# fast inner loop: host unit tests only (WERROR + ASan/UBSan) — no SDK needed
.claude/skills/run-dutler/driver.sh test

# firmware .uf2 for one board (default pico2) + flash/RAM size
.claude/skills/run-dutler/driver.sh build          # pico2
.claude/skills/run-dutler/driver.sh build pico     # or pico / pico_w / pico2_w

# every board CI covers: pico, pico_w, pico2, pico2_w
.claude/skills/run-dutler/driver.sh build-all

# default: tests + a pico2 firmware build
.claude/skills/run-dutler/driver.sh all
```

Each `build` writes to a **per-board** dir (`build-<board>/`, all gitignored) and
prints the `.uf2` path plus `arm-none-eabi-size`. Expected tail of a good run:

```
100% tests passed, 0 tests failed out of 3
...
-rw-r--r--  ... build-pico2/dutler.uf2
   text	   data	    bss	    dec	    hex	filename
  37364	      0	   8044	  45408	   b160	build-pico2/dutler.elf
=== driver: all OK ===
```

For a change under `src/core/` or `src/platform/` the fast check is
`driver.sh test`; before merging, `driver.sh build-all` confirms it still links
on both RP2040 and RP2350 and on the wireless (no-LED) variants.

## Prerequisites

**macOS (what this session used):**

```bash
brew install cmake ninja picotool     # cmake 4.3.4, ninja 1.13.2 here
```

Host tests also need a C compiler (Apple clang 21 here — comes with Xcode CLT).

**Ubuntu (from `.github/workflows/ci.yml` — not run this session):**

```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build          # host unit tests
sudo apt-get install -y libusb-1.0-0-dev           # + this, for the firmware build
```

The firmware build additionally needs the **Arm GNU bare-metal toolchain
14.2.Rel1** (`arm-none-eabi-gcc`) and the **Pico SDK 2.2.0** — see Build. The
host unit tests need **neither**.

## Build (firmware toolchain layout)

`env.sh` (sourced by the driver for firmware builds) locates the SDK and
toolchain as **siblings of the repo** and puts the toolchain on `PATH`. It honors
anything already exported, which is how CI pins its own paths.

```
<parent>/
  DUTler/                                       # this repo
  pico-sdk/                                     # Pico SDK 2.2.0 (+ TinyUSB submodule)
  arm-gnu-toolchain-14.2.rel1-*-arm-none-eabi/  # Arm toolchain
```

One-time SDK fetch as a sibling (from the repo root):

```bash
git -C .. clone --branch 2.2.0 https://github.com/raspberrypi/pico-sdk.git
git -C ../pico-sdk submodule update --init
```

On macOS `picotool` comes from Homebrew (`env.sh` picks up `picotool_DIR`); on
Linux the SDK builds `picotool` itself once `libusb-1.0-0-dev` is present.

## Run: on real hardware (human path)

Needs a physical Pico and cannot be verified headless. Flash, then drive the USB
serial ports:

```bash
./flash.sh                       # builds build/, reboots a running board to BOOTSEL, loads the .uf2
```

The board enumerates as three CDC serial ports (bridge / control / debug). The
`tools/*.py` scripts poke them over `/dev/cu.usbmodem*` (macOS) or `/dev/ttyACM*`
(Linux) — e.g. `tools/out_test.py`, `tools/bridge_test.py` (needs a GP0↔GP1
jumper), `tools/reset_bootsel.py`. **None run without a board attached**, so they
are not part of the driver.

## Gotchas

- **One build dir per board, always.** The SDK caches `PICO_PLATFORM` in the
  CMake cache. Reusing a `build/` dir configured for one architecture with a
  different board fails hard: *"PICO_PLATFORM is specified to be 'rp2040', but
  PICO_BOARD='pico2' uses 'rp2350' ... delete the CMake cache."* The driver
  sidesteps this by using `build-<board>/`. If you configure by hand, never
  reuse a dir across `pico`/`pico_w` (RP2040) and `pico2`/`pico2_w` (RP2350).
- **The tests don't compile `src/platform/console.c`** (it's faked). A console
  change that builds in tests can still break the firmware — run `driver.sh build`
  (or `build-all`) to actually compile the platform layer against the SDK.
- **`-DDUTLER_WERROR=ON` is on in the driver and CI, off in plain local builds.**
  A build that's clean locally can fail CI on a new warning; the driver catches
  that before you push.
- **Sanitizers (`-DDUTLER_SANITIZE=ON`) apply only to the host tests**, never the
  firmware (it's bare-metal). They catch OOB/UB in the pure logic.
- **Wireless boards (`pico_w`, `pico2_w`) skip the activity LED** — no
  `PICO_DEFAULT_LED_PIN` — so `build-all` proves the no-LED path compiles too.

## Troubleshooting

- **`PICO_PLATFORM ... incompatible ... delete the CMake cache`** — you reused a
  build dir across architectures. `rm -rf build*/` and rebuild, or just use the
  driver (per-board dirs). Seen this session when a stale `build/` from `pico`
  was reconfigured for `pico2`.
- **`Could not find a package configuration file provided by "picotool"`** — on
  macOS `brew install picotool`; on Linux install `libusb-1.0-0-dev` so the SDK
  can build picotool, or pre-set `picotool_DIR`.
- **`PICO_SDK_PATH` not found / SDK errors** — the SDK isn't a sibling of the
  repo. Clone it (see Build) or `export PICO_SDK_PATH=...` before running.
- **Firmware build can't find `arm-none-eabi-gcc`** — the toolchain isn't a
  sibling and isn't on `PATH`. Install 14.2.Rel1 and either drop it beside the
  repo or `export PICO_TOOLCHAIN_PATH=...`.
