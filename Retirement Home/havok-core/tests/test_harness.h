#pragma once
#include <cstdio>

// Minimal dependency-free test harness shared by all havok-core test TUs.
// Counters live in test_main.cpp; each test file exposes a `run_*()` function
// that test_main calls.

namespace havok_test {
extern int g_checks;
extern int g_failures;

inline void check(bool cond, const char* file, int line, const char* expr) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL [%s:%d]: %s\n", file, line, expr);
    }
}
}  // namespace havok_test

#define CHECK(cond) ::havok_test::check((cond), __FILE__, __LINE__, #cond)
