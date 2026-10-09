// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_TESTS_TEST_FRAMEWORK_H
#define PELAGIA_TESTS_TEST_FRAMEWORK_H

// Home-made mini test framework: no exceptions or RTTI,
// compatible with the core's constraints. Each test file
// defines its main() with CHECK(...) and returns test_failures().

#include <cstdio>

namespace testfw {

inline int& failure_count() {
    static int count = 0;
    return count;
}

inline void report_failure(const char* expr, const char* file, int line) {
    std::fprintf(stderr, "FAIL: %s (%s:%d)\n", expr, file, line);
    ++failure_count();
}

inline int test_failures() {
    if (failure_count() == 0) {
        std::fprintf(stderr, "OK : tous les tests passent\n");
    } else {
        std::fprintf(stderr, "%d failing test(s)\n", failure_count());
    }
    return failure_count();
}

}  // namespace testfw

#define CHECK(expr)                                              \
    do {                                                         \
        if (!(expr)) {                                           \
            ::testfw::report_failure(#expr, __FILE__, __LINE__); \
        }                                                        \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

#endif  // PELAGIA_TESTS_TEST_FRAMEWORK_H
