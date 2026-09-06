# ── havok-schema ────────────────────────────────────────────────────────────
if(NOT DEFINED SCT_ROOT_DIR)
    get_filename_component(SCT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
# The loader/model for the Havok/ class schema — the source of truth for what a
# Havok class IS in the SSE/AE packfile. PASSIVE: parses descriptors into
# ClassSchema and computes serialized size by walking fields. Knows nothing of
# read/write/compile — those are consumers (havok-io, merge, codegen). This is
# the foundation the havok-core v2 rewrite is built on (rewrite plan, Stage 1).
#
# DUAL-TRIPLET: added from the ROOT CMakeLists before the editor/plugins split
# (guarded by NOT TARGET), so it builds under both x64-windows and
# x64-windows-static. Single public header: <havok-schema/HavokSchema.h>.

sct_collect_sources(HAVOK_SCHEMA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp")

add_library(havok-schema STATIC ${HAVOK_SCHEMA_SOURCES})

target_include_directories(havok-schema
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/internal    # <havok-schema/HavokSchema.h>
           ${CMAKE_CURRENT_SOURCE_DIR}/include/external)   # RymlInclude.h (the ryml shim)

find_package(ryml CONFIG REQUIRED)                 # descriptor YAML parsing
target_link_libraries(havok-schema PUBLIC ryml::ryml)

set_target_properties(havok-schema PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(havok-schema PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
