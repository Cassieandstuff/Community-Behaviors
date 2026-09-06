# ---------------------------------------------------------------------------
# havok-pipeline — the compile-pipeline feature framework.
#
# The self-registered graph-feature SDK (IGraphFeature / FeatureRegistry / the
# BR_REGISTER_FEATURE macro), so the offline compiler and the editor present the
# same feature hook as the CB runtime. A feature's Apply() depends ONLY on
# havok-model's BehaviorData + the framework's FeatureContext interfaces (the
# pure-Apply rule) — never on SKSE/CommonLib/CB.
#
# Consumers include the SDK headers bare ("FeatureRegistry.h" / "IGraphFeature.h");
# the include dir is PUBLIC so that resolves for anyone linking havok-pipeline.
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
