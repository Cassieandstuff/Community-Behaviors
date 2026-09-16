# ============================================================================
# CBDebugAutoAttach — DEV-ONLY SKSE plugin (gated by CB_BUILD_AUTOATTACH in the root).
#
# Spawns x64dbg attached to the game at plugin-load so early breakpoints (e.g. the
# LoadClips call site) can be caught without racing the launch. See AutoAttach.cpp.
#
# NEVER shipped: EXCLUDE_FROM_ALL (never builds with the default ALL target) and NOT
# referenced by cb_deploy — it cannot reach end users. Build it explicitly:
#   cmake --preset release -DCB_BUILD_AUTOATTACH=ON
#   cmake --build build/release --config RelWithDebInfo --target CBDebugAutoAttach
# then drop the resulting CBDebugAutoAttach.dll into a dev-only MO2 mod's SKSE/Plugins/.
#
# add_commonlibsse_plugin bakes the SKSE version-data TU and links CommonLibSSE, whose
# PUBLIC interface includes make <SKSE/SKSE.h> resolve — no CB PCH or extra includes.
# ============================================================================
add_commonlibsse_plugin(CBDebugAutoAttach
    SOURCES AutoAttach.cpp
    NAME    CBDebugAutoAttach
    AUTHOR  "Community Behaviors"
    VERSION 0.1.0
    EXCLUDE_FROM_ALL)

target_compile_features(CBDebugAutoAttach PRIVATE cxx_std_23)
