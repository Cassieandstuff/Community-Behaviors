# ── Havok/tools/cbdiff ─────────────────────────────────────────────────────────
# cbdiff — semantic diff of CB's compiled output vs a PROVEN reference (Pandora). A build-time DRIVER over
# Havok/core: parses the animationdata / animationsetdata caches on both sides and reports per-record
# divergences (Pandora is the oracle). Links decompile (pulls codec/serialization/interface) so the
# behavior-graph decompile-diff mode can be added without touching this file.

add_executable(cbdiff cbdiff.cpp)

find_package(ryml CONFIG REQUIRED)                       # YAML front-end (recursive node-tree diff)
target_link_libraries(cbdiff PRIVATE decompile reflection ryml::ryml)  # reflection = the <RymlInclude.h> conduit

set_target_properties(cbdiff PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(cbdiff PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
