# ── tools/cbmatch ────────────────────────────────────────────────────────
# cbmatch — the byte-exact matching gate: compiles reimplemented engine functions with the toolchains Skyrim
# shipped with (VS2015 for Havok, VS2017 for most Bethesda code) and diffs them against the game exe.

add_executable(cbmatch cbmatch.cpp)

find_package(zydis CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(cbmatch PRIVATE engine Zydis::Zydis nlohmann_json::nlohmann_json dbghelp)

set_target_properties(cbmatch PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(cbmatch PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
