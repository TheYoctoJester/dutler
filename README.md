# DUTler 🎩

[![CI](https://github.com/TheYoctoJester/dutler/actions/workflows/ci.yml/badge.svg)](https://github.com/TheYoctoJester/dutler/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later OR Commercial](https://img.shields.io/badge/license-GPL--3.0--or--later%20OR%20Commercial-blue.svg)](LICENSE)
[![Platform: RP2040 / RP2350](https://img.shields.io/badge/platform-RP2040%20%2F%20RP2350-8a2be2.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Status: alpha](https://img.shields.io/badge/status-alpha-orange.svg)](#)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome%20(CLA)-brightgreen.svg)](CONTRIBUTING.md)

> **A bench butler for your Device Under Test.**
> Sponsored by [Northern.tech](https://northern.tech) as part of the
> [Mender.io](https://mender.io) community engagement.

DUTler is open-source firmware that turns a ~$4 **Raspberry Pi Pico (RP2040)** into a small,
always-there sidecar for **OS-level integration work** — building, flashing, updating, and
*recovering* embedded-Linux devices. It targets the loop you actually live in when doing
**Yocto** builds and **Mender** over-the-air updates: flash an image, watch it boot, and — when
one wedges the board mid-update — get it back **without walking over to the bench**.

Over a single USB cable, DUTler gives you the two things that loop always needs:

- a **serial console** to the device — a genuine USB-to-UART bridge with the host baud rate
  mirrored onto the hardware UART; and
- **power and control lines** — a relay to switch DUT power, plus low-side MOSFET drivers to
  assert the board's boot-mode/strapping and reset pins, so you can power-cycle it and drop it
  into a recovery/bootloader mode when an update leaves it unresponsive;

plus a separate **firmware debug log**, so DUTler's own diagnostics never pollute the device
console.

It's built for Mender + Yocto bring-up and **CI / hardware-in-the-loop** rigs, but nothing about
it is Mender-specific — it's a generic console-plus-power companion for *any* board under test.
The name is *DUT* (Device Under Test) + *butler*: it quietly attends your board, serves up the
console, and flips the power when asked.

The firmware makes a stock Pico enumerate as **three USB serial ports**:

| USB port | Purpose |
|----------|---------|
| **CDC0 — "UART Bridge"** | Transparent USB ↔ hardware-UART bridge (a real USB-serial adapter; host baud rate is mirrored onto the UART). |
| **CDC1 — "Control"** | Newline-terminated commands to switch the DUT control outputs — a power relay plus MOSFET strap/reset drivers. |
| **CDC2 — "Debug Log"** | Read-only firmware log stream (open it to start receiving; output is dropped when nobody is listening). |

They enumerate as three serial ports, in this order — **bridge, control, debug log**:

- **Linux:** `/dev/ttyACM0`, `/dev/ttyACM1`, `/dev/ttyACM2`. For access without `sudo`, add
  yourself to the `dialout` group once: `sudo usermod -aG dialout "$USER"` (then re-login).
  Open a port with `tio`, `minicom`, or `screen`.
- **macOS:** `/dev/cu.usbmodem*` — `…1` (bridge), `…3` (control), `…5` (debug log). Open with
  `screen`.

The examples below use the macOS `cu.usbmodem*` names; on Linux substitute the matching
`/dev/ttyACM*`.

## Requirements

**Host (build machine): macOS or Linux.**

- **CMake ≥ 3.13**, **Ninja**, **picotool**, and the **Arm GNU bare-metal toolchain**
  (`arm-none-eabi-gcc` — developed against *14.2.Rel1*).
  - macOS: `brew install cmake ninja picotool`, plus the Arm toolchain (download the
    `arm-gnu-toolchain-*-arm-none-eabi` for darwin and unpack it).
  - Linux: your distro's `cmake`, `ninja-build`, `picotool` (or build it), plus the Arm GNU
    toolchain (distro package or the official tarball).
- **Raspberry Pi Pico SDK 2.2.0** with submodules (it pulls in TinyUSB).
- **Python 3** — only for the host-side helper scripts in `tools/` (optional).

Put the SDK and toolchain **next to this repo** (siblings of the `DUTler/` directory) and `env.sh`
finds them automatically — or export `PICO_SDK_PATH` / `PICO_TOOLCHAIN_PATH` / `picotool_DIR`
yourself (env.sh honors anything already set, which is how CI pins its own paths).

**Target:** a stock **Raspberry Pi Pico (RP2040)** (`PICO_BOARD=pico`, the default) or a
**Pico 2 W (RP2350)** (`PICO_BOARD=pico2_w`), plus a USB cable. The firmware adapts the flash
layout to the board's actual size at runtime; on the wireless Pico 2 W the activity LED hangs off
the CYW43 chip and is simply skipped (no wireless stack is pulled in). A single jumper wire between
**GP0 and GP1** is handy for the bridge loopback self-test.

## Quick start

Once the requirements are in place:

```sh
# fetch the SDK as a sibling of this repo (one-time)
git -C .. clone --branch 2.2.0 https://github.com/raspberrypi/pico-sdk.git
git -C ../pico-sdk submodule update --init

# build -> build/dutler.uf2
source env.sh
cmake -S . -B build -G Ninja \
  -DPICO_SDK_PATH="$PICO_SDK_PATH" \
  -DPICO_TOOLCHAIN_PATH="$PICO_TOOLCHAIN_PATH" \
  -Dpicotool_DIR="$picotool_DIR"
# …add -DPICO_BOARD=pico2_w to build for the Pico 2 W (RP2350) instead.
cmake --build build

# flash: hold BOOTSEL while plugging in the Pico, then
./flash.sh                              # or drag build/dutler.uf2 onto the RPI-RP2 drive

# use it: three USB serial ports appear
ls /dev/cu.usbmodem*                    # macOS: …1 bridge, …3 control, …5 debug
# ls /dev/ttyACM*                       # Linux:  ttyACM0/1/2 = bridge/control/debug
screen /dev/cu.usbmodemXXXX1 115200     # the DUT serial console (Linux: tio/minicom/screen on ttyACM0)
#   …then open the control port and type 'help' for the output/strap/reset commands
```

See **Build**, **Flash** and **Test** below for the details, and **Wiring** for pin assignments.

## Wiring (defaults, see `include/config.h`)

| Function | Pin |
|----------|-----|
| Bridge UART TX | GP0 → device RX |
| Bridge UART RX | GP1 ← device TX |
| Control out 1 — MOSFET | GP2 — low-side driver for a DUT strap/boot-mode or reset line |
| Control out 2 — MOSFET | GP3 — low-side driver for a DUT strap/boot-mode or reset line |
| Control out 3 — power relay | GP4 — switch DUT power |
| Control out 4 — spare | GP5 — defined in firmware, **not wired on the v1 HAT** |
| Activity LED | GP25 (on-board) |
| GND | any GND pin — **share ground with the DUT and any relay/MOSFET board** |

(These match the v1 HAT in `hardware/`, where outs 1–2 are 2N7000 MOSFETs and out 3 drives a
relay via a ULN2003; on a bare Pico with jumpers, wire your own driver modules to the same pins.)

All control outputs are 3.3 V logic, **active-high, and OFF at boot**. In firmware they're just
generic switched GPIOs (all driven via the `out`/`name` commands); their *intended* roles are:

- **Outs 1 & 2 → low-side MOSFET gates** — to pull the DUT's strapping/boot-mode and reset lines
  (e.g. force USB/serial download mode, then toggle reset). The MOSFET does the level shift and
  the pull; the GPIO only drives its gate. Match the DUT's logic level and share ground.
- **Out 3 → a power relay** — to cut/restore DUT power. Use a relay **module** with its own
  driver/opto stage; don't switch a coil straight off a GPIO.
- **Out 4 → spare** — a free GPIO with no driver on the v1 HAT; wire your own if you need a fourth.

Tip: alias the outputs to their jobs once and drive them by name —
`name 1 bootmode`, `name 2 reset`, `name 3 power`, then `power off` / `bootmode on` / `reset on`.
Count, pins and polarity live in `include/config.h` (`OUT_*`); `OUT_ACTIVE_LOW 1` flips sense.

## Build

```sh
source env.sh                # sets PICO_SDK_PATH, toolchain, picotool (self-contained)
cmake -S . -B build -G Ninja \
  -DPICO_SDK_PATH="$PICO_SDK_PATH" \
  -DPICO_TOOLCHAIN_PATH="$PICO_TOOLCHAIN_PATH" \
  -Dpicotool_DIR="$picotool_DIR"     # first time only
cmake --build build                  # -> build/dutler.uf2
```

## Flash

```sh
./flash.sh
```

When the adapter is already running, `flash.sh` reboots it into the bootloader
automatically (no button) by sending the control port's `bootsel` command, then loads the new
image. You can also trigger the reset yourself:

- send `bootsel` to the control port, **or**
- open the **debug** port at **1200 baud** (the classic USB-serial "1200-baud touch";
  compile-time `ENABLE_BAUD_TOUCH_RESET` in `config.h`, on by default), **or**
- `python3 tools/reset_bootsel.py`

**First flash / wedged board:** hold the **BOOTSEL** button while plugging in (mounts as
`RPI-RP2`), then run `./flash.sh` or drag `build/dutler.uf2` onto the drive.

> The 1200-baud reset is wired to the *debug* port only — the bridge port stays free to use
> any real baud rate (including 1200).

## Test

### On hardware

After flashing, **three** devices appear: `ls /dev/tty.usbmodem*` (bridge / control / debug log).

- **Bridge loopback:** jumper **GP0↔GP1**, open the *UART Bridge* port and echo:
  ```sh
  screen /dev/tty.usbmodemXXXX 115200     # type chars -> should echo back
  ```
  (Ctrl-A then K to quit `screen`.) Changing baud still works — it's pushed to the UART. The
  control port's `selftest` command checks GP0↔GP1 continuity for you.
- **Control outputs:** open the *Control* port and send commands:
  ```
  help
  status
  out 1 on
  out 1 off
  name 2 fan
  fan toggle
  ```

### Unit tests (host)

The pure logic — CRC-32, integer parsing, and the settings record codec — has off-target unit
tests that build with the **native** compiler (no Pico, no SDK):

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

CI runs these on every push and pull request.

## Control commands

Open the **Control** port (CDC1) and type `help` — the firmware prints the authoritative,
always-current command list, so this README doesn't try to mirror it. In brief:

- **`out <id> on|off|toggle`** drives an output by number (`1..`) or configured name, with an
  `<id> on|off|toggle` shorthand (e.g. `pump on`); **`name <n> <alias|clear>`** labels one.
- **`set baud <n>`** / **`set format <8N1>`** set the bridge UART defaults; **`save`** persists
  names + bridge defaults to flash; **`status`** shows current state (and unsaved changes).
- **`selftest`** (GP0↔GP1 loopback), **`factory-reset confirm`**, **`version`**, and
  **`bootsel`** (reboot into the USB bootloader) round it out.

## Persistent settings

Output **names** and the **bridge boot UART config** are stored in flash. `set …`/`name …`
change them in RAM (shown as "unsaved changes" in `status`); `save` writes them. They survive
power cycles *and* normal firmware reflashes (the UF2 only overwrites the program region, not
the settings sectors).

Storage uses an **A/B (ping-pong) scheme** across the last two 4 KB sectors: each record
carries a monotonic sequence number, `save` writes the *inactive* slot and verifies it before
it counts, and load picks the valid slot with the highest sequence. The active slot is never
erased, so a **failed or power-interrupted save cannot lose the last good config** — load just
falls back to the older slot (the half-written one fails its CRC). A blank/garbage pair falls
back to safe defaults. The record is **versioned**; `src/core/settings.c` documents the append-only
evolution rules and migrates the older format in place (v1 single-slot records are upgraded to
v2 A/B on first boot, preserving names + baud).

**Outputs themselves always boot OFF** — their state is deliberately not persisted, so a power
blip can never silently re-energize a load. Implemented in `src/core/settings.c`
(struct, CRC32, flash erase/program) — note flash writes briefly mask interrupts (a few ms),
so avoid `save` in the middle of heavy bridge traffic.

## Watchdog

A hardware watchdog (`WATCHDOG_TIMEOUT_MS` in `config.h`, default 2 s) is fed every main-loop
iteration, so a wedged loop (e.g. a hung USB stack) reboots and recovers automatically. The
`save` path feeds it just before the interrupts-masked flash erase, which is the longest
blocking section. At boot the firmware records whether the reset was a real watchdog timeout
(via `watchdog_enable_caused_reboot()`, which ignores deliberate `bootsel`/reflash reboots)
and `status` reports it. Outputs always come up OFF after any reset.

## Design notes / non-goals

- **No UART flow control — intentional, not missing.** The bridge is a 3-wire link
  (TX/RX/GND): no hardware RTS/CTS and no software XON/XOFF. This is a deliberate scope
  decision. Backpressure exists where it matters — USB→UART is non-blocking and NAKs the host
  when the staging buffer fills — and the only lossy path (UART→USB ring overflow when the host
  stops reading) is **reported** on the debug port rather than prevented. Use matched baud
  rates and don't firehose the bridge from a flow-controlled peer. Please don't "add the missing
  flow control": it's a choice. If a future use case genuinely needs it, wire RTS/CTS pins and
  call `uart_set_hw_flow()` in `bridge.c` — but that's a new feature, not a fix.

## Debug log

Firmware logs go to the **Debug Log** port (CDC2). Watch them with:

```sh
screen /dev/cu.usbmodemXXXX5 115200      # or: python3 tools/debug_capture.py
```

The firmware also logs **`bridge: RX overflow ...`** here (rate-limited) if the UART-to-USB
ring ever drops bytes because the host stopped draining — so silent data loss becomes visible.

Add your own with `dbg_printf("...")` (declared in `src/platform/debug.h`). Output is only sent while a
host has the port open, so calls are cheap when unused. **Do not call `dbg_printf` from an
interrupt handler** (it touches the USB TX FIFO shared with the main loop).

> USB VID/PID in `config.h` are pid.codes **test** IDs — replace before any distribution.

## Contributing

Contributions are welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md). Because DUTler is
dual-licensed, contributions require a DCO sign-off (`git commit -s`) **and** agreement to the
[Contributor License Agreement](CLA.md), which lets Northern.tech offer your contribution under
both the GPL and the commercial license. Please follow the
[Code of Conduct](CODE_OF_CONDUCT.md), and report security issues privately per
[`SECURITY.md`](SECURITY.md).

## License

Copyright © 2026 Northern.tech AS.

The firmware is **dual-licensed** — use it under *either*:

- **GPL-3.0-or-later** (open source) — see [`LICENSE`](LICENSE); derivatives stay GPL; **or**
- a **commercial / proprietary license** from **Northern.tech AS**, for use without the GPL's
  copyleft obligations — see [`COMMERCIAL.md`](COMMERCIAL.md).

Every source file carries the [SPDX](https://spdx.dev) identifier
`Apache-2.0`. Northern.tech AS holds copyright on all
files, which is what makes both options possible; to keep that so, contributions must be
assignable/relicensable to Northern.tech (see [`COMMERCIAL.md`](COMMERCIAL.md)).

- **Hardware** (future `hardware/`): intended to be **CERN-OHL-P-2.0** — see `hardware/README.md`.
- **Documentation** may be offered under **CC-BY-4.0**.

Third-party components keep their own licenses and are *not* relicensed (Pico SDK, TinyUSB,
newlib, libgcc, …) — see [`THIRD_PARTY.md`](THIRD_PARTY.md).

## Acknowledgements

DUTler is **sponsored by [Northern.tech](https://northern.tech) as part of the
[Mender.io](https://mender.io) community engagement** — thank you for backing open tooling for
the embedded-Linux community. Thanks also to the Raspberry Pi Pico SDK and TinyUSB projects,
which do the heavy USB lifting.

The majority of the code in this repository was generated with
[Claude Code](https://claude.com/claude-code), under human direction and review.

> "Mender", "Northern.tech", and the Mender moose are trademarks of Northern.tech AS. They are
> used here only to describe the project's purpose and the integration workflow it serves.
