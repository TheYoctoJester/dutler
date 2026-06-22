# Third-party components

DUTler's own source is dual-licensed **GPL-3.0-or-later OR a commercial license** (see
[`LICENSE`](LICENSE) and [`COMMERCIAL.md`](COMMERCIAL.md)). It is built on, and links
against, the following third-party components. They keep their own licenses and are **not**
relicensed by this project. Full license texts ship inside each component's source tree (in the
Pico SDK / Arm GNU toolchain you install per the README *Requirements*).

## Linked into the firmware image

| Component | License | Role |
|-----------|---------|------|
| **Raspberry Pi Pico SDK** | BSD-3-Clause | RP2040 HAL, runtime, second-stage bootloader, build integration. |
| **TinyUSB** | MIT | USB device stack (the three CDC-ACM ports). Vendored in the Pico SDK at `lib/tinyusb`. |
| **newlib** (via the Arm GNU toolchain) | BSD-style / various permissive (see newlib `COPYING.NEWLIB`) | C library (`printf`, `string.h`, etc.) linked into the image. |
| **libgcc / GCC low-level runtime** (Arm GNU toolchain) | GPL-3.0-or-later **with the GCC Runtime Library Exception** | Compiler support routines. The Runtime Library Exception explicitly permits distributing the resulting binary under your own license terms. |

## Build-time only (not distributed in the image)

| Component | License | Role |
|-----------|---------|------|
| **Arm GNU Toolchain** (`arm-none-eabi-gcc`) | GPL-3.0-or-later (+ exceptions) | Cross-compiler/assembler/linker. |
| **CMake**, **Ninja** | BSD-3-Clause / Apache-2.0 | Build system. |
| **picotool** | BSD-3-Clause | UF2 generation and flashing. |
| **Python 3** (optional) | PSF License | Runs the host helper scripts in `tools/`. |
| **Unity** (ThrowTheSwitch) | MIT | C unit-test framework for the host tests. Vendored at `tests/vendor/unity/` (keeps its own MIT header). |

## Notes

- BSD-3-Clause and MIT are GPL-compatible, so combining them with DUTler's GPL-3.0 source and
  distributing the resulting firmware is fine.
- `pico_sdk_import.cmake` in this repo is copied verbatim from the Pico SDK and retains its own
  Raspberry Pi BSD-3-Clause header (so it carries no DUTler SPDX header).
- The vendored **Unity** sources under `tests/vendor/unity/` are host-test-only (never linked into
  the firmware image) and keep their upstream MIT headers; the SPDX and clang-format checks skip
  that directory.

SPDX identifiers used in this repo: `Apache-2.0`
(firmware), and — once hardware lands — `CERN-OHL-P-2.0` (board) and `CC-BY-4.0` (docs).
