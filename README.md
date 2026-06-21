# DUTler 🎩

[![CI](https://github.com/TheYoctoJester/dutler/actions/workflows/ci.yml/badge.svg)](https://github.com/TheYoctoJester/dutler/actions/workflows/ci.yml)
[![Platform: RP2040](https://img.shields.io/badge/platform-RP2040-8a2be2.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Status: alpha](https://img.shields.io/badge/status-alpha-orange.svg)](#)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome%20(DCO)-brightgreen.svg)](CONTRIBUTING.md)

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
| **CDC1 — "Relay Control"** | Newline-terminated commands to switch the DUT control outputs — a power relay plus MOSFET strap/reset drivers. |
| **CDC2 — "Debug Log"** | Read-only firmware log stream (open it to start receiving; output is dropped when nobody is listening). |

On macOS these enumerate as three ports, e.g. `usbmodemXXX1` (bridge), `usbmodemXXX3`
(control), `usbmodemXXX5` (debug log).

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

**Target:** a stock **Raspberry Pi Pico (RP2040)** (`PICO_BOARD=pico`) and a USB cable. A single
jumper wire between **GP0 and GP1** is handy for the bridge loopback self-test.

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
cmake --build build

# flash: hold BOOTSEL while plugging in the Pico, then
./flash.sh                              # or drag build/dutler.uf2 onto the RPI-RP2 drive

# use it: three USB serial ports appear
ls /dev/cu.usbmodem*                    # bridge (…1), control (…3), debug log (…5)
screen /dev/cu.usbmodemXXXX1 115200     # the DUT serial console
#   …then open the control port (…3) and type 'help' for relay/strap/reset commands
```

See **Build**, **Flash** and **Test** below for the details, and **Wiring** for pin assignments.

## Wiring (defaults, see `include/config.h`)

| Function | Pin |
|----------|-----|
| Bridge UART TX | GP0 → device RX |
| Bridge UART RX | GP1 ← device TX |
| Control out 1 — power relay | GP2 — switch DUT power |
| Control out 2–4 — MOSFET | GP3, GP4, GP5 — assert DUT strap/boot-mode & reset pins |
| Activity LED | GP25 (on-board) |
| GND | any GND pin — **share ground with the DUT and any relay/MOSFET board** |

All control outputs are 3.3 V logic, **active-high, and OFF at boot**. In firmware they're just
generic switched GPIOs (all driven via the `relay`/name commands); their *intended* roles are:

- **Out 1 → a power relay** — to cut/restore DUT power. Use a relay **module** with its own
  driver/opto stage; don't switch a coil straight off a GPIO.
- **Outs 2–4 → low-side MOSFET gates** — to pull the DUT's strapping/boot-mode and reset lines
  (e.g. force USB/serial download mode, then toggle reset). The MOSFET does the level shift and
  the pull; the GPIO only drives its gate. Match the DUT's logic level and share ground.

Tip: alias the outputs to their jobs once and drive them by name —
`name 1 power`, `name 2 bootmode`, `name 3 reset`, then `power off` / `bootmode on` / `reset on`.
Count, pins and polarity live in `include/config.h` (`RELAY_*`); `RELAY_ACTIVE_LOW 1` flips sense.

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
automatically (no button) by sending the relay port's `bootsel` command, then loads the new
image. You can also trigger the reset yourself:

- send `bootsel` to the relay port, **or**
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
- **Control outputs:** open the *Relay Control* port and send commands:
  ```
  help
  status
  relay 1 on
  relay 1 off
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

## Relay command grammar

```
relay <id> on|off|toggle    id = relay number (1..) or a configured name
<id> on|off|toggle          shorthand: e.g. 'pump on' == 'relay pump on'
name <n> <alias|clear>      label relay n (then usable as <id> above)
set baud <n>                bridge boot baud rate
set format <8N1>            bridge boot data/parity/stop (e.g. 8N1, 7E1)
save                        persist names + bridge defaults to flash
selftest                    GP0<->GP1 loopback continuity check (non-destructive)
factory-reset confirm       erase saved settings (back to defaults)
status                      list relays + bridge defaults
bootsel                     reboot into USB bootloader
help                        show command help
```

## Persistent settings

Relay **names** and the **bridge boot UART config** are stored in flash. `set …`/`name …`
change them in RAM (shown as "unsaved changes" in `status`); `save` writes them. They survive
power cycles *and* normal firmware reflashes (the UF2 only overwrites the program region, not
the settings sectors).

Storage uses an **A/B (ping-pong) scheme** across the last two 4 KB sectors: each record
carries a monotonic sequence number, `save` writes the *inactive* slot and verifies it before
it counts, and load picks the valid slot with the highest sequence. The active slot is never
erased, so a **failed or power-interrupted save cannot lose the last good config** — load just
falls back to the older slot (the half-written one fails its CRC). A blank/garbage pair falls
back to safe defaults. The record is **versioned**; `src/settings.c` documents the append-only
evolution rules and migrates the older format in place (v1 single-slot records are upgraded to
v2 A/B on first boot, preserving names + baud).

**Relays themselves always boot OFF** — their state is deliberately not persisted, so a power
blip can never silently re-energize a load. Implemented in `src/settings.c`
(struct, CRC32, flash erase/program) — note flash writes briefly mask interrupts (a few ms),
so avoid `save` in the middle of heavy bridge traffic.

## Watchdog

A hardware watchdog (`WATCHDOG_TIMEOUT_MS` in `config.h`, default 2 s) is fed every main-loop
iteration, so a wedged loop (e.g. a hung USB stack) reboots and recovers automatically. The
`save` path feeds it just before the interrupts-masked flash erase, which is the longest
blocking section. At boot the firmware records whether the reset was a real watchdog timeout
(via `watchdog_enable_caused_reboot()`, which ignores deliberate `bootsel`/reflash reboots)
and `status` reports it. Relay outputs always come up OFF after any reset.

## Design notes / non-goals

- **No UART flow control — intentional, not missing.** The bridge is a 3-wire link
  (TX/RX/GND): no hardware RTS/CTS and no software XON/XOFF. This is a deliberate scope
  decision. Backpressure exists where it matters — USB→UART is non-blocking and NAKs the host
  when the staging buffer fills — and the only lossy path (UART→USB ring overflow when the host
  stops reading) is **reported** on the debug port rather than prevented. Use matched baud
  rates and don't firehose the bridge from a flow-controlled peer. Please don't "add the missing
  flow control": it's a choice. If a future use case genuinely needs it, wire RTS/CTS pins and
  call `uart_set_hw_flow()` in `bridge.c` — but that's a new feature, not a fix.

## Layout

```
DUTler/
├── LICENSE                # Apache-2.0 license text
├── CONTRIBUTING.md        # how to contribute (DCO, build/style/PR)
├── THIRD_PARTY.md         # third-party components + their licenses
├── env.sh                 # self-contained build environment (source before building)
├── flash.sh               # build + picotool load
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── include/config.h       # pins, relay count/polarity, UART, USB IDs — main knobs
├── src/
│   ├── main.c             # init + super-loop (tud_task / bridge / relay)
│   ├── tusb_config.h      # TinyUSB: 3× CDC, full-speed device
│   ├── usb_descriptors.c  # composite triple-CDC descriptors + chip-ID serial
│   ├── bridge.c/.h        # CDC0 <-> uart0, IRQ RX ring buffer, line-coding sync
│   ├── relay.c/.h         # CDC1 command parser -> GPIO
│   ├── debug.c/.h         # dbg_printf() -> CDC2 debug-log port
│   ├── settings.c/.h      # power-loss-safe A/B flash settings (flash I/O + slots)
│   ├── settings_codec.c/.h # pure record (de)serialization — unit-tested
│   ├── crc32.c/.h         # pure CRC-32 — unit-tested
│   └── parse.c/.h         # pure integer parsing — unit-tested
├── tests/                 # host unit tests (native build, no SDK) + CMakeLists
├── tools/                 # host-side test/util scripts (loopback, relay, reset, debug)
└── hardware/              # (future) open-hardware carrier board — see hardware/README.md
```

## Debug log

Firmware logs go to the **Debug Log** port (CDC2). Watch them with:

```sh
screen /dev/cu.usbmodemXXXX5 115200      # or: python3 tools/debug_capture.py
```

The firmware also logs **`bridge: RX overflow ...`** here (rate-limited) if the UART-to-USB
ring ever drops bytes because the host stopped draining — so silent data loss becomes visible.

Add your own with `dbg_printf("...")` (declared in `src/debug.h`). Output is only sent while a
host has the port open, so calls are cheap when unused. **Do not call `dbg_printf` from an
interrupt handler** (it touches the USB TX FIFO shared with the main loop).

> USB VID/PID in `config.h` are pid.codes **test** IDs — replace before any distribution.

## Contributing

Contributions are welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md). Contributions are accepted
under a DCO sign-off (`git commit -s`). Please follow the [Code of Conduct](CODE_OF_CONDUCT.md),
and report security issues privately per [`SECURITY.md`](SECURITY.md).

## License

Copyright © 2026 Northern.tech AS.

DUTler is licensed under the **Apache License, Version 2.0** — see [`LICENSE`](LICENSE). Every
source file carries the [SPDX](https://spdx.dev) identifier `Apache-2.0`. The software is
distributed on an **"AS IS" basis, without warranties or conditions of any kind, and without
support**.

Third-party components keep their own licenses and are *not* relicensed — see
[`THIRD_PARTY.md`](THIRD_PARTY.md).

## Acknowledgements

DUTler is **sponsored by [Northern.tech](https://northern.tech) as part of the
[Mender.io](https://mender.io) community engagement** — thank you for backing open tooling for
the embedded-Linux community. Thanks also to the Raspberry Pi Pico SDK and TinyUSB projects,
which do the heavy USB lifting.

> "Mender", "Northern.tech", and the Mender moose are trademarks of Northern.tech AS. They are
> used here only to describe the project's purpose and the integration workflow it serves.
