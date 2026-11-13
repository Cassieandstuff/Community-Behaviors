// hky-tool — the standalone .hky pack/unpack driver over codec/serialization (HkyArchive): the
// base-master diff-strategy tooling, linking only the engine.
//
//   hky-tool pack        <bundleDir> -o <out.hky>        YAML tree  -> single-file .hky (round-trip-checked)
//   hky-tool unpack      <in.hky>    -o <outDir>          .hky       -> YAML tree on disk (original case kept)
//   hky-tool bsa-list    <in.bsa>    [prefix]             list an SSE/AE (v105) archive's files
//   hky-tool bsa-extract <in.bsa>    -o <outDir> [prefix] extract them (archive-stored lowercase paths)
//   hky-tool assemble    <featureDir> <injectables.json> [schemaDir] -o <out.hky>
//                                                         partitions + loose files + generators + inject configs
//   hky-tool merge-check <Skyrim.hky> <bundle.hky> <schemaDir> -o <outDir>
//                                                         merge + serialize every unit as the game's compile does
//   hky-tool embedded-roundtrip <in.hky> <schemaDir>
//                                                         embedded files parse + splice back byte-exact, curves sample alike
//   hky-tool loadorder   <schemaDir> (--mo2 <instance> [--profile <p>] | --data <Data>) [--unit <key>]... [--all] [--ls <prefix>]
//                                                         the .hky load order via VFS + CB-API's document; merge units
//   hky-tool mm-sample   <schemaDir> -o <outDir>          the motion-matching bundle generator over a sample spec
//   hky-tool serve-dump  <schemaDir> --data <Data> -o <outDir>
//                                                         every unit, the adsf and the asdsf a load order serves
//   hky-tool graph-html  <schemaDir> -o <out.html> --mo2 <instance> | --plugins <dir> | --bundle <f.hky> ...
//                                                         a load order's merged graphs as one reflective HTML page
//
// Case note (Havok is CASE-SENSITIVE): unpack writes each entry at its ORIGINAL-CASE path, never the
// normalized lookup key — a miscased path binds to nothing/wrong at load. Unpack to a SHORT dir (MAX_PATH).

