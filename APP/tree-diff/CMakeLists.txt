# ---------------------------------------------------------------------------
# tree-diff — the one record-keyed semantic diff tool.
#
# cb-tree-diff (STATIC): the domain-agnostic diff engine (TreeDiff) + the per-domain adapters and
#   the RunTreeDiff orchestrator (TreeDiffAdapters). Extracted from the quarantined havok-core-cli
#   into this APP-layer library so both the standalone exe and the Behavior Converter "Diff" tab
#   share one implementation. It decompiles from binary via the havok-* stack, so both sides of a
#   diff pass through the same emitter (the same-emitter invariant). See plans/zazzy-soaring-brook.md.
#
# tree-diff (exe): a thin main() that parses <A> <B> -o <deltaDir> [--domain X] [--skeleton s] and
#   calls RunTreeDiff. Exit 0 = identical, 1 = differences, 2 = usage/error.
# ---------------------------------------------------------------------------

add_library(cb-tree-diff STATIC
    TreeDiff.cpp
    TreeDiffAdapters.cpp)

# The whole surface (TreeDiff.h + TreeDiffAdapters.h) lives beside the sources; consumers include
# them bare ("TreeDiff.h" / "TreeDiffAdapters.h").
target_include_directories(cb-tree-diff PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# havok-core = ReadHavokFile / DecompileToDir / skeleton import + yaml; havok-model = the animdata
# singlefile decompose (havok::animdata) + the .hky model; havok-schema = ClassSchema AND the
# ryml::ryml + RymlInclude.h include dir (both PUBLIC), which TreeDiff.cpp's <RymlInclude.h> needs.
# havok-io + havok-skeleton complete the link set (mirrors havok-core-cli's).
target_link_libraries(cb-tree-diff PUBLIC havok-core havok-schema havok-io havok-model havok-skeleton)

set_target_properties(cb-tree-diff PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF)

if(MSVC)
    target_compile_options(cb-tree-diff PRIVATE /W4 /wd4100 /MP /EHsc)
endif()

add_executable(tree-diff main.cpp)
target_link_libraries(tree-diff PRIVATE cb-tree-diff)

set_target_properties(tree-diff PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
    OUTPUT_NAME           "tree-diff")

if(MSVC)
    target_compile_options(tree-diff PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
