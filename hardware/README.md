# DUTler — Hardware (work in progress)

This directory will hold the **open-source hardware** for a DUTler carrier/HAT: a small board
that breaks the Pico out into bench-friendly connectors — the bridge UART (level-shifted),
relay/MOSFET outputs with proper drivers + flyback protection, the GPIO pulls that keep relays
safe at power-on, and labelled headers.

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

Copyright © 2026 Northern.tech AS.

The hardware design files in this directory are licensed under **CERN-OHL-P-2.0** (the permissive
CERN Open Hardware Licence) — see [`LICENSE`](LICENSE). They are provided **"AS IS", without
warranties of any kind**. The firmware in the parent repo is licensed under **Apache-2.0**.

