# cb_deploy(TARGET MOD_NAME [SCHEMA_DIR <dir>] [HKY_STAGE <dir>])
#
# POST_BUILD deploy of Community Behaviors into the MO2 mods folder as a "<MOD_NAME>" mod, so a build
# immediately refreshes the in-game plugin AND its runtime data. The MO2 mod folder is the Data root, so:
#
#   <mod>/SKSE/Plugins/<dll>                 — the plugin        (always)
#   <mod>/Community Behaviors/Havok/...      — the class schema  (SCHEMA_DIR; runtime default
#                                              "Data/Community Behaviors/Havok", loaded recursively)
#   <mod>/community_behaviors/plugins/*.hky  — the packed bundles (HKY_STAGE)
#
# Schema is CLEAR-then-copy: its own Havok/ subtree is replaced whole, so a stale flat tree can't leave
# duplicate class definitions the recursive loader would double-register. Bundles are MERGE-copy: the
# regenerated Skyrim.hky master already lives in community_behaviors/plugins/ and must survive.
function(cb_deploy TARGET MOD_NAME)
    cmake_parse_arguments(D "" "SCHEMA_DIR;HKY_STAGE" "" ${ARGN})

    if(NOT DEFINED ENV{SKYRIM_MODS_FOLDER})
        message(STATUS "cb_deploy: SKYRIM_MODS_FOLDER unset — skipping deploy of ${TARGET}")
        return()
    endif()
    set(_mod "$ENV{SKYRIM_MODS_FOLDER}/${MOD_NAME}")

    # 1. The plugin DLL.
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mod}/SKSE/Plugins"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${TARGET}>" "${_mod}/SKSE/Plugins/"
        COMMENT "cb_deploy: ${TARGET}.dll -> ${MOD_NAME}/SKSE/Plugins"
        VERBATIM)

    # 2. The class schema tree (clear-then-copy → <mod>/Community Behaviors/Havok).
    if(D_SCHEMA_DIR AND IS_DIRECTORY "${D_SCHEMA_DIR}")
        set(_schema_dst "${_mod}/Community Behaviors/Havok")
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_schema_dst}"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${D_SCHEMA_DIR}" "${_schema_dst}"
            COMMENT "cb_deploy: schema -> ${MOD_NAME}/Community Behaviors/Havok (clean)"
            VERBATIM)
    endif()

    # 3. The packed .hky bundles (merge-copy → <mod>/community_behaviors/plugins, preserving Skyrim.hky).
    #    HKY_STAGE is a build tree already shaped as community_behaviors/plugins/*.hky.
    if(D_HKY_STAGE)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${D_HKY_STAGE}" "${_mod}"
            COMMENT "cb_deploy: packed .hky bundles -> ${MOD_NAME}/community_behaviors/plugins"
            VERBATIM)
    endif()

    # 4. Unpack the deployed master into a short D-drive root for the cross-build diff strategy.
    #    Env-gated on SCT_BASE_MASTER_DIR (a SHORT path — these trees blow past MAX_PATH otherwise).
    #    Packed .hky is not bit-reproducible, so only the unpacked tree diffs meaningfully; the helper
    #    rotates current->previous so a two-point diff is always available. Needs havok-core-cli, which
    #    pack-hky-bundles (a dependency of ${TARGET}) already builds first.
    if(DEFINED ENV{SCT_BASE_MASTER_DIR} AND TARGET havok-core-cli)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                    "-DCLI=$<TARGET_FILE:havok-core-cli>"
                    "-DMASTER=${_mod}/community_behaviors/plugins/Skyrim.hky"
                    "-DOUT=$ENV{SCT_BASE_MASTER_DIR}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/UnpackMaster.cmake"
            COMMENT "cb_deploy: unpack master -> $ENV{SCT_BASE_MASTER_DIR}/current (diff root)"
            VERBATIM)
    endif()
endfunction()
