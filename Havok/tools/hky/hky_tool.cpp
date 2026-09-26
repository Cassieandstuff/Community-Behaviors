// hky-tool — the standalone .hky pack/unpack driver over codec/serialization (org-pass firesale
// phase 4a). Pulled out of havok-core-cli so the base-master diff-strategy tooling has a home that
// links ONLY Havok/core (codec/serialization/HkyArchive) — NO typed havok-core. Proves the
// serialization codec stands alone.
//
//   hky-tool pack   <bundleDir> -o <out.hky>     YAML tree  -> single-file .hky (round-trip-checked)
//   hky-tool unpack <in.hky>    -o <outDir>       .hky       -> YAML tree on disk (original case kept)
//
// Case note (Havok is CASE-SENSITIVE): unpack writes each entry at its ORIGINAL-CASE path, never the
// normalized lookup key — a miscased path binds to nothing/wrong at load. Unpack to a SHORT dir (MAX_PATH).

#include <codec/serialization/HkyArchive.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

// hky pack: compress an uncompressed bundle YAML tree into a single-file .hky, then re-read it
// (LoadFromFile) to confirm the archive is valid — the pack<->load round-trip sanity.
int doHkyPack(const std::string& dir, const std::string& out) {
    if (out.empty()) { std::printf("usage: hky-tool pack <bundleDir> -o <out.hky>\n"); return 2; }
    std::string err;
    if (!havok::model::HkyArchive::PackDirectory(dir, out, err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    auto arc = havok::model::HkyArchive::LoadFromFile(out, err);   // round-trip sanity
    if (!arc) { std::printf("FAIL: packed but unreadable: %s\n", err.c_str()); return 1; }
    std::printf("OK: hky pack '%s' -> %s (%zu unit(s)).\n", dir.c_str(), out.c_str(), arc->units().size());
    return 0;
}

// hky unpack: decompress a single-file .hky into its uncompressed YAML tree on disk, ORIGINAL path
// case preserved. For inspecting/diffing a packed master (Skyrim.hky) or any bundle.
int doHkyUnpack(const std::string& hkyPath, const std::string& outDir) {
    if (outDir.empty()) { std::printf("usage: hky-tool unpack <in.hky> -o <outDir>\n"); return 2; }
    std::string err;
    auto arc = havok::model::HkyArchive::LoadFromFile(hkyPath, err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }

    // Same ordered range, two views: normalized keys drive file() lookup; original-case paths give the
    // on-disk layout (index-aligned — both iterate the archive's file map in order).
    const auto keys  = arc->filesUnder("");
    const auto paths = arc->filesUnderOrig("");
    namespace fs = std::filesystem;
    std::size_t written = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto content = arc->file(keys[i]);
        if (!content) continue;   // keys come from the same map — should always resolve
        const fs::path dst = fs::path(outDir) / fs::path(paths[i]);
        std::error_code ec;
        fs::create_directories(dst.parent_path(), ec);
        std::ofstream os(dst, std::ios::binary);
        if (!os) { std::printf("FAIL: cannot write %s\n", dst.string().c_str()); return 1; }
        os.write(content->data(), static_cast<std::streamsize>(content->size()));
        ++written;
    }
    std::printf("OK: hky unpack '%s' -> %s (%zu file(s), %zu unit(s)).\n",
                hkyPath.c_str(), outDir.c_str(), written, arc->units().size());
    return 0;
}

void usage() {
    std::printf("usage:\n"
                "  hky-tool pack   <bundleDir> -o <out.hky>\n"
                "  hky-tool unpack <in.hky>    -o <outDir>\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    const std::string verb = argv[1];

    // Parse a positional <arg> and an -o <out> from the remaining args.
    std::string arg, out;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) { out = argv[++i]; }
        else if (arg.empty())         { arg = a; }
    }

    if (verb == "pack")   return doHkyPack(arg, out);
    if (verb == "unpack") return doHkyUnpack(arg, out);
    usage();
    return 2;
}
