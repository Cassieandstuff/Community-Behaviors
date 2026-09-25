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
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# (The enum-table codegen moved to interface/ with HavokEnums.h in firesale phase 3c — the enum
#  accessors + their baked GeneratedEnums.cpp are model/reflection data, not packfile framing.)

add_library(havok-framing STATIC ${HAVOK_FRAMING_SOURCES})

set_target_properties(havok-framing PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

target_include_directories(havok-framing PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# common = the base value types; havok/core/HkTypes.h is now a shim over <common/HkTypes.h>.
# serialization = the packfile spine (IHavokObject/HavokRegistry/Binary*Ex/PackFile*), lifted to
# codec/serialization/ in org-pass firesale phase 1. The old havok/core/*.h + havok/classes/
# IHavokObject.h headers here are now forwarding SHIMS over <codec/serialization/…>; PUBLIC-linking
# serialization gives framing's remaining tenants (Xml/TagfileOracle/HavokEnums/PandoraCompatShim)
# and every downstream consumer the symbols + the Havok/core/cpp include path. Delete the shims when
# Retirement Home/havok-core (their last user) is cut.
target_link_libraries(havok-framing PUBLIC common serialization)

if(MSVC)
    target_compile_options(havok-framing PRIVATE /W4 /wd4100 /MP)
endif()
