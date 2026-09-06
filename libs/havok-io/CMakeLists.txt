# ── havok-io ─────────────────────────────────────────────────────────────────
if(NOT DEFINED SCT_ROOT_DIR)
    get_filename_component(SCT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
# The generic, schema-driven packfile reader/writer (havok-core v2 rewrite, Stage 2).
# ONE interpreter that walks a havok-schema ClassSchema over havok-core's salvaged packfile framing
# (sections/fixups/BinaryReaderEx), replacing the hand-written per-class ClassRead/ClassWrite. Reads a
# .hkx into a generic SchemaObject graph and writes it back byte-identically.
#
# DUAL-TRIPLET: added from the ROOT CMakeLists after havok-core (guarded by NOT TARGET), so it builds
# under both x64-windows and x64-windows-static. Single public header: <havok-io/HavokIo.h>.

sct_collect_sources(HAVOK_IO_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp")

add_library(havok-io STATIC ${HAVOK_IO_SOURCES})

target_include_directories(havok-io
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/internal)   # <havok-io/HavokIo.h>

# havok-core provides the packfile framing + BinaryReader/Writer (its include/ is PUBLIC);
# havok-schema provides the ClassSchema descriptors.
target_link_libraries(havok-io PUBLIC havok-framing havok-schema)

set_target_properties(havok-io PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(havok-io PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
