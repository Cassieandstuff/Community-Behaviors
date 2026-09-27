# ── codec ──────────────────────────────────────────────────────────────────────
# CB::core codecs — reusable, defined, bidirectional translators between two
# representations (spline, and later crc / format / motion). Org-pass firesale step 1.
# First tenant: the spline animation codec (CB::core::spline::CompressAnimation / DecodeSpline),
# lifted from src/havok-anim. Include ROOT is Havok/core/cpp/, so headers resolve as
# <codec/spline/*.h> — the function-folder IS the path namespace (no havok/ prefix).
sct_collect_sources(CB_CODEC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
# serialization/ is a NESTED sub-target (its own add_library), but sct_collect_sources GLOB_RECURSEs —
# so drop its sources here or codec double-compiles them (a latent duplicate that linked only by link
# order). codec owns spline/crc/format directly; serialization is its own lib.
list(FILTER CB_CODEC_SOURCES EXCLUDE REGEX "/serialization/")

add_library(codec STATIC ${CB_CODEC_SOURCES})

target_include_directories(codec
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Havok/core/cpp>   # <codec/spline/*.h>
            $<INSTALL_INTERFACE:include>)

# The model structs (AnimationDef + AnimationData) now live in `interface` — SplineCompressor.h pulls
# <interface/AnimationDef.h>. Clean link (interface is a std-only base); the transitional havok-anim/
# havok-model include hacks are GONE. havok-schema is the codebase's ryml provider (PUBLIC ryml::ryml +
# the RymlInclude.h shim dir) — the format YAML codec (AnimSetDataYaml movesets/crcs decompose) parses
# with it. It carries no descriptor dependency; it's the ryml conduit.
target_link_libraries(codec PUBLIC interface havok-schema)

# bsa/ (the Bethesda archive reader) decompresses v105 (SSE/AE) file blocks with the LZ4 FRAME API. GLOBAL so
# the imported target resolves in every consumer's scope (codec is a static lib: its PRIVATE link rides the
# export as $<LINK_ONLY:lz4::lz4>; the cb-api package config re-finds it via find_dependency(lz4)).
find_package(lz4 CONFIG REQUIRED GLOBAL)
target_link_libraries(codec PRIVATE lz4::lz4)

set_target_properties(codec PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(codec PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
