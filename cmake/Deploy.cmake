# cb_deploy(TARGET MOD_NAME [SCHEMA] [SCHEMA_EXCLUDE <name>...] [HKY_STAGE <dir>])
#
# POST_BUILD deploy of Community Behaviors into the MO2 mods folder as a "<MOD_NAME>" mod, so a build
# immediately refreshes the in-game plugin AND its runtime data. The MO2 mod folder is the Data root, so:
#
#   <mod>/SKSE/Plugins/<dll> + <pdb>         — the plugin + its symbols (always)
#   <mod>/community_behaviors/schema/...     — the class schema + its generated stamp (SCHEMA; schema/ minus
#                                              its SCHEMA_EXCLUDE top-level entries, via cb_stage_schema;
#                                              runtime default "Data/community_behaviors/schema", loaded
#                                              recursively)
#   <mod>/community_behaviors/plugins/*.hky  — the packed bundles (HKY_STAGE)
#
# Schema is CLEAR-then-copy (see cmake/Schema.cmake). Bundles are MERGE-copy: the
# regenerated Skyrim.hky master already lives in community_behaviors/plugins/ and must survive.
function(cb_deploy TARGET MOD_NAME)
    cmake_parse_arguments(D "SCHEMA" "HKY_STAGE" "SCHEMA_EXCLUDE" ${ARGN})

    if(NOT DEFINED ENV{SKYRIM_MODS_FOLDER})
        message(STATUS "cb_deploy: SKYRIM_MODS_FOLDER unset — skipping deploy of ${TARGET}")
        return()
    endif()
    set(_mod "$ENV{SKYRIM_MODS_FOLDER}/${MOD_NAME}")

    # 1. The plugin DLL + its PDB. The PDB must travel with the DLL it was built with: a stale one beside a
    #    new DLL makes crash loggers resolve every frame to the wrong function.
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mod}/SKSE/Plugins"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${TARGET}>" "${_mod}/SKSE/Plugins/"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${TARGET}>" "${_mod}/SKSE/Plugins/"
        COMMENT "cb_deploy: ${TARGET}.dll + .pdb -> ${MOD_NAME}/SKSE/Plugins"
        VERBATIM)

    # 2. The class schema tree + its generated stamp (clear-then-copy → <mod>/community_behaviors/schema, beside
    #    the bundles). Also clears the old separate "Community Behaviors/" folder (schema/ in 0.5.x, Havok/
    #    before it), which the runtime no longer reads.
    if(D_SCHEMA)
        cb_stage_schema(TARGET ${TARGET}
            DEST    "${_mod}/community_behaviors/schema"
            EXCLUDE ${D_SCHEMA_EXCLUDE}
            CLEAN   "${_mod}/Community Behaviors"
            COMMENT "cb_deploy: schema -> ${MOD_NAME}/community_behaviors/schema (clean)")
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
    #    rotates current->previous so a two-point diff is always available.
    if(DEFINED ENV{SCT_BASE_MASTER_DIR} AND TARGET hky-tool)
        add_dependencies(${TARGET} hky-tool)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                    "-DCLI=$<TARGET_FILE:hky-tool>"
                    "-DMASTER=${_mod}/community_behaviors/plugins/Skyrim.hky"
                    "-DOUT=$ENV{SCT_BASE_MASTER_DIR}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/UnpackMaster.cmake"
            COMMENT "cb_deploy: unpack master -> $ENV{SCT_BASE_MASTER_DIR}/current (diff root)"
            VERBATIM)
    endif()
endfunction()
