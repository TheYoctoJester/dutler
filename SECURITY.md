# Security policy

## Reporting a vulnerability

**Please report security issues privately — do not open a public issue.**

- Preferred: GitHub's **"Report a vulnerability"** (Security ▸ Advisories) on this repository, or
- email **security@northern.tech** with details and, ideally, a reproduction.

We'll acknowledge your report, work with you on a fix, and credit you in the advisory unless you
prefer otherwise.

## Supported versions

DUTler is pre-1.0; only the latest `main` (and the most recent tagged release) receive security
fixes.

## Threat model / scope

DUTler is a **local USB device**. By design, anything that can open its **control** CDC port can
switch the relay/MOSFET outputs, reset the target, and reboot DUTler into its USB bootloader —
the trust boundary is *"who can access the USB port and open the serial device on the host."*
That local control is **intended behaviour**, not a vulnerability; host-side access control is the
user's responsibility.

In scope (please do report):
- memory-safety bugs in the firmware's input handling (the control-port command parser, USB
  descriptor/control handling, the bridge, flash settings parsing);
- any way for **bridged UART data (CDC0)** to reach the command parser, or otherwise cross a
  channel boundary;
- flash-settings handling that could corrupt or be exploited via crafted/garbage records.

Out of scope:
- the documented local-control capabilities above (relays/reset/bootsel from the control port);
- physical attacks requiring board access (e.g. holding BOOTSEL, SWD).
