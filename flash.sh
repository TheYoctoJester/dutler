#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: Apache-2.0

# Build and flash the firmware — button-free when the adapter is already running.
#
# If the firmware is running, it is asked (via the relay port's 'bootsel' command)
# to reboot into the USB bootloader automatically. If that can't happen (e.g. very
# first flash, or a wedged board), fall back to the manual BOOTSEL button.
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh

cmake --build build

in_bootsel() { ls /Volumes/RPI-RP2 >/dev/null 2>&1 || picotool info >/dev/null 2>&1; }

if ! in_bootsel; then
    echo "=== asking running firmware to reboot into BOOTSEL ==="
    python3 tools/reset_bootsel.py || true
    for _ in $(seq 1 30); do in_bootsel && break; sleep 0.5; done
fi

if ! in_bootsel; then
    echo "Could not reach BOOTSEL automatically."
    echo "Hold the BOOTSEL button while replugging the Pico, then re-run ./flash.sh"
    exit 1
fi

echo "=== loading firmware ==="
picotool load -x build/dutler.uf2
echo "done."
