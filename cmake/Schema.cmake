# Schema.cmake — the schema version stamp + the ONE way the schema tree ships.
#
# CB_SCHEMA_VERSION (root CMakeLists.txt) is the single source of the schema contract version. This module
# generates the tree's SCHEMA.yaml stamp from it into the build dir; src/engine bakes the same value into
# SchemaVersion.gen.h, so the stamp a shipped tree carries and the version the binary expects can't drift.
#
# schema/ holds NO SCHEMA.yaml. Every place that ships the tree goes through these helpers, which lay the
# generated stamp over the copied tree:
#
#   cb_stage_schema(TARGET <t> DEST <dir> [EXCLUDE <name>...] [CLEAN <path>...] [COMMENT <text>])
#       POST_BUILD clear-then-copy of schema/ into <dir> (minus EXCLUDE'd top-level entries and C++ sources) + the stamp.
#       Clear-then-copy so a stale tree can't leave duplicate class definitions the recursive loader would
#       double-register. CLEAN = extra paths removed alongside <dir> (retired legacy layouts).
#   cb_builder_schema(MOD|APP <Name> TO <subpath> [EXCLUDE <name>...])
#       the same stamped tree, made in the build folder and handed to BUILDER as <Name>'s OWNED <subpath> (staged,
#       deployed and packaged with the product). Call from the directory that creates <Name>'s target.
#   cb_install_schema(DESTINATION <dir> [EXCLUDE <name>...])
#       install() rule for the same tree (C++ sources excluded) + stamp.
#
# Don't ship schema/ any other way: the runtime treats an ABSENT stamp as CB's own unstamped tree and lets it
# through, so a ship path that skips the stamp silently disarms the foreign-tree gate (SharedRegistry.cpp).
include_guard(GLOBAL)

if(NOT CB_SCHEMA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.]+)?$")
    message(FATAL_ERROR "CB_SCHEMA_VERSION '${CB_SCHEMA_VERSION}' is not MAJOR.MINOR.PATCH[-prerelease]")
endif()

get_filename_component(CB_SCHEMA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../schema" ABSOLUTE)
if(EXISTS "${CB_SCHEMA_SOURCE_DIR}/SCHEMA.yaml" OR EXISTS "${CB_SCHEMA_SOURCE_DIR}/SCHEMA.yml")
    message(FATAL_ERROR "schema/SCHEMA.yaml must not exist — the stamp is generated from CB_SCHEMA_VERSION "
                        "(root CMakeLists.txt). Delete it and bump CB_SCHEMA_VERSION instead.")
endif()

set(CB_SCHEMA_STAMP "${CMAKE_CURRENT_BINARY_DIR}/generated/schema-stamp/SCHEMA.yaml")
configure_file("${CMAKE_CURRENT_LIST_DIR}/SCHEMA.yaml.in" "${CB_SCHEMA_STAMP}" @ONLY)
# GLOBAL so the helpers work from any directory, including a consumer that fetched CB (the variables stay in CB's scope).
set_property(GLOBAL PROPERTY CB_SCHEMA_SOURCE_DIR "${CB_SCHEMA_SOURCE_DIR}")
set_property(GLOBAL PROPERTY CB_SCHEMA_STAMP "${CB_SCHEMA_STAMP}")

function(cb_stage_schema)
    cmake_parse_arguments(S "" "TARGET;DEST;COMMENT" "EXCLUDE;CLEAN" ${ARGN})
    get_property(_src GLOBAL PROPERTY CB_SCHEMA_SOURCE_DIR)
    get_property(_stamp GLOBAL PROPERTY CB_SCHEMA_STAMP)
    if(NOT S_TARGET OR NOT S_DEST)
        message(FATAL_ERROR "cb_stage_schema: TARGET and DEST are required")
    endif()
    if(NOT S_COMMENT)
        set(S_COMMENT "Staging schema/ -> ${S_DEST}")
    endif()

    # Entry by entry so an excluded top-level folder is never copied at all; CopySchemaData leaves any C++ source out.
    file(GLOB _entries CONFIGURE_DEPENDS LIST_DIRECTORIES true "${_src}/*")
    set(_copy "")
    foreach(_e IN LISTS _entries)
        get_filename_component(_n "${_e}" NAME)
        if(_n IN_LIST S_EXCLUDE)
            continue()
        endif()
        list(APPEND _copy COMMAND "${CMAKE_COMMAND}" "-DSRC=${_e}" "-DDST=${S_DEST}" -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CopySchemaData.cmake")
    endforeach()

    add_custom_command(TARGET ${S_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${S_DEST}" ${S_CLEAN}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${S_DEST}"
        ${_copy}
        COMMAND "${CMAKE_COMMAND}" -E copy "${_stamp}" "${S_DEST}/"
        COMMENT "${S_COMMENT}"
        VERBATIM)
endfunction()

function(cb_install_schema)
    cmake_parse_arguments(S "" "DESTINATION" "EXCLUDE" ${ARGN})
    get_property(_src GLOBAL PROPERTY CB_SCHEMA_SOURCE_DIR)
    get_property(_stamp GLOBAL PROPERTY CB_SCHEMA_STAMP)
    if(NOT S_DESTINATION)
        message(FATAL_ERROR "cb_install_schema: DESTINATION is required")
    endif()
    set(_excl "")
    foreach(_x IN LISTS S_EXCLUDE)
        list(APPEND _excl PATTERN "${_x}" EXCLUDE)
    endforeach()
    install(DIRECTORY "${_src}/" DESTINATION "${S_DESTINATION}" ${_excl}
            PATTERN "*.h" EXCLUDE PATTERN "*.hpp" EXCLUDE PATTERN "*.inl" EXCLUDE PATTERN "*.cpp" EXCLUDE)
    install(FILES "${_stamp}" DESTINATION "${S_DESTINATION}")
endfunction()

function(cb_builder_schema)
    cmake_parse_arguments(S "" "MOD;APP;TO" "EXCLUDE" ${ARGN})
    if(NOT COMMAND builder_stage_generated)
        message(FATAL_ERROR "cb_builder_schema: BUILDER is not available (FetchContent it first)")
    endif()
    if(S_MOD AND NOT S_APP)
        set(_kind MOD)
        set(_name "${S_MOD}")
    elseif(S_APP AND NOT S_MOD)
        set(_kind APP)
        set(_name "${S_APP}")
    else()
        message(FATAL_ERROR "cb_builder_schema: give exactly one of MOD or APP")
    endif()
    if(NOT S_TO)
        message(FATAL_ERROR "cb_builder_schema: TO <subpath> is required (the schema is an OWNED subfolder)")
    endif()
    string(MAKE_C_IDENTIFIER "${_name}" _id)
    set(_dir "${CMAKE_CURRENT_BINARY_DIR}/cb-schema/${_id}")
    add_custom_target(cb-schema-${_id})
    cb_stage_schema(TARGET cb-schema-${_id}
        DEST    "${_dir}"
        EXCLUDE ${S_EXCLUDE}
        COMMENT "${_name}: stamping schema/")
    builder_stage_generated(${_kind} "${_name}" DIR "${_dir}" TO "${S_TO}" OWNED DEPENDS cb-schema-${_id})
endfunction()
