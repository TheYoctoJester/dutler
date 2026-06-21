# Contributing to DUTler

Thanks for your interest! **DUTler is experimental and unsupported** — it is offered "AS IS",
without warranties of any kind, and there is no guaranteed response time on issues or pull
requests. That said, issues and PRs are welcome.

DUTler is licensed under the **Apache License, Version 2.0**. By contributing, you agree that your
contribution is provided under Apache-2.0 and you certify the Developer Certificate of Origin (DCO)
by signing off your commits (`git commit -s`). There is **no CLA**.

## Before you start

- For anything non-trivial, **open an issue first** to discuss the approach — it saves everyone a
  round of rework.
- Small fixes (typos, obvious bugs) can go straight to a PR.
- Keep the project's character in mind: it's small, dependency-light embedded firmware. New
  features should earn their footprint (this firmware deliberately links **no floating point**,
  has **no UART flow control** by design, etc. — see the README's *Design notes / non-goals*).

## Development setup

See the README's **Requirements** and **Quick start**. In short:

```sh
source env.sh
cmake -S . -B build -G Ninja \
  -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPICO_TOOLCHAIN_PATH="$PICO_TOOLCHAIN_PATH" \
  -Dpicotool_DIR="$picotool_DIR"
cmake --build build          # -> build/dutler.uf2
./flash.sh                   # flash a connected Pico
```

Host-side helpers live in `tools/` (loopback, relay, reset, debug-log capture). Where a change
affects device behaviour, **verify it on real hardware** and say so in the PR.

## Coding style

- C11, **match the surrounding style** (it's consistent — 4-space indent, braces, naming). If in
  doubt, mimic the nearest existing code.
- Keep it small and explicit; avoid pulling in new dependencies or floating point.
- **Every source file carries an SPDX header** — copy the existing two lines onto any new file:

  ```c
  // SPDX-FileCopyrightText: <year> Northern.tech AS
  // SPDX-License-Identifier: Apache-2.0
  ```
  (`#` comment for shell/CMake/Python.) The repository keeps copyright notices
  under Northern.tech AS; you keep authorship credit via git history.

## Commits & pull requests

- Write clear, imperative commit messages explaining **why**, not just what.
- **Sign off every commit** (Developer Certificate of Origin): `git commit -s` adds a
  `Signed-off-by:` line certifying you wrote it / have the right to submit it.
- Keep PRs focused; rebase on the latest `main`; make sure it builds (and CI passes, once set up).

