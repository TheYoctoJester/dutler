// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "bridge.h"
#include "fakes.h"

static bool selftest_result = true;

void fake_bridge_set_selftest(bool ok) { selftest_result = ok; }

bool bridge_selftest(void) { return selftest_result; }
