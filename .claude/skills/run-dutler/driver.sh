#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: Apache-2.0
#
# Driver for the run-dutler skill.
#
# DUTler is Raspberry Pi Pico firmware. With no board attached (the normal case
# in CI / a container / a dev checkout), "running" it means the two things you
# CAN do without hardware, and both mirror CI exactly:
#
#   * host unit tests  — the real runnable surface. command/settings/console
#                        logic compiled with the native cc + Unity, hardware
#                        seams faked. This is what most PRs here touch.
#   * firmware build   — cross-compile the .uf2 with the Pico SDK + Arm toolchain
#                        to prove the firmware still links for the Pico family.
#
# Driving the *flashed* firmware over USB (tools/*.py) needs a physical Pico and
# is out of scope here — see SKILL.md "Run: on real hardware".
#
# Usage:
#   ./driver.sh test              host unit tests (WERROR + ASan/UBSan)
#   ./driver.sh build [BOARD]     firmware .uf2 for BOARD (default pico2) + size
#   ./driver.sh build-all         firmware for pico, pico_w, pico2, pico2_w
#   ./driver.sh all               test + build pico2      (default when no arg)
set -euo pipefail

# Repo root is three levels up from .claude/skills/run-dutler/.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

run_tests() {
    echo "=== host unit tests (WERROR + ASan/UBSan) ==="
    cmake -S tests -B build-tests -G Ninja -DDUTLER_WERROR=ON -DDUTLER_SANITIZE=ON
    cmake --build build-tests
    ctest --test-dir build-tests --output-on-failure
}

build_board() {
    local board="${1:-pico2}"
    echo "=== firmware build: ${board} ==="
    # env.sh locates the SDK + Arm toolchain (siblings of the repo) and puts the
    # toolchain on PATH. It honors anything already exported (that is how CI pins
    # its own paths). A per-board build dir avoids the RP2040/RP2350 cache clash.
    # shellcheck disable=SC1091
    source ./env.sh
    local bdir="build-${board}"
    cmake -S . -B "${bdir}" -G Ninja \
        -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
        ${PICO_TOOLCHAIN_PATH:+-DPICO_TOOLCHAIN_PATH="${PICO_TOOLCHAIN_PATH}"} \
        ${picotool_DIR:+-Dpicotool_DIR="${picotool_DIR}"} \
        -DPICO_BOARD="${board}" -DDUTLER_WERROR=ON
    cmake --build "${bdir}"
    ls -la "${bdir}/dutler.uf2"
    arm-none-eabi-size "${bdir}/dutler.elf" 2>/dev/null || true
}

cmd="${1:-all}"
case "${cmd}" in
    test)      run_tests ;;
    build)     build_board "${2:-pico2}" ;;
    build-all) for b in pico pico_w pico2 pico2_w; do build_board "${b}"; done ;;
    all)       run_tests; build_board pico2 ;;
    *) echo "usage: $0 {test | build [board] | build-all | all}" >&2; exit 2 ;;
esac
echo "=== driver: ${cmd} OK ==="
