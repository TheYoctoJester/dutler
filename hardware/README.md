# DUTler — Hardware (work in progress)

This directory will hold the **open-source hardware** for a DUTler carrier/HAT: a small board
that breaks the Pico out into bench-friendly connectors:

- the **bridge UART** (level-shifted to the DUT's logic level);
- one **power relay** (isolated, with flyback protection) to switch DUT power;
- a few **low-side MOSFET drivers** for the DUT's strapping/boot-mode and reset lines;
- the **GPIO pull resistors** that hold every output in its safe state at power-on; and
- labelled headers.

Nothing here yet — the firmware runs fine on a bare Pico with jumper wires (see the top-level
README's *Wiring* section). This is a placeholder so the repo layout and licensing are ready
for the board design.

## Planned contents

```
hardware/
├── README.md           # this file
├── schematic/          # schematic source + PDF export
├── pcb/                # board layout + Gerbers/drill
├── bom/                # bill of materials (CSV) + assembly notes
└── docs/               # pinout, wiring, electrical safety notes for relay loads
```

## License

Hardware design files in this directory are intended to be released under the
**CERN Open Hardware Licence Version 2 – Strongly Reciprocal (CERN-OHL-P-2.0)** — the
copyleft OSHW licence (the hardware analogue of the firmware's GPL). The `LICENSE` text and
SPDX headers will be added here when the first design files land.

> Until then, treat this directory as reserved. The **firmware** in the parent repo is
> GPL-3.0-or-later (see `../LICENSE`).
