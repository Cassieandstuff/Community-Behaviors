# ── tools/hky ────────────────────────────────────────────────────────────
# hky-tool — the standalone .hky pack/unpack driver over the engine (never deployed); also unpacks the
# deployed master for the base-master diff strategy.

add_executable(hky-tool hky_tool.cpp)

target_link_libraries(hky-tool PRIVATE engine)   # HkyArchive (codec/serialization) + the BSA reader (codec/bsa)

set_target_properties(hky-tool PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(hky-tool PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
