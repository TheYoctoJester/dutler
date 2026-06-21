#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial
#
# Fail if any source/build/script file lacks an SPDX-License-Identifier header.
# Run from anywhere; operates on the repo it lives in.
set -euo pipefail
cd "$(dirname "$0")/.."

missing=0
while IFS= read -r f; do
    case "$f" in
        pico_sdk_import.cmake) continue ;;  # vendored; keeps its upstream BSD header
    esac
    if ! grep -q "SPDX-License-Identifier" "$f"; then
        echo "missing SPDX header: $f"
        missing=1
    fi
done < <(git ls-files \
    '*.c' '*.h' '*.py' '*.sh' '*.cmake' 'CMakeLists.txt' \
    '.github/workflows/*.yml' '.github/dependabot.yml' '.clang-format' '.editorconfig')

if [ "$missing" -ne 0 ]; then
    echo "SPDX check FAILED"
    exit 1
fi
echo "SPDX check OK"
