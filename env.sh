# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: GPL-3.0-or-later

# Source this before building:  source env.sh
#
# Portable, override-friendly build environment for DUTler. Anything already set
# in the environment is honored (so CI can pin its own paths); otherwise we fall
# back to siblings of this repo:
#
#     <parent>/pico-sdk
#     <parent>/arm-gnu-toolchain-*-arm-none-eabi
#
# and pick up picotool's CMake package from Homebrew if present.

# Directory containing this script (works when sourced from bash or zsh).
_dutler_dir="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
_pico_root="$(dirname "$_dutler_dir")"

# Pico SDK (override with a pre-set PICO_SDK_PATH, e.g. in CI).
export PICO_SDK_PATH="${PICO_SDK_PATH:-$_pico_root/pico-sdk}"

# Arm GNU bare-metal toolchain. `find` does the matching (no shell glob, so no
# zsh "no matches found" when absent); newest match wins.
if [ -z "${PICO_TOOLCHAIN_PATH:-}" ]; then
    _tc="$(find "$_pico_root" -maxdepth 1 -type d -name 'arm-gnu-toolchain-*-arm-none-eabi' 2>/dev/null | sort | tail -1)"
    [ -n "$_tc" ] && export PICO_TOOLCHAIN_PATH="$_tc"
    unset _tc
fi

# picotool's CMake package (so the SDK doesn't rebuild picotool). Homebrew on this
# machine; CI should pre-set picotool_DIR or install picotool to a known prefix.
if [ -z "${picotool_DIR:-}" ] && command -v brew >/dev/null 2>&1; then
    export picotool_DIR="$(brew --prefix picotool 2>/dev/null)/lib/cmake/picotool"
fi

# Put the toolchain on PATH if we found/were given one.
[ -n "${PICO_TOOLCHAIN_PATH:-}" ] && export PATH="$PICO_TOOLCHAIN_PATH/bin:$PATH"

unset _dutler_dir _pico_root
