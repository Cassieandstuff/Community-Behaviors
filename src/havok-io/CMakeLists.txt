# ── havok-io ─────────────────────────────────────────────────────────────────
# The generic, schema-driven packfile reader/writer (data-driven Havok stack).
# ONE interpreter that walks a havok-schema ClassSchema over the salvaged packfile framing
# (sections/fixups/BinaryReaderEx), replacing the hand-written per-class ClassRead/ClassWrite. Reads a
# .hkx into a generic SchemaObject graph and writes it back byte-identically.
#
# CB source module. Public header on the path as <havok-io/HavokIo.h>.

sct_collect_sources(HAVOK_IO_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp")

add_library(havok-io STATIC ${HAVOK_IO_SOURCES})

target_include_directories(havok-io
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>   # <havok-io/HavokIo.h>
           $<INSTALL_INTERFACE:include>)

# havok-framing provides the packfile framing + BinaryReader/Writer;
# havok-schema provides the ClassSchema descriptors.
target_link_libraries(havok-io PUBLIC havok-framing havok-schema)

set_target_properties(havok-io PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(havok-io PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
