# ---------------------------------------------------------------------------
# Regen bundles — regenerate every DERIVED .hky bundle from its dedicated source at BUILD time.
#
# SOURCE: $CB_REGEN_SOURCE is a root of per-bundle source folders, each named for the bundle it
# regenerates (e.g. `Skyrim.hky\`, `XPMSSE.hky\`) and laid out like a game Data folder (meshes\...).
# Every source runs the SAME regen (BehaviorConverter --regen-master: decompile whatever the source
# contains, gate every unit's served bytes vs its own binaries, pack) into <PACKED_DIR>/<Name>.hky.
# A new derived bundle = a new source folder; nothing here is bundle-specific. Unset / not a directory
# -> regen SKIPPED with a warning and whatever is already deployed stays.
#
# cb_regen_sources(<outVar>)       # the <Name>.hky source dirs under $CB_REGEN_SOURCE (may be empty)
#
# cb_regen_bundles(
#   CONVERTER_TARGET <target>      # the BehaviorConverter exe target
#   PACKED_DIR       <dir>         # where each packed <Name>.hky lands (deployed/consumed)
#   UNPACKED_DIR     <dir>         # short root where each unpacked <Name>.hky/ tree is KEPT
#   [STRICT]                       # fail the build on graphdata drift (default: report)
#   [DEPENDS <files...>]           # extra inputs that should retrigger regen
# )
# ---------------------------------------------------------------------------
function(cb_regen_sources out)
    set(_found "")
    set(_root "$ENV{CB_REGEN_SOURCE}")
    if(NOT _root STREQUAL "")
        file(TO_CMAKE_PATH "${_root}" _root)   # normalize backslashes from the Windows env var
        if(IS_DIRECTORY "${_root}")
            file(GLOB _cands CONFIGURE_DEPENDS "${_root}/*.hky")
            foreach(_c ${_cands})
                if(IS_DIRECTORY "${_c}")
                    list(APPEND _found "${_c}")
                endif()
            endforeach()
        endif()
    endif()
    set(${out} "${_found}" PARENT_SCOPE)
endfunction()

function(cb_regen_bundles)
    cmake_parse_arguments(RB "STRICT" "CONVERTER_TARGET;PACKED_DIR;UNPACKED_DIR" "DEPENDS" ${ARGN})

    if(NOT RB_CONVERTER_TARGET OR NOT RB_PACKED_DIR OR NOT RB_UNPACKED_DIR)
        message(FATAL_ERROR "cb_regen_bundles: CONVERTER_TARGET, PACKED_DIR and UNPACKED_DIR are required")
    endif()

    cb_regen_sources(_sources)
    if(NOT _sources)
        message(WARNING "cb_regen_bundles: CB_REGEN_SOURCE is unset, not a directory, or holds no <Name>.hky "
                        "source folders ('$ENV{CB_REGEN_SOURCE}') — skipping bundle regeneration; the "
                        "already-deployed bundles (if any) are used.")
        return()
    endif()

    set(_strict_arg "")
    if(RB_STRICT)
        set(_strict_arg "--strict")
    endif()

    # One custom command per source. DEPENDS on the converter exe so a converter/codec change re-derives +
    # re-validates every derived bundle (the automatic gate). Each regen stages under its own
    # <UNPACKED_DIR>/<Name>.hky, so the sources never clobber each other.
    set(_outs "")
    foreach(_src ${_sources})
        get_filename_component(_name "${_src}" NAME)   # "<Name>.hky" — original case, it names the bundle
        if(IS_DIRECTORY "${_src}/meshes")
            set(_meshes "${_src}/meshes")
        else()
            set(_meshes "${_src}")
        endif()
        set(_out "${RB_PACKED_DIR}/${_name}")
        add_custom_command(
            OUTPUT  "${_out}"
            COMMAND "$<TARGET_FILE:${RB_CONVERTER_TARGET}>" --regen-master
                    "${_meshes}" "${_out}"
                    --keep-unpacked "${RB_UNPACKED_DIR}" ${_strict_arg}
            DEPENDS ${RB_CONVERTER_TARGET} ${RB_DEPENDS}
            COMMENT "Regen: ${_name} from ${_src}"
            VERBATIM
            USES_TERMINAL)   # the Skyrim corpus is a multi-minute regen — stream progress live
        list(APPEND _outs "${_out}")
        message(STATUS "cb_regen_bundles: ${_name} regenerates from ${_src} (packed -> ${_out})")
    endforeach()

    add_custom_target(generate-base-master ALL DEPENDS ${_outs})
endfunction()
