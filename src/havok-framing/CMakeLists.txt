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

# Enum tables baked from the schema's enum data tree (Havok/core/Schema/enums/*.yaml — the single
# source). A build-time codegen step generates GeneratedEnums.cpp (backs enums::EnumTable, which
# HavokEnums.h's per-enum accessors call). Keeps havok-framing a std-only leaf: the generator is a
# tool-free `cmake -P` script, no dependency cycle. Regenerated whenever an enum yaml changes.
set(_gen_enums "${CMAKE_CURRENT_BINARY_DIR}/GeneratedEnums.cpp")
file(GLOB _enum_yamls CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Havok/core/Schema/enums/*.yaml")
add_custom_command(
    OUTPUT  "${_gen_enums}"
    COMMAND "${CMAKE_COMMAND}"
            "-DENUM_DIR=${CMAKE_SOURCE_DIR}/Havok/core/Schema/enums"
            "-DOUT_CPP=${_gen_enums}"
            -P "${CMAKE_SOURCE_DIR}/cmake/EmbedEnums.cmake"
    DEPENDS ${_enum_yamls} "${CMAKE_SOURCE_DIR}/cmake/EmbedEnums.cmake"
    COMMENT "Generating enum tables from Havok/core/Schema/enums/*.yaml"
    VERBATIM
)

add_library(havok-framing STATIC ${HAVOK_FRAMING_SOURCES} "${_gen_enums}")

set_target_properties(havok-framing PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

target_include_directories(havok-framing PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(havok-framing PRIVATE /W4 /wd4100 /MP)
endif()
