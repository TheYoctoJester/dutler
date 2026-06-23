# DUTler — Hardware

Open-source hardware for the DUTler carrier/HAT: a small board that breaks a Raspberry Pi Pico
out into bench-friendly connectors for driving a device under test —

- the **bridge UART** (GP0/GP1) on a labelled 3-pin header;
- two **low-side MOSFET drivers** for the DUT's strapping/boot-mode and reset lines;
- one **power relay** (driven via a ULN2003, with flyback protection) to switch DUT power; and
- the **gate pull-downs** that hold every output OFF at power-on.

The firmware also runs fine on a bare Pico with jumper wires — this board just makes the wiring
permanent and bench-friendly. See the top-level [`README.md`](../README.md) *Wiring* section for
the pin map (it is kept in sync with this board).

## Contents

```
hardware/
├── README.md                 # this file
├── LICENSE                   # CERN-OHL-P-2.0 (see "License" below)
└── v1/
    └── DUTler_v1/            # KiCad 9 project — the v1 design
        ├── DUTler_v1.kicad_pro   # project
        ├── DUTler_v1.kicad_sch   # schematic
        ├── DUTler_v1.kicad_pcb   # board layout
        └── BOM.md                # bill of materials + assembly notes
```

Only the KiCad **design source** is tracked. Generated outputs (Gerbers/drill, `*.kicad_prl`
local settings, `*-backups/`, the local-history dir) are `.gitignore`d — regenerate them from
the `.kicad_pcb` when you send the board to fabrication.

## Status

**v1 — first iteration, not yet fabricated or bench-verified.** The schematic and layout are
complete enough to review and route; treat footprints, the relay coil-voltage variant, and the
connector pinouts as **provisional** until a board has been built and tested.

## Opening the design

Open `v1/DUTler_v1/DUTler_v1.kicad_pro` in **KiCad 9** (or newer). Older KiCad will refuse the
file format (schematic `version 20260306`, PCB `version 20260206`).

## What it drives (v1)

| Pico | Net | Part | Connector |
|------|-----|------|-----------|
| GP0 / GP1 | bridge UART TX/RX | — | J2 (TX, RX, GND) |
| GP2 | out 1 | 2N7000 MOSFET (Q1), low-side | J1 |
| GP3 | out 2 | 2N7000 MOSFET (Q2), low-side | J3 |
| GP4 | out 3 | ULN2003 → relay coil (K1) | J4 (NC, NO, COM) |
| GP5 | out 4 | *(firmware spare — no driver on v1)* | — |

The MOSFET gates have 220 Ω series resistors (R2/R3) and 10 kΩ gate-to-source pull-downs
(R1/R4), so both MOSFETs sit OFF at power-on. The relay idles de-energized (ULN2003 input low),
and its coil free-wheels through the ULN2003's internal diode (COM tied to the coil supply).

J4 breaks out the relay contacts as **pin 1 = NC, pin 2 = NO, pin 3 = COM** (Finder terminals
12 / 14 / 11). Energizing the relay (`out 3 on`, active-high) connects **COM → NO**, so route the
DUT supply through **COM (pin 3) and NO (pin 2)** if you want `on` to mean *powered*.

> **Relay coil voltage:** K1's coil is wired to **VBUS (5 V)**. Fit the 5 V FINDER 34.51
> (`34.51.7.005.0010`) — a 12 V/24 V part will not actuate. See the BOM notes in
> [`v1/DUTler_v1/BOM.md`](v1/DUTler_v1/BOM.md).

## License

Copyright © 2026 Northern.tech AS.

The hardware design files in this directory are **dual-licensed** — use them under *either*:

- **CERN-OHL-P-2.0** (open hardware) — see [`LICENSE`](LICENSE). This is the strongly-reciprocal
  CERN Open Hardware Licence, the hardware analogue of the firmware's GPL: products and modified
  designs based on these files must make their complete source available under the same licence; **or**
- a **commercial / proprietary license** from **Northern.tech AS**, for use without CERN-OHL-P's
  reciprocity obligations — see [`../COMMERCIAL.md`](../COMMERCIAL.md).

> KiCad's S-expression files carry no per-file comment headers, so the SPDX identifier
> (`CERN-OHL-P-2.0`) is recorded here and in the project rather than inline. The firmware in the
> parent repo is separately licensed **GPL-3.0-or-later OR commercial** (see [`../LICENSE`](../LICENSE)).
