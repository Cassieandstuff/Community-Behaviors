# ── tools/cbmatch ────────────────────────────────────────────────────────
# cbmatch — the byte-exact matching gate: compiles reimplemented engine functions with the toolchains Skyrim
# shipped with (VS2015 for Havok, VS2017 for most Bethesda code) and diffs them against the game exe.

add_executable(cbmatch cbmatch.cpp)

find_package(zydis CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
# unicorn (--diff's emulator; GPL-2.0, fine for a dev tool that never ships) has no CMake config, only the static archive.
find_path(UNICORN_INCLUDE_DIR unicorn/unicorn.h REQUIRED)
find_library(UNICORN_LIBRARY unicorn REQUIRED)

target_include_directories(cbmatch PRIVATE "${UNICORN_INCLUDE_DIR}")
target_link_libraries(cbmatch PRIVATE engine Zydis::Zydis nlohmann_json::nlohmann_json dbghelp "${UNICORN_LIBRARY}")

set_target_properties(cbmatch PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(cbmatch PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
