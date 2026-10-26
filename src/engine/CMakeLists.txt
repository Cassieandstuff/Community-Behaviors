# ── engine ───────────────────────────────────────────────────────────────────────
# CB's shared reflection engine, one static library: the schema registry (interface/reflection), the
# model structs + unit vocabulary (interface, common), the codecs (codec, codec/serialization),
# and the one-way transforms (compile, decompile). The plugin, the converter, and the tools all link it.
#
# This directory is the include root, so headers resolve by purpose folder:
#   <common/…> <interface/…> <interface/reflection/…> <codec/…> <codec/serialization/…> <compile/…> <decompile/…>
#
# CommonLib-free: the engine builds with no SKSE/CommonLib on its include path so the converter and the
# tools can share it. A configure-time scan refuses any engine file that includes RE/ or SKSE/ headers.
sct_collect_sources(CB_ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

file(GLOB_RECURSE _cb_engine_files CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(_f ${_cb_engine_files})
    file(STRINGS "${_f}" _hits REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](RE|SKSE|REL)/")
    if(_hits)
        file(RELATIVE_PATH _rel "${PROJECT_SOURCE_DIR}" "${_f}")
        message(FATAL_ERROR "engine must stay CommonLib-free: ${_rel} includes ${_hits}")
    endif()
endforeach()

# Enum tables baked from the schema's enum data (every `kind: enum` descriptor under schema/, the single source)
# into GeneratedEnums.cpp, backing the enums::EnumTable accessors in <interface/HavokEnums.h>. The script finds them
# by kind; DEPENDS is the same filter taken at configure time, so a schema edit regenerates only when an enum changed.
set(_gen_enums "${CMAKE_CURRENT_BINARY_DIR}/GeneratedEnums.cpp")
file(GLOB_RECURSE _schema_yamls CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/schema/*.yaml")
set(_enum_yamls "")
foreach(_y IN LISTS _schema_yamls)
    file(STRINGS "${_y}" _kind REGEX "^kind:[ \t]*enum[ \t\r]*(#.*)?$" LIMIT_COUNT 1)
    if(_kind)
        list(APPEND _enum_yamls "${_y}")
    endif()
endforeach()
add_custom_command(
    OUTPUT  "${_gen_enums}"
    COMMAND "${CMAKE_COMMAND}"
            "-DSCHEMA_DIR=${PROJECT_SOURCE_DIR}/schema"
            "-DOUT_CPP=${_gen_enums}"
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedEnums.cmake"
    DEPENDS ${_enum_yamls} "${PROJECT_SOURCE_DIR}/cmake/EmbedEnums.cmake"
    COMMENT "Generating enum tables from the schema's kind: enum descriptors"
    VERBATIM)

# The schema version this build speaks (CB_SCHEMA_VERSION, root CMakeLists.txt) baked in for the SharedRegistry
# gate — the same value cmake/Schema.cmake stamps into every shipped tree's SCHEMA.yaml.
set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file("${PROJECT_SOURCE_DIR}/cmake/SchemaVersion.gen.h.in"
               "${_gen_dir}/interface/reflection/SchemaVersion.gen.h" @ONLY)

# Vendored miniz (amalgamated C, MIT) backs HkyArchive; compiled here with warnings off.
set(_miniz_c "${CMAKE_CURRENT_SOURCE_DIR}/common/miniz.c")
set_source_files_properties("${_miniz_c}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
if(MSVC)
    set_source_files_properties("${_miniz_c}" PROPERTIES COMPILE_FLAGS "/w")
endif()

add_library(engine STATIC ${CB_ENGINE_SOURCES} "${_gen_enums}" "${_miniz_c}")
target_include_directories(engine
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface/reflection/external>   # bare <RymlInclude.h>
            $<INSTALL_INTERFACE:include>
    PRIVATE $<BUILD_INTERFACE:${_gen_dir}>)   # <interface/reflection/SchemaVersion.gen.h>, engine-internal

find_package(ryml CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED GLOBAL)

# VFS: Skyrim's Data folder as the game sees it (loose over BSA, MO2-aware) and the one BSA reader. The load-order
# document reads bundles through it. Local builds point FETCHCONTENT_SOURCE_DIR_VFS at a checkout.
include(FetchContent)
FetchContent_Declare(VFS
    GIT_REPOSITORY https://github.com/Cassieandstuff/libs-VFS.git
    GIT_TAG        193fb3d)
FetchContent_MakeAvailable(VFS)

find_package(nlohmann_json CONFIG REQUIRED)   # bundle assembly's generate / inject configs
target_link_libraries(engine PUBLIC ryml::ryml vfs::vfs PRIVATE lz4::lz4 nlohmann_json::nlohmann_json)

set_target_properties(engine PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(engine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /wd4100 /MP /EHsc>)
endif()

# The shared vocabulary prelude (CB::engine::common + CB::engine::codec ambient in every engine TU).
target_precompile_headers(engine PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/common/CorePrelude.h>")
