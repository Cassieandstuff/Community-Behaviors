# ── niffer ────────────────────────────────────────────────────────────────────
# First-party, clean-room C++23 NIF (NetImmerse/Gamebryo) library targeting
# Skyrim Special Edition ONLY (version 20.2.0.7, stream version 100). Replaces
# nifly. No FO4 / LE / other-game compatibility paths — SSE parity is the spec.
# Dependency-free LEAF library: linked BY sct-assets (and eventually the NIF
# editor), so it must NOT depend on anything else in the tree.
#
# STANDALONE, OWN-REPO-READY LIBRARY: the whole library lives at repo root under
# NIFFER/ — include/niffer/Niffer.h (its OWN public include dir) + cpp/ + private
# hpp/ + tests/ + tools/ + this CMakeLists. It depends on nothing else in the
# tree and points PUBLIC at its own include/, so `NIFFER/` can be lifted into its
# own git repo wholesale with no rewiring.
#
# SINGLE PUBLIC HEADER: consumers include <niffer/Niffer.h> and nothing else
# (PUBLIC = ${CMAKE_CURRENT_SOURCE_DIR}/include). ALL component headers live in
# hpp/ (PRIVATE below), included internally as quoted "Foo.h".

sct_collect_sources(NIFFER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp")

add_library(niffer STATIC ${NIFFER_SOURCES})

target_include_directories(niffer
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/hpp)

set_target_properties(niffer PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(niffer PRIVATE /W4 /wd4100 /MP)
endif()

# ── Tests (opt-in, Tier 1: self-verifying synthetic round-trips) ──────────────
option(NIFFER_BUILD_TESTS "Build niffer tests" OFF)
if(NIFFER_BUILD_TESTS)
    sct_collect_sources(NIFFER_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    add_executable(niffer-test ${NIFFER_TEST_SOURCES})
    target_link_libraries(niffer-test PRIVATE niffer)
    # Whitebox: tests may include the lib's INTERNAL headers directly ("Foo.h").
    target_include_directories(niffer-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/hpp)
    set_target_properties(niffer-test PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
endif()

# ── Tools (opt-in, Tier 2: vanilla-corpus gates — roundtrip/histogram/dump) ───
option(NIFFER_BUILD_TOOLS "Build niffer corpus tools (niffer-tool)" OFF)
if(NIFFER_BUILD_TOOLS)
    sct_collect_sources(NIFFER_TOOL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp")
    add_executable(niffer-tool ${NIFFER_TOOL_SOURCES})
    target_link_libraries(niffer-tool PRIVATE niffer)
    # Whitebox like the tests: the tools dump internal block/framing state.
    target_include_directories(niffer-tool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/hpp)
    set_target_properties(niffer-tool PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
endif()
