# ── havok-model (shim) ────────────────────────────────────────────────────────
# FULLY DRAINED by the org-pass firesale (phase 3): the name-keyed .hky model split to its permanent
# homes — model structs + Cross + BashMerge + enums -> interface; the YAML loaders + adsf codec ->
# codec/format; MotionFromAmr -> codec/motion; the builder half -> compile (GraphBuild + SchemaBuilder +
# ResolveBindings + AnimDataDeriver + CompileTrace); the emit/identity/tagfile/convert half -> decompile
# (BehaviorDecompile + HavokModel.cpp).
#
# What's left here is forwarding SHIM headers (<havok-model/HavokModel.h>, havok/model/*, havok/anim/*,
# havok/cross/*) so consumers keep resolving without churn. INTERFACE target: no sources, PUBLIC-links
# interface to propagate the Havok/core/cpp include root (so the shims' <compile/…>/<decompile/…>/
# <codec/…>/<interface/…> resolve). It does NOT link compile/decompile/codec — those link havok-model,
# and consumers already link them directly for the real symbols. Delete when the last <havok-model/…>
# / havok/model/… include migrates.

add_library(havok-model INTERFACE)

target_include_directories(havok-model
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>   # <havok-model/HavokModel.h> + havok/model/* shims
              $<INSTALL_INTERFACE:include>)

target_link_libraries(havok-model INTERFACE interface)
