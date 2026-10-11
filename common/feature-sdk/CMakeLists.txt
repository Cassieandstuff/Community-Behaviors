# ── feature-sdk ────────────────────────────────────────────────────────────────
# The self-registered graph-feature SDK (IGraphFeature / FeatureRegistry / the registration macro), so the
# offline compiler and the CB runtime present the same feature hook. A feature's Apply() depends ONLY on
# the engine's GraphEdit (the merged reflective graph) + the SDK's FeatureContext interfaces (the
# pure-Apply rule) — never on SKSE/CommonLib.
#
# Include root is common/: consumers write <feature-sdk/FeatureRegistry.h>.
sct_collect_sources(CB_FEATURE_SDK_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(feature-sdk STATIC ${CB_FEATURE_SDK_SOURCES})
target_include_directories(feature-sdk PUBLIC "${CMAKE_SOURCE_DIR}/common")
target_link_libraries(feature-sdk PUBLIC engine)

set_target_properties(feature-sdk PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(feature-sdk PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