#include <cstring>
#include <vfs/Bsa.h>
#include <hky/codec/format/AnimationYamlLoader.h>
#include <hky/compile/MotionFeatures.h>
#include <hky/compile/BundleAssembly.h>
#include <hky/compile/MotionMatchingBundle.h>
#include <hky/codec/resource/PathKey.h>
#include <hky/codec/serialization/HkyArchive.h>
#include <hky/codec/serialization/MetaText.h>
#include <hky/codec/format/AnimDataYaml.h>
#include <hky/codec/format/AnimSetDataYaml.h>
#include <hky/codec/format/AnimationSetData.h>
#include <hky/codec/motion/RootMotion.h>
#include <hky/compile/AnimDataProjection.h>
#include <hky/compile/AnimDataFromBehavior.h>
#include <hky/decompile/BehaviorDecompile.h>
#include <hky/decompile/EmbeddedFile.h>
#include <hky/merge/HkyOverlay.h>
#include <hky/merge/LoadOrderDocument.h>
#include <vfs/Vfs.h>
#include <hky/disk/Disk.h>
#include <hky/interface/MasterTable.h>
#include <hky/interface/reflection/HavokSchema.h>
#include <hky/exec/Parallel.h>
#include <hky/text/Text.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <random>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// hky pack: compress an uncompressed bundle YAML tree into a single-file .hky, then re-read it
// (LoadFromFile) to confirm the archive is valid — the pack<->load round-trip sanity.
int doHkyPack(const std::string& dir, const std::string& out) {
    if (out.empty()) { std::printf("usage: hky-tool pack <bundleDir> -o <out.hky>\n"); return 2; }
    std::string err;
    if (!hky::codec::HkyArchive::PackDirectory(dir, out, err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    auto arc = hky::codec::HkyArchive::LoadFromFile(out, err);   // round-trip sanity
    if (!arc) { std::printf("FAIL: packed but unreadable: %s\n", err.c_str()); return 1; }
    std::printf("OK: hky pack '%s' -> %s (%zu unit(s)).\n", dir.c_str(), out.c_str(), arc->units().size());
    return 0;
}

// hky unpack: decompress a single-file .hky into its uncompressed YAML tree on disk, ORIGINAL path
// case preserved. For inspecting/diffing a packed master (Skyrim.hky) or any bundle.
int doHkyUnpack(const std::string& hkyPath, const std::string& outDir) {
    if (outDir.empty()) { std::printf("usage: hky-tool unpack <in.hky> -o <outDir>\n"); return 2; }
    std::string err;
    auto arc = hky::codec::HkyArchive::LoadFromFile(hkyPath, err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }

    // Same ordered range, two views: normalized keys drive file() lookup; original-case paths give the
    // on-disk layout (index-aligned — both iterate the archive's file map in order).
    const auto keys  = arc->filesUnder("");
    const auto paths = arc->filesUnderOrig("");
    namespace fs = std::filesystem;
    std::atomic<std::size_t> written{ 0 };
    try {
        /// @note Each file is read + inflated on the worker that writes it (HkyArchive reads are thread-safe).
        hky::exec::ParallelFor(keys.size(), [&](std::size_t i) {
            const auto content = arc->file(keys[i]);
            if (!content) return;   // keys come from the same map — should always resolve
            const fs::path dst = fs::path(outDir) / fs::path(paths[i]);
            if (std::string werr; !hky::disk::WriteFile(dst, *content, {}, &werr)) throw std::runtime_error(werr);
            ++written;
        });
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    std::printf("OK: hky unpack '%s' -> %s (%zu file(s), %zu unit(s)).\n",
                hkyPath.c_str(), outDir.c_str(), written.load(), arc->units().size());
    return 0;
}

/// @note Path identity (PathKey) on both sides, so "Meshes/Actors" matches the archive's "meshes\actors\…".
bool BsaPrefixMatch(const std::string& path, const std::string& prefix) {
    using hky::codec::resource::PathKey;
    return PathKey(path).rfind(PathKey(prefix), 0) == 0;
}

int doBsaList(const std::string& bsaPath, const std::string& prefix) {
    std::string err;
    const auto arc = vfs::bsa::Archive::Open(bsaPath, &err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    std::size_t shown = 0;
    for (const auto& e : arc->Entries()) {
        if (!prefix.empty() && !BsaPrefixMatch(e.path, prefix)) continue;
        std::printf("%s\t%u%s\n", e.path.c_str(), e.size, e.compressed ? "\tlz4" : "");
        ++shown;
    }
    std::printf("OK: v%u, %zu file(s), %zu listed.\n", arc->Version(), arc->Entries().size(), shown);
    return 0;
}

int doBsaExtract(const std::string& bsaPath, const std::string& outDir, const std::string& prefix) {
    if (outDir.empty()) { std::printf("usage: hky-tool bsa-extract <in.bsa> -o <outDir> [prefix]\n"); return 2; }
    std::string err;
    const auto arc = vfs::bsa::Archive::Open(bsaPath, &err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    namespace fs = std::filesystem;
    std::size_t written = 0, failed = 0;
    std::vector<std::uint8_t> bytes;
    for (const auto& e : arc->Entries()) {
        if (!prefix.empty() && !BsaPrefixMatch(e.path, prefix)) continue;
        if (!arc->Extract(e, bytes, &err)) { std::printf("  FAIL %s: %s\n", e.path.c_str(), err.c_str()); ++failed; continue; }
        std::string rel = e.path;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const fs::path dst = fs::path(outDir) / fs::path(rel);
        if (std::string werr; !hky::disk::WriteFile(dst, bytes, {}, &werr)) { std::printf("  FAIL %s\n", werr.c_str()); ++failed; continue; }
        ++written;
    }
    std::printf("%s: bsa extract '%s' -> %s (%zu written, %zu failed).\n", failed ? "FAIL" : "OK",
                bsaPath.c_str(), outDir.c_str(), written, failed);
    return failed ? 1 : 0;
}

/// @brief `assemble`: a feature folder in the repo -> its shipped .hky (compile::AssembleBundle). Every `*.hky` at the
///        folder's top is a partition, `generate/` and `inject/` hold the configs, and everything else (manifest.json,
///        loose trees) ships at its path.
/// @param schemaDir the class schema the generators build through; empty = none (a generator that needs it fails).
int doAssemble(const std::string& dir, const std::string& tablePath, const std::string& schemaDir, const std::string& out) {
    if (dir.empty() || tablePath.empty() || out.empty()) {
        std::printf("usage: hky-tool assemble <featureDir> <injectables.json> [schemaDir] -o <out.hky>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    using hky::codec::HkyArchive;
    using hky::codec::resource::HasExt;

    const auto table = hky::disk::ReadFile(tablePath, { .stripBom = true });
    if (!table) { std::printf("FAIL: cannot read %s\n", tablePath.c_str()); return 1; }
    const hky::schema::SchemaRegistry* reg = nullptr;
    if (!schemaDir.empty()) {
        reg = hky::schema::SharedRegistry();
        if (!reg) { std::printf("FAIL: schema: %s\n", hky::schema::SharedRegistryError().c_str()); return 1; }
    }

    hky::compile::BundleInput in;
    const fs::path root = fs::absolute(fs::path(dir));
    std::error_code ec;
    std::vector<fs::directory_entry> top;
    for (fs::directory_iterator it(hky::disk::LongPath(root), ec), end; !ec && it != end; it.increment(ec)) top.push_back(*it);
    if (ec) { std::printf("FAIL: cannot list '%s': %s\n", dir.c_str(), ec.message().c_str()); return 1; }
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });
    const auto configsUnder = [&](const fs::path& folder, std::vector<std::pair<std::string, std::string>>& found) {
        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(hky::disk::LongPath(folder), ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file() && HasExt(it->path().generic_string(), ".json"))
                files.push_back(folder / it->path().lexically_relative(hky::disk::LongPath(folder)));
        if (ec) { std::printf("FAIL: cannot walk '%s': %s\n", folder.string().c_str(), ec.message().c_str()); return false; }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            const auto text = hky::disk::ReadFile(f, { .stripBom = true });
            if (!text) { std::printf("FAIL: cannot read %s\n", f.string().c_str()); return false; }
            found.emplace_back(f.lexically_relative(folder).generic_string(), *text);
        }
        return true;
    };
    for (const auto& e : top) {
        const fs::path    child = root / e.path().filename();
        const std::string fname = e.path().filename().generic_string();
        if (e.is_directory() && fname == "inject") { if (!configsUnder(child, in.injections)) return 1; continue; }
        if (e.is_directory() && fname == "generate") { if (!configsUnder(child, in.generators)) return 1; continue; }
        if (e.is_regular_file() && HasExt(fname, ".hky")) {
            std::string err;
            auto arc = HkyArchive::LoadFromFile(child.string(), err);
            if (!arc) { std::printf("FAIL: partition %s: %s\n", fname.c_str(), err.c_str()); return 1; }
            in.partitions.emplace(fname, std::move(arc));
            continue;
        }
        const auto addLoose = [&](const fs::path& p) {
            in.loose.push_back({ p.lexically_relative(root).generic_string(), [p] { return hky::disk::ReadFile(p); } });
        };
        if (e.is_regular_file()) { addLoose(child); continue; }
        if (e.is_directory())
            for (fs::recursive_directory_iterator it(hky::disk::LongPath(child), ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file()) addLoose(child / it->path().lexically_relative(hky::disk::LongPath(child)));
        if (ec) { std::printf("FAIL: cannot walk '%s': %s\n", child.string().c_str(), ec.message().c_str()); return 1; }
    }

    hky::compile::AssembleReport r;
    const std::size_t nGenerators = in.generators.size(), nInjections = in.injections.size(), nPartitions = in.partitions.size();
    if (!hky::compile::AssembleBundle(in, *table, reg, out, r)) {
        for (const auto& e : r.errors) std::printf("  FAIL %s\n", e.c_str());
        std::printf("FAIL: assemble '%s': %zu error(s), nothing packed.\n", dir.c_str(), r.errors.size());
        return 1;
    }
    std::printf("OK: assemble '%s' -> %s: %zu file(s) from %zu partition(s) + %zu loose + %zu generated by %zu "
                "config(s); %zu clip(s) given measured curves; %zu injection(s) from %zu config(s) into %zu file(s).\n",
                dir.c_str(), out.c_str(), r.files, nPartitions, r.loose, r.generated, nGenerators, r.measured, r.injected,
                nInjections, r.touched);
    return 0;
}

/// @brief `shuffle-check`: every graph unit of a .hky (or the one named) read from the archive, then with its files scattered
///        through random folders and names, on disk and packed; all three must serialize to the same bytes.
/// @note  A unit whose bones need a rig takes its character's, else the humanoid one; a unit readable with neither is reported and
///        counted, not this check's subject.
int doShuffleCheck(const std::string& hkyPath, const std::string& schemaDir, const std::string& workDir, const std::string& onlyUnit) {
    if (hkyPath.empty() || schemaDir.empty() || workDir.empty()) {
        std::printf("usage: hky-tool shuffle-check <in.hky> <schemaDir> -o <workDir> [unit]\n");
        return 2;
    }
    namespace dc = hky::decompile;
    namespace fs = std::filesystem;
    using hky::codec::HkyArchive;
    using hky::codec::resource::PathKey;
    const auto* reg = hky::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", hky::schema::SharedRegistryError().c_str()); return 1; }
    std::string err;
    const auto  arc = HkyArchive::LoadFromFile(hkyPath, err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    std::vector<std::string> humanoid;
    {
        const std::string rig = "meshes/actors/character/character assets/skeleton.hkx";
        if (const auto src = arc->source(rig))
            if (auto names = dc::ReadSkeletonBones(*src, *reg, rig)) humanoid = std::move(*names);
    }
    const auto bytesOf = [&](const hky::common::IUnitSource& src, const std::string& unit, const std::vector<std::string>* bones,
                             std::vector<std::uint8_t>& out, std::string& why) {
        dc::ParsedHky graph;
        return dc::ParseHkyReflective(src, *reg, graph, why, bones, unit) && dc::SerializeGraph(graph, out, why);
    };
    std::vector<HkyArchive::Unit> units;
    for (const auto& u : arc->units())
        if (onlyUnit.empty() || PathKey(u.prefix) == PathKey(onlyUnit)) units.push_back(u);
    std::atomic<std::size_t> same{ 0 }, skipped{ 0 }, nodeFiles{ 0 };
    std::mutex               failMutex;
    std::vector<std::string> failures;
    const auto fail = [&](const std::string& f) { const std::lock_guard lock(failMutex); failures.push_back(f); };
    hky::exec::ParallelFor(units.size(), [&](std::size_t i) {
        try {
            const std::string& unit = units[i].prefix;
            const auto src = arc->source(unit);
            if (!src) return fail(unit + ": no source");
            std::vector<std::uint8_t> canon;
            std::string why;
            const std::vector<std::string>* bones = nullptr;
            std::vector<std::string> own;
            if (!bytesOf(*src, unit, nullptr, canon, why)) {
                canon.clear();
                const std::string key = PathKey(unit);
                const auto cs = dc::ReadCharacterStrings(*src);
                if (const auto at = key.rfind("/characters/"); cs && !cs->rigName.empty() && at != std::string::npos) {
                    std::string rig = cs->rigName;
                    std::replace(rig.begin(), rig.end(), '\\', '/');
                    rig = key.substr(0, at + 1) + PathKey(rig);
                    if (const auto rigSrc = arc->source(rig))
                        if (auto names = dc::ReadSkeletonBones(*rigSrc, *reg, rig)) own = std::move(*names);
                }
                bones = own.empty() ? &humanoid : &own;
                if (!bytesOf(*src, unit, bones, canon, why)) {
                    std::printf("skip %s: %s\n", unit.c_str(), why.c_str());
                    ++skipped;
                    return;
                }
            }
            std::mt19937 rng(static_cast<std::uint32_t>(i) ^ 0xCB);
            static const char* const kFolders[] = { "Combat", "combat2", "A", "Group With Spaces", "x.y", "Deep", "0000x0003", "bones", "states" };
            const fs::path dir = fs::path(workDir) / std::to_string(i);
            std::error_code ec;
            fs::remove_all(hky::disk::LongPath(dir), ec);
            std::vector<HkyArchive::PackEntry> entries;
            int n = 0;
            for (const std::string& rel : src->listYaml("", true)) {
                auto text = src->read(rel);
                if (!text) return fail(unit + ": " + rel + " unreadable");
                std::string path;
                if (PathKey(rel) == "graph.yaml") path = rel;
                else {
                    for (int d = static_cast<int>(rng() % 7); d > 0; --d) path += std::string(kFolders[rng() % std::size(kFolders)]) + "/";
                    path += "f" + std::to_string(n++) + ".yaml";
                }
                if (!hky::disk::WriteFile(dir / "u" / path, *text, {}, &why)) return fail(unit + ": " + why);
                entries.push_back({ "meshes/u.hkx/" + path, [body = std::move(*text)]() -> std::optional<std::string> { return body; } });
            }
            nodeFiles += entries.size();
            std::vector<std::uint8_t> got;
            if (!bytesOf(hky::common::DiskUnitSource(dir / "u"), unit, bones, got, why)) return fail(unit + ": disk: " + why);
            if (got != canon) return fail(unit + ": shuffled on disk, the bytes differ");
            const std::string hky = (dir / "u.hky").string();
            if (!HkyArchive::PackEntries(std::move(entries), hky, why)) return fail(unit + ": pack: " + why);
            const auto packed = HkyArchive::LoadFromFile(hky, why);
            const auto zipped = packed ? packed->source("meshes/u.hkx") : nullptr;
            if (!zipped) return fail(unit + ": zip: " + why);
            got.clear();
            if (!bytesOf(*zipped, unit, bones, got, why)) return fail(unit + ": zip: " + why);
            if (got != canon) return fail(unit + ": shuffled in a zip, the bytes differ");
            ++same;
            fs::remove_all(hky::disk::LongPath(dir), ec);
        } catch (const std::exception& e) {
            fail(units[i].prefix + ": " + e.what());
        }
    });
    for (const auto& f : failures) std::printf("FAIL %s\n", f.c_str());
    std::printf("shuffle-check: %zu/%zu unit(s) identical from archive, disk and zip (%zu files scattered), %zu skipped, %zu failed\n",
                same.load(), units.size(), nodeFiles.load(), skipped.load(), failures.size());
    return failures.empty() ? 0 : 1;
}

/// @brief `origins-check`: every graph unit of a .hky (or the one named) serialized with its object origins
///        (SerializeGraph); each origin's offset and class must be exactly what a loader reads from the packfile's
///        virtual fixups, one per object, and every file node (a unit file with an id) must carry its FormID.
int doOriginsCheck(const std::string& hkyPath, const std::string& schemaDir, const std::string& onlyUnit) {
    if (hkyPath.empty() || schemaDir.empty()) {
        std::printf("usage: hky-tool origins-check <in.hky> <schemaDir> [unit]\n");
        return 2;
    }
    namespace dc = hky::decompile;
    using hky::codec::HkyArchive;
    const auto* reg = hky::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", hky::schema::SharedRegistryError().c_str()); return 1; }
    std::string err;
    const auto  arc = HkyArchive::LoadFromFile(hkyPath, err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    int units = 0, failures = 0;
    std::size_t objects = 0, withFormId = 0;
    for (const auto& u : arc->units()) {
        if (!onlyUnit.empty() && hky::codec::resource::PathKey(u.prefix) != hky::codec::resource::PathKey(onlyUnit)) continue;
        const auto src = arc->source(u.prefix);
        if (!src) continue;
        dc::ParsedHky graph;
        if (!dc::ParseHkyReflective(*src, *reg, graph, err, nullptr, u.prefix)) {
            // A unit whose bones need its rig (the load order binds them) cannot be read alone; not this check's subject.
            std::printf("skip %s: %s\n", u.prefix.c_str(), err.c_str());
            continue;
        }
        std::vector<std::uint8_t>                     bytes;
        std::vector<hky::trace::ObjectOrigin> origins;
        if (!dc::SerializeGraph(graph, bytes, origins, err)) { std::printf("FAIL %s: %s\n", u.prefix.c_str(), err.c_str()); ++failures; continue; }
        hky::codec::PackFileDeserializer des;
        hky::codec::BinaryReaderEx       br(false, true, bytes);
        des.DeserializePartially(br);
        const auto listed = des.ListObjects();   // (data-section offset, class) per virtual fixup, ascending
        auto sorted = origins;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.offset < b.offset; });
        std::string why;
        if (listed.size() != sorted.size()) why = std::to_string(listed.size()) + " objects in the packfile, " + std::to_string(sorted.size()) + " origins";
        for (std::size_t i = 0; why.empty() && i < listed.size(); ++i)
            if (listed[i].first != sorted[i].offset || listed[i].second != sorted[i].className)
                why = "object " + std::to_string(i) + ": packfile " + listed[i].second + " @" + std::to_string(listed[i].first) + ", origin " +
                      sorted[i].className + " @" + std::to_string(sorted[i].offset);
        // Every file node has a FormID; an inline object has none.
        const std::size_t nodes = graph.identity.nodes.size();
        std::size_t named = 0, orphans = 0;
        for (const auto& o : origins) {
            named += o.formId.empty() ? 0 : 1;
            orphans += o.formId.empty() && o.owner.empty() ? 1 : 0;   // an inline object always has a file node holding it
        }
        if (why.empty() && named != nodes) why = std::to_string(nodes) + " file nodes, " + std::to_string(named) + " origins with a FormID";
        if (why.empty() && orphans) why = std::to_string(orphans) + " inline objects without an owner";
        ++units;
        objects += origins.size();
        withFormId += named;
        if (!why.empty()) { std::printf("FAIL %s: %s\n", u.prefix.c_str(), why.c_str()); ++failures; }
        else std::printf("ok   %s: %zu objects, %zu with a FormID\n", u.prefix.c_str(), origins.size(), named);
    }
    std::printf("%s: %d unit(s), %zu objects, %zu with a FormID, %d failure(s)\n", failures ? "FAIL" : "PASS", units, objects, withFormId, failures);
    return failures || units == 0 ? 1 : 0;
}

/// @brief `embedded-roundtrip`: every file of a `kind: embedded` descriptor in a .hky, parsed through the schema and
///        spliced back unchanged, must reproduce its text byte for byte; and every `view: curve` object must sample
///        (SampleCurve, by its view roles) exactly as the typed loader's curves do, at each key and midway between.
int doEmbeddedRoundTrip(const std::string& hkyPath, const std::string& schemaDir) {
    if (hkyPath.empty() || schemaDir.empty()) {
        std::printf("usage: hky-tool embedded-roundtrip <in.hky> <schemaDir>\n");
        return 2;
    }
    namespace dc = hky::decompile;
    namespace ea = hky::anim;
    const auto* reg = hky::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", hky::schema::SharedRegistryError().c_str()); return 1; }
    std::string err;
    const auto  arc = hky::codec::HkyArchive::LoadFromFile(hkyPath, err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }

    const auto keys = arc->filesUnder(""), orig = arc->filesUnderOrig("");
    std::atomic<std::size_t> files{ 0 }, curves{ 0 }, samples{ 0 };
    std::mutex               failMutex;
    std::vector<std::string> failures;
    const auto fail = [&](const std::string& f) { const std::lock_guard lock(failMutex); failures.push_back(f); };
    hky::exec::ParallelFor(keys.size(), [&](std::size_t i) {
        try {
            const auto text = arc->file(keys[i]);
            if (!text) return fail(orig[i] + ": unreadable");
            const auto* kind = dc::EmbeddedKindOf(*reg, *text);
            if (!kind) return;
            ++files;
            std::string perr;
            const auto  obj = dc::ParseEmbeddedFile(*text, *kind, *reg, orig[i], perr);
            if (!obj) return fail(perr);
            const auto back = dc::SpliceEmbeddedFile(*text, *kind, *obj, *reg, perr);
            if (!back) return fail(orig[i] + ": " + perr);
            if (*back != *text) {
                std::size_t at = 0;
                while (at < back->size() && at < text->size() && (*back)[at] == (*text)[at]) ++at;
                return fail(orig[i] + ": splice differs at byte " + std::to_string(at));
            }
            if (kind->cls != "CBAnimation") return;
            const auto def = ea::AnimationYamlLoader::LoadFromString(*text, orig[i]);
            const auto& objs = obj->FieldRef("curves").objs;
            if (objs.size() != def.curves.size()) return fail(orig[i] + ": curve count differs");
            for (std::size_t c = 0; c < objs.size(); ++c) {
                const auto* so = dynamic_cast<const hky::codec::io::SchemaObject*>(objs[c].get());
                const auto& typed = def.curves[c];
                ++curves;
                for (std::size_t k = 0; k < typed.keys.size(); ++k) {
                    const float t0 = typed.keys[k].time;
                    const float t1 = k + 1 < typed.keys.size() ? (t0 + typed.keys[k + 1].time) / 2 : t0 + 1;
                    for (const float t : { t0, t1 }) {
                        const auto s = so ? dc::SampleCurve(*so, t, *reg) : std::nullopt;
                        ++samples;
                        if (!s || *s != ea::SampleCurve(typed, t))
                            return fail(orig[i] + ": curve '" + typed.name + "' samples differently at " + std::to_string(t));
                    }
                }
            }
        } catch (const std::exception& e) {
            fail(orig[i] + ": " + e.what());
        }
    });
    for (const auto& f : failures) std::printf("  FAIL %s\n", f.c_str());
    std::printf("%s: embedded-roundtrip '%s': %zu embedded file(s), %zu curve(s), %zu sample(s) checked, %zu failure(s).\n",
                failures.empty() ? "OK" : "FAIL", hkyPath.c_str(), files.load(), curves.load(), samples.load(), failures.size());
    return failures.empty() && files > 0 ? 0 : 1;
}

/// @brief `loadorder`: the .hky load order of a Data folder or MO2 instance, as CB-API's LoadOrderDocument reads it
///        through VFS: its bundles in order, then every behavior and character unit merged (`--all`) or the named ones.
int doLoadOrder(int argc, char** argv) {
    std::string              schemaDir, mo2, profile, data;
    std::vector<std::string> units;
    bool                     all = false, listing = false;
    std::string              ls;
    for (int i = 0; i < argc; ++i) {
        const std::string a    = argv[i];
        const auto        next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--mo2") mo2 = next();
        else if (a == "--profile") profile = next();
        else if (a == "--data") data = next();
        else if (a == "--unit") units.push_back(next());
        else if (a == "--all") all = true;
        else if (a == "--ls") ls = next(), listing = true;
        else if (schemaDir.empty()) schemaDir = a;
    }
    if (schemaDir.empty() || (mo2.empty() == data.empty())) {
        std::printf("usage: hky-tool loadorder <schemaDir> (--mo2 <instance> [--profile <p>] | --data <DataFolder>) [--unit <key>]... [--all] [--ls <prefix>]\n");
        return 2;
    }
    const auto* reg = hky::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", hky::schema::SharedRegistryError().c_str()); return 1; }

    vfs::Options o;
    if (!mo2.empty()) o.kind = vfs::Options::Kind::Mo2, o.mo2Instance = mo2, o.mo2Profile = profile;
    else o.dataFolder = data;
    vfs::FileSystem fsys;
    std::string     err;
    if (!fsys.Open(o, &err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    const auto docPtr = std::make_shared<hky::merge::LoadOrderDocument>();
    auto&      doc    = *docPtr;
    if (!doc.Open(fsys, *reg, &err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }

    for (const auto& b : doc.Bundles())
        std::printf("  %3d %-28s %-34s%s%s\n", b.rank, b.stem.c_str(), b.provider.c_str(), b.dropped ? " DROPPED" : "",
                    b.error.empty() ? "" : (" UNREADABLE: " + b.error).c_str());
    for (const auto& w : doc.Warnings()) std::printf("  warn: %s\n", w.c_str());
    const auto every = doc.Units();
    std::size_t graphs = 0, rigged = 0, projects = 0, skeletons = 0;
    for (const auto& u : every) {
        projects  += u.kind == hky::codec::HkyArchive::UnitKind::Project;
        skeletons += u.kind == hky::codec::HkyArchive::UnitKind::Skeleton;
        if (std::find(units.begin(), units.end(), u.key) == units.end()) continue;
        std::string layers;
        for (const auto& l : u.layers) layers += (layers.empty() ? "" : " -> ") + l;
        std::printf("  %s: kind %d, layers %s\n", u.key.c_str(), static_cast<int>(u.kind), layers.c_str());
        if (u.kind == hky::codec::HkyArchive::UnitKind::Project)
            for (const auto& c : doc.ProjectCharacters(u.key)) std::printf("    character %s\n", c.c_str());
    }
    for (const auto& u : every) {
        using K = hky::codec::HkyArchive::UnitKind;
        if (u.kind != K::Behavior && u.kind != K::Character) continue;
        ++graphs;
        if (doc.RigBones(u.key)) ++rigged;
        if (std::find(units.begin(), units.end(), u.key) != units.end()) {
            std::string rigs;
            for (const auto& r : doc.Rigs(u.key)) rigs += (rigs.empty() ? "" : ", ") + r;
            std::printf("  %s binds: %s\n", u.key.c_str(), rigs.empty() ? "no rig" : rigs.c_str());
        }
        if (all) units.push_back(u.key);
    }
    std::printf("  %zu bundle(s), %zu file(s), %zu unit(s): %zu project(s), %zu skeleton(s), %zu behavior/character "
                "unit(s), %zu bound to a rig\n",
                doc.Bundles().size(), doc.Files().size(), every.size(), projects, skeletons, graphs, rigged);

    /// @note `--ls`: the load order streamed over the Data folder (HkyOverlay); every file below the prefix with its
    ///       winner, and each overlaid file read back through VFS.
    if (listing) {
        using Overlay = hky::merge::HkyOverlay;
        fsys.SetOverlay(".hky load order", std::make_shared<Overlay>(docPtr, Overlay::Scope::LoadOrder));
        std::size_t shown = 0, overlaid = 0, read = 0;
        std::vector<std::string> bad;
        for (const auto& p : fsys.List(ls)) {
            const auto w = fsys.Winner(p);
            ++shown;
            if (!w || !w->overlay) continue;
            ++overlaid;
            std::string why;
            const auto  bytes = fsys.Read(p, &why);
            if (bytes) ++read;
            else bad.push_back(p + ": " + why);
            std::printf("  %-90s %8zu  %s\n", p.c_str(), bytes ? bytes->size() : 0, w->layer.c_str());
        }
        for (const auto& b : bad) std::printf("  FAIL %s\n", b.c_str());
        std::printf("%s: loadorder --ls: %zu file(s) below '%s', %zu from the .hky overlay, %zu read.\n",
                    bad.empty() ? "OK" : "FAIL", shown, ls.c_str(), overlaid, read);
        return bad.empty() ? 0 : 1;
    }

    std::atomic<std::size_t> merged{ 0 };
    std::mutex               failMutex;
    std::vector<std::string> failures;
    const auto               t0 = std::chrono::steady_clock::now();
    hky::exec::ParallelFor(units.size(), [&](std::size_t i) {
        hky::decompile::ParsedHky g;
        std::string                      why;
        if (doc.MergedGraph(units[i], g, &why)) { ++merged; return; }
        const std::lock_guard lock(failMutex);
        failures.push_back(units[i] + ": " + why);
    });
    std::sort(failures.begin(), failures.end());
    for (const auto& f : failures) std::printf("  FAIL %s\n", f.c_str());
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s: loadorder: %zu of %zu unit(s) merged in %.1f s.\n", failures.empty() ? "OK" : "FAIL", merged.load(),
                units.size(), secs);
    return failures.empty() ? 0 : 1;
}

/// @brief `merge-check`: every unit in `bundle` merged onto its base in `base` (a patch unit) or alone (a unit the
///        bundle owns), then serialized: the compile the game's warm-up runs, offline. Each result is written to
///        `outDir/<unit>` (the .hkx) and decompiled back beside it as `<unit>.merged/` for inspection.
/// @note  Bones come from the base's humanoid skeleton, so the check covers graphs on that rig.
int doMergeCheck(const std::string& basePath, const std::string& bundlePath, const std::string& schemaDir,
                 const std::string& outDir) {
    if (basePath.empty() || bundlePath.empty() || schemaDir.empty() || outDir.empty()) {
        std::printf("usage: hky-tool merge-check <Skyrim.hky> <bundle.hky> <schemaDir> -o <outDir>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    using hky::codec::HkyArchive;
    using hky::codec::resource::PathKey;
    auto* reg = hky::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", hky::schema::SharedRegistryError().c_str()); return 1; }

    std::string err;
    const auto base = HkyArchive::LoadFromFile(basePath, err);
    if (!base) { std::printf("FAIL: base: %s\n", err.c_str()); return 1; }
    const auto bundle = HkyArchive::LoadFromFile(bundlePath, err);
    if (!bundle) { std::printf("FAIL: bundle: %s\n", err.c_str()); return 1; }

    std::vector<std::string> bones;
    {
        const std::string humanoid = "meshes/actors/character/character assets/skeleton.hkx";
        if (const auto src = base->source(humanoid))
            if (auto names = hky::decompile::ReadSkeletonBones(*src, *reg, humanoid)) bones = std::move(*names);
    }
    if (bones.empty()) { std::printf("FAIL: no humanoid skeleton bone list in the base\n"); return 1; }

    std::string stem = fs::path(bundlePath).stem().string();
    stem = PathKey(stem);
    std::vector<std::string> baseUnits;
    for (const auto& u : base->units()) baseUnits.push_back(u.prefix);
    std::sort(baseUnits.begin(), baseUnits.end());

    std::size_t ok = 0, failed = 0;
    for (const auto& u : bundle->units()) {
        std::vector<hky::common::LayerSource> layers;
        const bool patch = std::binary_search(baseUnits.begin(), baseUnits.end(), u.prefix);
        if (patch) layers.push_back({ base->source(u.prefix), std::string(hky::codec::formid::BASE_GAME_STEM),
                                      hky::codec::formid::BuildMasterTable(hky::codec::formid::BASE_GAME_STEM, {}, true) });
        layers.push_back({ bundle->source(u.prefix), stem, hky::codec::formid::BuildMasterTable(stem, {}, true) });

        hky::decompile::ParsedHky graph;
        std::vector<std::uint8_t>        bytes;
        const fs::path                   dst = fs::path(outDir) / u.prefix;
        if (!hky::decompile::MergeLayersReflective(layers, *reg, &bones, graph, err) ||
            !hky::decompile::SerializeGraph(graph, bytes, err) ||
            !hky::disk::WriteFile(dst, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), {}, &err) ||
            !hky::decompile::DecompileBehaviorReflective(bytes, *reg, dst.string() + ".merged", err, &bones)) {
            std::printf("  FAIL %s (%s): %s\n", u.prefix.c_str(), patch ? "patch" : "own unit", err.c_str());
            ++failed;
            continue;
        }
        std::printf("  OK   %s (%s): %zu object(s), %zu bytes\n", u.prefix.c_str(), patch ? "patch onto the base" : "own unit",
                    graph.objects.size(), bytes.size());
        ++ok;
    }
    std::printf("%s: merge-check '%s': %zu unit(s) merged and serialized, %zu failed.\n", failed ? "FAIL" : "OK",
                bundlePath.c_str(), ok, failed);
    return failed ? 1 : 0;
}

/// @return the first field where `a` and `b` differ ("" when every field the unit carries is equal)
std::string AnimDefDiff(const hky::anim::AnimationDef& a, const hky::anim::AnimationDef& b) {
    /// @note Bitwise, not `==`: vanilla clips carry NaN keys (`nan_7fffffff`), which must survive with their payload.
    const auto same = [](float x, float y) { return std::memcmp(&x, &y, sizeof x) == 0; };
    const auto vec  = [&](const auto& x, const auto& y, int n) {
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i) {
            if (!same(x[i].time, y[i].time)) return false;
            for (int k = 0; k < n; ++k)
                if (!same(x[i].value[k], y[i].value[k])) return false;
        }
        return true;
    };
    if (a.name != b.name) return "name";
    if (a.duration != b.duration || a.numFrames != b.numFrames || a.frameDuration != b.frameDuration) return "timing";
    if (a.skeleton != b.skeleton) return "skeleton";
    const auto& ca = a.compression; const auto& cb = b.compression;
    if (ca.rotationTolerance != cb.rotationTolerance || ca.translationTolerance != cb.translationTolerance ||
        ca.scaleTolerance != cb.scaleTolerance || ca.rotationDegree != cb.rotationDegree ||
        ca.translationDegree != cb.translationDegree || ca.scaleDegree != cb.scaleDegree || ca.maxFramesPerBlock != cb.maxFramesPerBlock)
        return "compression";
    if (a.tracks.size() != b.tracks.size()) return "track count";
    for (std::size_t t = 0; t < a.tracks.size(); ++t) {
        const auto& x = a.tracks[t]; const auto& y = b.tracks[t];
        if (x.bone != y.bone || !vec(x.translation, y.translation, 3) || !vec(x.rotation, y.rotation, 4) || !vec(x.scale, y.scale, 3))
            return "track " + std::to_string(t) + " (" + x.bone + ")";
    }
    if (a.floatTracks.size() != b.floatTracks.size()) return "float track count";
    for (std::size_t t = 0; t < a.floatTracks.size(); ++t) {
        const auto& x = a.floatTracks[t]; const auto& y = b.floatTracks[t];
        if (x.name != y.name || x.keyframes.size() != y.keyframes.size()) return "float track " + std::to_string(t);
        for (std::size_t k = 0; k < x.keyframes.size(); ++k)
            if (x.keyframes[k].time != y.keyframes[k].time || !same(x.keyframes[k].value, y.keyframes[k].value)) return "float track " + std::to_string(t);
    }
    if (a.annotationTracks.size() != b.annotationTracks.size()) return "annotation track count";
    for (std::size_t t = 0; t < a.annotationTracks.size(); ++t) {
        const auto& x = a.annotationTracks[t]; const auto& y = b.annotationTracks[t];
        if (x.trackName != y.trackName || x.annotations.size() != y.annotations.size()) return "annotation track " + std::to_string(t);
        for (std::size_t k = 0; k < x.annotations.size(); ++k)
            if (x.annotations[k].time != y.annotations[k].time || x.annotations[k].text != y.annotations[k].text)
                return "annotation track " + std::to_string(t);
    }
    if (a.motion.has_value() != b.motion.has_value()) return "motion presence";
    if (a.motion && (a.motion->duration != b.motion->duration || a.motion->translations != b.motion->translations ||
                     a.motion->rotations != b.motion->rotations))
        return "motion";
    if (a.rootMotionVisual != b.rootMotionVisual || a.motionTarget != b.motionTarget || a.preload != b.preload) return "motion flags / preload";
    if (a.curves.size() != b.curves.size()) return "curve count";
    for (std::size_t c = 0; c < a.curves.size(); ++c) {
        const auto& x = a.curves[c]; const auto& y = b.curves[c];
        if (x.name != y.name || x.keys.size() != y.keys.size()) return "curve " + x.name;
        for (std::size_t k = 0; k < x.keys.size(); ++k)
            if (x.keys[k].time != y.keys[k].time || !same(x.keys[k].value, y.keys[k].value) || x.keys[k].interp != y.keys[k].interp)
                return "curve " + x.name;
    }
    return {};
}

/// @brief `anim-reformat`: every animation unit under `dir` (an unpacked tree) rewritten in the schema's form
///        (EmitAnimationYaml of its read), and gated: the rewritten text reads back to the same AnimationDef, field
///        for field, and re-emits to itself byte for byte.
/// @note  How a partition holding units written before CBAnimation moves over: unpack, reformat, pack.
int doAnimReformat(const std::string& dir, const std::string& schemaDir) {
    namespace fs = std::filesystem;
    namespace ea = hky::anim;
    if (dir.empty() || schemaDir.empty()) {
        std::printf("usage: hky-tool anim-reformat <unpackedDir> <schemaDir>\n");
        return 2;
    }
    const auto* reg = hky::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", hky::schema::SharedRegistryError().c_str()); return 1; }
    std::vector<fs::path> files;
    std::error_code       ec;
    for (fs::recursive_directory_iterator it(hky::disk::LongPath(dir), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file()) files.push_back(it->path());
    std::atomic<std::size_t> units{ 0 }, rewritten{ 0 };
    std::mutex               failMutex;
    std::vector<std::string> failures;
    const auto fail = [&](const std::string& f) { const std::lock_guard lock(failMutex); failures.push_back(f); };
    hky::exec::ParallelFor(files.size(), [&](std::size_t i) {
        const std::string name = files[i].generic_string();
        try {
            const auto text = hky::disk::ReadFile(files[i]);
            if (!text) return fail(name + ": unreadable");
            const auto* kind = hky::decompile::EmbeddedKindOf(*reg, *text);
            if (!kind || kind->cls != "CBAnimation") return;
            ++units;
            const auto        before = ea::AnimationYamlLoader::LoadFromString(*text, name);
            const std::string y      = ea::EmitAnimationYaml(before);
            const auto        after  = ea::AnimationYamlLoader::LoadFromString(y, name);
            if (const std::string d = AnimDefDiff(before, after); !d.empty()) return fail(name + ": rewritten unit reads a different " + d);
            const std::string back = ea::EmitAnimationYaml(after);
            if (back != y) {
                std::size_t at = 0;
                while (at < back.size() && at < y.size() && back[at] == y[at]) ++at;
                return fail(name + ": not a fixpoint, differs at byte " + std::to_string(at));
            }
            if (y == *text) return;
            std::string werr;
            if (!hky::disk::WriteFile(files[i], y, { .atomic = true }, &werr)) return fail(name + ": " + werr);
            ++rewritten;
        } catch (const std::exception& e) {
            fail(name + ": " + e.what());
        }
    });
    for (const auto& f : failures) std::printf("  FAIL %s\n", f.c_str());
    std::printf("%s: anim-reformat '%s': %zu animation unit(s), %zu rewritten, %zu failure(s).\n", failures.empty() ? "OK" : "FAIL",
                dir.c_str(), units.load(), rewritten.load(), failures.size());
    return failures.empty() && units > 0 ? 0 : 1;
}

/// @brief `mm-sample`: the motion-matching bundle generator over a spec exercising every shape it writes (two databases
///        sharing a clip, flags, literal and expression biases, tags, chooser rows over bool, int and float variables,
///        a row that always holds, a fallback, an extra variable, two characters), its files written to `outDir`.
/// @note  The generator itself proves the behavior unit parses back to the graph it built; OK here means it did.
int doMmSample(const std::string& schemaDir, const std::string& outDir) {
    if (schemaDir.empty() || outDir.empty()) {
        std::printf("usage: hky-tool mm-sample <schemaDir> -o <outDir>\n");
        return 2;
    }
    namespace mm = hky::compile;
    auto* reg = hky::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", hky::schema::SharedRegistryError().c_str()); return 1; }

    const std::string dir = "meshes/actors/character/animations/MotionMatching/";
    mm::MmBundleSpec  spec;
    spec.databases.push_back({ "Idle", { { dir + "idle/Idle_Loop.hkx", "", mm::kMmLooping },
                                         { dir + "idle/TurnInPlace_L.hkx", "", mm::kMmDisableReselection } },
                               "", "-0.05", "", { "idle" } });
    spec.databases.push_back({ "Run", { { dir + "running/Run_Fwd_Loop.hkx", "", mm::kMmLooping },
                                        { dir + "idle/Idle_Loop.hkx", "", mm::kMmLooping } },
                               "0.1", "", "(GraphVar('MM_Speed') > 300) * -0.02", { "run", "locomotion" } });
    spec.rows.push_back({ "GraphVar(\"MM_Gait\") == 2 && GraphVar(\"MM_IsStarting\")", { "Run" } });
    spec.rows.push_back({ "GraphVar('MM_Speed') >= 0 && GraphVar('MM_Speed') <= 50", { "Idle", "Run" } });
    spec.rows.push_back({ "", { "Idle" } });
    spec.fallback = { "Idle" };
    spec.extraVariables.push_back({ "MM_Speed", "VARIABLE_TYPE_REAL", 0 });
    spec.characters = { { "meshes/actors/character/characters/defaultmale.hkx", "0001x000b" },
                        { "meshes/actors/character/characters female/defaultfemale.hkx", "0001x000b" } };

    mm::MmFiles files;
    std::string err;
    if (!mm::BuildMotionMatchingBundle(spec, *reg, files, err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    for (const auto& [path, text] : files)
        if (!hky::disk::WriteFile(std::filesystem::path(outDir) / path, text, {}, &err)) {
            std::printf("FAIL: %s\n", err.c_str());
            return 1;
        }
    std::printf("OK: mm-sample -> %s: %zu file(s); the behavior unit parsed back to the graph it was built from.\n",
                outDir.c_str(), files.size());
    return 0;
}

/// @brief `serve-dump`: what a Data folder's .hky load order serves, written to `outDir`: every behavior, character,
///        project and skeleton unit as LoadOrderDocument::GraphBytes serializes it, at its winning bundle's spelling;
///        the animationdatasinglefile.txt projected from the load order (ProjectAnimData, as AnimationDataServer feeds it);
///        the animationsetdatasinglefile.txt composed from the winning setdata/ files (AssembleSetdata); and
///        `_serve-dump.txt`, one sorted line per output. The golden-output harness (tools/golden) diffs two dumps.
/// @note  The asdsf is the composed base only: the mod delta merge and its coverage guard live in the plugin
///        (AnimationSetDataServer), not the engine. The adsf takes no OAR replacer copies (the plugin reads them from
///        the Data folder's configs).
int doServeDump(int argc, char** argv) {
    std::string schemaDir, data, out;
    for (int i = 0; i < argc; ++i) {
        const std::string a    = argv[i];
        const auto        next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--data") data = next();
        else if (a == "-o") out = next();
        else if (schemaDir.empty()) schemaDir = a;
    }
    if (schemaDir.empty() || data.empty() || out.empty()) {
        std::printf("usage: hky-tool serve-dump <schemaDir> --data <DataFolder> -o <outDir>\n");
        return 2;
    }
    namespace fs  = std::filesystem;
    namespace ad  = hky::animdata;
    namespace asd = hky::animsetdata;
    using Kind    = hky::codec::HkyArchive::UnitKind;
    const auto* reg = hky::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", hky::schema::SharedRegistryError().c_str()); return 1; }

    vfs::Options o;
    o.dataFolder = data;
    vfs::FileSystem fsys;
    std::string     err;
    if (!fsys.Open(o, &err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    hky::merge::LoadOrderDocument doc;
    if (!doc.Open(fsys, *reg, &err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    for (const auto& b : doc.Bundles())
        std::printf("  %3d %-28s%s%s\n", b.rank, b.stem.c_str(), b.dropped ? " DROPPED" : "",
                    b.error.empty() ? "" : (" UNREADABLE: " + b.error).c_str());

    std::vector<hky::merge::LoadOrderDocument::Unit> units;
    for (auto& u : doc.Units()) units.push_back(std::move(u));
    std::vector<std::string> lines(units.size());
    std::atomic<std::size_t> written{ 0 }, failed{ 0 };
    hky::exec::ParallelFor(units.size(), [&](std::size_t i) {
        const auto&               u = units[i];
        std::vector<std::uint8_t> bytes;
        std::string               why;
        std::string               layers;
        for (const auto& l : u.layers) layers += (layers.empty() ? "" : ",") + l;
        try {
            if (doc.GraphBytes(u.key, bytes, &why) &&
                hky::disk::WriteFile(fs::path(out) / fs::path(u.path), bytes, {}, &why)) {
                lines[i] = "unit " + u.path + " " + std::to_string(bytes.size()) + " " + layers;
                ++written;
                return;
            }
        } catch (const std::exception& e) {
            why = e.what();
        }
        lines[i] = "FAIL unit " + u.path + " " + layers + ": " + why;
        ++failed;
    });

    /// @note The adsf, fed as the plugin feeds it: merged rosters and clips, every animation from its winning bundle.
    bool        adsfOk = false;
    std::string adsfLine;
    if (const auto index = doc.Text("meshes/animationdatasinglefile.txt/index.yaml")) {
        const auto headers = ad::ParseAnimdataIndexYaml(*index, err);
        std::unordered_map<std::string, std::optional<hky::anim::AnimationDef>> anims;
        const auto parse = [&](const std::string& path) -> std::optional<hky::anim::AnimationDef> {
            const auto text = doc.Text(path);
            if (!text) return std::nullopt;
            try {
                auto def = hky::anim::AnimationYamlLoader::LoadFromString(*text, path);
                if (def.motionTarget != hky::anim::AnimationDef::MotionTarget::MotionMatching)
                    def.motion = hky::anim::ResolveRootMotion(def);
                return def;
            } catch (const std::exception&) {
                return std::nullopt;
            }
        };
        ad::ProjectionSource src;
        src.roster = [&](const std::string& key) {
            const auto cs = doc.Character(key);
            return cs ? cs->animationNames : std::vector<std::string>{};
        };
        src.clips = [&](const std::string& key) {
            hky::decompile::ParsedHky g;
            return doc.MergedGraph(key, g) ? hky::compile::DeriveClipInputsFromGraph(g.root)
                                           : std::vector<ad::DeriveClipInput>{};
        };
        src.behaviorUnits = [&] {
            std::vector<std::string> keys;
            for (const auto& u : units)
                if (u.kind == Kind::Behavior) keys.push_back(u.key);
            return keys;
        };
        src.prefetch = [&](const std::vector<std::string>& paths) {
            std::vector<std::optional<hky::anim::AnimationDef>> parsed(paths.size());
            hky::exec::ParallelFor(paths.size(), [&](std::size_t i) { parsed[i] = parse(paths[i]); });
            for (std::size_t i = 0; i < paths.size(); ++i) anims.emplace(paths[i], std::move(parsed[i]));
        };
        src.animation = [&](const std::string& path) -> const hky::anim::AnimationDef* {
            auto it = anims.find(path);
            if (it == anims.end()) it = anims.emplace(path, parse(path)).first;
            return it->second ? &*it->second : nullptr;
        };
        ad::ProjectionStats st;
        std::string         text;
        const auto          root = headers.empty() ? nullptr : ad::ProjectAnimData(*reg, headers, src, &st, err);
        if (root && hky::codec::io::WriteMetaText(*root, text, err) &&
            hky::disk::WriteFile(fs::path(out) / "meshes" / "animationdatasinglefile.txt", text, {}, &err)) {
            adsfOk   = true;
            adsfLine = "adsf meshes/animationdatasinglefile.txt " + std::to_string(text.size()) + " projects " +
                       std::to_string(st.projects) + " clips " + std::to_string(st.clips) + " motions " +
                       std::to_string(st.motions) + " unresolved " + std::to_string(st.unresolved);
        } else adsfLine = "FAIL adsf: " + err;
    } else adsfLine = "FAIL adsf: no meshes/animationdatasinglefile.txt/index.yaml in the load order";

    bool        asdsfOk = false;
    std::string asdsfLine;
    const std::string setDir = "meshes/animationsetdatasinglefile.txt";
    if (const auto index = doc.Text(setDir + "/index.yaml")) {
        std::string ierr;
        const auto  headers = asd::ParseSetdataIndexYaml(*index, ierr);
        std::map<std::string, asd::SingleFile> byStem;
        for (const auto& path : doc.Files(setDir + "/setdata")) {
            const auto y = doc.Text(path);
            if (!y || !hky::codec::resource::HasExt(path, ".yaml")) continue;
            std::string    yerr;
            asd::Project   proj = asd::ParseSetdataUnitYaml(*y, yerr);
            if (!yerr.empty()) ierr += (ierr.empty() ? "" : "; ") + path + ": " + yerr;
            if (proj.header.empty()) continue;
            const std::string stem = asd::StemForHeader(proj.header);
            asd::SingleFile   one;
            one.projects.push_back(std::move(proj));
            byStem[stem] = std::move(one);
        }
        const std::string text = asd::EmitSingleFile(asd::AssembleSetdata(headers, byStem, {}));
        if (ierr.empty() && hky::disk::WriteFile(fs::path(out) / "meshes" / "animationsetdatasinglefile.txt", text, {}, &ierr)) {
            asdsfOk   = true;
            asdsfLine = "asdsf meshes/animationsetdatasinglefile.txt " + std::to_string(text.size()) + " projects " +
                        std::to_string(headers.size());
        } else asdsfLine = "FAIL asdsf: " + ierr;
    } else asdsfLine = "FAIL asdsf: no " + setDir + "/index.yaml in the load order";

    std::sort(lines.begin(), lines.end());
    std::string report;
    for (const auto& b : doc.Bundles()) report += "bundle " + std::to_string(b.rank) + " " + b.stem + (b.dropped ? " DROPPED" : "") + "\n";
    for (const auto& l : lines) report += l + "\n";
    report += adsfLine + "\n" + asdsfLine + "\n";
    if (std::string werr; !hky::disk::WriteFile(fs::path(out) / "_serve-dump.txt", report, {}, &werr)) {
        std::printf("FAIL: %s\n", werr.c_str());
        return 1;
    }
    for (const auto& l : lines)
        if (l.starts_with("FAIL")) std::printf("  %s\n", l.c_str());
    std::printf("  %s\n  %s\n", adsfLine.c_str(), asdsfLine.c_str());
    const bool ok = failed == 0 && adsfOk && asdsfOk;
    std::printf("%s: serve-dump -> %s: %zu unit(s) written, %zu failed; adsf %s, asdsf %s.\n", ok ? "OK" : "FAIL", out.c_str(),
                written.load(), failed.load(), adsfOk ? "written" : "FAILED", asdsfOk ? "written" : "FAILED");
    return ok ? 0 : 1;
}

void usage() {
    std::printf("usage:\n"
                "  hky-tool pack        <bundleDir> -o <out.hky>\n"
                "  hky-tool unpack      <in.hky>    -o <outDir>\n"
                "  hky-tool bsa-list    <in.bsa>    [prefix]\n"
                "  hky-tool bsa-extract <in.bsa>    -o <outDir> [prefix]\n"
                "  hky-tool assemble    <featureDir> <injectables.json> [schemaDir] -o <out.hky>\n"
                "  hky-tool merge-check <Skyrim.hky> <bundle.hky> <schemaDir> -o <outDir>\n"
                "  hky-tool embedded-roundtrip <in.hky> <schemaDir>\n"
                "  hky-tool anim-reformat <unpackedDir> <schemaDir>\n"
                "  hky-tool origins-check <in.hky> <schemaDir> [unit]\n"
                "  hky-tool shuffle-check <in.hky> <schemaDir> -o <workDir> [unit]\n"
                "  hky-tool loadorder  <schemaDir> (--mo2 <instance> [--profile <p>] | --data <Data>) [--unit <key>]... [--all] [--ls <prefix>]\n"
                "  hky-tool mm-sample   <schemaDir> -o <outDir>\n"
                "  hky-tool serve-dump  <schemaDir> --data <DataFolder> -o <outDir>\n"
                "  hky-tool graph-html  <schemaDir> -o <out.html> (--mo2 <instance> [--profile <p>] | --plugins <dir> | --bundle <f.hky>)...\n"
                "                       [--loadorder <file>] [--unit <path>]... [--all]\n");
}

}  // namespace

/// @brief `graph-html` (GraphHtml.cpp): its own argument parser, for the repeatable --plugins / --bundle / --unit.
int doGraphHtml(int argc, char** argv);

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    const std::string verb = argv[1];
    if (verb == "graph-html") return doGraphHtml(argc - 2, argv + 2);
    if (verb == "loadorder") return doLoadOrder(argc - 2, argv + 2);
    if (verb == "serve-dump") return doServeDump(argc - 2, argv + 2);

    std::string arg, arg2, arg3, out;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) { out = argv[++i]; }
        else if (arg.empty())         { arg = a; }
        else if (arg2.empty())        { arg2 = a; }
        else if (arg3.empty())        { arg3 = a; }
    }

    if (verb == "pack")        return doHkyPack(arg, out);
    if (verb == "unpack")      return doHkyUnpack(arg, out);
    if (verb == "bsa-list")    return doBsaList(arg, arg2);
    if (verb == "bsa-extract") return doBsaExtract(arg, out, arg2);
    if (verb == "assemble")    return doAssemble(arg, arg2, arg3, out);
    if (verb == "merge-check") return doMergeCheck(arg, arg2, arg3, out);
    if (verb == "embedded-roundtrip") return doEmbeddedRoundTrip(arg, arg2);
    if (verb == "origins-check") return doOriginsCheck(arg, arg2, arg3);
    if (verb == "shuffle-check") return doShuffleCheck(arg, arg2, out, arg3);
    if (verb == "mm-sample")   return doMmSample(arg, out);
    if (verb == "anim-reformat") return doAnimReformat(arg, arg2);
    usage();
    return 2;
}
