# ---------------------------------------------------------------------------
# sct_generate_base_master — regenerate the Community Behaviors base master
# (Skyrim.hky) from source at BUILD time, instead of shipping a checked-in
# 39k-file YAML tree.
#
# WHY: the master is DERIVED (vanilla behavior corpus -> decompiled + validated
# -> packed .hky), so it should be generated, not stored. Regenerating it on
# every relevant build means a change to the converter / havok-core / havok-model
# is exercised against the whole vanilla corpus automatically, and its per-graph
# graphdata is byte-gated vs vanilla (--regen-master's validation). It also drops
# the giant checked-in tree from git and hands you a short, observable unpacked
# layout with no MAX_PATH grind and no manual unpack.
#
# SOURCE: the unpacked vanilla game data (meshes\actors\...\behaviors\*.hkx +
# characters + projects) — pointed at by the env var SKYRIM_DATASOURCE (the
# portable convention; see the root README). When it is unset / not a directory,
# generation is SKIPPED with a warning: whatever Skyrim.hky is already deployed
# stays, so a checkout with no datasource still configures + builds.
#
# TOOL: BehaviorConverter (the editor-preset app `sct-behavior-converter`) via
# its `--regen-master` verb (build-base + validate). This is an EDITOR-preset
# step; the plugins build (Community Behaviors) consumes the deployed Skyrim.hky.
#
# sct_generate_base_master(
#   CONVERTER_TARGET <target>      # the BehaviorConverter exe target
#   TEMPLATES        <dir>         # tagfile-template dir (<g>.xml oracle numbering)
#   PACKED_OUT       <file>        # the packed Skyrim.hky (deployed/consumed)
#   UNPACKED_DIR     <dir>         # short path where the unpacked tree is KEPT
#   [STRICT]                       # fail the build on graphdata drift (default: report)
#   [DEPENDS <files...>]           # extra inputs that should retrigger regen (templates, schema)
# )
# ---------------------------------------------------------------------------
function(sct_generate_base_master)
    cmake_parse_arguments(GBM "STRICT" "CONVERTER_TARGET;TEMPLATES;PACKED_OUT;UNPACKED_DIR" "DEPENDS" ${ARGN})

    if(NOT GBM_CONVERTER_TARGET OR NOT GBM_PACKED_OUT OR NOT GBM_UNPACKED_DIR)
        message(FATAL_ERROR "sct_generate_base_master: CONVERTER_TARGET, PACKED_OUT and UNPACKED_DIR are required")
    endif()

    # Source of truth = the unpacked vanilla data. No datasource -> skip (keep whatever's deployed).
    set(_src "$ENV{SKYRIM_DATASOURCE}")
    if(_src STREQUAL "")
        message(WARNING "sct_generate_base_master: SKYRIM_DATASOURCE is unset — skipping base-master "
                        "regeneration. Set it to your unpacked vanilla game data (see README) to enable "
                        "fresh, validated Skyrim.hky generation. The already-deployed Skyrim.hky (if any) is used.")
        return()
    endif()
    file(TO_CMAKE_PATH "${_src}" _src)   # normalize backslashes from the Windows env var
    if(NOT IS_DIRECTORY "${_src}")
        message(WARNING "sct_generate_base_master: SKYRIM_DATASOURCE='${_src}' is not a directory — skipping "
                        "base-master regeneration; the already-deployed Skyrim.hky (if any) is used.")
        return()
    endif()

    # SKYRIM_DATASOURCE is the vanilla data ROOT (contains meshes\, like the game's Data folder — the
    # natural convention). The base build walks the meshes\ behavior tree, so descend into it when
    # present; tolerate someone pointing directly at a meshes\ dir too.
    if(IS_DIRECTORY "${_src}/meshes")
        set(_meshes "${_src}/meshes")
    else()
        set(_meshes "${_src}")
    endif()

    set(_strict_arg "")
    if(GBM_STRICT)
        set(_strict_arg "--strict")
    endif()

    # OUTPUT = the packed .hky (deployed + consumed by the plugins build). DEPENDS on the converter
    # exe (so a converter/havok-core/havok-model change re-derives + re-validates the base — the
    # automatic gate) plus any extra inputs the caller lists (templates). The unpacked tree is kept
    # at UNPACKED_DIR for observation in the same pass (no separate unpack of ~39k files).
    add_custom_command(
        OUTPUT  "${GBM_PACKED_OUT}"
        COMMAND "$<TARGET_FILE:${GBM_CONVERTER_TARGET}>" --regen-master
                "${_meshes}" "${GBM_PACKED_OUT}" "${GBM_TEMPLATES}"
                --keep-unpacked "${GBM_UNPACKED_DIR}" ${_strict_arg}
        DEPENDS ${GBM_CONVERTER_TARGET} ${GBM_DEPENDS}
        COMMENT "Base master: regenerating + validating Skyrim.hky from SKYRIM_DATASOURCE (${_meshes})"
        VERBATIM
        USES_TERMINAL)   # a multi-minute corpus regen — stream progress live

    add_custom_target(generate-base-master ALL DEPENDS "${GBM_PACKED_OUT}")
    message(STATUS "sct_generate_base_master: Skyrim.hky will regenerate from ${_src} "
                   "(packed -> ${GBM_PACKED_OUT}; unpacked -> ${GBM_UNPACKED_DIR})")
endfunction()
