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

# Enum tables baked from the schema's enum data (schema/metadata/enums/*.yaml, the single source) into
# GeneratedEnums.cpp, backing the enums::EnumTable accessors in <interface/HavokEnums.h>.
set(_gen_enums "${CMAKE_CURRENT_BINARY_DIR}/GeneratedEnums.cpp")
file(GLOB _enum_yamls CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/schema/metadata/enums/*.yaml")
add_custom_command(
    OUTPUT  "${_gen_enums}"
    COMMAND "${CMAKE_COMMAND}"
            "-DENUM_DIR=${PROJECT_SOURCE_DIR}/schema/metadata/enums"
            "-DOUT_CPP=${_gen_enums}"
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedEnums.cmake"
    DEPENDS ${_enum_yamls} "${PROJECT_SOURCE_DIR}/cmake/EmbedEnums.cmake"
    COMMENT "Generating enum tables from schema/metadata/enums/*.yaml"
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
target_link_libraries(engine PUBLIC ryml::ryml PRIVATE lz4::lz4)

set_target_properties(engine PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(engine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /wd4100 /MP /EHsc>)
endif()

# The shared vocabulary prelude (CB::engine::common + CB::engine::codec ambient in every engine TU).
target_precompile_headers(engine PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/common/CorePrelude.h>")
