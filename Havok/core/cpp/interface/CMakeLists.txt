# ── interface ────────────────────────────────────────────────────────────────
# CB::core interface — the schema-operating layer + the shared MODEL structs. First tenants
# (org-pass firesale): the animation model — AnimationDef (havok::anim, the authoring model) +
# AnimationData (havok::animdata, the adsf .txt model + parse/emit). Moved here to break the
# AnimationDef<->AnimationData coupling that cycle-locked the adsf codec work: interface is a
# std-only BASE lib (depends on nothing), so codec / compile / decompile / havok-model all depend on
# IT, no cycle. Also holds MotionSample (the root-motion sample-label codec, beside its MotionRecord).
# Include root Havok/core/cpp/ -> <interface/*.h>.
sct_collect_sources(CB_INTERFACE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(interface STATIC ${CB_INTERFACE_SOURCES})

target_include_directories(interface
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Havok/core/cpp>   # <interface/*.h>
            $<INSTALL_INTERFACE:include>)

set_target_properties(interface PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(interface PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
