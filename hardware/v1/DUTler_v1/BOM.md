# DUTler v1 — Bill of Materials

Derived from `DUTler_v1.kicad_sch`. **Provisional** — v1 has not been fabricated or assembled,
so confirm footprints and order codes against the layout before buying. All parts are
through-hole (THT).

| Ref    | Qty | Value / Part            | Package / Footprint                         | Notes |
|--------|-----|-------------------------|---------------------------------------------|-------|
| A1     | 1   | Raspberry Pi Pico       | Pico module on 2×20 0.1″ headers            | The MCU; socket it so it can be replaced. |
| U1     | 1   | ULN2003A                | DIP-16 (W7.62 mm)                           | Darlington array; drives the relay coil and provides coil flyback (COM → VBUS). |
| K1     | 1   | FINDER 34.51, **5 V DC coil** | THT SPDT (Finder 34.51 vertical)      | **Coil runs off VBUS = 5 V.** Order the 5 V variant (e.g. `34.51.7.005.0010` — confirm) — a 12 V/24 V part will not actuate. |
| Q1, Q2 | 2   | 2N7000                  | TO-92 (inline)                              | Logic-level N-channel MOSFET, low-side switch. |
| R1, R4 | 2   | 10 kΩ                   | Axial THT (DIN0207, P10.16 mm)              | MOSFET gate-to-source pull-down → MOSFET OFF at power-on. |
| R2, R3 | 2   | 220 Ω                   | Axial THT (DIN0207, P10.16 mm)              | MOSFET gate series resistor. |
| J1, J3 | 2   | 1×02 pin header, 2.54 mm | PinHeader 1×02 vertical                    | MOSFET output: pin 1 = drain (switched), pin 2 = GND. |
| J2     | 1   | 1×03 pin header, 2.54 mm | PinHeader 1×03 vertical                    | Bridge UART: TX (GP0), RX (GP1), GND. |
| J4     | 1   | 1×03 terminal block, 5.08 mm | Phoenix MKDS-1,5-3 (or compatible)     | Relay contacts: COM / NO / NC. |

## Assembly & wiring notes

- **Safe state at power-on.** Both MOSFETs idle OFF (10 kΩ gate pull-downs); the relay idles
  de-energized (ULN2003 input low). Outputs are 3.3 V logic, active-high (`OUT_ACTIVE_LOW 0`).
- **Relay coil load.** The 5 V Finder 34.51 coil draws roughly 80 mA (~0.4 W) from VBUS while
  energized — budget for it on the USB 5 V supply. Flyback is handled by the ULN2003's internal
  free-wheeling diode (no discrete diode fitted).
- **Relay contact / connector rating** bounds what DUT load you may switch through J4 — keep
  within the Finder 34.51 contact rating and the terminal block's current rating.
- **GP5 / out 4** is a firmware-defined spare with **no driver on this board**; there is no
  connector for it on v1.

> Pinout and roles are kept in sync with the firmware — see the repository
> [`README.md`](../../../README.md) *Wiring* section and `include/config.h`.
