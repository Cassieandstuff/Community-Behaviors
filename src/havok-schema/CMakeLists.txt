# ── havok-schema ────────────────────────────────────────────────────────────
# The loader/model for the Havok/ class schema — the source of truth for what a
# Havok class IS in the SSE/AE packfile. PASSIVE: parses descriptors into
# ClassSchema and computes serialized size by walking fields. Knows nothing of
# read/write/compile — those are consumers (havok-io, merge, codegen).
#
# CB source module. Public header on the path as <havok-schema/HavokSchema.h>.

sct_collect_sources(HAVOK_SCHEMA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp")

add_library(havok-schema STATIC ${HAVOK_SCHEMA_SOURCES})

target_include_directories(havok-schema
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>            # <havok-schema/HavokSchema.h>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/external>  # RymlInclude.h (build-only shim)
           $<INSTALL_INTERFACE:include>)

find_package(ryml CONFIG REQUIRED)                 # descriptor YAML parsing
target_link_libraries(havok-schema PUBLIC ryml::ryml)

set_target_properties(havok-schema PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(havok-schema PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
