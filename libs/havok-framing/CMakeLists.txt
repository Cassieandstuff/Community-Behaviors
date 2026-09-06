# ---------------------------------------------------------------------------
if(NOT DEFINED SCT_ROOT_DIR)
    get_filename_component(SCT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
# havok-framing — the packfile framing leaf (havok-core v2 rewrite).
#
# The salvaged packfile primitives, extracted OUT of havok-core so the new lib
# set (havok-io / havok-schema / havok-model) is standalone and havok-core drops
# to an oracle-only "corpse" depended on by nothing in the new set. Holds:
#   - IHavokObject           (the base object interface)
#   - HkTypes / BinaryReaderEx / BinaryWriterEx   (byte primitives)
#   - PackFileTypes / PackFileDeserializer / PackFileSerializer   (framing)
#   - HavokRegistry          (className->factory INFRASTRUCTURE only; havok-core
#                             registers the typed-class CONTENT at static-init)
#
# Zero dependencies (std only) — a true leaf. DUAL-TRIPLET: added from the root
# CMakeLists before havok-core, guarded by NOT TARGET so it's added once.
# ---------------------------------------------------------------------------

sct_collect_sources(HAVOK_FRAMING_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
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
