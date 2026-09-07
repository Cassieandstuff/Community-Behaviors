#include "app/App.h"
#include "Converter.h"

#include <havok-schema/HavokSchema.h>   // schema::SetSharedSchemaDir — arm the anim pipeline's registry
#include <sct-utilities/SctUtilities.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>  // GetModuleFileNameW — resolve the bundled templates next to the exe
#endif

// The tagfile templates ship next to the exe (checked in + deployed/zipped by CMake). When
// no templatesDir is given, default to <exe dir>/templates so the packaged converter and
// --build-base are self-contained (the templates are what make conversion possible at all).
static std::string ExeTemplatesDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        return (std::filesystem::path(buf).parent_path() / "templates").string();
#endif
    return "templates";  // fallback: cwd-relative
}

// The Havok/ schema tree ships next to the exe too (staged/deployed by CMake beside templates/). The
// schema-native animation pipeline (havok-anim) resolves its registry from here via SharedRegistry —
// set it from the EXE dir, not the passed templatesDir, since --regen-master is handed the repo
// templates path whose sibling Havok/ does not exist.
static std::string ExeHavokDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        return (std::filesystem::path(buf).parent_path() / "Havok").string();
#endif
    return "Havok";  // fallback: cwd-relative
}

// SCT Behavior Converter — a standalone, MO2-launchable GUI that converts a Nemesis/
// Pandora behavior load order into per-mod Community Behaviors .hky bundles (each mod -> its
// own bundle, merged at runtime by the Community Behaviors SKSE plugin). Wraps the same
// havok-core converter the havok-core-cli `vanbase`/`patchdelta` verbs use.
//
// Headless: `BehaviorConverter --cli <dataDir> <templatesDir> <outDir> [<baseDir>]` runs the
// conversion with no window (for automation / CI), streaming the log to stdout. <baseDir>
// (optional) holds the pristine vanilla animation{set,}datasinglefile.txt copied into base/.
int main(int argc, char** argv) {
    if (argc >= 5 && std::string(argv[1]) == "--cli") {
        bconv::Options opt;
        opt.dataDir      = argv[2];
        opt.templatesDir = argv[3];
        opt.outputDir    = argv[4];
        if (opt.templatesDir.empty()) opt.templatesDir = ExeTemplatesDir();
        // baseDir is the 5th positional ONLY when it isn't the start of a flag; --mo2 <root>
        // (optional) points at the MO2 instance so bundles are attributed to owning mods.
        for (int i = 5; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--mo2" && i + 1 < argc)      opt.mo2Instance = argv[++i];
            else if (opt.baseDir.empty() && !a.empty() && a.rfind("--", 0) != 0) opt.baseDir = a;
        }
        std::atomic<bool> cancel{ false };
        const auto res = bconv::ConvertLoadOrder(
            opt, [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }, cancel);
        if (res.ok) std::printf("OK: %d mods, %d behavior deltas, %d set-data, %d anim-data, %d char-roster, %d skipped "
                                "(deltas over the shipped Skyrim.hky master)\n",
                                res.mods, res.deltas, res.setMods, res.animMods, res.charDeltas, res.skipped);
        else        std::printf("FAILED: %s\n", res.error.c_str());
        return res.ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc >= 4 && std::string(argv[1]) == "--zip") {  // headless: --zip <srcDir> <zipPath>
        std::string err;
        const bool ok = sct::util::ZipDir(argv[2], argv[3], err);
        std::printf("%s\n", ok ? ("zipped -> " + std::string(argv[3])).c_str() : ("zip FAILED: " + err).c_str());
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // .hky pack/unpack — a .hky IS a zip of its unit tree. `pack` is the ship-time
    // compress step (CMake/converter); `unpack` is the author's decompress-to-edit.
    if (argc >= 4 && std::string(argv[1]) == "--pack") {    // --pack <dir> <out.hky>
        std::string err;
        const bool ok = sct::util::ZipDir(argv[2], argv[3], err);
        std::printf("%s\n", ok ? ("packed -> " + std::string(argv[3])).c_str() : ("pack FAILED: " + err).c_str());
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 4 && (std::string(argv[1]) == "--unpack" || std::string(argv[1]) == "--unzip")) {  // <in.hky> <destDir>
        std::string err;
        const bool ok = sct::util::UnzipDir(argv[2], argv[3], err);
        std::printf("%s\n", ok ? ("unpacked -> " + std::string(argv[3])).c_str() : ("unpack FAILED: " + err).c_str());
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // Build the shippable Skyrim.hky master from the vanilla meshes tree.
    // Optional 4th arg = a tagfile-template dir (<behavior>.xml); behaviors with a template
    // are numbered by the #NNNN oracle so they merge against the per-mod deltas.
    if (argc >= 4 && std::string(argv[1]) == "--build-base") {  // <vanillaMeshesDir> <out.hky> [<templatesDir>]
        std::atomic<bool> cancel{ false };
        std::string templatesDir = (argc >= 5) ? argv[4] : "";
        if (templatesDir.empty()) templatesDir = ExeTemplatesDir();  // bundled templates next to the exe
        havok::schema::SetSharedSchemaDir(ExeHavokDir());            // arm the anim pipeline's registry
        const auto res = bconv::BuildBaseBundle(argv[2], argv[3],
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }, cancel,
            templatesDir);
        if (!res.ok) { std::printf("build-base FAILED: %s\n", res.error.c_str()); return EXIT_FAILURE; }
        std::printf("OK: Skyrim.hky master -> %s (%d behaviors, %d projects, %d characters, %d skeletons, %d skipped)\n",
                    argv[3], res.behaviors, res.projects, res.characters, res.skeletons, res.failed);
        return EXIT_SUCCESS;
    }

    // Regenerate + VALIDATE the shipped Skyrim.hky master in one call (see RegenerateMaster):
    // build-base to a temp, gate every templated graph's graphdata against vanilla, then promote.
    //   --regen-master <vanillaMeshesDir> <outHky> [<templatesDir>] [--strict] [--keep-unpacked <dir>]
    // --strict makes any drift fatal (master not written); default writes + reports drift.
    // --keep-unpacked <dir> keeps the unpacked Skyrim.hky/ tree at <dir> (short path) for observation.
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
        if (pos.size() < 2) {
            std::printf("usage: --regen-master <vanillaMeshesDir> <outHky> [<templatesDir>] [--strict] [--keep-unpacked <dir>]\n");
            return EXIT_FAILURE;
        }
        const std::string templatesDir = (pos.size() >= 3) ? pos[2] : ExeTemplatesDir();
        havok::schema::SetSharedSchemaDir(ExeHavokDir());            // arm the anim pipeline's registry
        const auto res = bconv::RegenerateMaster(pos[0], templatesDir, pos[1], strict,
            [](std::string s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }, cancel, keepUnpacked);
        if (!res.ok) { std::printf("regen-master FAILED: %s\n", res.error.c_str()); return EXIT_FAILURE; }
        std::printf("OK: Skyrim.hky master -> %s (%d behaviors, %d projects, %d characters, %d skeletons; "
                    "validated %d/%d graphdata match%s)\n",
                    pos[1].c_str(), res.build.behaviors, res.build.projects, res.build.characters,
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
