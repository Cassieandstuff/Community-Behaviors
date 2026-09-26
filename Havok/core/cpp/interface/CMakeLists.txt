# ── interface ────────────────────────────────────────────────────────────────
# CB::core interface — the schema-operating layer + the shared MODEL structs. First tenants
# (org-pass firesale): the animation model — AnimationDef (havok::anim, the authoring model) +
# AnimationData (havok::animdata, the adsf .txt model + parse/emit). Moved here to break the
# AnimationDef<->AnimationData coupling that cycle-locked the adsf codec work: interface is a
# std-only BASE lib (depends on nothing), so codec / compile / decompile / havok-model all depend on
# IT, no cycle. Also holds MotionSample (the root-motion sample-label codec, beside its MotionRecord).
# Include root Havok/core/cpp/ -> <interface/*.h>.
sct_collect_sources(CB_INTERFACE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
# reflection/ is a NESTED sub-target (the descriptor registry, its own add_library + ryml link), but
# sct_collect_sources GLOB_RECURSEs — drop its sources here so interface neither double-compiles them
# nor drags ryml into the ryml-free model-structs lib.
list(FILTER CB_INTERFACE_SOURCES EXCLUDE REGEX "/reflection/")

# Enum tables baked from the schema's enum data tree (Havok/core/Schema/metadata/enums/*.yaml — the
# single source). A build-time `cmake -P` codegen bakes GeneratedEnums.cpp, backing the thin
# enums::EnumTable accessors in <interface/HavokEnums.h>. Moved here with HavokEnums.h in firesale
# phase 3c (enum name<->value is model/reflection data, not packfile framing). std-only, no cycle.
set(_gen_enums "${CMAKE_CURRENT_BINARY_DIR}/GeneratedEnums.cpp")
file(GLOB _enum_yamls CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Havok/core/Schema/metadata/enums/*.yaml")
add_custom_command(
    OUTPUT  "${_gen_enums}"
    COMMAND "${CMAKE_COMMAND}"
            "-DENUM_DIR=${CMAKE_SOURCE_DIR}/Havok/core/Schema/metadata/enums"
            "-DOUT_CPP=${_gen_enums}"
            -P "${CMAKE_SOURCE_DIR}/cmake/EmbedEnums.cmake"
    DEPENDS ${_enum_yamls} "${CMAKE_SOURCE_DIR}/cmake/EmbedEnums.cmake"
    COMMENT "Generating enum tables from Havok/core/Schema/metadata/enums/*.yaml"
    VERBATIM
)

add_library(interface STATIC ${CB_INTERFACE_SOURCES} "${_gen_enums}")

target_include_directories(interface
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Havok/core/cpp>   # <interface/*.h>
            $<INSTALL_INTERFACE:include>)

# common = the foundational value types (HkTypes) SkeletonData/SkeletonMath rest on. Header-only
# INTERFACE lib with no deps of its own, so interface stays a base (nothing above it in the graph).
target_link_libraries(interface PUBLIC common)

set_target_properties(interface PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(interface PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
