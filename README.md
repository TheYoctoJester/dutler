# DUTler 🎩

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
- **power / GPIO control** — relays to power-cycle the DUT, or yank it into a recovery/bootloader
  mode, when an update leaves it unresponsive;

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
| **CDC1 — "Relay Control"** | Newline-terminated text commands to switch relay GPIOs. |
| **CDC2 — "Debug Log"** | Read-only firmware log stream (open it to start receiving; output is dropped when nobody is listening). |

On macOS these enumerate as three ports, e.g. `usbmodemXXX1` (bridge), `usbmodemXXX3`
(relay), `usbmodemXXX5` (debug log).

## Wiring (defaults, see `include/config.h`)

| Function | Pin |
|----------|-----|
| Bridge UART TX | GP0 → device RX |
| Bridge UART RX | GP1 ← device TX |
| Relay 1..4 | GP2, GP3, GP4, GP5 (active-high, **OFF at boot**) |
| Activity LED | GP25 (on-board) |
| GND | any GND pin — **share ground with the UART peer and relay board** |

Relays are driven at 3.3 V logic. Use a relay **module** with its own driver/opto stage
(do not switch a coil directly from a GPIO). For active-low boards set `RELAY_ACTIVE_LOW 1`.

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

After flashing, two devices appear: `ls /dev/tty.usbmodem*` (two entries).

- **Bridge loopback:** jumper **GP0↔GP1**, open the *UART Bridge* port and echo:
  ```sh
  screen /dev/tty.usbmodemXXXX 115200     # type chars -> should echo back
  ```
  (Ctrl-A then K to quit `screen`.) Changing baud still works — it's pushed to the UART.
- **Relays:** open the *Relay Control* port and send commands:
  ```
  help
  status
  relay 1 on
  relay 1 off
  relay 2 toggle
  relay 3 pulse 500
  ```

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
├── LICENSE                # firmware license: GPL-3.0-or-later
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
│   └── settings.c/.h      # power-loss-safe A/B flash settings
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

## License

- **Firmware** (this repo's `src/`, `include/`, build scripts): **GPL-3.0-or-later** — see
  [`LICENSE`](LICENSE). Copyleft: redistributed/modified firmware must stay GPL.
- **Hardware** (future `hardware/`): intended to be **CERN-OHL-P-2.0** (strongly-reciprocal
  open hardware) — see `hardware/README.md`.
- **Documentation** may be offered under **CC-BY-4.0**.

Third-party components are under their own permissive licenses and are *not* relicensed: the
Raspberry Pi **Pico SDK** (BSD-3-Clause) and **TinyUSB** (MIT), pulled in at build time.

## Acknowledgements

DUTler is **sponsored by [Northern.tech](https://northern.tech) as part of the
[Mender.io](https://mender.io) community engagement** — thank you for backing open tooling for
the embedded-Linux community. Thanks also to the Raspberry Pi Pico SDK and TinyUSB projects,
which do the heavy USB lifting.

> "Mender", "Northern.tech", and the Mender moose are trademarks of Northern.tech AS. They are
> used here only to describe the project's purpose and the integration workflow it serves.
