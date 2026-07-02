#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: Apache-2.0
#
# Fail if any source/build/script file lacks the expected SPDX-License-Identifier header.
# The project is licensed Apache-2.0, so every non-vendored file must declare exactly that.
# Run from anywhere; operates on the repo it lives in.
set -euo pipefail
cd "$(dirname "$0")/.."

expected="SPDX-License-Identifier: Apache-2.0"
bad=0
while IFS= read -r f; do
    case "$f" in
        pico_sdk_import.cmake) continue ;;  # vendored; keeps its upstream BSD header
        tests/vendor/*) continue ;;         # vendored Unity; keeps its upstream MIT header
    esac
    if ! grep -q "SPDX-License-Identifier" "$f"; then
        echo "missing SPDX header: $f"
        bad=1
    elif ! grep -qF "$expected" "$f"; then
        echo "unexpected SPDX license (want '$expected'): $f"
        bad=1
    fi
done < <(git ls-files \
    '*.c' '*.h' '*.py' '*.sh' '*.cmake' 'CMakeLists.txt' \
    '.github/workflows/*.yml' '.github/dependabot.yml' '.clang-format' '.editorconfig')

if [ "$bad" -ne 0 ]; then
    echo "SPDX check FAILED"
    exit 1
fi
echo "SPDX check OK"
