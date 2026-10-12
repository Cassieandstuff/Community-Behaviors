#include "app/App.h"
#include "Converter.h"

#include <interface/reflection/HavokSchema.h>   // schema::SetSharedSchemaDir — arm the anim pipeline's registry
#include <codec/serialization/HkyArchive.h>
#include "platform/Platform.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <unordered_set>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>  // GetModuleFileNameW — resolve the bundled schema next to the exe
#endif

/// @brief <exe dir>/schema — the class schema CMake stages beside the exe (cwd-relative "schema" fallback).
/// @note The shared registry is armed from the EXE dir, never from a passed templatesDir (--cli may be handed
///       any templates path), so the packaged converter stays self-contained.
static std::filesystem::path ExeSchemaDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        return std::filesystem::path(buf).parent_path() / "schema";
#endif
    return "schema";
}

/// @brief The default templatesDir when none is given: the Nemesis-ingest templates staged inside the schema.
static std::string ExeTemplatesDir() { return (ExeSchemaDir() / "templates").string(); }

// SCT Behavior Converter — a standalone, MO2-launchable GUI that converts a Nemesis/
// Pandora behavior load order into per-mod Community Behaviors .hky bundles (each mod -> its
// own bundle, merged at runtime by the Community Behaviors SKSE plugin).
//
// Headless: `BehaviorConverter --cli <dataDir> <templatesDir> <outDir>` runs the
// conversion with no window (for automation / CI), streaming the log to stdout.
int main(int argc, char** argv) {
    /// @note Arm the process-wide schema registry for EVERY mode (GUI, --cli, the gates, the base build): every
    ///       decompile / derive / gate reads it. SetSharedSchemaDir only records the dir; SharedRegistry() lazily
    ///       loads once on first use, so the per-verb re-arms below are harmless.
    CB::engine::schema::SetSharedSchemaDir(ExeSchemaDir().string());

    if (argc >= 5 && std::string(argv[1]) == "--cli") {
        CB::converter::Options opt;
        opt.dataDir      = argv[2];
        opt.templatesDir = argv[3];
        opt.outputDir    = argv[4];
        if (opt.templatesDir.empty()) opt.templatesDir = ExeTemplatesDir();
        // --mo2 <root> (optional) points at the MO2 instance so bundles are attributed to owning mods.
        for (int i = 5; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--mo2" && i + 1 < argc)      opt.mo2Instance = argv[++i];
        }
        std::atomic<bool> cancel{ false };
        const auto res = CB::converter::ConvertLoadOrder(
            opt, [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }, cancel);
        if (res.ok) std::printf("OK: %d mods, %d behavior deltas, %d set-data, %d anim-data, %d char-roster, %d skipped "
                                "(deltas over the shipped Skyrim.hky master)\n",
                                res.mods, res.deltas, res.setMods, res.animMods, res.charDeltas, res.skipped);
        else        std::printf("FAILED: %s\n", res.error.c_str());
        return res.ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 3 && std::string(argv[1]) == "--list-archives") {
        const auto archives = CB::converter::ResolveModArchives(argv[2], [](std::string s) { std::printf("%s\n", s.c_str()); });
        int counted = 0;
        for (const auto& a : archives) {
            std::printf("%s  [%s] %s\n    %s\n", a.counted ? "COUNT" : "skip ", a.modName.c_str(),
                        std::filesystem::path(a.archive).filename().string().c_str(), a.reason.c_str());
            counted += a.counted ? 1 : 0;
        }
        std::printf("OK: %d archive(s) counted, %d skipped.\n", counted, static_cast<int>(archives.size()) - counted);
        return EXIT_SUCCESS;
    }

    if (argc >= 4 && std::string(argv[1]) == "--package-animations") {
        const bool ok = CB::converter::PackageAllModAnimations(argv[2], argv[3],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); });
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 4 && std::string(argv[1]) == "--serializer-roundtrip") {   // <meshesDir> <outRoot>
        const bool ok = CB::converter::SerializerRoundTrip(argv[2], argv[3],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); });
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 4 && std::string(argv[1]) == "--adsf-project") {   // <Skyrim.hky> <vanilla animationdatasinglefile.txt>
        const bool ok = CB::converter::AdsfProjectionCheck(argv[2], argv[3],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); });
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 3 && std::string(argv[1]) == "--metatext-roundtrip") {   // <file>...
        const bool ok = CB::converter::MetaTextRoundTrip(std::vector<std::string>(argv + 2, argv + argc),
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); });
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 4 && std::string(argv[1]) == "--anim-float-offsets") {   // <meshesDir> <workRoot>
        const bool ok = CB::converter::AnimFloatOffsetsGate(argv[2], argv[3],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); });
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 4 && std::string(argv[1]) == "--motion-storage-check") {   // <newTree> <oldTree>
        const bool ok = CB::converter::MotionStorageCheck(argv[2], argv[3],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); });
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 4 && std::string(argv[1]) == "--motion-fold-gate") {   // <unpackedTree> <out.csv>
        const bool ok = CB::converter::MotionFoldGate(argv[2], argv[3],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); });
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 5 && std::string(argv[1]) == "--multilayer-roundtrip") {   // <mo2ModsDir> <templatesDir> <work> [<base.hky>]
        const bool ok = CB::converter::MultiLayerRoundTripAll(argv[2], argv[3], argv[4],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }, argc >= 6 ? argv[5] : "");
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 7 && std::string(argv[1]) == "--hky-merge") {   // <base.hky> <meshesDir> <rel.hkx> <outDir> <patchDir>...
        const std::vector<std::string> patches(argv + 6, argv + argc);
        const bool ok = CB::converter::HkyMerge(argv[2], argv[3], argv[4], argv[5], patches,
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); });
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 5 && std::string(argv[1]) == "--patch-roundtrip") {   // <vanillaMeshes> <moddedMeshes> <work> [<base.hky>]
        const bool ok = CB::converter::PatchRoundTripAll(argv[2], argv[3], argv[4],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }, argc >= 6 ? argv[5] : "");
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 4 && std::string(argv[1]) == "--hky-roundtrip") {   // <meshesDir> <workRoot> [<base.hky>] [--all-kinds]
        std::string base; bool all = false;
        for (int i = 4; i < argc; ++i) { if (std::string(argv[i]) == "--all-kinds") all = true; else base = argv[i]; }
        const bool ok = CB::converter::HkyRoundTrip(argv[2], argv[3],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }, base, all);
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 4 && std::string(argv[1]) == "--zip") {  // headless: --zip <srcDir> <zipPath>
        std::string err;
        const bool ok = CB::engine::codec::HkyArchive::PackDirectory(argv[2], argv[3], err);
        std::printf("%s\n", ok ? ("zipped -> " + std::string(argv[3])).c_str() : ("zip FAILED: " + err).c_str());
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // .hky pack/unpack — a .hky IS a zip of its unit tree. `pack` is the ship-time
    // compress step (CMake/converter); `unpack` is the author's decompress-to-edit.
    if (argc >= 4 && std::string(argv[1]) == "--pack") {    // --pack <dir> <out.hky>
        std::string err;
        const bool ok = CB::engine::codec::HkyArchive::PackDirectory(argv[2], argv[3], err);
        std::printf("%s\n", ok ? ("packed -> " + std::string(argv[3])).c_str() : ("pack FAILED: " + err).c_str());
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 4 && (std::string(argv[1]) == "--unpack" || std::string(argv[1]) == "--unzip")) {  // <in.hky> <destDir>
        std::string err;
        const bool ok = CB::converter::platform::UnzipDir(argv[2], argv[3], err);
        std::printf("%s\n", ok ? ("unpacked -> " + std::string(argv[3])).c_str() : ("unpack FAILED: " + err).c_str());
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // Build the shippable Skyrim.hky master from the vanilla meshes tree (unvalidated; prefer --regen-master).
    if (argc >= 4 && std::string(argv[1]) == "--build-base") {  // <vanillaMeshesDir> <out.hky>
        if (argc > 4) { std::printf("usage: --build-base <vanillaMeshesDir> <out.hky>\n"); return EXIT_FAILURE; }
        std::atomic<bool> cancel{ false };
        CB::engine::schema::SetSharedSchemaDir(ExeSchemaDir().string());            // arm the anim pipeline's registry
        const auto res = CB::converter::BuildBaseBundle(argv[2], argv[3],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }, cancel);
        if (!res.ok) { std::printf("build-base FAILED: %s\n", res.error.c_str()); return EXIT_FAILURE; }
        std::printf("OK: Skyrim.hky master -> %s (%d behaviors, %d projects, %d characters, %d skeletons, %d skipped)\n",
                    argv[3], res.behaviors, res.projects, res.characters, res.skeletons, res.failed);
        return EXIT_SUCCESS;
    }

    // Regenerate + VALIDATE one derived bundle (<outHky>'s filename names it) in one call (see RegenerateMaster):
    // build-base to a temp, gate every unit's served bytes against its source, then promote.
    //   --regen-master <vanillaMeshesDir> <outHky> [--strict] [--keep-unpacked <dir>]
    // --strict makes any drift fatal (master not written); default writes + reports drift.
    // --keep-unpacked <dir> keeps the unpacked <Name>.hky/ tree under <dir> (short path) for observation.
    if (argc >= 4 && std::string(argv[1]) == "--regen-master") {
        std::atomic<bool> cancel{ false };
        bool strict = false;
        std::string keepUnpacked;
        std::vector<std::string> pos;
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--strict") strict = true;
            else if (a == "--keep-unpacked" && i + 1 < argc) keepUnpacked = argv[++i];
            else pos.push_back(a);
        }
        if (pos.size() != 2) {
            std::printf("usage: --regen-master <vanillaMeshesDir> <outHky> [--strict] [--keep-unpacked <dir>]\n");
            return EXIT_FAILURE;
        }
        CB::engine::schema::SetSharedSchemaDir(ExeSchemaDir().string());            // arm the anim pipeline's registry
        const auto res = CB::converter::RegenerateMaster(pos[0], pos[1], strict,
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }, cancel, keepUnpacked);
        if (!res.ok) { std::printf("regen-master FAILED: %s\n", res.error.c_str()); return EXIT_FAILURE; }
        std::printf("OK: %s -> %s (%d behaviors, %d projects, %d characters, %d skeletons; "
                    "served-bytes gate %d/%d%s)\n",
                    std::filesystem::path(pos[1]).filename().string().c_str(), pos[1].c_str(), res.build.behaviors, res.build.projects, res.build.characters,
                    res.build.skeletons, res.faithful, res.checked, res.drift.empty() ? "" : ", drift present (see log)");
        return EXIT_SUCCESS;
    }

    try {
        App app;
        if (!app.Init("SCT Behavior Converter", 1100, 720))
            return EXIT_FAILURE;
        app.Run();
    }
    catch (const std::exception& ex) {
        std::fprintf(stderr, "fatal: %s\n", ex.what());
        return EXIT_FAILURE;
    }
    catch (...) {
        std::fprintf(stderr, "fatal: unknown exception\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
