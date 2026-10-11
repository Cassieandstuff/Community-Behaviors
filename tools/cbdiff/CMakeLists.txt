# ── tools/cbdiff ─────────────────────────────────────────────────────────
# cbdiff — semantic diff of CB's compiled output vs a PROVEN reference (Pandora). A build-time DRIVER over
# the engine: parses the animationdata / animationsetdata caches on both sides and reports per-record
# divergences (Pandora is the oracle).

add_executable(cbdiff cbdiff.cpp)

target_link_libraries(cbdiff PRIVATE engine)

set_target_properties(cbdiff PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(cbdiff PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
