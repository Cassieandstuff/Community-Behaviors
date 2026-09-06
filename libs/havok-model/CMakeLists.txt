# ── havok-model ──────────────────────────────────────────────────────────────
if(NOT DEFINED SCT_ROOT_DIR)
    get_filename_component(SCT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
# The name-keyed .hky model + schema-driven emit/parse (havok-core v2 rewrite, Stage 3). Sits on top of
# havok-io's generic SchemaObject graph:
#
#   .hkx ↔ SchemaObject graph            (havok-io)
#        ↕ identity/index pass           ← obj → canonical id + category (the .hky yaml filenames)
#   id-keyed model + name-resolution     (event/var/… index ↔ name via hkbBehaviorGraphStringData)
#        ↕ schema-driven emit/parse      ← pure projection to/from the .hky tree
#   .hky tree                            ── gated file-vs-Skyrim.hky, hkx→hky→hkx byte-exact
#
# Canonical ids (yaml filenames) are owned HERE (the identity/index step), NOT the Havok/ schema and NOT
# the emit formatting. The emit is a pure projection that consumes the ids the identity pass produced.
#
# DUAL-TRIPLET: added from the ROOT CMakeLists after havok-io (guarded by NOT TARGET). Single public
# header: <havok-model/HavokModel.h>.

sct_collect_sources(HAVOK_MODEL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp")

add_library(havok-model STATIC ${HAVOK_MODEL_SOURCES})

target_include_directories(havok-model
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/internal        # <havok-model/HavokModel.h>
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")      # havok/model/*, havok/anim/*, havok/sct/AnimDataFromBehavior.h
                                                       # (Stage 3r.1: the name-keyed doc + merge + animdata
                                                       #  derivation, moved here out of havok-core)

# havok-io provides the SchemaObject graph + framing; havok-schema the ClassSchema descriptors
# (and ryml, PUBLIC-propagated — the moved YAML loaders need it). havok-framing comes transitively.
target_link_libraries(havok-model PUBLIC havok-io havok-schema)

# The .hky in-memory archive reader (HkyArchive) is backed by vendored miniz (amalgamated C, MIT),
# relocated here with the loader out of havok-core.
set(_MINIZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_sources(havok-model PRIVATE "${_MINIZ_DIR}/miniz.c")
target_include_directories(havok-model PRIVATE "${_MINIZ_DIR}")
if(MSVC)
    set_source_files_properties("${_MINIZ_DIR}/miniz.c" PROPERTIES COMPILE_FLAGS "/w")
endif()

set_target_properties(havok-model PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(havok-model PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
