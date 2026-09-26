# ── havok-io (shim) ──────────────────────────────────────────────────────────
# The generic schema-driven packfile reader/writer moved to codec/serialization/ (HavokIo.{h,cpp})
# in org-pass firesale phase 1 — HavokIo IS the object layer over the packfile spine, so it belongs
# beside it under one serialization roof (mirrors Havok's Common/Serialize).
#
# What's left here is a forwarding SHIM header at <havok-io/HavokIo.h> so the many quarantined
# Retirement Home/havok-core consumers keep resolving without churn. INTERFACE target: no sources,
# PUBLIC-links serialization (the real symbols) and keeps this dir on the include path for the shim.
# Delete when havok-core is cut and the last <havok-io/HavokIo.h> include is migrated.

add_library(havok-io INTERFACE)

target_include_directories(havok-io
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>   # <havok-io/HavokIo.h> (shim)
              $<INSTALL_INTERFACE:include>)

target_link_libraries(havok-io INTERFACE serialization)
