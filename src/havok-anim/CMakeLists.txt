# ── havok-anim ───────────────────────────────────────────────────────────────
# The schema-driven ANIMATION pipeline (data-driven Havok stack). Lifted out of the
# quarantined havok-core so the animation path is schema-native end to end:
#
#   animation.yaml ─ AnimationYamlLoader ─▶ AnimationDef            (authoring model)
#        │  spline codec (CompressAnimation / DecodeSpline)          (opaque blob math)
#        ▼  AssembleAnimation ─▶ io::SchemaObject graph              (via Havok/ descriptors)
#   .hkx ◀─ PackFileSerializer (havok-framing) ─┘                    (byte-identical emit)
#        ▲  DecompileAnimation ◀─ PackFileDeserializer ◀─ .hkx       (schema import leg)
#
# NO typed hka* classes and NO havok-core dependency — assembly/deserialize go through the
# schema object model (havok-io) + descriptors (havok-schema). The bone/track membrane
# (havok/cross/Cross.h, from havok-model) resolves hkaAnimationBinding track↔bone by name.
#
# CB source module. Public headers on the path as <havok/anim/*.h>.

sct_collect_sources(HAVOK_ANIM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(havok-anim STATIC ${HAVOK_ANIM_SOURCES})

target_include_directories(havok-anim
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>   # <havok/anim/*.h>
           $<INSTALL_INTERFACE:include>)

# havok-model brings AnimationData.h (MotionRecord), the AnimDataYaml loader, and the
# havok/cross/Cross.h membrane; io/schema/framing (+ryml) come transitively.
target_link_libraries(havok-anim PUBLIC havok-model)

set_target_properties(havok-anim PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(havok-anim PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
