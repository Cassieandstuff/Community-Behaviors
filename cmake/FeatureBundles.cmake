# ---------------------------------------------------------------------------
# Feature bundles — pack every AUTHORED .hky under features/ at build time.
#
# SOURCE: <repo>/features/<Name>.hky/ is a bundle tree already in CB's YAML form (laid out like a game Data
# folder, manifest.json at its root); unlike the derived bundles (GenerateBaseMaster.cmake) there is nothing to
# decompile, so each is packed as-is (hky-tool pack) into <PACKED_DIR>/<Name>.hky, where cb_deploy ships it.
# The clips are Git LFS objects (.gitattributes): a clone without `git lfs pull` holds pointer stubs, which the
# guard (CheckLfsPointers.cmake) refuses before packing, instead of shipping 252 pointer files as animations.
#
# cb_feature_bundles(
#   PACKER_TARGET <target>      # hky-tool
#   PACKED_DIR    <dir>         # where each packed <Name>.hky lands (the deploy stage)
#   OUT_TARGET    <var>         # receives the custom target's name (empty when there are no feature bundles)
# )
# ---------------------------------------------------------------------------
function(cb_feature_bundles)
    cmake_parse_arguments(FB "" "PACKER_TARGET;PACKED_DIR;OUT_TARGET" "" ${ARGN})
    if(NOT FB_PACKER_TARGET OR NOT FB_PACKED_DIR OR NOT FB_OUT_TARGET)
        message(FATAL_ERROR "cb_feature_bundles: PACKER_TARGET, PACKED_DIR and OUT_TARGET are required")
    endif()
    set(${FB_OUT_TARGET} "" PARENT_SCOPE)

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
            COMMAND "$<TARGET_FILE:${FB_PACKER_TARGET}>" pack "${_src}" -o "${_out}"
            DEPENDS ${FB_PACKER_TARGET} ${_inputs} "${CMAKE_SOURCE_DIR}/cmake/CheckLfsPointers.cmake"
            COMMENT "Feature bundle: ${_name}"
            VERBATIM)
        list(APPEND _outs "${_out}")
        message(STATUS "cb_feature_bundles: ${_name} packs from ${_src} (-> ${_out})")
    endforeach()

    if(_outs)
        add_custom_target(feature-bundles DEPENDS ${_outs})
        set(${FB_OUT_TARGET} feature-bundles PARENT_SCOPE)
    endif()
endfunction()
