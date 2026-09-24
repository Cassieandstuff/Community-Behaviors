# ── interface/reflection ─────────────────────────────────────────────────────
# CB::core reflection — the descriptor registry: CB's analogue of Havok's own Base/Reflection
# (hkClass/hkClassMember). The source of truth for what a Havok class IS in the SSE/AE packfile:
# parses the Havok/core/Schema/*.yaml class descriptors into ClassSchema and computes serialized
# size by walking fields. PASSIVE — knows nothing of read/write/compile; those are consumers.
#
# A DISTINCT target under interface/ (not merged into the interface model-structs lib) on purpose:
# the descriptor LOADER uses ryml (HavokSchema.cpp parses YAML), and interface is deliberately
# ryml-free (its model structs are pure data). Keeping reflection separate quarantines ryml here —
# reflection is the codebase's ryml PROVIDER (PUBLIC ryml::ryml + the bare <RymlInclude.h> conduit),
# the role the retired src/havok-schema target used to hold.
#
# Include ROOT is Havok/core/cpp/, so the public header resolves as <interface/reflection/HavokSchema.h>.
sct_collect_sources(CB_REFLECTION_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(reflection STATIC ${CB_REFLECTION_SOURCES})

target_include_directories(reflection
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Havok/core/cpp>            # <interface/reflection/HavokSchema.h>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external>          # bare <RymlInclude.h> (the C++20 ryml shim)
            $<INSTALL_INTERFACE:include>)

find_package(ryml CONFIG REQUIRED)                 # descriptor YAML parsing
target_link_libraries(reflection PUBLIC ryml::ryml)

set_target_properties(reflection PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(reflection PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
