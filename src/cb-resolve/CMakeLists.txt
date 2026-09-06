# ── cb-resolve ────────────────────────────────────────────────────────────────
# The minimal, havok-core-free public contract for load-order resolution + the .hky
# document. An INTERFACE library: no compiled code of its own — it curates a single
# public header (<cb-resolve/CbResolve.h>) and PUBLIC-links the engine modules that
# implement it (havok-model → havok-io/havok-framing transitively; havok-schema),
# so any consumer links `cb-resolve` and gets the exact resolution the CB compiler
# uses, and nothing from havok-core.
#
# This is the surface the Scene Editor (and other tools) consume cross-repo. Kept
# curated so the contract outlives havok-core.

add_library(cb-resolve INTERFACE)

target_include_directories(cb-resolve INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# A namespaced alias so consumers link `cb::cb-resolve` in BOTH modes — FetchContent
# (this alias) and find_package (the exported cb:: namespace). One link name everywhere.
add_library(cb::cb-resolve ALIAS cb-resolve)

# havok-model brings the .hky document + merge + resolved model (and havok-io/framing
# transitively); havok-schema brings the SchemaRegistry classifier. Both PUBLIC on those
# targets, so their include paths + link propagate to cb-resolve's consumers.
target_link_libraries(cb-resolve INTERFACE havok-model havok-schema)

# Compile-check: the shipped lib is header-only (INTERFACE), so this tiny TU is what actually
# compiles the umbrella + odr-uses every alias — a drift in the public contract fails the build here.
add_library(cb-resolve-check STATIC CompileCheck.cpp)
target_link_libraries(cb-resolve-check PRIVATE cb-resolve)
set_target_properties(cb-resolve-check PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(cb-resolve-check PRIVATE /W4 /wd4100 /EHsc)
endif()
