# ── cb-api ────────────────────────────────────────────────────────────────────
# Community Behaviors' single public API facade: one header (<CB-API.h>, at the repo
# root include/) re-exporting the resolve + anim + schema surfaces, and one link
# target (cb::CB-API). An INTERFACE library — no compiled code of its own; it curates
# the facade and PUBLIC-links the engine modules that implement it (havok-model →
# havok-io/havok-framing transitively; havok-schema; havok-anim). havok-core-FREE.
#
# The Skyrim.hky master regen is deliberately NOT here (first-party-only) — see CB-API.h.

add_library(cb-api INTERFACE)

# The public header lives at the REPO ROOT include/ (not under src/) — the one place a
# consumer's -I points at, and where the portfile ships from.
target_include_directories(cb-api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# One link name in both modes: FetchContent (this alias) and find_package (exported cb:: namespace).
add_library(cb::CB-API ALIAS cb-api)

# The engine modules that back the facade. All PUBLIC on their own targets, so their include
# paths + link propagate to any cb-api consumer. None pulls in havok-core.
target_link_libraries(cb-api INTERFACE havok-model havok-schema havok-anim)

# Dogfood / drift gate: the facade is header-only (INTERFACE), so this tiny TU is what actually
# compiles <CB-API.h> and odr-uses every re-exported name. CB being a first-party consumer means a
# drift in the public contract fails the build HERE, not in a downstream tool.
add_library(cb-api-check STATIC CompileCheck.cpp)
target_link_libraries(cb-api-check PRIVATE cb-api)
set_target_properties(cb-api-check PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(cb-api-check PRIVATE /W4 /wd4100 /EHsc)
endif()
