# ---------------------------------------------------------------------------
# havok-framing — the packfile framing leaf of the data-driven Havok stack.
#
# The salvaged packfile primitives, so the module set (havok-io / havok-schema /
# havok-model) is standalone and the legacy havok-core corpse is depended on by
# nothing in it. Holds:
#   - IHavokObject           (the base object interface)
#   - HkTypes / BinaryReaderEx / BinaryWriterEx   (byte primitives)
#   - PackFileTypes / PackFileDeserializer / PackFileSerializer   (framing)
#   - HavokRegistry          (className->factory INFRASTRUCTURE only; havok-core
#                             registers the typed-class CONTENT at static-init)
#
# Zero dependencies (std only) — a true leaf.
# ---------------------------------------------------------------------------

sct_collect_sources(HAVOK_FRAMING_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(havok-framing STATIC ${HAVOK_FRAMING_SOURCES})

set_target_properties(havok-framing PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

target_include_directories(havok-framing PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(MSVC)
    target_compile_options(havok-framing PRIVATE /W4 /wd4100 /MP)
endif()
