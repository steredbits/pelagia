// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "util/session_id.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace util {

namespace {

uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

std::atomic<uint64_t> g_counter{0};

}  // namespace

std::string make_session_id() {
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const uint64_t wall = static_cast<uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    const uint64_t n = g_counter.fetch_add(1);
    const uint64_t a = splitmix64(now ^ (n << 32) ^ reinterpret_cast<uintptr_t>(&n));
    const uint64_t b = splitmix64(wall + n);
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx", static_cast<unsigned long long>(a),
                  static_cast<unsigned long long>(b));
    return buf;
}

}  // namespace util
