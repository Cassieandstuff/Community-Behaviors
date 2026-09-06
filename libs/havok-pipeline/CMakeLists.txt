# ---------------------------------------------------------------------------
if(NOT DEFINED SCT_ROOT_DIR)
    get_filename_component(SCT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
# havok-pipeline — the compile-pipeline feature framework (havok-core v2, Stage 5).
#
# The self-registered graph-feature SDK (IGraphFeature / FeatureRegistry / the
# BR_REGISTER_FEATURE macro), relocated OUT of Community Behaviors so the offline
# compiler and the editor can present the same feature hook as the BR runtime.
# A feature's Apply() depends ONLY on havok-model's BehaviorData + the framework's
# FeatureContext interfaces (the pure-Apply rule) — never on SKSE/CommonLib/BR.
#
# Consumers include the SDK headers bare ("FeatureRegistry.h" / "IGraphFeature.h"),
# mirroring the old features/core/hpp on-the-path convention — the include dir is
# PUBLIC so that resolves for anyone linking havok-pipeline.
#
# DUAL-TRIPLET; added from the root CMakeLists after havok-model (it links it for
# BehaviorData). Guarded by NOT TARGET so it's added once across the split.
# ---------------------------------------------------------------------------

sct_collect_sources(HAVOK_PIPELINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(havok-pipeline STATIC ${HAVOK_PIPELINE_SOURCES})

set_target_properties(havok-pipeline PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

# Bare includes ("FeatureRegistry.h") resolve from here for every consumer.
target_include_directories(havok-pipeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# The framework operates on the name-keyed document (havok-model::BehaviorData).
target_link_libraries(havok-pipeline PUBLIC havok-model)

if(MSVC)
    target_compile_options(havok-pipeline PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
