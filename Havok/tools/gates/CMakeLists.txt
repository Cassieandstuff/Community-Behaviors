# ── Havok/tools/gates ────────────────────────────────────────────────────────
# gates — the havok-core-FREE correctness gates (org-pass firesale phase 4b). A build-time DRIVER over
# Havok/core only: validates the schema pipeline vs VANILLA / by schema-self round-trip, never vs the
# typed oracle, so it outlives havok-core's deletion. Seeded with emit-check; the rest of the free gate
# cluster migrates here as havok-core is cut. Links decompile (which pulls serialization/reflection/
# havok-io/interface/codec) — NO havok-core.

add_executable(gates gates.cpp)

target_link_libraries(gates PRIVATE decompile)

set_target_properties(gates PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(gates PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
