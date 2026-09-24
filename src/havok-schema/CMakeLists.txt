# ── havok-schema (shim) ──────────────────────────────────────────────────────
# The descriptor registry moved to interface/reflection/ (the `reflection` target) in org-pass
# firesale phase 2 — it's CB's Base/Reflection analogue and belongs under interface/, kept a DISTINCT
# target so its ryml dependency stays quarantined off the ryml-free interface model-structs lib.
#
# What's left here is a forwarding SHIM header at <havok-schema/HavokSchema.h> so the ~16 source
# consumers + the 11 CMake link references keep resolving without churn. INTERFACE target: no sources,
# PUBLIC-links reflection (real symbols + the bare <RymlInclude.h> conduit) and keeps this dir on the
# include path for the shim. Delete when the last <havok-schema/…> include migrates.

add_library(havok-schema INTERFACE)

target_include_directories(havok-schema
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>   # <havok-schema/HavokSchema.h> (shim: the havok-schema/ segment is a subdir here)
              $<INSTALL_INTERFACE:include>)

target_link_libraries(havok-schema INTERFACE reflection)
