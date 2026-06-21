# Contributing to DUTler

Thanks for your interest! DUTler is **dual-licensed** (GPL-3.0-or-later **or** a commercial
license from Northern.tech AS — see [`COMMERCIAL.md`](COMMERCIAL.md)). To keep that possible,
contributions are accepted only under a **Contributor License Agreement** (see
[CLA](#contributor-license-agreement-cla) below). Please read this before opening a pull request.

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
  // SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial
  ```
  (`#` comment for shell/CMake/Python.) Per the CLA, the repository consolidates copyright notices
  under Northern.tech AS; you keep authorship credit via git history.

## Commits & pull requests

- Write clear, imperative commit messages explaining **why**, not just what.
- **Sign off every commit** (Developer Certificate of Origin): `git commit -s` adds a
  `Signed-off-by:` line certifying you wrote it / have the right to submit it.
- Keep PRs focused; rebase on the latest `main`; make sure it builds (and CI passes, once set up).

## Contributor License Agreement (CLA)

Because DUTler is dual-licensed, the maintainer (**Northern.tech AS**) must be able to license your
contribution under **both** the GPL and commercial terms. So, in addition to the DCO sign-off,
contributions require agreement to the **[Contributor License Agreement](CLA.md)**.

You agree to the CLA by:

1. signing off your commits (`-s`, the DCO), **and**
2. confirming acceptance of [`CLA.md`](CLA.md) on your first contribution — via the CLA-assistant
   bot if configured on the repo, or by stating in the PR: *"I have read and agree to the DUTler
   CLA."*

If you're contributing on behalf of an employer, make sure you're authorised to agree (an entity
CLA may be needed). Questions: contact Northern.tech (<https://northern.tech>).
