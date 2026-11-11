# ── tools/cb-tests ───────────────────────────────────────────────────────
# cb-tests — unit tests over CB's own libraries (never deployed). The engine's tests (engine-tests) live in HKY-SDK.

add_executable(cb-tests CbTests.cpp CompatTests.cpp)
target_link_libraries(cb-tests PRIVATE config)

set_target_properties(cb-tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(cb-tests PRIVATE /W4 /wd4100 /MP /EHsc)
endif()

add_test(NAME cb-tests COMMAND cb-tests)
