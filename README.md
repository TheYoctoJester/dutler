# USB-UART + Relay Adapter (Raspberry Pi Pico / RP2040)

Firmware that makes a stock Pico enumerate as **two USB serial ports**:

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
cmake --build build                  # -> build/usb_uart_relay.uf2
```

## Flash

```sh
./flash.sh
```

When the adapter is already running, `flash.sh` reboots it into the bootloader
automatically (no button) by sending the relay port's `bootsel` command, then loads the new
image. You can also trigger the reset yourself:

- send `bootsel` to the relay port, **or**
- open the **debug** port at **1200 baud** (the classic USB-serial "1200-baud touch"), **or**
- `python3 tools/reset_bootsel.py`

**First flash / wedged board:** hold the **BOOTSEL** button while plugging in (mounts as
`RPI-RP2`), then run `./flash.sh` or drag `build/usb_uart_relay.uf2` onto the drive.

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
relay <id> pulse <ms>       on for <ms> then auto-off
name <n> <alias|clear>      label relay n (then usable as <id> above)
set baud <n>                bridge boot baud rate
set format <8N1>            bridge boot data/parity/stop (e.g. 8N1, 7E1)
save                        persist names + bridge defaults to flash
status                      list relays + bridge defaults
bootsel                     reboot into USB bootloader
help                        show command help
```

## Persistent settings

Relay **names** and the **bridge boot UART config** are stored in the last 4 KB sector of the
Pico's flash. `set …`/`name …` change them in RAM (shown as "unsaved changes" in `status`);
`save` writes them to flash. They survive power cycles *and* normal firmware reflashes (the
UF2 only overwrites the program region, not the top sector). A version+CRC header means a
blank/garbage sector falls back to safe defaults. The record is **versioned** with a
migration dispatch in `src/settings.c` — that file's header documents the append-only rules
and includes a worked migrator template, so a future layout change can upgrade old records
in place instead of discarding them.

**Relays themselves always boot OFF** — their state is deliberately not persisted, so a power
blip can never silently re-energize a load. Implemented in `src/settings.c`
(struct, CRC32, flash erase/program) — note flash writes briefly mask interrupts (a few ms),
so avoid `save` in the middle of heavy bridge traffic.

## Layout

```
usb-uart-relay/
├── env.sh                 # self-contained build environment (source before building)
├── flash.sh               # build + picotool load
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── include/config.h       # pins, relay count/polarity, UART, USB IDs — main knobs
└── src/
    ├── main.c             # init + super-loop (tud_task / bridge / relay)
    ├── tusb_config.h      # TinyUSB: 2× CDC, full-speed device
    ├── usb_descriptors.c  # composite dual-CDC descriptors + chip-ID serial
    ├── bridge.c/.h        # CDC0 <-> uart0, IRQ RX ring buffer, line-coding sync
    ├── relay.c/.h         # CDC1 command parser -> GPIO
    └── debug.c/.h         # dbg_printf() -> CDC2 debug-log port
```

## Debug log

Firmware logs go to the **Debug Log** port (CDC2). Watch them with:

```sh
screen /dev/cu.usbmodemXXXX5 115200      # or: python3 tools/debug_capture.py
```

Add your own with `dbg_printf("...")` (declared in `src/debug.h`). Output is only sent while a
host has the port open, so calls are cheap when unused. **Do not call `dbg_printf` from an
interrupt handler** (it touches the USB TX FIFO shared with the main loop).

> USB VID/PID in `config.h` are pid.codes **test** IDs — replace before any distribution.
