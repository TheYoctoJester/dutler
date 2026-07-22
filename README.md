# DUTler 🎩

[![CI](https://github.com/TheYoctoJester/dutler/actions/workflows/ci.yml/badge.svg)](https://github.com/TheYoctoJester/dutler/actions/workflows/ci.yml)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![Platform: RP2040 / RP2350](https://img.shields.io/badge/platform-RP2040%20%2F%20RP2350-8a2be2.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Status: alpha](https://img.shields.io/badge/status-alpha-orange.svg)](#)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome%20(DCO)-brightgreen.svg)](CONTRIBUTING.md)

> **A bench butler for your Device Under Test.**
> Sponsored by [Northern.tech](https://northern.tech) as part of the
> [Mender.io](https://mender.io) community engagement.

> ⚠️ **Experimental.** DUTler is provided **"AS IS", without warranties or conditions of any
> kind, and without any support**. It's an experiment, not a supported product — don't rely on
> it for anything critical.

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

**Target:** a stock **Raspberry Pi Pico (RP2040)** (`PICO_BOARD=pico`, the default), a
**Pico 2 (RP2350)** (`PICO_BOARD=pico2`), or a **Pico 2 W (RP2350)** (`PICO_BOARD=pico2_w`), plus
a USB cable. The firmware adapts the flash layout to the board's actual size at runtime; on the
wireless Pico 2 W the activity LED hangs off the CYW43 chip and is simply skipped (no wireless stack
is pulled in), whereas the non-wireless Pico 2 keeps its GP25 LED heartbeat. A single jumper wire
between **GP0 and GP1** is handy for the bridge loopback self-test.

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
# …add -DPICO_BOARD=pico2 (or pico2_w for the wireless Pico 2) to build for the RP2350 instead.
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

## Wiring (firmware defaults — `include/config.h`)

These are the firmware's pin assignments. They match the **v1 HAT** in [`hardware/`](hardware/)
one-for-one; on a bare Pico, wire your own driver modules to the same pins.

| Function | Pin | On the v1 HAT |
|----------|-----|---------------|
| Bridge UART TX | GP0 → device RX | header J2 |
| Bridge UART RX | GP1 ← device TX | header J2 |
| Control out 1 — MOSFET | GP2 — low-side strap/boot-mode or reset line | 2N7000 → J1 |
| Control out 2 — MOSFET | GP3 — low-side strap/boot-mode or reset line | 2N7000 → J3 |
| Control out 3 — power relay | GP4 — switch DUT power | ULN2003 → relay → J4 |
| Control out 4 — spare | GP5 — firmware-defined, no driver on v1 | — |
| Activity LED | GP25 (on-board) | — |
| GND | any GND pin — **share ground with the DUT and any relay/MOSFET board** | J2 / J1 / J3 |

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
  `<id> on|off|toggle` shorthand (e.g. `pump on`).
- Settings and device properties are a small **key/value store**: **`set <key> <value>`** writes and
  **`get [<key>]`** reads (with no key, `get` dumps everything). The keys are:
  - **`baud`** / **`format`** (e.g. `8N1`) — bridge UART defaults (effective after `save` + reboot).
  - **`echo`** (`on|off`) — control-port local echo (off by default; effective immediately, handy
    for raw terminals that don't echo locally).
  - **`shell`** (`on|off`) — interactive-shell mode (off by default); see *Interactive shell* below.
  - **`outname <n>`** (`<alias|clear>`) — label output `n` (usable as the shorthand verb above).
  - **`dutname`** (`<str|clear>`) — a human/DUT-oriented label (`[A-Za-z0-9._-]`, ≤ 24 chars). It
    goes into the USB product string, so the `/dev/serial/by-id/` path becomes
    `usb-theyoctojester_DUTler-<name>_<serial>-if0N` — handy for telling several DUTlers apart on
    one host. Applied immediately via a live USB re-enumeration (open handles on that DUTler drop
    and must be reopened); persisted by `save`.
  - **`serial`** — read-only device property; `get serial` prints the Pico's hardware unique ID
    (the same value used as the USB `iSerial`). `set serial` is rejected.
  - **`version`** — read-only; `get version` prints the firmware version. `set version` is rejected.
  - **`kv <key>`** — a user key/value store for arbitrary strings (asset tag, rack location, DUT
    notes…), kept in its own flash area separate from the built-in settings. `set kv <key> <value>`
    stores (the value is the rest of the line, so spaces are allowed); `set kv <key> clear` deletes;
    `get kv <key>` reads one; `get kv` lists all. Keys are `[A-Za-z0-9._-]` (≤ 31 chars), values
    ≤ 111 chars. Like the other settings it stages until `save`.
- **`save`** persists the settings to flash; **`status`** shows the outputs plus a quick summary.
- **`selftest`** (GP0↔GP1 loopback), **`factory-reset confirm`**,
  **`bootsel`** (reboot into the USB bootloader), and **`reset`** (warm reboot into the
  application, e.g. to clear an occasional UART lockup) round it out.

### Interactive shell

`set shell on` turns the Control port into a readline-style shell: a `dutler> ` prompt, in-line
editing (Left/Right/Home/End, Ctrl-A/E, backspace/Delete), Up/Down command **history**, **Ctrl-R**
reverse search, Ctrl-U/K/W kill + Ctrl-Y yank, **Tab** completion of commands/keys/values/output
names, and coloured replies (errors red, `ok` green). The firmware drives the whole terminal, so
the host just needs a raw, VT100-style terminal (`picocom`, `screen`, …). Note that **scrollback is
a terminal feature**, not the DUTler's — use `screen`/`tmux` if you want it (`picocom` has none);
history, by contrast, lives in the DUTler.

Shell mode is **off by default** so the promptless, un-echoed line protocol stays clean for scripts
and CI; persist your choice with `save`. A DUTler left in shell mode will feed prompt/echo/colour to
a scripted `printf | cat` driver, so keep it off for automation.

## Persistent settings

Output **names**, the **bridge boot UART config**, the **control-port echo** flag, the
**device name**, and the **interactive-shell** flag are stored in flash. `set …` commands
change them in RAM (shown as "unsaved changes" in `status`); `save` writes them. They survive
power cycles *and* normal firmware reflashes (the UF2 only overwrites the program region, not
the settings sectors).

Storage uses an **A/B (ping-pong) scheme** across the last two 4 KB sectors: each record
carries a monotonic sequence number, `save` writes the *inactive* slot and verifies it before
it counts, and load picks the valid slot with the highest sequence. The active slot is never
erased, so a **failed or power-interrupted save cannot lose the last good config** — load just
falls back to the older slot (the half-written one fails its CRC). A blank/garbage pair falls
back to safe defaults. The record is **versioned**; `src/core/settings.c` documents the append-only
evolution rules and migrates older formats in place on first boot (single-slot v1 records are
upgraded to A/B, pre-device-name v2 records to v3, and pre-shell-flag v3 records to v4, preserving
the existing settings).

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

Contributions are welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md). Contributions are accepted
under a DCO sign-off (`git commit -s`) — no CLA. Please follow the
[Code of Conduct](CODE_OF_CONDUCT.md), and report security issues privately per
[`SECURITY.md`](SECURITY.md).

## License

Copyright © 2026 Northern.tech AS.

The firmware is licensed under the **Apache License, Version 2.0** — see [`LICENSE`](LICENSE) for
the full text and [`NOTICE`](NOTICE) for attribution. Every source file carries the
[SPDX](https://spdx.dev) identifier `Apache-2.0`.

The software is distributed on an **"AS IS" basis, without warranties or conditions of any kind,
and without support**.

- **Hardware** design files in [`hardware/`](hardware/) are licensed under **CERN-OHL-P-2.0** (the
  permissive CERN Open Hardware Licence) — see [`hardware/LICENSE`](hardware/LICENSE) and
  `hardware/README.md`.
- **Documentation** is offered under **CC-BY-4.0**.

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
