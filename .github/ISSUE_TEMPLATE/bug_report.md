---
name: Bug report
about: Something doesn't work as expected
title: ""
labels: bug
assignees: ""
---

**What happened / what you expected**
A clear description of the bug and what you expected instead.

**Steps to reproduce**
1. …
2. …

**Affected area**
- [ ] UART bridge (CDC0)
- [ ] Control outputs / commands (CDC1)
- [ ] Debug log (CDC2)
- [ ] Settings / persistence
- [ ] Build / flash / tooling
- [ ] Other:

**Environment**
- DUTler version / commit:
- Host OS + tool (e.g. `screen`, `pyserial`):
- Board: Raspberry Pi Pico (RP2040)?  Wiring of relevant pins:
- Pico SDK version (if building): 2.2.0

**Logs / output**
Anything from the **Debug Log** port (CDC2) or the failing command's reply, plus the exact
commands you sent. Did `selftest` pass?
