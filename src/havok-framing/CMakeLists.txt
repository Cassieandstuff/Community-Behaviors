# ── havok-framing (shim) ──────────────────────────────────────────────────────
# FULLY DRAINED by the org-pass firesale: the packfile spine -> codec/serialization/ (phase 1),
# Xml -> common (3b), HavokEnums + its codegen -> interface (3c), TagfileOracle + PandoraCompatShim ->
# decompile (3f-3). What's left is forwarding SHIM headers at the old havok/core/*, havok/classes/,
# havok/model/HavokEnums.h, havok/xml/Xml.h, havok/sct/TagfileOracle.h, havok/compat/ paths so the
# quarantined Retirement Home/havok-core (and a couple of converter/cli sites) keep resolving.
#
# INTERFACE target: no sources. PUBLIC-links serialization (the packfile symbols + the Havok/core/cpp
# include root, which also resolves the <common/…>/<interface/…>/<decompile/…> the other shims forward
# to). Delete when havok-core is cut and the last old-path include migrates.

add_library(havok-framing INTERFACE)

target_include_directories(havok-framing
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>   # old havok/core/*, havok/xml/*, … shims
              $<INSTALL_INTERFACE:include>)

target_link_libraries(havok-framing INTERFACE serialization)
