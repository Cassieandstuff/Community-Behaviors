# ── decompile ────────────────────────────────────────────────────────────────
# CB::core decompile — emit-direction orchestration (binary -> editable form). First tenant
# (org-pass firesale step 1): AnimationDecompiler — a compiled .hkx animation -> animation.yaml,
# schema-native (deserialize via havok-io's generic SchemaObject path, decode the spline through
# codec). Include root Havok/core/cpp/ -> headers resolve as <decompile/*.h>.
sct_collect_sources(CB_DECOMPILE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(decompile STATIC ${CB_DECOMPILE_SOURCES})

target_include_directories(decompile
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Havok/core/cpp>   # <decompile/*.h>
            $<INSTALL_INTERFACE:include>)

# codec = the spline decompressor; havok-framing = BinaryReaderEx/PackFileDeserializer; havok-io =
# SchemaObject/MakeSchemaFactory; havok-schema = SharedRegistry; havok-model = AnimDataYaml
# (MotionFromAmrAnnotations/EmitMotionSidecar — the AMR->motion sidecar, transitional until it splits
# to codec/motion). interface = the AnimationDef/AnimationData model + the sample-label codec.
target_link_libraries(decompile PUBLIC interface havok-io havok-schema havok-framing havok-model codec)

set_target_properties(decompile PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(decompile PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
