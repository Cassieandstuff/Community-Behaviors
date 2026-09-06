#include "test_harness.h"

#include <cstdio>

namespace havok_test {
int g_checks = 0;
int g_failures = 0;
}  // namespace havok_test

// Each test TU exposes one entry point.
void run_binaryio_tests();
void run_classes_tests();
void run_packfile_tests();
void run_roundtrip_tests();
void run_builder_tests();
void run_m2_oracle_tests();
void run_sct_tests();

int main() {
    run_binaryio_tests();
    run_classes_tests();
    run_packfile_tests();
    run_roundtrip_tests();
    run_builder_tests();
    run_m2_oracle_tests();
    run_sct_tests();

    std::printf("havok-core tests: %d checks, %d failure(s)\n",
                havok_test::g_checks, havok_test::g_failures);
    return havok_test::g_failures == 0 ? 0 : 1;
}
