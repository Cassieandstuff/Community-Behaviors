# ---------------------------------------------------------------------------
# SctSources.cmake — the global "underscore = excluded from the build" rule.
#
#   sct_collect_sources(<out_var> <root_dir> <glob_expr>...)
#       GLOB_RECURSE (CONFIGURE_DEPENDS) the given globs, then DROP any file whose
#       path — RELATIVE to <root_dir> — has a segment that begins with '_'. So a
#       whole excluded subtree …
#           src/_reference/…   src/_archive/…   cpp/_wip/…
#       … or a single excluded file …
#           cpp/_scratch.cpp   hpp/_draft.h
#       … never reaches the compiler. This is the ONE mechanism behind the rule;
#       every source/header glob in the tree funnels through it.
#
#   Convention (project-wide): a leading underscore on ANY file or folder means
#   "kept in the tree, tracked in git, visible in the IDE — but NOT compiled."
#   Use it for reference material (src/_reference/), retired code (src/_archive/),
#   work-in-progress, or a single header/TU you want to park. Top-level _-dirs
#   under src/ are additionally just never add_subdirectory'd by the orchestrators.
#   (Independent of '.'-prefixed paths, which are a git/tooling concern, not a
#   build one — see .gitignore.) DeployMod.cmake applies the same '_' skip to
#   shipped Data/ content.
#
#   Filtering on the RELATIVE path (not the absolute one GLOB returns) keeps it
#   robust to a '_' appearing somewhere in the repo's parent directories.
# ---------------------------------------------------------------------------

function(sct_collect_sources out_var root_dir)
    file(GLOB_RECURSE _all CONFIGURE_DEPENDS ${ARGN})
    set(_kept "")
    foreach(_f IN LISTS _all)
        file(RELATIVE_PATH _rel "${root_dir}" "${_f}")
        if(NOT _rel MATCHES "(^|/)_")
            list(APPEND _kept "${_f}")
        endif()
    endforeach()
    set(${out_var} "${_kept}" PARENT_SCOPE)
endfunction()
