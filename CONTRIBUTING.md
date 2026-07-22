# Contributing to DUTler

Thanks for your interest! **DUTler is experimental and unsupported** — it is offered "AS IS",
without warranties of any kind, and there is no guaranteed response time on issues or pull
requests. That said, issues and PRs are welcome.

DUTler is licensed under the **Apache License, Version 2.0**. By contributing, you agree that your
contribution is provided under Apache-2.0 and you certify the Developer Certificate of Origin (DCO)
by signing off your commits — see [Commits & pull requests](#commits--pull-requests) below. There
is **no CLA**.

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

Host-side helpers live in `tools/` (loopback, output control, reset, debug-log capture). Where a change
affects device behaviour, **verify it on real hardware** and say so in the PR.

## Coding style

- C11, formatted with **clang-format** — run `clang-format -i` on your changed files (the repo
  has a `.clang-format`). Use the exact version CI pins (see the `clang-format` job in
  `.github/workflows/ci.yml`); formatting output differs between releases, so matching it avoids
  spurious diffs.
  - A **pre-commit hook** in `.githooks/` runs the same check on your staged files and blocks a
    commit that isn't clean. Enable it once per clone: `git config core.hooksPath .githooks`. It
    needs `clang-format` on `PATH` (set `$CLANG_FORMAT` for a versioned binary); if it's missing
    the hook skips rather than blocks. Bypass a single commit with `git commit --no-verify`.
- Keep it small and explicit; avoid pulling in new dependencies or floating point.
- **Every source file carries an SPDX header** — copy the existing two lines onto any new file:

  ```c
  // SPDX-FileCopyrightText: <year> Northern.tech AS
  // SPDX-License-Identifier: Apache-2.0
  ```
  (`#` comment for shell/CMake/Python.) The repository keeps copyright notices under Northern.tech
  AS; you keep authorship credit via git history.

## Tests & CI checks

Every push and PR runs CI (`.github/workflows/ci.yml`): clang-format, SPDX headers, script
linting (shellcheck + ruff), `cppcheck` static analysis, host unit tests (built `-Werror` and
run under AddressSanitizer + UBSan), and the firmware build (`-Werror`). To reproduce locally:

```sh
# host unit tests, as CI runs them (warnings = errors, sanitized)
cmake -S tests -B build-tests -G Ninja -DDUTLER_WERROR=ON -DDUTLER_SANITIZE=ON
cmake --build build-tests && ctest --test-dir build-tests --output-on-failure

# firmware build with warnings-as-errors
cmake -S . -B build -G Ninja -DPICO_SDK_PATH="$PICO_SDK_PATH" -DDUTLER_WERROR=ON && cmake --build build

# the linters (CI pins ruff; clang-format pinned to 22.x)
shellcheck $(git ls-files '*.sh')
ruff check tools/*.py
cppcheck --enable=warning,performance,portability --inline-suppr \
  --suppress=missingIncludeSystem --error-exitcode=1 -I include -I src src/
```

`-DDUTLER_WERROR` / `-DDUTLER_SANITIZE` default **off**, so a plain local build stays lenient —
CI turns them on.

The host suite uses **[Unity](https://www.throwtheswitch.org/unity)** (vendored under
`tests/vendor/`) and is split into runners: `test_pure.c` (CRC/parsing/codec), `test_settings.c`
(the A/B flash store, via the RAM fake in `tests/fakes/`), and `test_command.c` (the command
interpreter, via the fakes + minimal SDK shims in `tests/shims/`). Hardware is abstracted behind
`src/platform/flash_port.h` (real impl `flash_port_pico.c`; the tests link
`tests/fakes/flash_port_fake.c`). New logic should come with a test in the matching runner — or a
new one wired up in `tests/CMakeLists.txt`.

## Source layout & the layering rule

`src/` is organised by dependency, and headers are included by their path from `src/`
(`#include "core/settings.h"`, `"platform/bridge.h"`, `"util/crc32.h"`) so every include announces
which layer it reaches into:

- **`src/core/`** — application logic (command interpreter, output model, settings store + codec).
  This is the real code the host suite exercises.
- **`src/platform/`** — everything bound to the board, the Pico SDK, or TinyUSB (the flash port,
  the UART/USB bridge, the CDC console, USB descriptors, the debug log). Each of these has a host
  fake in `tests/fakes/`.
- **`src/util/`** — pure, product-agnostic helpers (CRC-32, integer parsing).
- **`src/main.c`** — the composition root that wires the layers together; it may touch anything.

**The invariant: everything in `core/` must build and pass on the host** (against `tests/shims/` +
`tests/fakes/`) — it must never depend on real *hardware behaviour*. It may make the handful of
thin SDK calls the shims already cover (GPIO, timers, `tud_task`, bootrom reset); anything with
behaviour worth verifying goes behind a **seam** — an interface used by `core/` with a real impl in
`platform/` and a fake in `tests/fakes/` (as `flash_port`, `bridge`, and `console` do). If you reach
for an SDK symbol that isn't shimmed, that's the signal to either add a shim or move the code to
`platform/`. Don't let `core/` come to depend on the real hardware; that's what makes it testable.

## Commits & pull requests

- Write clear, imperative commit messages explaining **why**, not just what.
- **Sign off every commit** (Developer Certificate of Origin): `git commit -s` adds a
  `Signed-off-by:` line certifying you wrote it / have the right to submit it and that it may be
  distributed under the project's Apache-2.0 license. This is the only agreement required — there
  is no CLA.
- Keep PRs focused; rebase on the latest `main`; make sure it builds (and CI passes, once set up).

Questions: contact Northern.tech (<https://northern.tech>).
