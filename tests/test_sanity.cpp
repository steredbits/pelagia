// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Dummy test: checks that the ctest chain works.

#include "test_framework.h"

int main() {
    CHECK(true);
    CHECK_EQ(1 + 1, 2);
    return testfw::test_failures();
}
