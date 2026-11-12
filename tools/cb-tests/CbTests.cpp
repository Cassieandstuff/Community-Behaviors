/// @file
/// @brief cb-tests: unit tests over CB's own libraries (the engine's tests live in HKY-SDK).

#include "TestSupport.h"

#include <cstdio>

namespace CB::config::tests {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    ++g_failures;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
}

}  // namespace CB::config::tests

int main() {
    namespace t = CB::config::tests;
    t::RunCompatTests();
    if (t::g_failures) std::fprintf(stderr, "%d failure(s)\n", t::g_failures);
    else std::printf("cb-tests: all passed\n");
    return t::g_failures;
}
