# ── Havok/tools/hky ────────────────────────────────────────────────────────────
# hky-tool — the standalone .hky pack/unpack driver (org-pass firesale phase 4a). A build-time DRIVER
# over Havok/core (never deployed): links ONLY the serialization codec (HkyArchive + the packfile spine
# it sits beside), NO typed havok-core. This is the first tenant of Havok/tools/ and proves
# codec/serialization stands alone — the base-master diff-strategy tooling's havok-core-free home.

add_executable(hky-tool hky_tool.cpp)

target_link_libraries(hky-tool PRIVATE serialization)   # HkyArchive + codec/serialization (pulls common/interface/reflection)

set_target_properties(hky-tool PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(hky-tool PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
