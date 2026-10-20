# ---------------------------------------------------------------------------
# Feature bundles — assemble every AUTHORED .hky under features/ at build time.
#
# SOURCE: <repo>/features/<Name>.hky/ is a feature's folder: PARTITIONS (*.hky at its top, packed YAML trees laid
# out like a game Data folder), loose files shipped at their path (manifest.json), and inject/ (JSON configs that
# write values into fields of the bundle's files, as tools/hky/injectables.json allows per file kind). Unlike the
# derived bundles (GenerateBaseMaster.cmake) there is nothing to decompile: `hky-tool assemble` streams the
# partitions, applies the configs and packs <PACKED_DIR>/<Name>.hky, where cb_deploy ships it. A folder with no
# partitions and no inject/ assembles to a plain pack of its tree.
# Partitions (and any loose .hkx) are Git LFS objects (.gitattributes): a clone without `git lfs pull` holds pointer
# stubs, which the guard (CheckLfsPointers.cmake) refuses before assembling, instead of shipping pointer text.
# A partition is repacked only deliberately (hky-tool pack): a packed .hky is not bit-reproducible, so the build
# never rewrites one.
#
# cb_feature_bundles(
#   PACKER_TARGET <target>      # hky-tool
#   PACKED_DIR    <dir>         # where each assembled <Name>.hky lands (the deploy stage)
#   OUT_TARGET    <var>         # receives the custom target's name (empty when there are no feature bundles)
# )
# ---------------------------------------------------------------------------
function(cb_feature_bundles)
    cmake_parse_arguments(FB "" "PACKER_TARGET;PACKED_DIR;OUT_TARGET" "" ${ARGN})
    if(NOT FB_PACKER_TARGET OR NOT FB_PACKED_DIR OR NOT FB_OUT_TARGET)
        message(FATAL_ERROR "cb_feature_bundles: PACKER_TARGET, PACKED_DIR and OUT_TARGET are required")
    endif()
    set(${FB_OUT_TARGET} "" PARENT_SCOPE)
    set(_table "${CMAKE_SOURCE_DIR}/tools/hky/injectables.json")

    file(GLOB _cands CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/features/*.hky")
    set(_outs "")
    foreach(_src ${_cands})
        if(NOT IS_DIRECTORY "${_src}")
            continue()
        endif()
        get_filename_component(_name "${_src}" NAME)   # "<Name>.hky" — original case, it names the bundle
        file(GLOB_RECURSE _inputs CONFIGURE_DEPENDS "${_src}/*")
        set(_out "${FB_PACKED_DIR}/${_name}")
        add_custom_command(
            OUTPUT  "${_out}"
            COMMAND "${CMAKE_COMMAND}" "-DBUNDLE_DIR=${_src}" -P "${CMAKE_SOURCE_DIR}/cmake/CheckLfsPointers.cmake"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${FB_PACKED_DIR}"
            COMMAND "$<TARGET_FILE:${FB_PACKER_TARGET}>" assemble "${_src}" "${_table}" -o "${_out}"
            DEPENDS ${FB_PACKER_TARGET} ${_inputs} "${_table}" "${CMAKE_SOURCE_DIR}/cmake/CheckLfsPointers.cmake"
            COMMENT "Feature bundle: ${_name}"
            VERBATIM)
        list(APPEND _outs "${_out}")
        message(STATUS "cb_feature_bundles: ${_name} assembles from ${_src} (-> ${_out})")
    endforeach()

    if(_outs)
        add_custom_target(feature-bundles DEPENDS ${_outs})
        set(${FB_OUT_TARGET} feature-bundles PARENT_SCOPE)
    endif()
endfunction()
