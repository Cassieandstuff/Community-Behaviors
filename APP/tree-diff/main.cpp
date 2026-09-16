// tree-diff — the standalone record-keyed semantic diff tool.
//
//   tree-diff <A> <B> -o <deltaDir> [--domain auto|behavior|setdata|animdata|skeleton|character]
//                                   [--skeleton <skel.hkx|bones.txt>]
//
// Matches records by their stable `Class:name` editorID (never by node id / position), diffs to the
// field, and writes a delta-only folder of just the differing records plus a summary.yaml. Both
// sides are decompiled from binary here, so a formatting-only difference is impossible. The engine +
// adapters live in cb-tree-diff (TreeDiff / TreeDiffAdapters). See plans/zazzy-soaring-brook.md.
//
// Exit codes: 0 = no semantic difference, 1 = differences, 2 = usage / error.

#include "TreeDiffAdapters.h"

#include <cstdio>
#include <string>

namespace {

int usage() {
    std::printf(
        "usage: tree-diff <A> <B> -o <deltaDir> "
        "[--domain auto|behavior|setdata|animdata|skeleton|character] [--skeleton <skel.hkx>]\n"
        "record-keyed semantic diff (delta-only folder); exit 0 = identical, 1 = differences.\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();

    havok::diff::TreeDiffOptions opts;
    opts.a = argv[1];
    opts.b = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "-o" || a == "--out") && i + 1 < argc) opts.deltaDir = argv[++i];
        else if (a == "--domain" && i + 1 < argc)        opts.domain   = argv[++i];
        else if (a == "--skeleton" && i + 1 < argc)      opts.skeleton = argv[++i];
        else return usage();
    }

    const auto outcome = havok::diff::RunTreeDiff(
        opts, [](const std::string& line) { std::printf("%s\n", line.c_str()); });

    if (!outcome.ok) {
        std::printf("ERROR: %s\n", outcome.error.c_str());
        return 2;
    }
    std::printf("summary -> %s\n", outcome.summaryPath.c_str());
    return outcome.anyDifference ? 1 : 0;
}
