# ── common ───────────────────────────────────────────────────────────────────
# CB::core common — cross-cutting foundational headers with no deps of their own (the tier every
# other function-folder can rest on). First tenant (org-pass): HkTypes — the portable Havok math
# value types (Vector4 / Quaternion / QSTransform), std-only, header-only. miniz / ryml shim / math
# land here later as they're drained. Header-only for now, so an INTERFACE library. Include root
# Havok/core/cpp/ -> headers resolve as <common/*.h>.
add_library(common INTERFACE)

target_include_directories(common
    INTERFACE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Havok/core/cpp>   # <common/*.h>
              $<INSTALL_INTERFACE:include>)
