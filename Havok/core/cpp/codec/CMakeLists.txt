# ── codec ──────────────────────────────────────────────────────────────────────
# CB::core codecs — reusable, defined, bidirectional translators between two
# representations (spline, and later crc / format / motion). Org-pass firesale step 1.
# First tenant: the spline animation codec (CB::core::spline::CompressAnimation / DecodeSpline),
# lifted from src/havok-anim. Include ROOT is Havok/core/cpp/, so headers resolve as
# <codec/spline/*.h> — the function-folder IS the path namespace (no havok/ prefix).
sct_collect_sources(CB_CODEC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(codec STATIC ${CB_CODEC_SOURCES})

target_include_directories(codec
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Havok/core/cpp>   # <codec/spline/*.h>
            $<INSTALL_INTERFACE:include>)

# TRANSITIONAL: SplineCompressor.h pulls <havok/anim/AnimationDef.h> (havok-anim), which pulls
# <havok/anim/AnimationData.h> (havok-model) for MotionRecord. Both are header-only model structs, so
# this is an include-path dep, NOT a link (no cycle). Collapses when the model → interface.
target_include_directories(codec PRIVATE
    ${CMAKE_SOURCE_DIR}/src/havok-anim
    ${CMAKE_SOURCE_DIR}/src/havok-model)

set_target_properties(codec PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(codec PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
