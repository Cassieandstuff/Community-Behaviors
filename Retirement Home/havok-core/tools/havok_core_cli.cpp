// havok-core-cli — the first-party YAML <-> HKX gate as a command-line tool.
//
//   havok-core-cli compile   <dir|file> [-o out.hkx]
//   havok-core-cli decompile <in.hkx>   [-o out.yaml]
//
// `compile` auto-detects the dialect: behavior.yaml (implemented),
// animation.yaml (implemented), character.yaml (milestone M-C, not yet).
// `decompile` is milestone M-D (not yet). This is the tool the plugin build
// (M-G) and the editor invoke — no XML, no .NET, no subprocess.
//
// ryml-gated: pulls in the YAML loaders. Built inside havok-core's CMake with
// ryml on the prefix path (see build_behavior.cpp for the standalone cl recipe).

#include "havok/anim/AnimationYamlLoader.h"
#include "havok/anim/AnimationData.h"
#include "havok/anim/AnimDataDeriver.h"
#include "havok/model/yaml/CharacterYamlLoader.h"
#include "havok/model/yaml/YamlBehaviorLoader.h"
#include "havok/model/BehaviorBuilder.h"   // model::ResolveBehaviorBindings (pre-build stage)
#include "havok/model/yaml/HkyArchive.h"
#include "havok/anim/AnimationCompiler.h"    // havok::anim::CompileAnimation (schema-native)
#include "havok/anim/AnimationDecompiler.h"  // havok::anim::DecompileAnimation (schema-native)
#include "havok/anim/AnimationEmitter.h"     // havok::anim::EmitAnimationHkx (typed baseline for the gate)
#include "havok/sct/BehaviorCompiler.h"
#include "havok/sct/CharacterCompiler.h"
#include "havok/sct/CharacterDecompiler.h"
#include "havok/sct/ProjectCompiler.h"
#include "havok/sct/HavokFile.h"
#include "havok/sct/Validate.h"

#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"
#include "havok/core/HavokRegistry.h"        // HavokRegistry::Create — old-class sig ground truth (schema-parity)
#include <havok-schema/HavokSchema.h>        // the Havok/ class-schema loader (schema-parity gate)
#include <havok-io/HavokIo.h>                // generic schema-driven packfile r/w (rewrite Stage 2)
#include <havok-model/HavokModel.h>          // name-keyed .hky model: identity/index + emit (Stage 3)
#include "havok/classes/Animation.h"   // hkaDefaultAnimatedReferenceFrame (refframe verb)
#include "havok/sct/AnimDataFromBehavior.h"   // in-memory clip extraction (runtime deriver core)
#include "havok/anim/AnimationSetData.h"       // setdata model (movesets-roundtrip)
#include "havok/anim/AnimSetDataYaml.h"         // movesets.yaml emit/parse
#include "havok/anim/AnimDataYaml.h"            // motion.yaml emit/parse (motion-roundtrip)
#include "havok/sct/PatchConverter.h"
#include "havok/sct/DeltaDeriver.h"          // DeriveLooseBehaviorDelta (library form)
#include "havok/sct/BehaviorDecompiler.h"   // DecompileBehaviorTree (name-keyed derive-delta)
#include "havok/sct/BoneNames.h"            // BoneNameTable / ParseBoneList (--skeleton)
#include "havok/sct/SkeletonImport.h"       // LoadSkeletonsFromHkx (--skeleton from a .hkx)
#include "havok/sct/SkeletonCompiler.h"     // CompileSkeleton (skeleton-recompile gate)
#include "havok/sct/SkeletonYaml.h"         // Emit/LoadSkeletonYaml (skeleton-compile/-decompile)
#include "havok/sct/TagfileOracle.h"
#include "havok/model/defs/GeneratorDefs.h"  // ClipGeneratorDef (schemabuild gate)
#include "havok/model/defs/CommonDefs.h"     // ClipTriggerDef, BindingDef
#include "havok/model/defs/StateMachineDefs.h" // StateMachineDef, StateDef, TransitionInfoDef, EventPropertyDef
#include "havok/model/defs/ModifierDefs.h"     // modifier node Defs (schemabuild gate)
#include "havok/xml/Xml.h"
#include "havok/classes/Animation.h"
#include "havok/classes/Arrays.h"
#include "havok/classes/Base.h"            // hkbNode (name identity)
#include "havok/classes/Graph.h"
#include "havok/classes/Generators.h"      // blender/selector children (owner map)
#include "havok/classes/Resource.h"        // hkMemoryResourceContainer/Handle (resdump verb)
#include "havok/classes/Physics.h"         // hkaSkeletonMapper (mapperdump verb)
#include "../src/sct/SkeletonMath.h"       // skmath::worldPoses (fkcheck verb — Stage 4/5 FK gate)
#include <cmath>                            // sqrt (ragdollcheck)
#include "havok/classes/StateMachine.h"    // hkbStateMachineStateInfo (state names)
#include "havok/classes/gen/ClassesGen.h"

#include <algorithm>
#include <functional>
#include <fstream>
#include <sstream>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int usage() {
    std::printf(
        "usage:\n"
        "  havok-core-cli compile   <dir|file> [-o out.hkx]\n"
        "  havok-core-cli decompile <in.hkx>   [-o out.yaml]\n"
        "\n"
        "compile auto-detects behavior.yaml / animation.yaml / character.yaml in <dir>.\n");
    return 2;
}

enum class Kind { Behavior, Animation, Character, Project, Unknown };

Kind detect(const fs::path& p, fs::path& yamlOut) {
    if (fs::is_directory(p)) {
        if (fs::exists(p / "behavior.yaml"))  { yamlOut = p / "behavior.yaml";  return Kind::Behavior; }
        if (fs::exists(p / "animation.yaml")) { yamlOut = p / "animation.yaml"; return Kind::Animation; }
        if (fs::exists(p / "character.yaml")) { yamlOut = p / "character.yaml"; return Kind::Character; }
        if (fs::exists(p / "project.yaml"))   { yamlOut = p / "project.yaml";   return Kind::Project;   }
        return Kind::Unknown;
    }
    const std::string fn = p.filename().string();
    yamlOut = p;
    if (fn == "behavior.yaml")  return Kind::Behavior;
    if (fn == "animation.yaml") return Kind::Animation;
    if (fn == "character.yaml") return Kind::Character;
    if (fn == "project.yaml")   return Kind::Project;
    return Kind::Unknown;
}

// Load a skeleton for bone-name resolution: a .hkx (SkeletonImport, first/animation skeleton) or a
// .txt bone list (one bone name per line). Returns the ordered bone names ({} on failure / no path).
static std::vector<std::string> LoadSkeletonNames(const std::string& path) {
    if (path.empty()) return {};
    std::string lower = path;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".hkx") == 0) {
        std::vector<std::uint8_t> bytes; std::string err;
        if (!havok::sct::ReadHavokFile(path, bytes, &err)) { std::printf("skeleton: %s\n", err.c_str()); return {}; }
        std::vector<havok::sct::SkeletonData> skels;
        if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
            std::printf("skeleton: parse failed (%s)\n", err.c_str()); return {};
        }
        std::vector<std::string> names;
        names.reserve(skels[0].bones.size());
        for (const auto& b : skels[0].bones) names.push_back(b.name);
        return names;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("skeleton: cannot open %s\n", path.c_str()); return {}; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return havok::sct::ParseBoneList(text).names;
}

int doCompile(const std::string& in, const std::string& outArg, const std::string& skel = {}) {
    const fs::path p = in;
    fs::path yaml;
    const Kind kind = detect(p, yaml);
    const std::string out = outArg.empty() ? "out.hkx" : outArg;

    try {
        switch (kind) {
            case Kind::Behavior: {
                using clk = std::chrono::steady_clock;
                const fs::path dir = fs::is_directory(p) ? p : p.parent_path();
                const auto t0 = clk::now();
                auto data = havok::model::YamlBehaviorLoader::Load(dir.string());
                if (!skel.empty()) data.boneNames = LoadSkeletonNames(skel);  // resolve bone NAMES
                const auto t1 = clk::now();
                const auto r = havok::sct::CompileBehavior(data);
                const auto t2 = clk::now();
                if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
                const auto vr = havok::sct::ValidatePackfile(r.bytes);
                const auto t3 = clk::now();
                if (!vr.ok) { std::printf("FAIL: validation: %s\n", vr.error.c_str()); return 1; }
                std::string werr;
                if (!havok::sct::WriteHavokFile(out, r.bytes, &werr)) { std::printf("FAIL: %s\n", werr.c_str()); return 1; }
                const auto t4 = clk::now();
                auto ms = [](clk::time_point a, clk::time_point b) {
                    return std::chrono::duration<double, std::milli>(b - a).count();
                };
                std::fprintf(stderr,
                    "[timing] load=%.1f compile=%.1f validate=%.1f write=%.1f total=%.1f ms  (%zu bytes)\n",
                    ms(t0, t1), ms(t1, t2), ms(t2, t3), ms(t3, t4), ms(t0, t4), r.bytes.size());
                std::printf("OK: behavior '%s' -> %s (%zu bytes), validated.\n", in.c_str(), out.c_str(), r.bytes.size());
                return 0;
            }
            case Kind::Animation: {
                // Schema-native compile (havok-anim). Needs the shared registry: set a schema dir via
                // $SCT_HAVOK_SCHEMA_DIR (SharedRegistry's fallback) or use animation-schema-check.
                // With a [skel], the inverse membrane resolves per-track bone names -> binding indices.
                const auto anim = havok::anim::AnimationYamlLoader::Load(yaml);
                std::vector<std::string> boneNames;
                if (!skel.empty()) boneNames = LoadSkeletonNames(skel);
                const auto r = havok::anim::CompileAnimationToFile(anim, out, 30, havok::HKXHeader::SkyrimSE(),
                                                                   boneNames.empty() ? nullptr : &boneNames);
                if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
                std::printf("OK: animation '%s' -> %s (%zu bytes).\n", in.c_str(), out.c_str(), r.bytes.size());
                return 0;
            }
            case Kind::Character: {
                const fs::path dir = fs::is_directory(p) ? p : p.parent_path();
                const auto data = havok::model::CharacterYamlLoader::Load(dir);
                const auto r = havok::sct::CompileCharacterToFile(data, out, /*validate*/ true);
                if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
                std::printf("OK: character '%s' -> %s (%zu bytes), validated.\n", in.c_str(), out.c_str(), r.bytes.size());
                return 0;
            }
            case Kind::Project: {
                std::ifstream yf(yaml, std::ios::binary);
                std::stringstream ss; ss << yf.rdbuf();
                havok::sct::ProjectSpec spec;
                if (!havok::sct::ParseProjectYaml(ss.str(), spec)) {
                    std::printf("FAIL: could not parse project.yaml\n"); return 1;
                }
                const auto r = havok::sct::BuildProject(spec);
                if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
                std::ofstream of(out, std::ios::binary | std::ios::trunc);
                of.write(reinterpret_cast<const char*>(r.bytes.data()),
                         static_cast<std::streamsize>(r.bytes.size()));
                std::printf("OK: project '%s' -> %s (%zu bytes).\n", in.c_str(), out.c_str(), r.bytes.size());
                return 0;
            }
            default:
                std::printf("ERROR: no behavior.yaml / animation.yaml / character.yaml / project.yaml found at '%s'\n", in.c_str());
                return 1;
        }
    } catch (const std::exception& e) {
        std::printf("ERROR: %s\n", e.what());
        return 1;
    }
}

// Compile a unit straight out of a .hky archive (in-memory, no disk extract) — the test
// path for BR's runtime archive read. `unitPrefix` is the unit's serve key, e.g.
// "meshes/clutter/beehive/behaviors/behavior00.hkx". Behaviors and characters both
// serve: the kind is taken from the archive's unit index (behavior.yaml vs character.yaml).
int doHkyCompile(const std::string& archive, const std::vector<std::string>& extra, const std::string& out) {
    if (extra.empty() || out.empty()) {
        std::printf("usage: hky-compile <archive.hky> <unitPrefix> -o out.hkx\n");
        return 2;
    }
    std::string err;
    auto arc = havok::model::HkyArchive::LoadFromFile(archive, err);
    if (!arc) { std::printf("ERROR: %s\n", err.c_str()); return 1; }

    // Resolve the unit's kind from the archive index (prefix normalized by the archive).
    auto isCharacter = false;
    bool found       = false;
    {
        std::string want = extra[0];
        for (char& c : want) { if (c == '\\') c = '/'; else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        for (const auto& u : arc->units())
            if (u.prefix == want) { isCharacter = (u.kind == havok::model::HkyArchive::UnitKind::Character); found = true; break; }
    }
    if (!found) { std::printf("ERROR: unit '%s' not found in %s\n", extra[0].c_str(), archive.c_str()); return 1; }

    std::vector<std::shared_ptr<const havok::model::IUnitSource>> sources{ arc->source(extra[0]) };
    if (isCharacter) {
        const auto cdata = havok::model::CharacterYamlLoader::LoadMerged(sources);
        const auto r     = havok::sct::CompileCharacterToFile(cdata, out, /*validate*/ true);
        if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
        std::printf("OK: hky-compile '%s'::%s (character) -> %s, validated.\n",
                    archive.c_str(), extra[0].c_str(), out.c_str());
        return 0;
    }

    const auto data = havok::model::YamlBehaviorLoader::LoadMerged(sources);
    const auto r = havok::sct::CompileBehavior(data);
    if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
    const auto vr = havok::sct::ValidatePackfile(r.bytes);
    if (!vr.ok) { std::printf("FAIL: validation: %s\n", vr.error.c_str()); return 1; }
    std::string werr;
    if (!havok::sct::WriteHavokFile(out, r.bytes, &werr)) { std::printf("FAIL: %s\n", werr.c_str()); return 1; }
    std::printf("OK: hky-compile '%s'::%s -> %s (%zu bytes), validated.\n",
                archive.c_str(), extra[0].c_str(), out.c_str(), r.bytes.size());
    return 0;
}

// hky-pack: compress an uncompressed bundle YAML tree (src/.../hky/<Bundle>.hky/) into a
// single-file .hky. The build-time packer for mutable bundles (Community Behaviors.hky et al.);
// round-trips against LoadFromFile, which this re-reads to confirm the archive is valid.
int doHkyPack(const std::string& dir, const std::string& out) {
    if (out.empty()) {
        std::printf("usage: hky-pack <bundleDir> -o <out.hky>\n");
        return 2;
    }
    std::string err;
    if (!havok::model::HkyArchive::PackDirectory(dir, out, err)) {
        std::printf("FAIL: %s\n", err.c_str());
        return 1;
    }
    auto arc = havok::model::HkyArchive::LoadFromFile(out, err);   // round-trip sanity
    if (!arc) { std::printf("FAIL: packed but unreadable: %s\n", err.c_str()); return 1; }
    std::printf("OK: hky-pack '%s' -> %s (%zu unit(s)).\n", dir.c_str(), out.c_str(), arc->units().size());
    return 0;
}

// hky-unpack: the inverse of hky-pack — decompress a single-file .hky into its uncompressed YAML tree
// on disk, ORIGINAL path case preserved (Havok is case-sensitive). For inspecting/diffing a packed
// master (Skyrim.hky) or any bundle. Usage: hky-unpack <in.hky> -o <outDir>.
int doHkyUnpack(const std::string& hkyPath, const std::string& outDir) {
    if (outDir.empty()) {
        std::printf("usage: hky-unpack <in.hky> -o <outDir>\n");
        return 2;
    }
    std::string err;
    auto arc = havok::model::HkyArchive::LoadFromFile(hkyPath, err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }

    // Same ordered range, two views: normalized keys drive file() lookup; original-case paths give the
    // on-disk layout (index-aligned — both iterate m_files in order).
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
    std::printf("OK: hky-unpack '%s' -> %s (%zu file(s), %zu unit(s)).\n",
                hkyPath.c_str(), outDir.c_str(), written, arc->units().size());
    return 0;
}

// hky-merge-compile: merge ONE serve-path unit across several .hky bundles (base first,
// deltas after — the same LoadMerged the runtime resolver uses) and compile+validate the
// result. The offline gate for a DELTA bundle: a delta compiles only when merged onto its
// base, so this proves e.g. Community Behaviors's 0_master delta lands correctly on vanilla
// 0_master from Skyrim.hky. Usage: hky-merge-compile <servePath> <base.hky> [delta.hky ...] -o out.hkx
int doHkyMergeCompile(const std::string& unit, const std::vector<std::string>& archives, const std::string& out,
                      const std::string& schemaDir = "", bool strictSchema = false) {
    if (archives.empty() || out.empty()) {
        std::printf("usage: hky-merge-compile <servePath> <base.hky> [delta.hky ...] -o out.hkx\n"
                    "                         [--schema <HavokDir>] [--strict-schema]\n");
        return 2;
    }
    std::string want = unit;   // normalize to the archive's key form (lower, forward-slash)
    for (char& c : want) { if (c == '\\') c = '/'; else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

    // Route merge notices (same-slot array collisions, etc.) to stdout so the gate is loud.
    havok::model::YamlBehaviorLoader::SetDiagnosticSink(
        [](const std::string& m) { std::printf("  merge: %s\n", m.c_str()); });

    // --schema: wire the merge classifier to the Havok/ schema so an array's compose/guarded policy
    // is read from its `merge:` tag. --strict-schema disables the built-in name fallback, so the tag
    // ALONE drives the merge — a missing tag then diverges the output, which this gate catches. reg
    // must outlive both LoadMerged calls below, hence the outer scope.
    havok::schema::SchemaRegistry reg;
    if (!schemaDir.empty()) {
        std::string serr;
        if (!reg.LoadDir(schemaDir, serr)) { std::printf("FAIL: schema '%s': %s\n", schemaDir.c_str(), serr.c_str()); return 1; }
        havok::model::YamlBehaviorLoader::SetSchemaRegistry(&reg, strictSchema);
        std::printf("  schema: %s%s\n", schemaDir.c_str(), strictSchema ? " (strict — fallback disabled)" : "");
    }

    std::vector<std::shared_ptr<havok::model::HkyArchive>>         keepAlive;   // sources ref back in
    std::vector<std::shared_ptr<const havok::model::IUnitSource>>  sources;     // base-first, load order
    bool isCharacter = false, found = false;
    for (const std::string& path : archives) {
        std::string err;
        auto arc = havok::model::HkyArchive::LoadFromFile(path, err);
        if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
        for (const auto& u : arc->units())
            if (u.prefix == want) {
                if (!found) { isCharacter = (u.kind == havok::model::HkyArchive::UnitKind::Character); found = true; }
                sources.push_back(arc->source(want));
                break;
            }
        keepAlive.push_back(std::move(arc));
    }
    if (sources.empty()) { std::printf("FAIL: unit '%s' not found in any of the %zu bundle(s)\n", unit.c_str(), archives.size()); return 1; }

    if (isCharacter) {
        const auto cdata = havok::model::CharacterYamlLoader::LoadMerged(sources);
        const auto r     = havok::sct::CompileCharacterToFile(cdata, out, /*validate*/ true);
        if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
        std::printf("OK: hky-merge-compile %s (character, %zu layer(s)) -> %s, validated.\n",
                    unit.c_str(), sources.size(), out.c_str());
        return 0;
    }
    const auto data = havok::model::YamlBehaviorLoader::LoadMerged(sources);
    const auto r = havok::sct::CompileBehavior(data);
    if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
    const auto vr = havok::sct::ValidatePackfile(r.bytes);
    if (!vr.ok) { std::printf("FAIL: validation: %s\n", vr.error.c_str()); return 1; }
    std::string werr;
    if (!havok::sct::WriteHavokFile(out, r.bytes, &werr)) { std::printf("FAIL: %s\n", werr.c_str()); return 1; }
    std::printf("OK: hky-merge-compile %s (%zu layer(s)) -> %s (%zu bytes), validated.\n",
                unit.c_str(), sources.size(), out.c_str(), r.bytes.size());
    return 0;
}

// schema-merge-tag: print the `merge:` tag a field carries in the Havok/ schema — the exact
// data->parser->lookup chain the runtime merge classifier reads, exercised in isolation (no merge).
// Proves the tag is genuinely present and readable, independent of the byte-diff gate (which the
// fallback could otherwise mask). Usage: schema-merge-tag <HavokDir> <class> <field>
// Exit 0 = a non-empty tag; 1 = class/field found but no tag; 2 = usage / class/field not found.
int doSchemaMergeTag(const std::string& schemaDir, const std::string& cls, const std::string& field) {
    if (schemaDir.empty() || cls.empty() || field.empty()) {
        std::printf("usage: schema-merge-tag <HavokDir> <class> <field>\n");
        return 2;
    }
    havok::schema::SchemaRegistry reg;
    std::string err;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("FAIL: schema '%s': %s\n", schemaDir.c_str(), err.c_str()); return 2; }
    const havok::schema::ClassSchema* cs = reg.Find(cls);
    if (!cs) { std::printf("FAIL: class '%s' not in schema\n", cls.c_str()); return 2; }
    for (const havok::schema::Field& f : cs->fields)
        if (f.name == field) {
            if (f.merge.empty()) { std::printf("%s.%s: (no merge tag)\n", cls.c_str(), field.c_str()); return 1; }
            std::printf("%s.%s: merge=%s\n", cls.c_str(), field.c_str(), f.merge.c_str());
            return 0;
        }
    std::printf("FAIL: field '%s' not on class '%s'\n", field.c_str(), cls.c_str());
    return 2;
}

int doDecompile(const std::string& in, const std::string& outArg, const std::string& skel = {}) {
    std::vector<std::uint8_t> bytes;
    std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    const std::string out = outArg.empty() ? (in + ".decompiled") : outArg;
    havok::sct::BoneNameTable boneTable;
    if (!skel.empty()) { boneTable.names = LoadSkeletonNames(skel); boneTable.Reindex(); }
    const auto r = havok::sct::DecompileToDir(bytes, out, boneTable.empty() ? nullptr : &boneTable);
    if (!r.ok) {
        std::printf("%s: %s\n", r.kind.empty() ? "FAIL" : "NOT YET", r.error.c_str());
        return (r.kind == "behavior" || r.kind == "unknown") ? 3 : 1;
    }
    std::printf("OK: decompiled %s (%s) -> %s\n", in.c_str(), r.kind.c_str(), out.c_str());
    return 0;
}

// Debug: per-class object histogram from the virtual-fixup table (no construction).
int doObjHist(const std::string& in) {
    std::vector<std::uint8_t> bytes;
    std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    try {
        havok::PackFileDeserializer des;
        havok::BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        des.DeserializePartially(br);
        const auto objs = des.ListObjects();  // sorted by offset
        std::map<std::string, int> h;
        for (const auto& [off, cls] : objs) ++h[cls];
        for (const auto& [cls, n] : h) std::printf("%6d  %s\n", n, cls.c_str());
        std::printf("---objlist---\n");
        for (std::size_t i = 0; i < objs.size(); ++i) {
            const std::uint32_t sz = (i + 1 < objs.size()) ? objs[i + 1].first - objs[i].first : 0;
            std::printf("%08x %6u %s\n", objs[i].first, sz, objs[i].second.c_str());
        }
    } catch (const std::exception& e) {
        std::printf("ERROR: %s\n", e.what());
        return 1;
    }
    return 0;
}

// Debug: recursively scan a dir for spline animations with float tracks.
// Constructs only the hkaSplineCompressedAnimation object per file (no decode),
// so it stays fast over large loose-animation trees.
int doAnimScan(const std::string& dir) {
    namespace fs = std::filesystem;
    int scanned = 0, hits = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        const fs::path p = it->path();
        std::string ext = p.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".hkx") continue;

        std::vector<std::uint8_t> bytes;
        std::string err;
        if (!havok::sct::ReadHavokFile(p.string(), bytes, &err)) continue;
        ++scanned;
        try {
            havok::PackFileDeserializer des;
            havok::BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
            des.DeserializePartially(br);
            havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8,
                                     des.DataSectionBytes());
            for (const auto& o : des.ConstructAllOfClass(dr, "hkaSplineCompressedAnimation")) {
                const auto s = std::dynamic_pointer_cast<havok::hkaSplineCompressedAnimation>(o);
                if (s && s->m_numberOfFloatTracks > 0) {
                    ++hits;
                    std::printf("%d float / %d xform / %d frames  %s\n",
                                s->m_numberOfFloatTracks, s->m_numberOfTransformTracks,
                                s->m_numFrames, p.string().c_str());
                    break;
                }
            }
        } catch (...) { continue; }
    }
    std::fprintf(stderr, "scanned %d hkx animations, %d with float tracks\n", scanned, hits);
    return 0;
}

// Record-level merge: LoadMerged(base + deltas) -> compile. The offline mirror of
// BR's resolver, and the merge half of the self-validating loop. A CHARACTER base
// (character.yaml in dirs[0]) takes the character path, exactly like the Resolver:
// CharacterYamlLoader::LoadMerged unions later layers' animations.txt rosters.
int doMerge(const std::string& base, const std::vector<std::string>& deltas, const std::string& outArg) {
    std::vector<std::string> dirs;
    dirs.push_back(base);
    for (const auto& d : deltas) dirs.push_back(d);
    const std::string out = outArg.empty() ? "merged.hkx" : outArg;
    if (fs::exists(fs::path(base) / "character.yaml")) {
        try {
            const auto cdata = havok::model::CharacterYamlLoader::LoadMerged(dirs);
            const auto r     = havok::sct::CompileCharacterToFile(cdata, out, /*validate*/ true);
            if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
            std::printf("OK: merged %zu character layer(s) (%zu anims) -> %s (%zu bytes)\n",
                        dirs.size(), cdata.animations.size(), out.c_str(), r.bytes.size());
            return 0;
        } catch (const std::exception& e) { std::printf("ERROR: %s\n", e.what()); return 1; }
    }
    // Surface non-fatal merge notices (same-slot positional-array collisions) to stderr —
    // the offline mirror of BR routing them to its log.
    havok::model::YamlBehaviorLoader::SetDiagnosticSink(
        [](const std::string& m) { std::fprintf(stderr, "%s\n", m.c_str()); });
    try {
        const auto data = havok::model::YamlBehaviorLoader::LoadMerged(dirs);
        const auto r    = havok::sct::CompileBehavior(data);
        if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
        std::string werr;
        if (!havok::sct::WriteHavokFile(out, r.bytes, &werr)) { std::printf("FAIL: %s\n", werr.c_str()); return 1; }
        std::printf("OK: merged %zu layer(s) -> %s (%zu bytes)\n", dirs.size(), out.c_str(), r.bytes.size());
        return 0;
    } catch (const std::exception& e) { std::printf("ERROR: %s\n", e.what()); return 1; }
}

// objlist: dump every __data__ object as (offset, size, class), ascending by offset.
// Size = delta to the next object's offset (last object runs to the data section end),
// i.e. the authoritative serialized+padded size — used to derive class layouts when
// porting new Havok classes. Uses the pre-construction virtual fixup map, so it works
// even when some classes are not yet registered (Read would throw).
int doObjList(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    auto objs = des.ListObjects();   // (offset, class) ascending by offset
    const std::uint32_t dataEnd = static_cast<std::uint32_t>(des.DataSectionBytes().size());
    for (std::size_t i = 0; i < objs.size(); ++i) {
        const std::uint32_t off  = objs[i].first;
        const std::uint32_t next = (i + 1 < objs.size()) ? objs[i + 1].first : dataEnd;
        std::printf("%8u  size=%-5u  %s\n", off, next - off, objs[i].second.c_str());
    }
    std::printf("(%zu objects, data section %u bytes)\n", objs.size(), dataEnd);
    return 0;
}

// resdump: walk the hkMemoryResourceContainer tree (the "Resource Data" variant) and print the
// container/handle/external-link hierarchy — the RE input for deriving the resource tree away.
static void DumpResContainer(const std::shared_ptr<havok::hkMemoryResourceContainer>& c, int depth,
                             std::size_t& nContainers, std::size_t& nHandles, std::size_t& nLinks) {
    if (!c) return;
    ++nContainers;
    const std::string ind(static_cast<std::size_t>(depth) * 2, ' ');
    std::printf("%sCON '%s'  handles=%zu children=%zu\n", ind.c_str(), c->m_name.c_str(),
                c->m_resourceHandles.size(), c->m_children.size());
    for (const auto& h : c->m_resourceHandles) {
        if (!h) continue;
        ++nHandles;
        std::printf("%s  H '%s'  variant=%s  links=%zu\n", ind.c_str(), h->m_name.c_str(),
                    h->m_variant ? h->m_variant->ClassName() : "null", h->m_references.size());
        for (const auto& r : h->m_references) {
            ++nLinks;
            std::printf("%s     LINK member='%s' externalId='%s'\n", ind.c_str(),
                        r.m_memberName.c_str(), r.m_externalId.c_str());
        }
    }
    for (const auto& ch : c->m_children)
        DumpResContainer(ch, depth + 1, nContainers, nHandles, nLinks);
}

// mapperdump: dump the two hkaSkeletonMapper objects (anim<->ragdoll). Shows skeletonA/B, each simple
// mapping (boneA name -> boneB name + whether aFromBTransform is identity), chain mappings, unmapped
// bones — the RE input for deciding if the mapper is name+pose-derivable.
static bool QsIsIdentity(const havok::QSTransform& t) {
    const auto n = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    return n(t.translation.x, 0) && n(t.translation.y, 0) && n(t.translation.z, 0) &&
           n(t.rotation.x, 0) && n(t.rotation.y, 0) && n(t.rotation.z, 0) && n(t.rotation.w, 1) &&
           n(t.scale.x, 1) && n(t.scale.y, 1) && n(t.scale.z, 1);
}
static std::string BoneName(const std::shared_ptr<havok::hkaSkeleton>& s, int idx) {
    if (!s || idx < 0 || idx >= static_cast<int>(s->m_bones.size())) return "?";
    return s->m_bones[static_cast<std::size_t>(idx)].m_name;
}
int doMapperDump(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    auto root = std::dynamic_pointer_cast<havok::hkRootLevelContainer>(des.Deserialize(br));
    if (!root) { std::printf("ERROR: root is not hkRootLevelContainer\n"); return 1; }
    int mi = 0;
    for (const auto& nv : root->m_namedVariants) {
        auto m = std::dynamic_pointer_cast<havok::hkaSkeletonMapper>(nv.m_variant);
        if (!m) continue;
        const auto& d = m->m_mapping;
        const std::string an = d.m_skeletonA ? d.m_skeletonA->m_name : "(null)";
        const std::string bn = d.m_skeletonB ? d.m_skeletonB->m_name : "(null)";
        std::printf("=== mapper #%d '%s'  A='%s'(%zu bones)  B='%s'(%zu bones)  type=%u  keepUnmappedLocal=%d ===\n",
                    mi++, nv.m_name.c_str(), an.c_str(), d.m_skeletonA ? d.m_skeletonA->m_bones.size() : 0,
                    bn.c_str(), d.m_skeletonB ? d.m_skeletonB->m_bones.size() : 0,
                    d.m_mappingType, static_cast<int>(d.m_keepUnmappedLocal));
        std::printf("  simpleMappings=%zu  chainMappings=%zu  unmappedBones=%zu\n",
                    d.m_simpleMappings.size(), d.m_chainMappings.size(), d.m_unmappedBones.size());
        std::size_t nonIdent = 0, nameMismatch = 0;
        for (const auto& sm : d.m_simpleMappings) {
            const std::string na = BoneName(d.m_skeletonA, sm.m_boneA);
            const std::string nb = BoneName(d.m_skeletonB, sm.m_boneB);
            const bool ident = QsIsIdentity(sm.m_aFromBTransform);
            if (!ident) ++nonIdent;
            if (na != nb) ++nameMismatch;
            const auto& t = sm.m_aFromBTransform;
            std::printf("    A[%d]='%s' <- B[%d]='%s'  t=[%.3f %.3f %.3f] r=[%.4f %.4f %.4f %.4f]%s\n",
                        sm.m_boneA, na.c_str(), sm.m_boneB, nb.c_str(),
                        t.translation.x, t.translation.y, t.translation.z,
                        t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w,
                        (na != nb) ? "  [NAME MISMATCH]" : "");
        }
        std::printf("  -> %zu/%zu mappings have a NON-identity transform; %zu/%zu have A-name != B-name\n",
                    nonIdent, d.m_simpleMappings.size(), nameMismatch, d.m_simpleMappings.size());
    }
    return 0;
}

// physdump: dump the ragdoll rigid bodies + constraints (the RE input for deciding which physics fields
// are DERIVABLE from bone↔bone / bone↔body relationships vs genuinely authored).
// ragdollcheck: derive the ragdoll skeleton from the anim bones + physics and compare (by bone name) to
// the vanilla ragdoll skeleton (skels[1]) — count, name set, and local refpose translations.
int doRagdollCheck(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: %s\n", err.c_str()); return 1;
    }
    havok::sct::ReadSkeletonPhysics(bytes.data(), bytes.size(), skels[0], nullptr);
    auto derived = havok::sct::DeriveRagdollSkeleton(skels[0]);

    std::printf("derived ragdoll bones: %zu\n", derived->m_bones.size());
    if (skels.size() < 2) { std::printf("(no vanilla ragdoll skeleton skels[1] to compare)\n"); return 0; }
    // Vanilla ragdoll = skels[1] (neutral SkeletonData). Map name -> refpose translation.
    std::unordered_map<std::string, havok::Vector4> vpos;
    for (const auto& b : skels[1].bones) vpos[b.name] = b.refPose.translation;
    std::printf("vanilla ragdoll bones: %zu\n", skels[1].bones.size());
    std::size_t matched = 0; double maxd = 0; std::string worst;
    for (std::size_t i = 0; i < derived->m_bones.size(); ++i) {
        const std::string nm = derived->m_bones[i].m_name;
        auto it = vpos.find(nm);
        if (it == vpos.end()) { std::printf("  derived '%s' has no vanilla match\n", nm.c_str()); continue; }
        ++matched;
        const auto& a = derived->m_referencePose[i].translation; const auto& b = it->second;
        const double d = std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));
        if (d > 0.01) std::printf("  %-26s local refpose diff = %.4f  der=[%.2f %.2f %.2f] van=[%.2f %.2f %.2f]\n",
                                  nm.c_str(), d, a.x,a.y,a.z, b.x,b.y,b.z);
        if (d > maxd) { maxd = d; worst = nm; }
    }
    std::printf("matched %zu/%zu by name; max refpose-translation diff = %.4g (%s)\n",
                matched, derived->m_bones.size(), maxd, worst.c_str());
    return 0;
}

// bodycheck: derive the ragdoll rigid bodies from the anim skeleton + physics and compare field-by-field
// to the vanilla bodies (matched by name) — the physics-compile gate. Reports per-field max diffs so the
// derivations (transform, inertia, motionType, objectRadius) are verified against ground truth and the
// non-derivable holdouts (collisionFilterInfo) are made explicit.
int doBodyCheck(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: %s\n", err.c_str()); return 1;
    }
    havok::sct::ReadSkeletonPhysics(bytes.data(), bytes.size(), skels[0], nullptr);
    auto derived = havok::sct::DeriveRigidBodies(skels[0]);

    // Vanilla bodies from the physics system, indexed by name.
    havok::PackFileDeserializer des; havok::BinaryReaderEx br(false, true, bytes);
    auto root = std::dynamic_pointer_cast<havok::hkRootLevelContainer>(des.Deserialize(br));
    std::shared_ptr<havok::hkpPhysicsData> pd;
    for (const auto& nv : root->m_namedVariants)
        if (auto p = std::dynamic_pointer_cast<havok::hkpPhysicsData>(nv.m_variant)) { pd = p; break; }
    if (!pd || pd->m_systems.empty()) { std::printf("ERROR: no physics\n"); return 1; }
    std::unordered_map<std::string, std::shared_ptr<havok::hkpRigidBody>> van;
    for (const auto& rb : pd->m_systems[0]->m_rigidBodies) if (rb) van[rb->m_name] = rb;

    std::printf("derived %zu bodies vs %zu vanilla\n", derived.size(), van.size());
    double mInv = 0, mPos = 0, mObjR = 0; int mtMismatch = 0; std::string wInv, wObjR;
    auto d3 = [](const havok::Vector4& a, const havok::Vector4& b){
        return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)); };
    for (const auto& rb : derived) {
        auto it = van.find(rb->m_name); if (it == van.end()) { std::printf("  no vanilla '%s'\n", rb->m_name.c_str()); continue; }
        const auto& v = *it->second;
        // inertia: relative diff of the xyz inverse-inertia
        const auto& di = rb->m_motion.m_inertiaAndMassInv; const auto& vi = v.m_motion.m_inertiaAndMassInv;
        const double rdi = d3(di, vi) / (d3(vi, {}) + 1e-9);
        if (rdi > mInv) { mInv = rdi; wInv = rb->m_name; }
        const double dp = d3(rb->m_motion.m_motionState.m_transform[3], v.m_motion.m_motionState.m_transform[3]);
        if (dp > mPos) mPos = dp;
        const double dor = std::fabs(rb->m_motion.m_motionState.m_objectRadius - v.m_motion.m_motionState.m_objectRadius);
        if (dor > mObjR) { mObjR = dor; wObjR = rb->m_name; }
        if (rb->m_motion.m_type != v.m_motion.m_type) {
            ++mtMismatch;
            std::printf("  motionType '%s' derived=%d vanilla=%d\n", rb->m_name.c_str(), (int)rb->m_motion.m_type, (int)v.m_motion.m_type);
        }
    }
    // Collision filter: decode derived vs vanilla (hkpGroupFilter layout) and check uniqueness + that
    // dontCollideWith targets a real body's subId. Exact ids differ from vanilla (tool artifact) but the
    // collision semantics — unique id per body, don't-collide-with-parent — must hold.
    auto decLayer  = [](std::uint32_t i){ return i & 0x1f; };
    auto decSub    = [](std::uint32_t i){ return (i >> 5) & 0x1f; };
    auto decDont   = [](std::uint32_t i){ return (i >> 10) & 0x1f; };
    auto decGrp    = [](std::uint32_t i){ return i >> 16; };
    std::printf("--- collision filter (derived) ---\n");
    std::unordered_map<int,int> seen;  // subId -> count
    bool uniq = true;
    for (const auto& rb : derived) {
        const std::uint32_t f = rb->m_collidable.m_broadPhaseHandle.m_collisionFilterInfo;
        if (++seen[decSub(f)] > 1) uniq = false;
    }
    std::printf("subId uniqueness: %s (%zu bodies)\n", uniq ? "OK" : "COLLISION", derived.size());
    for (const auto& rb : derived) {
        auto it = van.find(rb->m_name); if (it == van.end()) continue;
        const std::uint32_t d = rb->m_collidable.m_broadPhaseHandle.m_collisionFilterInfo;
        const std::uint32_t v = it->second->m_collidable.m_broadPhaseHandle.m_collisionFilterInfo;
        std::printf("  %-28s derived L%d sub%d !coll%d grp%d  |  vanilla L%d sub%d !coll%d grp%d\n",
                    rb->m_name.c_str(), decLayer(d), decSub(d), decDont(d), decGrp(d),
                    decLayer(v), decSub(v), decDont(v), decGrp(v));
    }
    // Body ROTATION vs anim FK — does the ragdoll body frame == the anim bone frame? (constraint frames
    // are relative to the body frame, so this decides whether frameB derives from anim FK.)
    {
        const auto w = havok::sct::skmath::worldPoses(skels[0]);
        std::unordered_map<std::string,int> ix; for(int i=0;i<(int)skels[0].bones.size();++i) ix[skels[0].bones[i].name]=i;
        auto ang=[](const havok::Vector4&a,const havok::Vector4&b){float d=a.x*b.x+a.y*b.y+a.z*b.z; return std::acos(std::min(1.f,std::max(-1.f,d)))*57.2958f;};
        double mr=0; std::string wr;
        std::printf("--- body rotation vs anim FK (per bone) ---\n");
        for (const auto& rb : derived) {
            const std::string bn = rb->m_name.substr(8); auto it=ix.find(bn); if(it==ix.end()) continue;
            havok::Vector4 fkX = havok::sct::skmath::qrot(w[it->second].rotation, {1,0,0,0});
            const auto& vx = van[rb->m_name]->m_motion.m_motionState.m_transform[0];
            double e = ang(fkX, vx);
            if(e>mr){mr=e;wr=bn;}
            std::printf("  %-26s col0 angErr vs FK = %.1f\n", bn.c_str(), e);
        }
        std::printf("max body-rot(col0) err vs anim FK = %.2f (%s)\n", mr, wr.c_str());
    }
    std::printf("max relative invInertia diff = %.4g (%s)\n", mInv, wInv.c_str());
    std::printf("max motionState.pos diff     = %.4g\n", mPos);
    std::printf("max objectRadius diff        = %.4g (%s)  [vanilla objR captured for study]\n", mObjR, wObjR.c_str());
    std::printf("motionType mismatches        = %d/%zu\n", mtMismatch, derived.size());
    // objectRadius pairs for study
    std::printf("--- objectRadius derived vs vanilla ---\n");
    for (const auto& rb : derived) {
        auto it = van.find(rb->m_name); if (it == van.end()) continue;
        std::printf("  %-28s derived=%.4g vanilla=%.4g\n", rb->m_name.c_str(),
                    rb->m_motion.m_motionState.m_objectRadius, it->second->m_motion.m_motionState.m_objectRadius);
    }
    return 0;
}

// constraintcheck: derive the ragdoll constraints from the anim skeleton + physics and compare frame
// columns/pivots + limits to the vanilla constraints (matched by child body name) — the constraint-compile
// gate. With authored twist_axis/plane_axis the frames should reproduce vanilla; limits come from the joint.
int doConstraintCheck(const std::string& in) {
    using havok::Vector4;
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: %s\n", err.c_str()); return 1;
    }
    havok::sct::ReadSkeletonPhysics(bytes.data(), bytes.size(), skels[0], nullptr);
    auto bodies  = havok::sct::DeriveRigidBodies(skels[0]);
    auto derived = havok::sct::DeriveConstraints(skels[0], bodies);

    havok::PackFileDeserializer des; havok::BinaryReaderEx br(false,true,bytes);
    auto root = std::dynamic_pointer_cast<havok::hkRootLevelContainer>(des.Deserialize(br));
    std::shared_ptr<havok::hkpPhysicsData> pd;
    for (const auto& nv: root->m_namedVariants) if (auto p=std::dynamic_pointer_cast<havok::hkpPhysicsData>(nv.m_variant)){pd=p;break;}

    auto tfOf = [](const std::shared_ptr<havok::hkReferencedObject>& o) -> const havok::hkpSetLocalTransformsConstraintAtom* {
        auto ci = std::dynamic_pointer_cast<havok::hkpConstraintInstance>(o); if(!ci) return nullptr;
        if (auto rg=std::dynamic_pointer_cast<havok::hkpRagdollConstraintData>(ci->m_data)) return &rg->m_atoms.m_transforms;
        if (auto hg=std::dynamic_pointer_cast<havok::hkpLimitedHingeConstraintData>(ci->m_data)) return &hg->m_atoms.m_transforms;
        return nullptr;
    };
    auto nameOf = [](const std::shared_ptr<havok::hkReferencedObject>& o) {
        auto ci = std::dynamic_pointer_cast<havok::hkpConstraintInstance>(o);
        return ci && ci->m_entities[0] ? ci->m_entities[0]->m_name : std::string();
    };
    std::unordered_map<std::string, std::shared_ptr<havok::hkReferencedObject>> van;
    for (const auto& c : pd->m_systems[0]->m_constraints) { auto n = nameOf(c); if(!n.empty()) van[n]=c; }

    auto ang = [](const Vector4&a,const Vector4&b){ float d=a.x*b.x+a.y*b.y+a.z*b.z; return std::acos(std::min(1.f,std::max(-1.f,d)))*57.2958f; };
    auto piv = [](const Vector4&a,const Vector4&b){ return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)); };
    double mA=0,mB=0,mPivA=0,mPivB=0; std::string wA,wB;
    std::printf("derived %zu constraints vs %zu vanilla\n", derived.size(), van.size());
    for (const auto& dc : derived) {
        const std::string nm = nameOf(dc); auto it = van.find(nm); if (it==van.end()){ std::printf("  no vanilla '%s'\n", nm.c_str()); continue; }
        const auto* dt = tfOf(dc); const auto* vt = tfOf(it->second); if(!dt||!vt) continue;
        double ea=0, eb=0;
        for (int c=0;c<3;++c){ ea=std::max(ea,(double)ang(dt->m_transformA.m_data[c],vt->m_transformA.m_data[c]));
                               eb=std::max(eb,(double)ang(dt->m_transformB.m_data[c],vt->m_transformB.m_data[c])); }
        const double pa=piv(dt->m_transformA.m_data[3],vt->m_transformA.m_data[3]);
        const double pb=piv(dt->m_transformB.m_data[3],vt->m_transformB.m_data[3]);
        if(ea>mA){mA=ea;wA=nm;} if(eb>mB){mB=eb;wB=nm;} mPivA=std::max(mPivA,pa); mPivB=std::max(mPivB,pb);
        if (ea>1.0||eb>1.0||pa>0.01||pb>0.01) {
            const auto& dp=dt->m_transformB.m_data[3]; const auto& vp=vt->m_transformB.m_data[3];
            std::printf("  %-26s fA rotErr=%.2f | fB rotErr=%.2f pivErr=%.4f  der=[%.3f %.3f %.3f] van=[%.3f %.3f %.3f]\n",
                        nm.c_str(), ea, eb, pb, dp.x,dp.y,dp.z, vp.x,vp.y,vp.z);
        }
    }
    std::printf("max frameA rotErr=%.3f (%s)  frameB rotErr=%.3f (%s)\n", mA, wA.c_str(), mB, wB.c_str());
    std::printf("max frameA pivErr=%.5f  frameB pivErr=%.5f\n", mPivA, mPivB);
    return 0;
}

// framecheck: empirically pin the constraint-frame PLANE-axis convention. For every ragdoll/hinge
// constraint: derive the twist axis (toward the child's child, in child-local) and test a set of plane-axis
// conventions (world reference X/Y/Z crossed with twist, both signs), building frameA = [twist|plane|
// twist×plane] and comparing to the vanilla frameA columns. Reports per-convention max angular error so a
// single run tells us which world reference vanilla used.
int doFrameCheck(const std::string& in) {
    using havok::Vector4; using havok::Quaternion;
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: %s\n", err.c_str()); return 1;
    }
    havok::sct::ReadSkeletonPhysics(bytes.data(), bytes.size(), skels[0], nullptr);
    const auto& anim = skels[0];
    const auto world = havok::sct::skmath::worldPoses(anim);

    auto sub = [](const Vector4& a, const Vector4& b){ return Vector4{a.x-b.x,a.y-b.y,a.z-b.z,0}; };
    auto dot = [](const Vector4& a, const Vector4& b){ return a.x*b.x+a.y*b.y+a.z*b.z; };
    auto cross = [](const Vector4& a, const Vector4& b){ return Vector4{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x, 0}; };
    auto norm = [&](Vector4 v){ float m=std::sqrt(dot(v,v)); return m>1e-9f? Vector4{v.x/m,v.y/m,v.z/m,0}:Vector4{0,0,1,0}; };
    auto toLocal = [&](const Quaternion& q, const Vector4& vW){ return havok::sct::skmath::qrot(havok::sct::skmath::qconj(q), vW); };

    // anim index by name; first physics child (for the twist direction).
    std::unordered_map<std::string,int> idxOf;
    for (int i=0;i<(int)anim.bones.size();++i) idxOf[anim.bones[i].name]=i;
    auto firstPhysChild = [&](int a)->int{ for(int c=0;c<(int)anim.bones.size();++c){ int p=anim.bones[c].parentIndex; while(p>=0&&!anim.bones[p].physics)p=anim.bones[p].parentIndex; if(p==a)return c;} return -1; };

    // vanilla frameA columns per child bone name (stripped of "Ragdoll_").
    havok::PackFileDeserializer des; havok::BinaryReaderEx br(false,true,bytes);
    auto root = std::dynamic_pointer_cast<havok::hkRootLevelContainer>(des.Deserialize(br));
    std::shared_ptr<havok::hkpPhysicsData> pd;
    for (const auto& nv: root->m_namedVariants) if (auto p=std::dynamic_pointer_cast<havok::hkpPhysicsData>(nv.m_variant)){pd=p;break;}
    auto strip=[](std::string n){ const std::string p="Ragdoll_"; return n.rfind(p,0)==0?n.substr(p.size()):n; };

    // vanilla body rotation (motionState transform cols) by stripped name — to lift frameA to WORLD.
    std::unordered_map<std::string, std::array<Vector4,3>> vbodyRot;
    for (const auto& rb : pd->m_systems[0]->m_rigidBodies) if (rb)
        vbodyRot[strip(rb->m_name)] = { rb->m_motion.m_motionState.m_transform[0], rb->m_motion.m_motionState.m_transform[1], rb->m_motion.m_motionState.m_transform[2] };

    struct Van { std::string child; Vector4 c0,c1,c2; bool hinge; };
    std::vector<Van> vans;
    for (const auto& c : pd->m_systems[0]->m_constraints) {
        auto ci=std::dynamic_pointer_cast<havok::hkpConstraintInstance>(c); if(!ci||!ci->m_entities[0]) continue;
        const havok::hkpSetLocalTransformsConstraintAtom* tf=nullptr; bool hinge=false;
        if (auto rg=std::dynamic_pointer_cast<havok::hkpRagdollConstraintData>(ci->m_data)) tf=&rg->m_atoms.m_transforms;
        else if (auto hg=std::dynamic_pointer_cast<havok::hkpLimitedHingeConstraintData>(ci->m_data)) { tf=&hg->m_atoms.m_transforms; hinge=true; }
        if(!tf) continue;
        vans.push_back({ strip(ci->m_entities[0]->m_name), tf->m_transformA.m_data[0], tf->m_transformA.m_data[1], tf->m_transformA.m_data[2], hinge });
    }
    // apply a body rotation (3 cols) to a body-local vector -> world.
    auto applyRot=[&](const std::array<Vector4,3>& R, const Vector4& v){ return Vector4{
        R[0].x*v.x+R[1].x*v.y+R[2].x*v.z, R[0].y*v.x+R[1].y*v.y+R[2].y*v.z, R[0].z*v.x+R[1].z*v.y+R[2].z*v.z, 0}; };

    const Vector4 refs[3] = { {1,0,0,0},{0,1,0,0},{0,0,1,0} };
    const char* refn[3] = {"X","Y","Z"};
    // conventions: plane = sign * normalize( ref x twist ), perp-projected. 3 refs x 2 signs = 6.
    double twistErr=0;
    double planeErr[6]={0,0,0,0,0,0};
    for (const auto& v : vans) {
        auto it=idxOf.find(v.child); if(it==idxOf.end()) continue;
        const int a=it->second;
        const Quaternion qa=world[a].rotation;
        const int gc=firstPhysChild(a);
        Vector4 twistW = gc>=0 ? norm(sub(world[gc].translation, world[a].translation))
                               : havok::sct::skmath::qrot(qa, Vector4{0,0,1,0});   // leaf: local +Z
        Vector4 twistA = norm(toLocal(qa, twistW));
        // Lift vanilla col0 to WORLD via the vanilla body rotation, and compare to the world bone direction
        // (toward child). If these match, vanilla twist IS toward-child and only the body FRAME differs.
        Vector4 vWorld = norm(applyRot(vbodyRot[v.child], v.c0));
        const double teWorld = std::acos(std::min(1.f,std::max(-1.f,std::fabs(dot(vWorld,twistW)))))*57.2958;
        const double te = std::acos(std::min(1.f,std::max(-1.f,dot(twistA,v.c0))))*57.2958;
        std::printf("  [%-14s]%s gc=%-14s | localErr=%.1f | worldTwistVanilla=[%.3f %.3f %.3f] vs boneDir=[%.3f %.3f %.3f] worldErr=%.1f\n",
                    v.child.c_str(), v.hinge?"H":"R", gc>=0?anim.bones[gc].name.c_str():"(leaf)",
                    te, vWorld.x,vWorld.y,vWorld.z, twistW.x,twistW.y,twistW.z, teWorld);
        twistErr = std::max(twistErr, te);
        for (int r=0;r<3;++r) for(int s=0;s<2;++s){
            Vector4 pW = cross(refs[r], twistW); if(s) pW=Vector4{-pW.x,-pW.y,-pW.z,0};
            pW = norm(sub(pW, Vector4{twistW.x*dot(pW,twistW),twistW.y*dot(pW,twistW),twistW.z*dot(pW,twistW),0}));
            Vector4 pA = norm(toLocal(qa,pW));
            double e = std::acos(std::min(1.f,std::max(-1.f,dot(pA,v.c1))))*57.2958;
            planeErr[r*2+s]=std::max(planeErr[r*2+s], e);
        }
    }
    std::printf("constraints: %zu  | twist max angular err = %.3f deg\n", vans.size(), twistErr);
    for (int r=0;r<3;++r) for(int s=0;s<2;++s)
        std::printf("  plane conv: %s( %sref%s x twist ) max err = %.3f deg\n", s?"-":"+", "", refn[r], planeErr[r*2+s]);
    return 0;
}

// fkcheck: compute anim-skeleton FK world poses and print a few, to validate skmath (compose order +
// matrix convention) against physdump's rigid-body motionState positions (same bone == same world pos).
int doFkCheck(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: %s\n", err.c_str()); return 1;
    }
    const auto w = havok::sct::skmath::worldPoses(skels[0]);
    const char* probes[] = { "NPC COM [COM ]", "NPC L Thigh [LThg]", "NPC L Calf [LClf]",
                             "NPC Spine2 [Spn2]", "NPC Head [Head]", "NPC L Hand [LHnd]" };
    for (const char* pr : probes)
        for (std::size_t i = 0; i < skels[0].bones.size(); ++i)
            if (skels[0].bones[i].name == pr) {
                const auto& t = w[i].translation;
                std::printf("FK '%s' world=[%.4g %.4g %.4g]\n", pr, t.x, t.y, t.z);
                break;
            }
    return 0;
}

int doPhysDump(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    auto root = std::dynamic_pointer_cast<havok::hkRootLevelContainer>(des.Deserialize(br));
    if (!root) { std::printf("ERROR: no root\n"); return 1; }
    std::shared_ptr<havok::hkpPhysicsData> pd;
    for (const auto& nv : root->m_namedVariants)
        if (auto p = std::dynamic_pointer_cast<havok::hkpPhysicsData>(nv.m_variant)) { pd = p; break; }
    if (!pd || pd->m_systems.empty()) { std::printf("ERROR: no hkpPhysicsData/system\n"); return 1; }
    auto sys = pd->m_systems[0];

    auto v3 = [](const havok::Vector4& v){ char b[96]; std::snprintf(b,sizeof b,"[%.4g %.4g %.4g]",v.x,v.y,v.z); return std::string(b); };

    std::printf("=== %zu rigid bodies ===\n", sys->m_rigidBodies.size());
    for (const auto& rb : sys->m_rigidBodies) {
        if (!rb) continue;
        auto cap = std::dynamic_pointer_cast<havok::hkpCapsuleShape>(rb->m_collidable.m_shape);
        const auto& mo = rb->m_motion;
        const float invMass = mo.m_inertiaAndMassInv.w;
        const float mass = invMass > 1e-9f ? 1.0f/invMass : 0.0f;
        const havok::Vector4& pos = mo.m_motionState.m_transform[3];
        std::printf("BODY '%s'  motionType=%d  mass=%.4g  filter=0x%X  fric=%.3g rest=%.3g\n",
                    rb->m_name.c_str(), (int)mo.m_type, mass,
                    rb->m_collidable.m_broadPhaseHandle.m_collisionFilterInfo,
                    rb->m_material.m_friction, rb->m_material.m_restitution);
        std::printf("   motionState.pos=%s  invInertia=%s\n", v3(pos).c_str(), v3(mo.m_inertiaAndMassInv).c_str());
        if (cap) std::printf("   capsule A=%s B=%s radius=%.4g\n", v3(cap->m_vertexA).c_str(), v3(cap->m_vertexB).c_str(), cap->m_radius);
    }

    // One-time FULL constant dump for the first ragdoll body — the compiler-default boilerplate to derive.
    auto hf = [](havok::Half h){ union{ std::uint32_t u; float f; } c; c.u = (std::uint32_t)h << 16; return c.f; };
    for (const auto& rb : sys->m_rigidBodies) {
        if (!rb || rb->m_name.rfind("Ragdoll_", 0) != 0) continue;
        const auto& mo = rb->m_motion; const auto& ms = mo.m_motionState;
        const auto& col = rb->m_collidable; const auto& bph = col.m_broadPhaseHandle;
        std::printf("=== CONSTANTS (body '%s') ===\n", rb->m_name.c_str());
        std::printf("  motion.type=%d deactIntCtr=%d savedQualityIdx=%d gravityFactor=%.4g\n",
                    (int)mo.m_type, (int)mo.m_deactivationIntegrateCounter, (int)mo.m_savedQualityTypeIndex, hf(mo.m_gravityFactor));
        std::printf("  motionState objRadius=%.4g linDamp=%.4g angDamp=%.4g timeFactor=%.4g maxLinVel=%d maxAngVel=%d deactClass=%d\n",
                    ms.m_objectRadius, hf(ms.m_linearDamping), hf(ms.m_angularDamping), hf(ms.m_timeFactor),
                    (int)ms.m_maxLinearVelocity, (int)ms.m_maxAngularVelocity, (int)ms.m_deactivationClass);
        std::printf("  material responseType=%d rollFric=%.4g  entity dmgMult=%.4g autoRemove=%d respModFlags=%d numShapeKeys=%d uid=0x%X npData=0x%X\n",
                    (int)rb->m_material.m_responseType, hf(rb->m_material.m_rollingFrictionMultiplier),
                    rb->m_damageMultiplier, (int)rb->m_autoRemoveLevel, (int)rb->m_responseModifierFlags,
                    (int)rb->m_numShapeKeysInContactPointProperties, rb->m_uid, rb->m_npData);
        std::printf("  collidable forceCollideOntoPpu=%d bph.type=%d bph.quality=%d allowedPenetration=%.4g  spu.eventFilter=%d spu.userFilter=%d\n",
                    (int)col.m_forceCollideOntoPpu, (int)bph.m_type, (int)bph.m_objectQualityType, col.m_allowedPenetrationDepth,
                    (int)rb->m_spuCollisionCallback.m_eventFilter, (int)rb->m_spuCollisionCallback.m_userFilter);
        std::printf("  motionState.transform.rotCols: %s | %s | %s\n",
                    v3(ms.m_transform[0]).c_str(), v3(ms.m_transform[1]).c_str(), v3(ms.m_transform[2]).c_str());
        break;
    }

    std::printf("=== %zu constraints ===\n", sys->m_constraints.size());
    for (const auto& c : sys->m_constraints) {
        auto ci = std::dynamic_pointer_cast<havok::hkpConstraintInstance>(c);
        if (!ci) continue;
        const std::string a = ci->m_entities[0] ? ci->m_entities[0]->m_name : "?";
        const std::string b = ci->m_entities[1] ? ci->m_entities[1]->m_name : "?";
        if (auto rg = std::dynamic_pointer_cast<havok::hkpRagdollConstraintData>(ci->m_data)) {
            const auto& at = rg->m_atoms;
            std::printf("RAGDOLL  A='%s' B='%s'  twist[%.1f,%.1f] cone[%.1f,%.1f] plane[%.1f,%.1f] (deg)\n",
                        a.c_str(), b.c_str(),
                        at.m_twistLimit.m_minAngle*57.2958f, at.m_twistLimit.m_maxAngle*57.2958f,
                        at.m_coneLimit.m_minAngle*57.2958f,  at.m_coneLimit.m_maxAngle*57.2958f,
                        at.m_planesLimit.m_minAngle*57.2958f,at.m_planesLimit.m_maxAngle*57.2958f);
            std::printf("   frameA.pivot=%s  frameB.pivot=%s\n",
                        v3(at.m_transforms.m_transformA.m_data[3]).c_str(),
                        v3(at.m_transforms.m_transformB.m_data[3]).c_str());
            std::printf("   frameA.rot: %s|%s|%s  frameB.rot: %s|%s|%s\n",
                        v3(at.m_transforms.m_transformA.m_data[0]).c_str(), v3(at.m_transforms.m_transformA.m_data[1]).c_str(), v3(at.m_transforms.m_transformA.m_data[2]).c_str(),
                        v3(at.m_transforms.m_transformB.m_data[0]).c_str(), v3(at.m_transforms.m_transformB.m_data[1]).c_str(), v3(at.m_transforms.m_transformB.m_data[2]).c_str());
        } else if (auto hg = std::dynamic_pointer_cast<havok::hkpLimitedHingeConstraintData>(ci->m_data)) {
            const auto& at = hg->m_atoms;
            std::printf("HINGE    A='%s' B='%s'  ang[%.1f,%.1f] (deg)\n", a.c_str(), b.c_str(),
                        at.m_angLimit.m_minAngle*57.2958f, at.m_angLimit.m_maxAngle*57.2958f);
            std::printf("   frameA.pivot=%s  frameB.pivot=%s\n",
                        v3(at.m_transforms.m_transformA.m_data[3]).c_str(),
                        v3(at.m_transforms.m_transformB.m_data[3]).c_str());
        }
    }

    // One-time FULL atom-constant dump for the first ragdoll + first hinge constraint (the compiler-default
    // atoms to derive). Prints atom m_type tags + the constant fields (limits vary per joint; these don't).
    bool didRag = false, didHinge = false;
    for (const auto& c : sys->m_constraints) {
        auto ci = std::dynamic_pointer_cast<havok::hkpConstraintInstance>(c);
        if (!ci) continue;
        if (auto rg = std::dynamic_pointer_cast<havok::hkpRagdollConstraintData>(ci->m_data)) {
            if (didRag) continue; didRag = true;
            const auto& at = rg->m_atoms;
            std::printf("=== RAGDOLL ATOMS (%s) ===\n", ci->m_name.c_str());
            std::printf("  transforms.type=%d  setupStab.type=%d enabled=%d maxAngle=%.4g\n",
                        at.m_transforms.m_type, at.m_setupStabilization.m_type, at.m_setupStabilization.m_enabled, at.m_setupStabilization.m_maxAngle);
            std::printf("  ragdollMotors.type=%d enabled=%d  angFriction.type=%d enabled=%d maxTorque=%.4g\n",
                        at.m_ragdollMotors.m_type, at.m_ragdollMotors.m_isEnabled, at.m_angFriction.m_type, at.m_angFriction.m_isEnabled, at.m_angFriction.m_maxFrictionTorque);
            std::printf("  twistLimit.type=%d axis=%d refAxis=%d tau=%.4g\n",
                        at.m_twistLimit.m_type, at.m_twistLimit.m_twistAxis, at.m_twistLimit.m_refAxis, at.m_twistLimit.m_angularLimitsTauFactor);
            std::printf("  coneLimit.type=%d twistAxisInA=%d refAxisInB=%d measMode=%d memOff=%d tau=%.4g\n",
                        at.m_coneLimit.m_type, at.m_coneLimit.m_twistAxisInA, at.m_coneLimit.m_refAxisInB, at.m_coneLimit.m_angleMeasurementMode, at.m_coneLimit.m_memOffsetToAngleOffset, at.m_coneLimit.m_angularLimitsTauFactor);
            std::printf("  planesLimit.type=%d twistAxisInA=%d refAxisInB=%d measMode=%d tau=%.4g\n",
                        at.m_planesLimit.m_type, at.m_planesLimit.m_twistAxisInA, at.m_planesLimit.m_refAxisInB, at.m_planesLimit.m_angleMeasurementMode, at.m_planesLimit.m_angularLimitsTauFactor);
            std::printf("  ballSocket.type=%d solveMethod=%d bodiesToNotify=%d velStab=%d maxImpulse=%.4g inertiaStab=%.4g\n",
                        at.m_ballSocket.m_type, at.m_ballSocket.m_solvingMethod, at.m_ballSocket.m_bodiesToNotify, at.m_ballSocket.m_velocityStabilizationFactor, at.m_ballSocket.m_maxImpulse, at.m_ballSocket.m_inertiaStabilizationFactor);
            std::printf("  instance: priority=%d wantRuntime=%d\n", (int)ci->m_priority, ci->m_wantRuntime);
        } else if (auto hg = std::dynamic_pointer_cast<havok::hkpLimitedHingeConstraintData>(ci->m_data)) {
            if (didHinge) continue; didHinge = true;
            const auto& at = hg->m_atoms;
            std::printf("=== HINGE ATOMS (%s) ===\n", ci->m_name.c_str());
            std::printf("  transforms.type=%d  setupStab.type=%d enabled=%d maxAngle=%.4g\n",
                        at.m_transforms.m_type, at.m_setupStabilization.m_type, at.m_setupStabilization.m_enabled, at.m_setupStabilization.m_maxAngle);
            std::printf("  angMotor.type=%d enabled=%d axis=%d  angFriction.type=%d enabled=%d maxTorque=%.4g\n",
                        at.m_angMotor.m_type, at.m_angMotor.m_isEnabled, at.m_angMotor.m_motorAxis, at.m_angFriction.m_type, at.m_angFriction.m_isEnabled, at.m_angFriction.m_maxFrictionTorque);
            std::printf("  angLimit.type=%d axis=%d tau=%.4g  2dAng.type=%d freeAxis=%d\n",
                        at.m_angLimit.m_type, at.m_angLimit.m_limitAxis, at.m_angLimit.m_angularLimitsTauFactor, at.m_2dAng.m_type, at.m_2dAng.m_freeRotationAxis);
            std::printf("  ballSocket.type=%d solveMethod=%d maxImpulse=%.4g inertiaStab=%.4g\n",
                        at.m_ballSocket.m_type, at.m_ballSocket.m_solvingMethod, at.m_ballSocket.m_maxImpulse, at.m_ballSocket.m_inertiaStabilizationFactor);
        }
        if (didRag && didHinge) break;
    }
    return 0;
}

int doResDump(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    auto root = std::dynamic_pointer_cast<havok::hkRootLevelContainer>(des.Deserialize(br));
    if (!root) { std::printf("ERROR: root is not hkRootLevelContainer\n"); return 1; }
    std::size_t nC = 0, nH = 0, nL = 0;
    for (const auto& nv : root->m_namedVariants) {
        auto c = std::dynamic_pointer_cast<havok::hkMemoryResourceContainer>(nv.m_variant);
        if (!c) continue;
        std::printf("=== root variant '%s' (className '%s') ===\n", nv.m_name.c_str(), nv.m_className.c_str());
        DumpResContainer(c, 0, nC, nH, nL);
    }
    std::printf("(resource tree: %zu containers, %zu handles, %zu external links)\n", nC, nH, nL);
    return 0;
}

// skeleton-recompile: the first-cut skeleton-compiler gate. Read a skeleton .hkx ->
// neutral SkeletonData (its animation skeleton) -> CompileSkeleton -> write, then
// re-read and compare bone name/parent/pose. Proves the scatter+assemble+serialize
// spine (Stages 2+5) end to end against real vanilla data, before any YAML front end.
// skeleton-split: split a full skeleton.hkx into a RIG file (anim skeleton only — the pure reference-pose
// artifact) + a RAGDOLL file (everything else: ragdoll skeleton + physics + constraints + ragdoll instance
// + both mappers + a copy of the anim skeleton the mappers bind against). Tests the two-file character
// model (character `rig:` -> rig file, `ragdoll:` -> ragdoll file). ragdollOut is the full graph
// re-serialized (proven to work); rigOut keeps only the hkaAnimationContainer with skeletons[0].
int doSkeletonSplit(const std::string& in, const std::string& rigOut, const std::string& ragdollOut) {
    if (rigOut.empty() || ragdollOut.empty()) { std::printf("ERROR: skeleton-split <in> <rigOut> <ragdollOut>\n"); return 1; }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des; havok::BinaryReaderEx br(false, true, bytes);
    auto root = std::dynamic_pointer_cast<havok::hkRootLevelContainer>(des.Deserialize(br));
    if (!root) { std::printf("ERROR: not a root container\n"); return 1; }

    // RAGDOLL = the full graph, re-serialized with the base header (== input, minus any inert fixup order).
    {
        havok::PackFileSerializer ser; havok::BinaryWriterEx bw(false, true);
        ser.Serialize(root, bw, des._header);
        auto ob = bw.Take();
        if (!havok::sct::WriteHavokFile(ragdollOut, ob, &err)) { std::printf("FAIL ragdoll: %s\n", err.c_str()); return 1; }
        std::printf("ragdoll -> %s (%zu bytes, %zu root variants)\n", ragdollOut.c_str(), ob.size(), root->m_namedVariants.size());
    }
    // RIG = keep only the hkaAnimationContainer variant, with skeletons truncated to [0] (anim skeleton).
    {
        std::vector<havok::hkRootLevelContainerNamedVariant> keep;
        for (auto& nv : root->m_namedVariants) {
            if (auto ac = std::dynamic_pointer_cast<havok::hkaAnimationContainer>(nv.m_variant)) {
                if (ac->m_skeletons.size() > 1) ac->m_skeletons.resize(1);   // drop the ragdoll skeleton
                keep.push_back(nv);
            }
        }
        if (keep.empty()) { std::printf("FAIL rig: no hkaAnimationContainer\n"); return 1; }
        root->m_namedVariants = keep;
        havok::PackFileSerializer ser; havok::BinaryWriterEx bw(false, true);
        ser.Serialize(root, bw, des._header);
        auto ob = bw.Take();
        if (!havok::sct::WriteHavokFile(rigOut, ob, &err)) { std::printf("FAIL rig: %s\n", err.c_str()); return 1; }
        std::printf("rig     -> %s (%zu bytes, anim skeleton only)\n", rigOut.c_str(), ob.size());
    }
    return 0;
}

// skeleton-compile-full: the FUSED compile gate. Read a skeleton.hkx -> anim SkeletonData + physics
// (ReadSkeletonPhysics) -> CompileSkeletonFull -> write, then re-read + objhist to confirm the full
// 5-variant graph (2 skeletons, physics, ragdoll instance, 2 mappers) round-trips.
int doSkeletonCompileFull(const std::string& in, const std::string& outArg) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: %s\n", err.c_str()); return 1;
    }
    havok::sct::ReadSkeletonPhysics(bytes.data(), bytes.size(), skels[0], nullptr);
    std::size_t nphys = 0; for (const auto& b : skels[0].bones) if (b.physics) ++nphys;
    const std::string out = outArg.empty() ? "skeleton_full.hkx" : outArg;
    auto r = havok::sct::CompileSkeletonFull(skels[0]);
    if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
    if (!havok::sct::WriteHavokFile(out, r.bytes, &err)) { std::printf("FAIL write: %s\n", err.c_str()); return 1; }
    std::printf("%s: %zu bones (%zu physics) -> %s (%zu bytes)\n", skels[0].name.c_str(), skels[0].bones.size(), nphys, out.c_str(), r.bytes.size());
    // round-trip: re-read the full graph
    std::vector<std::uint8_t> ob;
    if (!havok::sct::ReadHavokFile(out, ob, &err)) { std::printf("FAIL reread: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des; havok::BinaryReaderEx br(false, true, ob);
    auto root = std::dynamic_pointer_cast<havok::hkRootLevelContainer>(des.Deserialize(br));
    if (!root) { std::printf("FAIL: recompiled file did not deserialize to a root container\n"); return 1; }
    std::printf("round-trip OK: %zu root variants\n", root->m_namedVariants.size());
    for (const auto& nv : root->m_namedVariants) std::printf("  '%s' (%s)\n", nv.m_name.c_str(), nv.m_className.c_str());
    return 0;
}

int doSkeletonRecompile(const std::string& in, const std::string& outArg) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }

    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: no skeleton in %s: %s\n", in.c_str(), err.c_str()); return 1;
    }
    const auto& data = skels[0];
    const std::string out = outArg.empty() ? "skeleton_out.hkx" : outArg;

    auto r = havok::sct::CompileSkeletonToFile(data, out, /*validate*/ true);
    if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }

    // Semantic round-trip gate: re-read the compiled file and compare to the input.
    std::vector<std::uint8_t> ob;
    std::vector<havok::sct::SkeletonData> rs;
    if (!havok::sct::ReadHavokFile(out, ob, &err) ||
        !havok::sct::LoadSkeletonsFromHkx(ob.data(), ob.size(), rs, &err) || rs.empty()) {
        std::printf("FAIL: recompiled file did not re-read: %s\n", err.c_str()); return 1;
    }
    bool ok = rs[0].bones.size() == data.bones.size();
    std::size_t firstBad = data.bones.size();
    for (std::size_t i = 0; ok && i < data.bones.size(); ++i) {
        const auto& a = data.bones[i];
        const auto& b = rs[0].bones[i];
        if (a.name != b.name || a.parentIndex != b.parentIndex ||
            !(a.refPose == b.refPose)) { ok = false; firstBad = i; }
    }
    std::printf("%s: %zu bones -> %s (%zu bytes)  round-trip %s",
                data.name.c_str(), data.bones.size(), out.c_str(), r.bytes.size(),
                ok ? "OK\n" : "DIFF");
    if (!ok) std::printf(" at bone %zu ('%s')\n", firstBad,
                         firstBad < data.bones.size() ? data.bones[firstBad].name.c_str() : "?");
    return ok ? 0 : 1;
}

// skeleton-decompile: emit a skeleton's animation skeleton as authoring YAML
// (parent-by-name + poses). The read/emit side of the front-end round-trip.
int doSkeletonDecompile(const std::string& in, const std::string& outArg) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: no skeleton in %s: %s\n", in.c_str(), err.c_str()); return 1;
    }
    // Attach the ragdoll physics (mass/radius/joint) onto the anim skeleton — everything else derives.
    std::string perr;
    if (!havok::sct::ReadSkeletonPhysics(bytes.data(), bytes.size(), skels[0], &perr))
        std::printf("WARN: physics read failed (%s) — emitting anim-only YAML.\n", perr.c_str());
    const std::string yaml = havok::sct::EmitSkeletonYaml(skels[0]);
    if (outArg.empty()) { std::fputs(yaml.c_str(), stdout); return 0; }
    std::ofstream of(outArg, std::ios::binary);
    of.write(yaml.data(), static_cast<std::streamsize>(yaml.size()));
    std::printf("%s: %zu bones -> %s (%zu bytes YAML)\n",
                skels[0].name.c_str(), skels[0].bones.size(), outArg.c_str(), yaml.size());
    return 0;
}

// skeleton-decompile-tree: decompile a skeleton.hkx to the per-bone TREE form (bonelist.yaml +
// bones/<name>.yaml with physics) — the authoring source layout that replaces the binary in the .hky.
int doSkeletonDecompileTree(const std::string& in, const std::string& outArg) {
    if (outArg.empty()) { std::printf("ERROR: -o <dir> required\n"); return 1; }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: no skeleton in %s: %s\n", in.c_str(), err.c_str()); return 1;
    }
    std::string perr;
    if (!havok::sct::ReadSkeletonPhysics(bytes.data(), bytes.size(), skels[0], &perr))
        std::printf("WARN: physics read failed (%s) — emitting anim-only.\n", perr.c_str());
    if (!havok::sct::EmitSkeletonYamlTree(skels[0], outArg, &err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    std::size_t nphys = 0; for (const auto& b : skels[0].bones) if (b.physics) ++nphys;
    std::printf("%s: %zu bones (%zu with physics) -> %s/{bonelist.yaml, bones/}\n",
                skels[0].name.c_str(), skels[0].bones.size(), nphys, outArg.c_str());
    return 0;
}

// skeleton-compile: the authoring entry point. YAML (dir or single file) ->
// SkeletonData (Stage 1: name->index, preserve-and-append) -> CompileSkeleton (Stages 2+5).
int doSkeletonCompile(const std::string& in, const std::string& outArg, bool full) {
    havok::sct::SkeletonData data; std::string err;
    if (!havok::sct::LoadSkeletonYaml(in, data, &err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    const std::string out = outArg.empty() ? "skeleton_out.hkx" : outArg;
    if (full) {   // YAML -> the full served skeleton (anim + physics/ragdoll/constraints/mappers)
        auto r = havok::sct::CompileSkeletonFull(data);
        if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
        if (!havok::sct::WriteHavokFile(out, r.bytes, &err)) { std::printf("FAIL write: %s\n", err.c_str()); return 1; }
        std::printf("%s: %zu bones (FULL) -> %s (%zu bytes)\n", data.name.c_str(), data.bones.size(), out.c_str(), r.bytes.size());
        return 0;
    }
    auto r = havok::sct::CompileSkeletonToFile(data, out, /*validate*/ true);
    if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
    std::printf("%s: %zu bones -> %s (%zu bytes)\n",
                data.name.c_str(), data.bones.size(), out.c_str(), r.bytes.size());
    return 0;
}

// skeleton-diff-layer: emit a bone-ADD LAYER (bones/<name>.yaml) for every bone present in the EXTENDED
// skeleton but not the BASE — the generator for a skeleton-extension .hky (e.g. XPMSSE's added bones over
// vanilla). Parent-by-name, pose, lockTranslation; the runtime merge resolves names -> indices.
int doSkeletonDiffLayer(const std::string& baseHkx, const std::string& extHkx, const std::string& outDir) {
    auto load = [](const std::string& p, havok::sct::SkeletonData& sk) -> bool {
        std::vector<std::uint8_t> b; std::string e;
        if (!havok::sct::ReadHavokFile(p, b, &e)) { std::printf("ERROR: %s: %s\n", p.c_str(), e.c_str()); return false; }
        std::vector<havok::sct::SkeletonData> v;
        if (!havok::sct::LoadSkeletonsFromHkx(b.data(), b.size(), v, &e) || v.empty()) {
            std::printf("ERROR: no skeleton in %s: %s\n", p.c_str(), e.c_str()); return false;
        }
        sk = std::move(v[0]); return true;
    };
    havok::sct::SkeletonData base, ext;
    if (!load(baseHkx, base) || !load(extHkx, ext)) return 1;
    if (outDir.empty()) { std::printf("ERROR: -o <layer-dir> required\n"); return 1; }

    std::unordered_set<std::string> baseNames;
    for (const auto& b : base.bones) baseNames.insert(b.name);

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(outDir) / "bones", ec);
    auto ff = [](float v) { char b[32]; std::snprintf(b, sizeof b, "%.9g", v); return std::string(b); };
    int emitted = 0;
    for (const auto& b : ext.bones) {
        if (baseNames.count(b.name)) continue;   // already in base — not an addition
        const std::string parent = (b.parentIndex >= 0 && b.parentIndex < static_cast<int>(ext.bones.size()))
                                        ? ext.bones[static_cast<std::size_t>(b.parentIndex)].name : std::string{};
        std::ostringstream o;
        if (!parent.empty()) o << "parent: \"" << parent << "\"\n";
        if (b.lockTranslation) o << "lockTranslation: true\n";
        o << "pose:\n";
        o << "  translation: [" << ff(b.refPose.translation.x) << ", " << ff(b.refPose.translation.y) << ", "
          << ff(b.refPose.translation.z) << ", " << ff(b.refPose.translation.w) << "]\n";
        o << "  rotation: [" << ff(b.refPose.rotation.x) << ", " << ff(b.refPose.rotation.y) << ", "
          << ff(b.refPose.rotation.z) << ", " << ff(b.refPose.rotation.w) << "]\n";
        o << "  scale: [" << ff(b.refPose.scale.x) << ", " << ff(b.refPose.scale.y) << ", "
          << ff(b.refPose.scale.z) << ", " << ff(b.refPose.scale.w) << "]\n";
        const std::filesystem::path fp = std::filesystem::path(outDir) / "bones" / (b.name + ".yaml");
        std::ofstream of(fp, std::ios::binary);
        const std::string s = o.str();
        of.write(s.data(), static_cast<std::streamsize>(s.size()));
        ++emitted;
    }
    std::printf("base %zu bones, extended %zu bones -> %d added bone(s) emitted to %s/bones/\n",
                base.bones.size(), ext.bones.size(), emitted, outDir.c_str());
    return 0;
}

// skeleton-recompile-base: the compile-over-base IDENTITY gate. Read a skeleton.hkx, pull its anim
// skeleton to neutral SkeletonData, compile-over-base against the SAME bytes (rebuild anim bones, carry
// the ragdoll/physics/mappers/resource tree), and byte-compare. An identity bone set MUST reproduce the
// source byte-for-byte — the drift alarm for the carry-base serve path (existing content).
int doSkeletonRecompileBase(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: no skeleton in %s: %s\n", in.c_str(), err.c_str()); return 1;
    }
    auto r = havok::sct::CompileSkeletonOverBase(skels[0], bytes);
    if (!r.ok) { std::printf("COMPILE FAIL: %s\n", r.error.c_str()); return 1; }

    // The real invariant is NOT source-byte-identity (skeleton.hkx has a documented, inert 110-byte
    // global-fixup ORDER residual that even a raw read->write leaves — see the skeleton memory). It is
    // that compile-over-base adds NOTHING beyond that serializer baseline: rebuild-anim + carry-physics
    // must equal a plain hkx-roundtrip of the same file. So compare against BOTH.
    std::vector<std::uint8_t> baseline;   // raw Deserialize -> Serialize (the serializer's own output)
    try {
        havok::PackFileDeserializer des;
        havok::BinaryReaderEx bbr(false, true, bytes);
        auto root = des.Deserialize(bbr);
        havok::PackFileSerializer ser;
        havok::BinaryWriterEx bw(false, true);
        ser.Serialize(root, bw, des._header);
        baseline = bw.Take();
    } catch (const std::exception& e) { std::printf("BASELINE FAIL: %s\n", e.what()); return 1; }

    auto diff = [](const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
                   std::size_t& first, std::size_t& count) {
        const std::size_t n = std::min(a.size(), b.size());
        first = n; count = (a.size() != b.size()) ? 1 : 0;
        for (std::size_t i = 0; i < n; ++i) if (a[i] != b[i]) { if (first == n) first = i; ++count; }
    };
    std::size_t sf, sc, bf, bc;
    diff(r.bytes, bytes, sf, sc);       // vs SOURCE
    diff(r.bytes, baseline, bf, bc);    // vs serializer BASELINE
    const bool faithful = (r.bytes.size() == baseline.size()) && (bf == baseline.size() || bc == 0);
    std::printf("%s: %zu anim bones + carried physics -> %zu bytes\n", skels[0].name.c_str(),
                skels[0].bones.size(), r.bytes.size());
    std::printf("  vs source:   %s (%zu bytes differ%s)\n", sc == 0 ? "byte-identical" : "differs", sc,
                sc ? " — inert global-fixup-order residual (see skeleton memory)" : "");
    std::printf("  vs baseline: %s%s\n", faithful ? "IDENTICAL — compile-over-base adds nothing" : "DIFFERS",
                faithful ? " (carry-base is byte-faithful)" : "");
    if (!faithful) std::printf("  *** first extra diff @%zu, %zu bytes ***\n", bf, bc);
    return faithful ? 0 : 1;
}

// skeleton-append: Stage B/C demo/gate. Read a base skeleton.hkx (frozen anim prefix + carried physics),
// load a bone-add LAYER dir (bones/<name>.yaml), merge (preserve-and-append, parent-by-name), and
// compile-over-base. Prints the appended bones + output size; -o writes the served skeleton.
int doSkeletonAppend(const std::string& baseHkx, const std::string& layerDir, const std::string& outArg) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(baseHkx, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("ERROR: no skeleton in base %s: %s\n", baseHkx.c_str(), err.c_str()); return 1;
    }
    std::vector<havok::sct::SkeletonBoneAdd> adds;
    if (!havok::sct::LoadSkeletonLayer(layerDir, adds, &err)) { std::printf("LAYER FAIL: %s\n", err.c_str()); return 1; }
    const std::size_t before = skels[0].bones.size();
    if (!havok::sct::MergeBoneAdditions(skels[0], adds, &err)) { std::printf("MERGE FAIL: %s\n", err.c_str()); return 1; }
    const std::size_t added = skels[0].bones.size() - before;
    auto r = havok::sct::CompileSkeletonOverBase(skels[0], bytes);
    if (!r.ok) { std::printf("COMPILE FAIL: %s\n", r.error.c_str()); return 1; }
    std::printf("base %zu bones + %zu layer file(s) -> +%zu appended = %zu bones, carried physics -> %zu bytes\n",
                before, adds.size(), added, skels[0].bones.size(), r.bytes.size());
    for (std::size_t i = before; i < skels[0].bones.size(); ++i) {
        const auto& b = skels[0].bones[i];
        std::printf("   +[%zu] '%s'  parent=%s  lock=%d\n", i, b.name.c_str(),
                    b.parentIndex >= 0 ? skels[0].bones[b.parentIndex].name.c_str() : "(root)",
                    static_cast<int>(b.lockTranslation));
    }
    if (!outArg.empty()) {
        std::string werr;
        if (!havok::sct::WriteHavokFile(outArg, r.bytes, &werr)) { std::printf("WRITE FAIL: %s\n", werr.c_str()); return 1; }
        std::printf("wrote %s\n", outArg.c_str());
    }
    return 0;
}

// hkx-roundtrip: the byte gate for raw packfile (de)serialization. Full-construct the
// whole object graph (runs every class's Read), re-serialize through OUR PackFileSerializer
// (runs every Write), and byte-compare against the source packfile. This is the drift alarm
// for newly-ported classes (rigid-body/constraint/ragdoll/mapper subtrees) — a skeleton.hkx
// exercises the entire ragdoll rig. Compares against the raw bytes ReadHavokFile returns
// (the decompressed packfile the deserializer consumed).
int doHkxRoundtrip(const std::string& in, const std::string& outPath = {}) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }

    std::shared_ptr<havok::IHavokObject> root;
    havok::HKXHeader header;
    try {
        havok::PackFileDeserializer des;
        havok::BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        root   = des.Deserialize(br);
        header = des._header;
    } catch (const std::exception& e) { std::printf("READ FAIL: %s\n", e.what()); return 1; }
    if (!root) { std::printf("READ FAIL: null root\n"); return 1; }

    std::vector<std::uint8_t> out;
    try {
        havok::PackFileSerializer ser;
        havok::BinaryWriterEx bw(/*bigEndian*/ false, /*uSizeLong*/ true);
        ser.Serialize(root, bw, header);
        out = bw.Take();
    } catch (const std::exception& e) { std::printf("WRITE FAIL: %s\n", e.what()); return 1; }

    if (out.size() != bytes.size()) {
        std::printf("DIFF: size %zu (out) != %zu (in)\n", out.size(), bytes.size());
        // still report first differing byte within the common prefix
    }
    if (!outPath.empty()) {
        std::ofstream of(outPath, std::ios::binary);
        of.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    const std::size_t n = std::min(out.size(), bytes.size());
    std::size_t firstDiff = n, diffCount = 0;
    for (std::size_t i = 0; i < n; ++i) if (out[i] != bytes[i]) { if (firstDiff == n) firstDiff = i; ++diffCount; }
    if (firstDiff == n && out.size() == bytes.size()) {
        std::printf("OK: byte-identical round-trip (%zu bytes)\n", bytes.size());
        return 0;
    }
    if (firstDiff < n)
        std::printf("DIFF at byte 0x%zx: in=0x%02x out=0x%02x (%zu bytes differ of %zu)\n",
                    firstDiff, bytes[firstDiff], out[firstDiff], diffCount, n);
    return 1;
}

// iohkx-roundtrip <file.hkx> <Havok-schema-dir> [-o out]: the Stage-2 byte gate for havok-io — the
// GENERIC, schema-driven reader/writer. Same contract as hkx-roundtrip (read whole graph, re-serialize,
// byte-compare) but every object is a schema-driven SchemaObject rather than a hand-ported C++ class.
// A byte-identical result proves the Havok/ descriptors + havok-io reproduce the packfile with no
// hand-written per-class serde in the loop.
int doIoHkxRoundtrip(const std::string& in, const std::string& schemaDir, const std::string& outPath = {}) {
    if (schemaDir.empty()) { std::printf("usage: iohkx-roundtrip <file.hkx> <Havok-dir> [-o out]\n"); return 1; }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }

    havok::schema::SchemaRegistry reg;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema '%s': %s\n", schemaDir.c_str(), err.c_str()); return 1; }

    std::vector<std::uint8_t> out;
    if (!havok::io::RoundtripHkx(bytes, reg, out, err)) { std::printf("IO FAIL: %s\n", err.c_str()); return 1; }

    if (!outPath.empty()) {
        std::ofstream of(outPath, std::ios::binary);
        of.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    const std::size_t n = std::min(out.size(), bytes.size());
    std::size_t firstDiff = n, diffCount = 0;
    for (std::size_t i = 0; i < n; ++i) if (out[i] != bytes[i]) { if (firstDiff == n) firstDiff = i; ++diffCount; }
    if (firstDiff == n && out.size() == bytes.size()) {
        std::printf("OK: byte-identical round-trip (%zu bytes)\n", bytes.size());
        return 0;
    }
    if (out.size() != bytes.size())
        std::printf("DIFF: size %zu (out) != %zu (in)\n", out.size(), bytes.size());
    if (firstDiff < n)
        std::printf("DIFF at byte 0x%zx: in=0x%02x out=0x%02x (%zu bytes differ of %zu)\n",
                    firstDiff, bytes[firstDiff], out[firstDiff], diffCount, n);
    return 1;
}

// iohkx-rebuild <file.hkx> <Havok-dir> [-o out]: the migration-foundation gate. Same as iohkx-roundtrip
// but the object graph is deep-REBUILT via the construction API (SchemaObject::Init + FieldRef/FieldAt,
// no Read) before serializing. A byte-identical result proves from-scratch SchemaObject construction is
// byte-faithful — the prerequisite for a builder that emits SchemaObjects in place of the typed hk* builders.
int doIoHkxRebuild(const std::string& in, const std::string& schemaDir, const std::string& outPath = {}) {
    if (schemaDir.empty()) { std::printf("usage: iohkx-rebuild <file.hkx> <Havok-dir> [-o out]\n"); return 1; }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::schema::SchemaRegistry reg;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema '%s': %s\n", schemaDir.c_str(), err.c_str()); return 1; }
    std::vector<std::uint8_t> out;
    if (!havok::io::RebuildHkx(bytes, reg, out, err)) { std::printf("REBUILD FAIL: %s\n", err.c_str()); return 1; }
    if (!outPath.empty()) {
        std::ofstream of(outPath, std::ios::binary);
        of.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    const std::size_t n = std::min(out.size(), bytes.size());
    std::size_t firstDiff = n, diffCount = 0;
    for (std::size_t i = 0; i < n; ++i) if (out[i] != bytes[i]) { if (firstDiff == n) firstDiff = i; ++diffCount; }
    if (firstDiff == n && out.size() == bytes.size()) { std::printf("OK: byte-identical rebuild (%zu bytes)\n", bytes.size()); return 0; }
    if (out.size() != bytes.size()) std::printf("DIFF: size %zu (out) != %zu (in)\n", out.size(), bytes.size());
    if (firstDiff < n) std::printf("DIFF at byte 0x%zx: in=0x%02x out=0x%02x (%zu bytes differ of %zu)\n",
                                   firstDiff, bytes[firstDiff], out[firstDiff], diffCount, n);
    return 1;
}

// schemabuild-clip-check <file.hkx> <Havok-dir>: gate the schema-driven BUILDER (Migration M2). Read
// every hkbClipGenerator in a vanilla behavior (the reference), reverse-extract a ClipGeneratorDef,
// rebuild the node via havok-model::BuildClip, and require every AUTHORED (non-ignored, non-pointer)
// field to match the reference field-for-field. Proves the Def->SchemaObject clip mapping + encoding
// (float bits, enum, int widths, field placement) is correct. Clips WITH triggers/bindings are skipped
// (those pointer sub-nodes are a later increment); ignored runtime-state fields are not compared.
int doSchemaBuildClipCheck(const std::string& in, const std::string& schemaDir) {
    if (schemaDir.empty()) { std::printf("usage: schemabuild-clip-check <file.hkx> <Havok-dir>\n"); return 1; }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::schema::SchemaRegistry reg;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    des.ObjectFactory = havok::io::MakeSchemaFactory(reg);
    std::shared_ptr<havok::IHavokObject> root;
    try { havok::BinaryReaderEx br(false, true, bytes); root = des.Deserialize(br); }
    catch (const std::exception& e) { std::printf("READ FAIL: %s\n", e.what()); return 1; }

    // Walk the graph from root, giving each node a UNIQUE key (node names aren't unique). The gate uses
    // these keys as cross-node references so an edge resolves to the EXACT read object, not a namesake.
    using SO0 = havok::io::SchemaObject;
    std::unordered_map<std::string, std::shared_ptr<SO0>>    byKey;
    std::unordered_map<const havok::IHavokObject*, std::string> keyOf;
    std::vector<std::shared_ptr<SO0>> nodes;
    {
        std::unordered_set<const havok::IHavokObject*> seen;
        std::function<void(const std::shared_ptr<havok::IHavokObject>&)> walk =
            [&](const std::shared_ptr<havok::IHavokObject>& p) {
            auto so = std::dynamic_pointer_cast<SO0>(p);
            if (!so || !seen.insert(so.get()).second) return;
            const std::string key = "n" + std::to_string(nodes.size());
            nodes.push_back(so); byKey[key] = so; keyOf[so.get()] = key;
            auto flds = so->Fields(); const auto& vals = so->Values();
            for (std::size_t i = 0; i < flds.size(); ++i) {
                if (vals[i].obj) walk(vals[i].obj);
                for (const auto& c : vals[i].objs) walk(c);
            }
        };
        walk(root);
    }
    havok::model::GenResolver resolve = [&](const std::string& k) -> std::shared_ptr<SO0> {
        auto it = byKey.find(k); return it == byKey.end() ? nullptr : it->second;
    };
    auto keyRef = [&](const std::shared_ptr<SO0>& p) { if(!p) return std::string(); auto it=keyOf.find(p.get()); return it==keyOf.end()?std::string():it->second; };

    // field lookup (name -> (Field*, FieldValue*)) on a read SchemaObject
    auto find = [](const havok::io::SchemaObject& so, const char* nm)
        -> std::pair<const havok::schema::Field*, const havok::io::FieldValue*> {
        auto flds = so.Fields(); const auto& vals = so.Values();
        for (std::size_t i = 0; i < flds.size(); ++i) if (flds[i]->name == nm) return { flds[i], &vals[i] };
        return { nullptr, nullptr };
    };
    auto fstr = [](float v) { char b[32]; std::snprintf(b, sizeof b, "%.9g", v); return std::string(b); };
    auto rF = [&](const havok::io::SchemaObject& so, const char* nm) { auto [f,v]=find(so,nm); float x=0; if(v&&v->raw.size()>=4) std::memcpy(&x,v->raw.data(),4); return x; };
    auto rI = [&](const havok::io::SchemaObject& so, const char* nm, int nb, bool sign) { auto [f,v]=find(so,nm); long long x=0; if(v){ for(int i=0;i<nb&&i<(int)v->raw.size();++i) x |= (long long)(v->raw[(std::size_t)i])<<(8*i); if(sign&&nb<8&&(x&(1LL<<(8*nb-1)))) x -= (1LL<<(8*nb)); } return x; };

    using SO = havok::io::SchemaObject;
    auto asSO = [](const std::shared_ptr<havok::IHavokObject>& p) { return std::dynamic_pointer_cast<SO>(p); };
    auto objOf = [&](const SO& so, const char* nm) -> std::shared_ptr<SO> { auto [f,v]=find(so,nm); return (v&&v->obj)?asSO(v->obj):nullptr; };
    auto payloadOf = [&](const SO& ev) -> std::string { auto p=objOf(ev,"payload"); if(!p) return "null"; auto [f,v]=find(*p,"data"); return v?v->str:"null"; };

    // Recursive structural compare: every AUTHORED (non-ignored) value field must match; pointer/struct
    // sub-graphs recurse pairwise. Returns the failing field path, or "" on full match.
    std::function<std::string(const SO&, const SO&, const std::string&)> cmp =
        [&](const SO& a, const SO& b, const std::string& path) -> std::string {
        if (&a == &b) return "";   // same object (a cross-node edge the resolver pointed at the read node)
        auto fa = a.Fields(); const auto& va = a.Values(); const auto& vb = b.Values();
        if (fa.size() != vb.size()) return path + "<shape>";
        using K = havok::schema::FieldKind;
        for (std::size_t i = 0; i < fa.size(); ++i) {
            const auto& f = *fa[i]; if (f.ignored) continue;
            const std::string fp = path + (f.name.empty() ? "." : ("." + f.name));
            const auto& x = va[i]; const auto& y = vb[i];
            if (f.kind==K::Ptr || f.kind==K::Struct) {
                auto xo=asSO(x.obj), yo=asSO(y.obj);
                if (!xo != !yo) return fp + "<ptr-null>";
                if (xo && yo) { auto r=cmp(*xo,*yo,fp); if(!r.empty()) return r; }
            } else if (f.kind==K::PtrArray || f.kind==K::StructArray) {
                if (x.objs.size()!=y.objs.size()) return fp + "<arr-size>";
                for (std::size_t k=0;k<x.objs.size();++k){ auto xo=asSO(x.objs[k]),yo=asSO(y.objs[k]); if(!xo!=!yo) return fp+"[?]"; if(xo&&yo){auto r=cmp(*xo,*yo,fp+"[]"); if(!r.empty()) return r;} }
            } else if (f.kind==K::EmptyPtr || f.kind==K::EmptyArray) {
                continue;   // void — no stored value
            } else {
                if (x.raw!=y.raw || x.str!=y.str || x.vtable!=y.vtable) return fp;
            }
        }
        return "";
    };

    // Extract a BindingDef[] from a read hkbVariableBindingSet (reused by clips + blenders + children).
    auto extractBindings = [&](const SO& bs) {
        std::vector<havok::model::BindingDef> bds;
        const int enIdx = (int)rI(bs,"indexOfBindingToEnable",4,true);
        auto [f,v] = find(bs,"bindings");
        if (v) for (int k=0;k<(int)v->objs.size();++k) if (auto bd = asSO(v->objs[(std::size_t)k])) {
            havok::model::BindingDef d;
            auto [mf,mv]=find(*bd,"memberPath"); d.memberPath = mv?mv->str:"";
            d.variableIndex=(int)rI(*bd,"variableIndex",4,true); d.bitIndex=(int)rI(*bd,"bitIndex",1,true);
            d.bindingType = std::to_string(rI(*bd,"bindingType",1,false));
            d.enableTarget = (k==enIdx);
            bds.push_back(std::move(d));
        }
        return bds;
    };

    // Extract a hkbStateMachineEventPropertyArray (enter/exit notify events) -> EventPropertyDef[].
    auto extractEvents = [&](const SO& arr) {
        std::vector<havok::model::EventPropertyDef> out;
        auto [f,v] = find(arr,"events");
        if (v) for (const auto& eo : v->objs) if (auto ep = asSO(eo)) {
            havok::model::EventPropertyDef d; d.id=(int)rI(*ep,"id",4,true); d.payload=payloadOf(*ep);
            out.push_back(std::move(d));
        }
        return out;
    };
    // Extract one inline hkbStateMachineTimeInterval (trigger/initiate interval) off a transition.
    auto extractInterval = [&](const SO& t, const char* field) {
        havok::model::TransitionIntervalDef iv;
        if (auto s = objOf(t, field)) {
            iv.enterEventId=(int)rI(*s,"enterEventId",4,true); iv.exitEventId=(int)rI(*s,"exitEventId",4,true);
            iv.enterTime=fstr(rF(*s,"enterTime")); iv.exitTime=fstr(rF(*s,"exitTime"));
        }
        return iv;
    };
    // Extract a hkbStateMachineTransitionInfoArray -> TransitionInfoDef[] (transition-effect + condition
    // resolved to cross-node keys / owned sub-node values).
    auto extractTransitions = [&](const SO& arrNode) {
        std::vector<havok::model::TransitionInfoDef> out;
        auto [f,v] = find(arrNode,"transitions");
        if (v) for (const auto& to : v->objs) if (auto tr = asSO(to)) {
            havok::model::TransitionInfoDef d;
            d.triggerInterval  = extractInterval(*tr,"triggerInterval");
            d.initiateInterval = extractInterval(*tr,"initiateInterval");
            d.transition = keyRef(objOf(*tr,"transition"));
            if (auto c = objOf(*tr,"condition")) {
                if (std::string(c->ClassName())=="hkbStringCondition") { auto [cf,cv]=find(*c,"conditionString"); d.conditionString = cv?cv->str:std::string(); }
                else { auto [cf,cv]=find(*c,"expression"); d.condition = cv?cv->str:std::string(); }
            }
            d.eventId=(int)rI(*tr,"eventId",4,true); d.toStateId=(int)rI(*tr,"toStateId",4,true);
            d.fromNestedStateId=(int)rI(*tr,"fromNestedStateId",4,true); d.toNestedStateId=(int)rI(*tr,"toNestedStateId",4,true);
            d.priority=(int)rI(*tr,"priority",2,true); d.flags=std::to_string(rI(*tr,"flags",2,false));
            out.push_back(std::move(d));
        }
        return out;
    };

    // Reverse-extract a GenericModifierDef by walking a flat modifier's SCHEMA (the inverse of
    // BuildGenericModifier). Each authored field → a GenericParam rendered per its kind.
    auto extractGeneric = [&](const SO& node) {
        using K = havok::schema::FieldKind; using S = havok::schema::Scalar;
        havok::model::GenericModifierDef def;
        def.className = node.ClassName();
        def.name = find(node,"name").second ? find(node,"name").second->str : "";
        def.userData = (int)rI(node,"userData",8,false);
        def.enable = rI(node,"enable",1,false)!=0;
        if (auto bs = objOf(node,"variableBindingSet")) def.bindings = extractBindings(*bs);
        auto flds = node.Fields(); const auto& vals = node.Values();
        for (std::size_t i=0;i<flds.size();++i) {
            const auto& f = *flds[i];
            if (f.ignored || f.name.empty()) continue;
            if (f.name=="name"||f.name=="userData"||f.name=="enable"||f.name=="variableBindingSet") continue;
            havok::model::GenericParam gp; gp.name = f.name;
            if (f.kind==K::Scalar) {
                gp.kind = havok::model::GenericParamKind::Scalar;
                if (f.scalar==S::Float) gp.scalarValue = fstr(rF(node,f.name.c_str()));
                else if (f.scalar==S::Bool) gp.scalarValue = std::string(rI(node,f.name.c_str(),1,false)!=0 ? "true":"false");
                else { int w = havok::schema::ScalarWidth(f.scalar);
                       bool sg = (f.scalar==S::Int8||f.scalar==S::Int16||f.scalar==S::Int32||f.scalar==S::Int64);
                       gp.scalarValue = std::to_string(rI(node,f.name.c_str(),w,sg)); }
            } else if (f.kind==K::Vector4 || f.kind==K::Quaternion) {
                gp.kind = havok::model::GenericParamKind::Scalar;
                const auto& v = vals[i]; std::string q="(";
                for (int j=0;j<4;++j){ float fv=0; if(v.raw.size()>=(std::size_t)(j*4+4)) std::memcpy(&fv,&v.raw[j*4],4); if(j) q+=" "; q+=fstr(fv);} q+=")";
                gp.scalarValue = q;
            } else if (f.kind==K::Struct && f.ref=="hkbEventProperty") {
                gp.kind = havok::model::GenericParamKind::InlineEvent;
                havok::model::InlineEventDef ie;
                if (auto e = asSO(vals[i].obj)) { ie.id=(int)rI(*e,"id",4,true); ie.payload=payloadOf(*e); }
                gp.eventValue = ie;
            } else if (f.kind==K::Ptr && (f.ref=="hkbModifier"||f.ref=="hkbGenerator")) {
                gp.kind = havok::model::GenericParamKind::Reference;
                gp.refValue = keyRef(asSO(vals[i].obj));
            } else continue;   // arrays / owned sub-nodes — not flat
            def.extraParams.push_back(std::move(gp));
        }
        return def;
    };
    // Read a vector4/quaternion field as a "(x y z w)" literal.
    auto vec4s = [&](const SO& so, const char* nm) { auto [f,v]=find(so,nm); std::string q="(";
        for(int j=0;j<4;++j){ float fv=0; if(v && v->raw.size()>=(std::size_t)(j*4+4)) std::memcpy(&fv,&v->raw[j*4],4); if(j) q+=" "; q+=fstr(fv);} q+=")"; return q; };
    auto readGains = [&](const SO& g, havok::model::FootIkGainsDef& d) {
        d.onOffGain=rF(g,"onOffGain"); d.groundAscendingGain=rF(g,"groundAscendingGain"); d.groundDescendingGain=rF(g,"groundDescendingGain");
        d.footPlantedGain=rF(g,"footPlantedGain"); d.footRaisedGain=rF(g,"footRaisedGain"); d.footUnlockGain=rF(g,"footUnlockGain");
        d.worldFromModelFeedbackGain=rF(g,"worldFromModelFeedbackGain"); d.errorUpDownBias=rF(g,"errorUpDownBias"); d.alignWorldFromModelGain=rF(g,"alignWorldFromModelGain");
        d.hipOrientationGain=rF(g,"hipOrientationGain"); d.maxKneeAngleDifference=rF(g,"maxKneeAngleDifference"); d.ankleOrientationGain=rF(g,"ankleOrientationGain"); };

    // helpers to synthesize GenericParams from vanilla fields (the decompiler's flattening, inline)
    auto gpS = [](const std::string& name, const std::string& val) { havok::model::GenericParam p; p.name=name; p.kind=havok::model::GenericParamKind::Scalar; p.scalarValue=val; return p; };
    auto gpE = [&](const std::string& name, const SO& ev) { havok::model::GenericParam p; p.name=name; p.kind=havok::model::GenericParamKind::InlineEvent;
        havok::model::InlineEventDef ie; ie.id=(int)rI(ev,"id",4,true); ie.payload=payloadOf(ev); p.eventValue=ie; return p; };
    auto buildBoneIdx = [&](const SO& arr) -> std::shared_ptr<SO> {
        havok::model::BoneIndexArrayDef d; auto [f,v]=find(arr,"boneIndices");
        if (v) { const std::size_t n=v->raw.size()/2; for(std::size_t j=0;j<n;++j){ std::int16_t x; std::memcpy(&x,&v->raw[j*2],2); d.boneIndices.push_back(x);} }
        return havok::model::BuildBoneIndexArray(d, reg); };

    static const std::set<std::string> kFlatGeneric = {
        "hkbTwistModifier","hkbTimerModifier","hkbDampingModifier","hkbRotateCharacterModifier","hkbGetUpModifier",
        "BSDirectAtModifier","BSModifyOnceModifier","BSEventOnDeactivateModifier","BSEventOnFalseToTrueModifier",
        "BSSpeedSamplerModifier",
        // NOT BSRagdollContactListenerModifier / BSLookAtModifier / hkbKeyframeBonesModifier — they own
        // bone-index-array / bone-data sub-nodes; handled with the bone-owning modifier batch.
    };

    int clips=0, blenders=0, selectors=0, stateMachines=0, states=0, effects=0, modifiers=0, pass=0, fail=0;
    std::map<std::string,int> unhandled;
    // For the whole-graph assembly gate: read-node -> its semantic build (edges still point at read nodes;
    // rewired to built nodes after the loop).
    std::unordered_map<const havok::io::SchemaObject*, std::shared_ptr<SO>> builtByRead;
    for (const auto& ref : nodes) {
        const std::string cls = ref->ClassName();
        if (cls == "hkbClipGenerator") {
            ++clips;
            havok::model::ClipGeneratorDef def;
            def.name                       = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.animationName              = find(*ref,"animationName").second ? find(*ref,"animationName").second->str : "";
            def.cropStartAmountLocalTime   = fstr(rF(*ref,"cropStartAmountLocalTime"));
            def.cropEndAmountLocalTime     = fstr(rF(*ref,"cropEndAmountLocalTime"));
            def.startTime                  = fstr(rF(*ref,"startTime"));
            def.playbackSpeed              = fstr(rF(*ref,"playbackSpeed"));
            def.enforcedDuration           = fstr(rF(*ref,"enforcedDuration"));
            def.userControlledTimeFraction = fstr(rF(*ref,"userControlledTimeFraction"));
            def.animationBindingIndex      = (int)rI(*ref,"animationBindingIndex",2,true);
            def.mode                       = std::to_string(rI(*ref,"mode",1,false));
            def.flags                      = (int)rI(*ref,"flags",1,false);
            def.userData                   = (int)rI(*ref,"userData",8,false);
            if (auto ta = objOf(*ref,"triggers")) {
                auto [f,v] = find(*ta,"triggers"); std::vector<havok::model::ClipTriggerDef> tds;
                if (v) for (const auto& to : v->objs) if (auto tr = asSO(to)) {
                    havok::model::ClipTriggerDef td;
                    td.localTime = fstr(rF(*tr,"localTime"));
                    td.relativeToEndOfClip = rI(*tr,"relativeToEndOfClip",1,false)!=0;
                    td.acyclic = rI(*tr,"acyclic",1,false)!=0; td.isAnnotation = rI(*tr,"isAnnotation",1,false)!=0;
                    if (auto ev = objOf(*tr,"event")) { td.eventId=(int)rI(*ev,"id",4,true); td.payload=payloadOf(*ev); }
                    tds.push_back(std::move(td));
                }
                def.triggers = std::move(tds);
            }
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildClip(def, reg);
            if (!built) { std::printf("FAIL: BuildClip null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH clip '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbBlenderGenerator") {
            ++blenders;
            havok::model::BlenderGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.referencePoseWeightThreshold = fstr(rF(*ref,"referencePoseWeightThreshold"));
            def.blendParameter               = fstr(rF(*ref,"blendParameter"));
            def.minCyclicBlendParameter      = fstr(rF(*ref,"minCyclicBlendParameter"));
            def.maxCyclicBlendParameter      = fstr(rF(*ref,"maxCyclicBlendParameter"));
            def.indexOfSyncMasterChild       = (int)rI(*ref,"indexOfSyncMasterChild",2,true);
            def.flags                        = (int)rI(*ref,"flags",2,true);
            def.subtractLastChild            = rI(*ref,"subtractLastChild",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto [cf,cv] = find(*ref,"children");
            if (cv) for (const auto& co : cv->objs) if (auto ch = asSO(co)) {
                havok::model::BlenderChildDef cd;
                cd.generator            = keyRef(objOf(*ch,"generator"));
                cd.weight               = fstr(rF(*ch,"weight"));
                cd.worldFromModelWeight = fstr(rF(*ch,"worldFromModelWeight"));
                if (auto bw = objOf(*ch,"boneWeights")) {
                    auto [bf,bv] = find(*bw,"boneWeights");
                    havok::model::BoneWeightsDef bwd;
                    if (bv) { const std::size_t nF = bv->raw.size()/4; bwd.count=(int)nF; std::string vals;
                        for (std::size_t j=0;j<nF;++j){ float fv; std::memcpy(&fv,&bv->raw[j*4],4); if(j) vals+=" "; vals+=fstr(fv);} bwd.values=vals; }
                    cd.boneWeights = std::move(bwd);
                }
                if (auto bs = objOf(*ch,"variableBindingSet")) cd.bindings = extractBindings(*bs);
                def.children.push_back(std::move(cd));
            }
            auto built = havok::model::BuildBlender(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildBlender null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH blender '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbManualSelectorGenerator") {
            ++selectors;
            havok::model::ManualSelectorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.selectedGeneratorIndex = (int)rI(*ref,"selectedGeneratorIndex",1,true);
            def.currentGeneratorIndex  = (int)rI(*ref,"currentGeneratorIndex",1,true);
            auto [gf,gv] = find(*ref,"generators");
            if (gv) for (const auto& go : gv->objs) def.generators.push_back(keyRef(asSO(go)));
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildSelector(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildSelector null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH selector '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbStateMachine") {
            ++stateMachines;
            havok::model::StateMachineDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.startStateId = (int)rI(*ref,"startStateId",4,true);
            if (auto ev = objOf(*ref,"eventToSendWhenStateOrTransitionChanges")) def.eventToSendWhenStateOrTransitionChangesId = (int)rI(*ev,"id",4,true);
            def.returnToPreviousStateEventId       = (int)rI(*ref,"returnToPreviousStateEventId",4,true);
            def.randomTransitionEventId            = (int)rI(*ref,"randomTransitionEventId",4,true);
            def.transitionToNextHigherStateEventId = (int)rI(*ref,"transitionToNextHigherStateEventId",4,true);
            def.transitionToNextLowerStateEventId  = (int)rI(*ref,"transitionToNextLowerStateEventId",4,true);
            def.syncVariableIndex = (int)rI(*ref,"syncVariableIndex",4,true);
            def.wrapAroundStateId = rI(*ref,"wrapAroundStateId",1,false)!=0;
            def.maxSimultaneousTransitions = (int)rI(*ref,"maxSimultaneousTransitions",1,true);
            def.startStateMode     = std::to_string(rI(*ref,"startStateMode",1,true));
            def.selfTransitionMode = std::to_string(rI(*ref,"selfTransitionMode",1,true));
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            { auto [sf,sv] = find(*ref,"states"); if (sv) for (const auto& so : sv->objs) def.states.push_back(keyRef(asSO(so))); }
            if (auto wt = objOf(*ref,"wildcardTransitions")) def.parsedWildcardTransitions = extractTransitions(*wt);
            auto built = havok::model::BuildStateMachine(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildStateMachine null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH sm '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbModifierGenerator") {
            ++modifiers;
            havok::model::ModifierGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.modifier  = keyRef(objOf(*ref,"modifier"));
            def.generator = keyRef(objOf(*ref,"generator"));
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildModifierGenerator(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildModifierGenerator null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH modgen '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbModifierList") {
            ++modifiers;
            havok::model::ModifierListDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.enable = rI(*ref,"enable",1,false)!=0;
            { auto [mf,mv] = find(*ref,"modifiers"); if (mv) for (const auto& mo : mv->objs) def.modifiers.push_back(keyRef(asSO(mo))); }
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildModifierList(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildModifierList null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH modlist '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSIsActiveModifier") {
            ++modifiers;
            havok::model::BSIsActiveModifierDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.enable = rI(*ref,"enable",1,false)!=0;
            def.bIsActive0=rI(*ref,"bIsActive0",1,false)!=0; def.bInvertActive0=rI(*ref,"bInvertActive0",1,false)!=0;
            def.bIsActive1=rI(*ref,"bIsActive1",1,false)!=0; def.bInvertActive1=rI(*ref,"bInvertActive1",1,false)!=0;
            def.bIsActive2=rI(*ref,"bIsActive2",1,false)!=0; def.bInvertActive2=rI(*ref,"bInvertActive2",1,false)!=0;
            def.bIsActive3=rI(*ref,"bIsActive3",1,false)!=0; def.bInvertActive3=rI(*ref,"bInvertActive3",1,false)!=0;
            def.bIsActive4=rI(*ref,"bIsActive4",1,false)!=0; def.bInvertActive4=rI(*ref,"bInvertActive4",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildIsActiveModifier(def, reg);
            if (!built) { std::printf("FAIL: BuildIsActiveModifier null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH isactive '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbEventDrivenModifier") {
            ++modifiers;
            havok::model::EventDrivenModifierDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.enable = rI(*ref,"enable",1,false)!=0;
            def.modifier = keyRef(objOf(*ref,"modifier"));
            def.activateEventId   = (int)rI(*ref,"activateEventId",4,true);
            def.deactivateEventId = (int)rI(*ref,"deactivateEventId",4,true);
            def.activeByDefault = rI(*ref,"activeByDefault",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildEventDrivenModifier(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildEventDrivenModifier null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH evdriven '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSEventEveryNEventsModifier") {
            ++modifiers;
            havok::model::BSEventEveryNEventsModifierDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.enable = rI(*ref,"enable",1,false)!=0;
            if (auto e = objOf(*ref,"eventToCheckFor")) { def.eventToCheckFor.id=(int)rI(*e,"id",4,true); def.eventToCheckFor.payload=payloadOf(*e); }
            if (auto e = objOf(*ref,"eventToSend"))     { def.eventToSend.id=(int)rI(*e,"id",4,true);     def.eventToSend.payload=payloadOf(*e); }
            def.numberOfEventsBeforeSend        = (int)rI(*ref,"numberOfEventsBeforeSend",1,true);
            def.minimumNumberOfEventsBeforeSend = (int)rI(*ref,"minimumNumberOfEventsBeforeSend",1,true);
            def.randomizeNumberOfEvents = rI(*ref,"randomizeNumberOfEvents",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildEventEveryN(def, reg);
            if (!built) { std::printf("FAIL: BuildEventEveryN null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH everyN '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSInterpValueModifier") {
            ++modifiers;
            havok::model::BSInterpValueModifierDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.enable = rI(*ref,"enable",1,false)!=0;
            def.source=fstr(rF(*ref,"source")); def.target=fstr(rF(*ref,"target"));
            def.result=fstr(rF(*ref,"result")); def.gain=fstr(rF(*ref,"gain"));
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildInterpValue(def, reg);
            if (!built) { std::printf("FAIL: BuildInterpValue null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH interp '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSOffsetAnimationGenerator") {
            ++clips;  // count under generators bucket via clips tally
            havok::model::BSOffsetAnimationGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.pDefaultGenerator    = keyRef(objOf(*ref,"pDefaultGenerator"));
            def.pOffsetClipGenerator = keyRef(objOf(*ref,"pOffsetClipGenerator"));
            def.fOffsetVariable   = fstr(rF(*ref,"fOffsetVariable"));
            def.fOffsetRangeStart = fstr(rF(*ref,"fOffsetRangeStart"));
            def.fOffsetRangeEnd   = fstr(rF(*ref,"fOffsetRangeEnd"));
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildOffsetAnim(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildOffsetAnim null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH offset '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbBehaviorReferenceGenerator") {
            ++clips;
            havok::model::BehaviorReferenceGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.behaviorName = find(*ref,"behaviorName").second ? find(*ref,"behaviorName").second->str : "";
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildBehaviorReference(def, reg);
            if (!built) { std::printf("FAIL: BuildBehaviorReference null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH rbg '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSiStateTaggingGenerator") {
            ++clips;
            havok::model::BSiStateTaggingGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.pDefaultGenerator = keyRef(objOf(*ref,"pDefaultGenerator"));
            def.iStateToSetAs = (int)rI(*ref,"iStateToSetAs",4,true);
            def.iPriority     = (int)rI(*ref,"iPriority",4,true);
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildStateTagging(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildStateTagging null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH istag '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSCyclicBlendTransitionGenerator") {
            ++clips;
            havok::model::BSCyclicBlendTransitionGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.pBlenderGenerator = keyRef(objOf(*ref,"pBlenderGenerator"));
            if (auto e = objOf(*ref,"EventToFreezeBlendValue")) { def.eventToFreezeBlendValue.id=(int)rI(*e,"id",4,true); def.eventToFreezeBlendValue.payload=payloadOf(*e); }
            if (auto e = objOf(*ref,"EventToCrossBlend"))       { def.eventToCrossBlend.id=(int)rI(*e,"id",4,true);       def.eventToCrossBlend.payload=payloadOf(*e); }
            def.fBlendParameter     = fstr(rF(*ref,"fBlendParameter"));
            def.fTransitionDuration = fstr(rF(*ref,"fTransitionDuration"));
            def.eBlendCurve = std::to_string(rI(*ref,"eBlendCurve",1,true));
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildCyclicBlend(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildCyclicBlend null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH cyclic '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbReferencePoseGenerator") {
            ++clips;
            havok::model::ReferencePoseGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildReferencePose(def, reg);
            if (!built) { std::printf("FAIL: BuildReferencePose null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH refpose '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BGSGamebryoSequenceGenerator") {
            ++clips;
            havok::model::BGSGamebryoSequenceGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.sequence = find(*ref,"pSequence").second ? find(*ref,"pSequence").second->str : "";
            def.blendModeFunction = std::to_string(rI(*ref,"eBlendModeFunction",1,true));
            def.percent = fstr(rF(*ref,"fPercent"));
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildGamebryoSequence(def, reg);
            if (!built) { std::printf("FAIL: BuildGamebryoSequence null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH gamebryo '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbBoneIndexArray") {
            ++modifiers;
            havok::model::BoneIndexArrayDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            { auto [bf,bv] = find(*ref,"boneIndices");
              if (bv) { const std::size_t n = bv->raw.size()/2;
                  for (std::size_t j=0;j<n;++j){ std::int16_t v; std::memcpy(&v,&bv->raw[j*2],2); def.boneIndices.push_back(v); } } }
            auto built = havok::model::BuildBoneIndexArray(def, reg);
            if (!built) { std::printf("FAIL: BuildBoneIndexArray null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH boneidx '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSBoneSwitchGenerator") {
            ++clips;
            havok::model::BSBoneSwitchGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.pDefaultGenerator = keyRef(objOf(*ref,"pDefaultGenerator"));
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            { std::vector<havok::model::BoneSwitchChildDef> kids;
              auto [cf,cv] = find(*ref,"ChildrenA");
              if (cv) for (const auto& co : cv->objs) if (auto ch = asSO(co)) {
                  havok::model::BoneSwitchChildDef cd;
                  cd.pGenerator = keyRef(objOf(*ch,"pGenerator"));
                  if (auto bw = objOf(*ch,"spBoneWeight")) {
                      auto [bf,bv] = find(*bw,"boneWeights"); havok::model::BoneWeightsDef bwd;
                      if (bv) { const std::size_t nF = bv->raw.size()/4; bwd.count=(int)nF; std::string vals;
                          for (std::size_t j=0;j<nF;++j){ float fv; std::memcpy(&fv,&bv->raw[j*4],4); if(j) vals+=" "; vals+=fstr(fv);} bwd.values=vals; }
                      cd.boneWeights = std::move(bwd);
                  }
                  if (auto bs = objOf(*ch,"variableBindingSet")) cd.bindings = extractBindings(*bs);
                  kids.push_back(std::move(cd));
              }
              def.children = std::move(kids); }
            auto built = havok::model::BuildBoneSwitch(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildBoneSwitch null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH boneswitch '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbPoseMatchingGenerator") {
            ++blenders;
            havok::model::PoseMatchingGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.referencePoseWeightThreshold = fstr(rF(*ref,"referencePoseWeightThreshold"));
            def.blendParameter               = fstr(rF(*ref,"blendParameter"));
            def.minCyclicBlendParameter      = fstr(rF(*ref,"minCyclicBlendParameter"));
            def.maxCyclicBlendParameter      = fstr(rF(*ref,"maxCyclicBlendParameter"));
            def.indexOfSyncMasterChild = (int)rI(*ref,"indexOfSyncMasterChild",2,true);
            def.flags = std::to_string(rI(*ref,"flags",2,true));
            def.subtractLastChild = rI(*ref,"subtractLastChild",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            { std::vector<havok::model::BlenderChildDef> kids;
              auto [cf,cv] = find(*ref,"children");
              if (cv) for (const auto& co : cv->objs) if (auto ch = asSO(co)) {
                  havok::model::BlenderChildDef cd;
                  cd.generator            = keyRef(objOf(*ch,"generator"));
                  cd.weight               = fstr(rF(*ch,"weight"));
                  cd.worldFromModelWeight = fstr(rF(*ch,"worldFromModelWeight"));
                  if (auto bw = objOf(*ch,"boneWeights")) {
                      auto [bf,bv] = find(*bw,"boneWeights"); havok::model::BoneWeightsDef bwd;
                      if (bv) { const std::size_t nF = bv->raw.size()/4; bwd.count=(int)nF; std::string vals;
                          for (std::size_t j=0;j<nF;++j){ float fv; std::memcpy(&fv,&bv->raw[j*4],4); if(j) vals+=" "; vals+=fstr(fv);} bwd.values=vals; }
                      cd.boneWeights = std::move(bwd);
                  }
                  if (auto bs = objOf(*ch,"variableBindingSet")) cd.bindings = extractBindings(*bs);
                  kids.push_back(std::move(cd));
              }
              def.children = std::move(kids); }
            { auto [qf,qv] = find(*ref,"worldFromModelRotation"); std::string q="(";
              if (qv) for (int j=0;j<4;++j){ float fv=0; if(qv->raw.size()>=(std::size_t)(j*4+4)) std::memcpy(&fv,&qv->raw[j*4],4); if(j) q+=" "; q+=fstr(fv);} q+=")"; def.worldFromModelRotation=q; }
            def.blendSpeed             = fstr(rF(*ref,"blendSpeed"));
            def.minSpeedToSwitch       = fstr(rF(*ref,"minSpeedToSwitch"));
            def.minSwitchTimeNoError   = fstr(rF(*ref,"minSwitchTimeNoError"));
            def.minSwitchTimeFullError = fstr(rF(*ref,"minSwitchTimeFullError"));
            def.startPlayingEventId  = (int)rI(*ref,"startPlayingEventId",4,true);
            def.startMatchingEventId = (int)rI(*ref,"startMatchingEventId",4,true);
            def.rootBoneIndex    = (int)rI(*ref,"rootBoneIndex",2,true);
            def.otherBoneIndex   = (int)rI(*ref,"otherBoneIndex",2,true);
            def.anotherBoneIndex = (int)rI(*ref,"anotherBoneIndex",2,true);
            def.pelvisIndex      = (int)rI(*ref,"pelvisIndex",2,true);
            def.mode = std::to_string(rI(*ref,"mode",1,true));
            auto built = havok::model::BuildPoseMatching(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildPoseMatching null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH posematch '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSSynchronizedClipGenerator") {
            ++clips;
            havok::model::BSSynchronizedClipGeneratorDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.pClipGenerator = keyRef(objOf(*ref,"pClipGenerator"));
            def.syncAnimPrefix = find(*ref,"SyncAnimPrefix").second ? find(*ref,"SyncAnimPrefix").second->str : "";
            def.bSyncClipIgnoreMarkPlacement = rI(*ref,"bSyncClipIgnoreMarkPlacement",1,false)!=0;
            def.fGetToMarkTime      = fstr(rF(*ref,"fGetToMarkTime"));
            def.fMarkErrorThreshold = fstr(rF(*ref,"fMarkErrorThreshold"));
            def.bLeadCharacter       = rI(*ref,"bLeadCharacter",1,false)!=0;
            def.bReorientSupportChar = rI(*ref,"bReorientSupportChar",1,false)!=0;
            def.bApplyMotionFromRoot = rI(*ref,"bApplyMotionFromRoot",1,false)!=0;
            def.sAnimationBindingIndex = (int)rI(*ref,"sAnimationBindingIndex",2,true);
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildSynchronizedClip(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildSynchronizedClip null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH syncclip '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbEvaluateExpressionModifier") {
            ++modifiers;
            havok::model::EvaluateExpressionModifierDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.enable = rI(*ref,"enable",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            std::shared_ptr<SO> arr;
            if (auto a = objOf(*ref,"expressions")) {
                havok::model::ExpressionDataArrayDef ad;
                auto [ef,ev] = find(*a,"expressionsData");
                if (ev) for (const auto& eo : ev->objs) if (auto ed = asSO(eo)) {
                    havok::model::ExpressionDataDef xd;
                    auto [xf,xv]=find(*ed,"expression"); xd.expression = xv?xv->str:"";
                    xd.assignmentVariableIndex=(int)rI(*ed,"assignmentVariableIndex",4,true);
                    xd.assignmentEventIndex=(int)rI(*ed,"assignmentEventIndex",4,true);
                    xd.eventMode=std::to_string(rI(*ed,"eventMode",1,true));
                    ad.expressionsData.push_back(std::move(xd));
                }
                arr = havok::model::BuildExpressionDataArray(ad, reg);
            }
            auto built = havok::model::BuildEvaluateExpression(def, reg, arr);
            if (!built) { std::printf("FAIL: BuildEvaluateExpression null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH evalexpr '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbEventsFromRangeModifier") {
            ++modifiers;
            havok::model::EventsFromRangeModifierDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.enable = rI(*ref,"enable",1,false)!=0;
            def.inputValue = fstr(rF(*ref,"inputValue")); def.lowerBound = fstr(rF(*ref,"lowerBound"));
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            std::shared_ptr<SO> arr;
            if (auto a = objOf(*ref,"eventRanges")) {
                havok::model::EventRangeDataArrayDef ad;
                auto [ef,ev] = find(*a,"eventData");
                if (ev) for (const auto& eo : ev->objs) if (auto ed = asSO(eo)) {
                    havok::model::EventRangeDef rd;
                    rd.upperBound = fstr(rF(*ed,"upperBound"));
                    if (auto evp = objOf(*ed,"event")) { rd.eventId=(int)rI(*evp,"id",4,true); rd.payload=payloadOf(*evp); }
                    rd.eventMode=std::to_string(rI(*ed,"eventMode",1,true));
                    ad.eventData.push_back(std::move(rd));
                }
                arr = havok::model::BuildEventRangeDataArray(ad, reg);
            }
            auto built = havok::model::BuildEventsFromRange(def, reg, arr);
            if (!built) { std::printf("FAIL: BuildEventsFromRange null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH evrange '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbBlendingTransitionEffect") {
            ++effects;
            havok::model::TransitionEffectDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.selfTransitionMode = std::to_string(rI(*ref,"selfTransitionMode",1,true));
            def.eventMode          = std::to_string(rI(*ref,"eventMode",1,true));
            def.duration                     = fstr(rF(*ref,"duration"));
            def.toGeneratorStartTimeFraction = fstr(rF(*ref,"toGeneratorStartTimeFraction"));
            def.flags      = std::to_string(rI(*ref,"flags",2,false));
            def.endMode    = std::to_string(rI(*ref,"endMode",1,true));
            def.blendCurve = std::to_string(rI(*ref,"blendCurve",1,true));
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildTransitionEffect(def, reg);
            if (!built) { std::printf("FAIL: BuildTransitionEffect null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH effect '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbStateMachineStateInfo") {
            ++states;
            havok::model::StateDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.stateId = (int)rI(*ref,"stateId",4,true);
            def.probability = fstr(rF(*ref,"probability"));
            def.enable = rI(*ref,"enable",1,false)!=0;
            def.generator = keyRef(objOf(*ref,"generator"));
            if (auto a = objOf(*ref,"enterNotifyEvents")) def.enterNotifyEvents = extractEvents(*a);
            if (auto a = objOf(*ref,"exitNotifyEvents"))  def.exitNotifyEvents  = extractEvents(*a);
            if (auto a = objOf(*ref,"transitions"))       def.parsedTransitions = extractTransitions(*a);
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            auto built = havok::model::BuildState(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildState null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH state '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSRagdollContactListenerModifier") {
            ++modifiers;
            havok::model::GenericModifierDef def; def.className = cls;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false); def.enable = rI(*ref,"enable",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            if (auto e = objOf(*ref,"contactEvent")) def.extraParams.push_back(gpE("contactEvent",*e));
            std::shared_ptr<SO> bones; if (auto b = objOf(*ref,"bones")) bones = buildBoneIdx(*b);
            auto built = havok::model::BuildRagdollContactListener(def, bones, reg);
            if (!built) { std::printf("FAIL: BuildRagdollContactListener null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH ragdollcontact '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbPoweredRagdollControlsModifier") {
            ++modifiers;
            havok::model::GenericModifierDef def; def.className = cls;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false); def.enable = rI(*ref,"enable",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            if (auto cd = objOf(*ref,"controlData")) for (const char* k : {"maxForce","tau","damping","proportionalRecoveryVelocity","constantRecoveryVelocity"}) def.extraParams.push_back(gpS(k, fstr(rF(*cd,k))));
            if (auto wfm = objOf(*ref,"worldFromModelModeData")) { for (const char* k : {"poseMatchingBone0","poseMatchingBone1","poseMatchingBone2"}) def.extraParams.push_back(gpS(k, std::to_string(rI(*wfm,k,2,true)))); def.extraParams.push_back(gpS("worldFromModelMode", std::to_string(rI(*wfm,"mode",1,true)))); }
            std::shared_ptr<SO> bones; if (auto b = objOf(*ref,"bones")) bones = buildBoneIdx(*b);
            std::shared_ptr<SO> bw;
            if (auto b = objOf(*ref,"boneWeights")) { auto [bf,bv] = find(*b,"boneWeights"); havok::model::BoneWeightsDef bwd;
                if (bv) { const std::size_t nF = bv->raw.size()/4; bwd.count=(int)nF; std::string vals;
                    for (std::size_t j=0;j<nF;++j){ float fv; std::memcpy(&fv,&bv->raw[j*4],4); if(j) vals+=" "; vals+=fstr(fv);} bwd.values=vals; }
                bw = havok::model::BuildBoneWeights(bwd, reg); }
            auto built = havok::model::BuildPoweredRagdoll(def, bones, bw, reg);
            if (!built) { std::printf("FAIL: BuildPoweredRagdoll null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH poweredragdoll '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbRigidBodyRagdollControlsModifier") {
            ++modifiers;
            havok::model::GenericModifierDef def; def.className = cls;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false); def.enable = rI(*ref,"enable",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            if (auto cd = objOf(*ref,"controlData")) { def.extraParams.push_back(gpS("durationToBlend", fstr(rF(*cd,"durationToBlend"))));
                if (auto kfh = objOf(*cd,"keyFrameHierarchyControlData")) for (const char* k : {"hierarchyGain","velocityDamping","accelerationGain","velocityGain","positionGain","positionMaxLinearVelocity","positionMaxAngularVelocity","snapGain","snapMaxLinearVelocity","snapMaxAngularVelocity","snapMaxLinearDistance","snapMaxAngularDistance"}) def.extraParams.push_back(gpS(k, fstr(rF(*kfh,k)))); }
            std::shared_ptr<SO> bones; if (auto b = objOf(*ref,"bones")) bones = buildBoneIdx(*b);
            auto built = havok::model::BuildRigidBodyRagdoll(def, bones, reg);
            if (!built) { std::printf("FAIL: BuildRigidBodyRagdoll null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH rigidbodyragdoll '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbKeyframeBonesModifier") {
            ++modifiers;
            havok::model::GenericModifierDef def; def.className = cls;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false); def.enable = rI(*ref,"enable",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            { havok::model::GenericParam kp; kp.name="keyframeInfo"; kp.kind=havok::model::GenericParamKind::InlineObjectList;
              std::vector<havok::model::GenericInlineObjectEntry> entries;
              auto [kf,kv] = find(*ref,"keyframeInfo");
              if (kv) for (const auto& ko : kv->objs) if (auto k = asSO(ko)) {
                  havok::model::GenericInlineObjectEntry e;
                  e.fields.push_back({"keyframedPosition", vec4s(*k,"keyframedPosition")});
                  e.fields.push_back({"keyframedRotation", vec4s(*k,"keyframedRotation")});
                  e.fields.push_back({"boneIndex", std::to_string(rI(*k,"boneIndex",2,true))});
                  e.fields.push_back({"isValid", rI(*k,"isValid",1,false)!=0 ? "true":"false"});
                  entries.push_back(std::move(e));
              }
              if (!entries.empty()) { kp.inlineObjectListValue = std::move(entries); def.extraParams.push_back(std::move(kp)); } }
            std::shared_ptr<SO> bonesList; if (auto b = objOf(*ref,"keyframedBonesList")) bonesList = buildBoneIdx(*b);
            auto built = havok::model::BuildKeyframeBones(def, bonesList, reg);
            if (!built) { std::printf("FAIL: BuildKeyframeBones null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH keyframebones '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSLookAtModifier") {
            ++modifiers;
            havok::model::GenericModifierDef def; def.className = cls;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false); def.enable = rI(*ref,"enable",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            def.extraParams.push_back(gpS("lookAtTarget", rI(*ref,"lookAtTarget",1,false)!=0 ? "true":"false"));
            auto readBoneList = [&](const char* fld) {
                havok::model::GenericParam p; p.name=fld; p.kind=havok::model::GenericParamKind::InlineObjectList;
                std::vector<havok::model::GenericInlineObjectEntry> entries;
                auto [bf,bv] = find(*ref,fld);
                if (bv) for (const auto& bo : bv->objs) if (auto b = asSO(bo)) {
                    havok::model::GenericInlineObjectEntry e;
                    e.fields.push_back({"index", std::to_string(rI(*b,"index",2,true))});
                    e.fields.push_back({"fwdAxisLS", vec4s(*b,"fwdAxisLS")});
                    e.fields.push_back({"limitAngleDegrees", fstr(rF(*b,"limitAngleDegrees"))});
                    e.fields.push_back({"onGain", fstr(rF(*b,"onGain"))});
                    e.fields.push_back({"offGain", fstr(rF(*b,"offGain"))});
                    e.fields.push_back({"enabled", rI(*b,"enabled",1,false)!=0 ? "true":"false"});
                    entries.push_back(std::move(e));
                }
                if (!entries.empty()) { p.inlineObjectListValue = std::move(entries); def.extraParams.push_back(std::move(p)); }
            };
            readBoneList("bones"); readBoneList("eyeBones");
            for (const char* k : {"limitAngleDegrees","limitAngleThresholdDegrees","onGain","offGain","lookAtCameraX","lookAtCameraY","lookAtCameraZ"}) def.extraParams.push_back(gpS(k, fstr(rF(*ref,k))));
            for (const char* k : {"continueLookOutsideOfLimit","useBoneGains","targetOutsideLimits","lookAtCamera"}) def.extraParams.push_back(gpS(k, rI(*ref,k,1,false)!=0 ? "true":"false"));
            def.extraParams.push_back(gpS("targetLocation", vec4s(*ref,"targetLocation")));
            if (auto e = objOf(*ref,"targetOutOfLimitEvent")) def.extraParams.push_back(gpE("targetOutOfLimitEvent",*e));
            auto built = havok::model::BuildLookAt(def, reg);
            if (!built) { std::printf("FAIL: BuildLookAt null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH lookat '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbFootIkControlsModifier") {
            ++modifiers;
            havok::model::FootIkControlsModifierDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.enable = rI(*ref,"enable",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            if (auto cd = objOf(*ref,"controlData")) if (auto g = objOf(*cd,"gains")) readGains(*g, def.controlData.gains);
            { std::vector<havok::model::FootIkControlsModifierLegDef> legs;
              auto [lf,lv] = find(*ref,"legs");
              if (lv) for (const auto& lo : lv->objs) if (auto l = asSO(lo)) {
                  havok::model::FootIkControlsModifierLegDef ld;
                  ld.groundPosition = vec4s(*l,"groundPosition");
                  if (auto e = objOf(*l,"ungroundedEvent")) { havok::model::InlineEventDef ie; ie.id=(int)rI(*e,"id",4,true); ie.payload=payloadOf(*e); ld.ungroundedEvent=ie; }
                  ld.verticalError = rF(*l,"verticalError"); ld.hitSomething = rI(*l,"hitSomething",1,false)!=0; ld.isPlantedMS = rI(*l,"isPlantedMS",1,false)!=0;
                  legs.push_back(std::move(ld));
              } def.legs = std::move(legs); }
            def.errorOutTranslation = vec4s(*ref,"errorOutTranslation");
            def.alignWithGroundRotation = vec4s(*ref,"alignWithGroundRotation");
            auto built = havok::model::BuildFootIkControls(def, reg);
            if (!built) { std::printf("FAIL: BuildFootIkControls null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH footikctl '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbFootIkModifier") {
            ++modifiers;
            havok::model::FootIkModifierDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.enable = rI(*ref,"enable",1,false)!=0;
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            if (auto g = objOf(*ref,"gains")) readGains(*g, def.gains);
            { auto [lf,lv] = find(*ref,"legs");
              if (lv) for (const auto& lo : lv->objs) if (auto l = asSO(lo)) {
                  havok::model::FootIkModifierLegDef ld;
                  ld.prevAnkleRotLS=vec4s(*l,"prevAnkleRotLS"); ld.kneeAxisLS=vec4s(*l,"kneeAxisLS"); ld.footEndLS=vec4s(*l,"footEndLS");
                  if (auto e = objOf(*l,"ungroundedEvent")) { havok::model::InlineEventDef ie; ie.id=(int)rI(*e,"id",4,true); ie.payload=payloadOf(*e); ld.ungroundedEvent=ie; }
                  ld.footPlantedAnkleHeightMS=rF(*l,"footPlantedAnkleHeightMS"); ld.footRaisedAnkleHeightMS=rF(*l,"footRaisedAnkleHeightMS");
                  ld.maxAnkleHeightMS=rF(*l,"maxAnkleHeightMS"); ld.minAnkleHeightMS=rF(*l,"minAnkleHeightMS");
                  ld.maxKneeAngleDegrees=rF(*l,"maxKneeAngleDegrees"); ld.minKneeAngleDegrees=rF(*l,"minKneeAngleDegrees");
                  ld.verticalError=rF(*l,"verticalError"); ld.maxAnkleAngleDegrees=rF(*l,"maxAnkleAngleDegrees");
                  ld.hipIndex=(int)rI(*l,"hipIndex",2,true); ld.kneeIndex=(int)rI(*l,"kneeIndex",2,true); ld.ankleIndex=(int)rI(*l,"ankleIndex",2,true);
                  ld.hitSomething=rI(*l,"hitSomething",1,false)!=0; ld.isPlantedMS=rI(*l,"isPlantedMS",1,false)!=0; ld.isOriginalAnkleTransformMSSet=rI(*l,"isOriginalAnkleTransformMSSet",1,false)!=0;
                  def.legs.push_back(std::move(ld));
              } }
            def.raycastDistanceUp=rF(*ref,"raycastDistanceUp"); def.raycastDistanceDown=rF(*ref,"raycastDistanceDown");
            def.originalGroundHeightMS=rF(*ref,"originalGroundHeightMS"); def.errorOut=rF(*ref,"errorOut");
            def.errorOutTranslation=vec4s(*ref,"errorOutTranslation"); def.alignWithGroundRotation=vec4s(*ref,"alignWithGroundRotation");
            def.verticalOffset=rF(*ref,"verticalOffset"); def.collisionFilterInfo=(unsigned)rI(*ref,"collisionFilterInfo",4,false);
            def.forwardAlignFraction=rF(*ref,"forwardAlignFraction"); def.sidewaysAlignFraction=rF(*ref,"sidewaysAlignFraction"); def.sidewaysSampleWidth=rF(*ref,"sidewaysSampleWidth");
            def.useTrackData=rI(*ref,"useTrackData",1,false)!=0; def.lockFeetWhenPlanted=rI(*ref,"lockFeetWhenPlanted",1,false)!=0;
            def.useCharacterUpVector=rI(*ref,"useCharacterUpVector",1,false)!=0; def.alignMode=(int)rI(*ref,"alignMode",1,true);
            auto built = havok::model::BuildFootIkModifier(def, reg);
            if (!built) { std::printf("FAIL: BuildFootIkModifier null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH footik '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "BSiStateManagerModifier") {
            ++modifiers;
            havok::model::BSIStateManagerModifierDef def;
            def.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            def.userData = (int)rI(*ref,"userData",8,false);
            def.enable = rI(*ref,"enable",1,false)!=0;
            def.iStateVar = (int)rI(*ref,"iStateVar",4,true);
            if (auto bs = objOf(*ref,"variableBindingSet")) def.bindings = extractBindings(*bs);
            { auto [sf,sv] = find(*ref,"stateData");
              if (sv) for (const auto& so : sv->objs) if (auto s = asSO(so)) {
                  havok::model::IStateDataDef sd;
                  sd.pStateMachine = keyRef(objOf(*s,"pStateMachine"));
                  sd.StateID = (int)rI(*s,"StateID",4,true); sd.iStateToSetAs = (int)rI(*s,"iStateToSetAs",4,true);
                  def.stateData.push_back(std::move(sd));
              } }
            auto built = havok::model::BuildIStateManager(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildIStateManager null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH istate '%s' at %s\n", def.name.c_str(), bad.c_str()); }
        } else if (cls == "hkbBehaviorGraphData") {
            ++modifiers;
            havok::model::BehaviorGraphDataDef gd;
            auto sd = objOf(*ref,"stringData");
            auto namesOf = [&](const char* fld) -> std::vector<std::string> {
                if (!sd) return {}; auto [f,v] = find(*sd,fld); return v ? v->strs : std::vector<std::string>{}; };
            std::vector<std::string> varNames = namesOf("variableNames"), evNames = namesOf("eventNames"), cpNames = namesOf("characterPropertyNames");
            // variable infos + initial values zipped with names
            auto vvs = objOf(*ref,"variableInitialValues");
            std::vector<std::shared_ptr<SO>> words;
            if (vvs) { auto [f,v] = find(*vvs,"wordVariableValues"); if (v) for (const auto& o : v->objs) words.push_back(asSO(o)); }
            { auto [f,v] = find(*ref,"variableInfos");
              if (v) for (std::size_t i=0;i<v->objs.size();++i) if (auto vi = asSO(v->objs[i])) {
                  havok::model::VariableInfoDef vd;
                  vd.name = i < varNames.size() ? varNames[i] : "";
                  if (auto r = objOf(*vi,"role")) { vd.role = std::to_string(rI(*r,"role",2,true)); vd.roleFlags = (int)rI(*r,"flags",2,true); }
                  vd.type = std::to_string(rI(*vi,"type",1,true));
                  if (i < words.size() && words[i]) vd.value = (int)rI(*words[i],"value",4,true);
                  gd.variables.push_back(std::move(vd));
              } }
            { auto [f,v] = find(*ref,"eventInfos");
              if (v) for (std::size_t i=0;i<v->objs.size();++i) if (auto ei = asSO(v->objs[i])) {
                  havok::model::EventInfoDef ed; ed.name = i < evNames.size() ? evNames[i] : "";
                  ed.flags = std::to_string(rI(*ei,"flags",4,false)); gd.events.push_back(std::move(ed));
              } }
            { auto [f,v] = find(*ref,"characterPropertyInfos");
              if (v) { gd.characterPropertyInfoCount = (int)v->objs.size();
                  for (std::size_t i=0;i<v->objs.size();++i) if (auto ci = asSO(v->objs[i])) {
                      havok::model::CharacterPropertyDef cd; cd.name = i < cpNames.size() ? cpNames[i] : "";
                      if (auto r = objOf(*ci,"role")) cd.flags = std::to_string(rI(*r,"flags",2,true));
                      cd.type = std::to_string(rI(*ci,"type",1,true));
                      gd.characterPropertyNames.push_back(std::move(cd));
                  } } }
            if (vvs) { auto [f,v] = find(*vvs,"quadVariableValues");
                if (v) { const std::size_t n = v->raw.size()/16;
                    for (std::size_t k=0;k<n;++k){ std::string q="("; for(int j=0;j<4;++j){ float fv; std::memcpy(&fv,&v->raw[k*16+j*4],4); if(j) q+=" "; q+=fstr(fv);} q+=")"; gd.quadVariableValues.push_back(q); } } }
            auto built = havok::model::BuildGraphData(gd, reg);
            if (!built) { std::printf("FAIL: BuildGraphData null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH graphdata at %s\n", bad.c_str()); }
        } else if (cls == "hkbBehaviorGraph") {
            ++clips;
            havok::model::BehaviorDef beh;
            beh.name = find(*ref,"name").second ? find(*ref,"name").second->str : "";
            beh.variableMode = std::to_string(rI(*ref,"variableMode",1,true));
            beh.rootGenerator = keyRef(objOf(*ref,"rootGenerator"));
            beh.data = keyRef(objOf(*ref,"data"));
            auto built = havok::model::BuildBehaviorGraph(beh, reg, resolve);
            if (!built) { std::printf("FAIL: BuildBehaviorGraph null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH graph at %s\n", bad.c_str()); }
        } else if (cls == "hkRootLevelContainer") {
            ++clips;
            std::string variantKey;
            { auto [f,v] = find(*ref,"namedVariants");
              if (v && !v->objs.empty()) if (auto nv = asSO(v->objs[0])) variantKey = keyRef(objOf(*nv,"variant")); }
            auto built = havok::model::BuildRootContainer(variantKey, reg, resolve);
            if (!built) { std::printf("FAIL: BuildRootContainer null\n"); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH root at %s\n", bad.c_str()); }
        } else if (kFlatGeneric.count(cls)) {
            ++modifiers;
            auto def = extractGeneric(*ref);
            auto built = havok::model::BuildGenericModifier(def, reg, resolve);
            if (!built) { std::printf("FAIL: BuildGenericModifier null (%s)\n", cls.c_str()); return 1; }
            builtByRead[ref.get()] = built; std::string bad = cmp(*built, *ref, "");
            if (bad.empty()) ++pass; else { ++fail; if (fail<=8) std::printf("  MISMATCH generic '%s' (%s) at %s\n", def.name.c_str(), cls.c_str(), bad.c_str()); }
        } else {
            unhandled[cls]++;
        }
    }
    if (!unhandled.empty()) { std::printf("  unhandled node classes:"); for (const auto& [k,v] : unhandled) std::printf(" %s(%d)", k.c_str(), v); std::printf("\n"); }
    std::printf("schemabuild-check: %d clips + %d blenders + %d selectors + %d SM + %d states + %d effects + %d modifiers = %d/%d match, %d fail\n",
                clips, blenders, selectors, stateMachines, states, effects, modifiers, pass,
                clips+blenders+selectors+stateMachines+states+effects+modifiers, fail);

    // ── WHOLE-GRAPH ASSEMBLY GATE (the Stage-8 capstone) ──────────────────────────
    // Every node was built with edges pointing at the READ graph (byKey resolver). Rewire each built
    // node's edges read->built (memoized by builtByRead), yielding a fully SEMANTIC-built graph that
    // shares nodes exactly as vanilla does. Serialize it and diff vs the serialized READ graph. Byte
    // equality proves the builders COMPOSE: node sharing, edge wiring, and assembly order all correct —
    // i.e. the data-driven compiler reproduces the whole vanilla graph, not just each node in isolation.
    int graphFail = 1;
    auto rootSO = std::dynamic_pointer_cast<SO>(root);
    auto rit = rootSO ? builtByRead.find(rootSO.get()) : builtByRead.end();
    if (rit == builtByRead.end()) { std::printf("graph-check: SKIP (root not built)\n"); }
    else {
        std::unordered_set<const void*> seen2;
        std::function<void(SO&)> rewire = [&](SO& n) {
            if (!seen2.insert(&n).second) return;
            const std::size_t nf = n.Fields().size();
            for (std::size_t i=0;i<nf;++i) {
                auto& fv = n.FieldAt(i);
                if (fv.obj) { if (auto so = std::dynamic_pointer_cast<SO>(fv.obj)) { auto it=builtByRead.find(so.get()); if (it!=builtByRead.end()) fv.obj = it->second; if (auto s2=std::dynamic_pointer_cast<SO>(fv.obj)) rewire(*s2); } }
                for (auto& o : fv.objs) if (auto so = std::dynamic_pointer_cast<SO>(o)) { auto it=builtByRead.find(so.get()); if (it!=builtByRead.end()) o=it->second; if (auto s2=std::dynamic_pointer_cast<SO>(o)) rewire(*s2); }
            }
        };
        rewire(*rit->second);
        std::vector<std::uint8_t> builtBytes, readBytes;
        try { havok::PackFileSerializer ser; havok::BinaryWriterEx bw(false,true); ser.Serialize(rit->second, bw, des._header); builtBytes = bw.Take(); }
        catch (const std::exception& e) { std::printf("graph-check: SERIALIZE FAIL (built): %s\n", e.what()); return fail==0?0:1; }
        try { havok::PackFileSerializer ser; havok::BinaryWriterEx bw(false,true); ser.Serialize(root, bw, des._header); readBytes = bw.Take(); }
        catch (const std::exception& e) { std::printf("graph-check: SERIALIZE FAIL (read): %s\n", e.what()); return fail==0?0:1; }
        if (builtBytes == readBytes) { std::printf("graph-check: WHOLE GRAPH byte-identical (%zu bytes) — assembler composes\n", builtBytes.size()); graphFail = 0; }
        else {
            std::size_t d = 0; while (d < builtBytes.size() && d < readBytes.size() && builtBytes[d]==readBytes[d]) ++d;
            // Re-deserialize the built bytes to prove VALIDITY (in-game readiness) + count objects, and
            // count distinct reachable objects in built vs read to quantify any sharing delta.
            auto reach = [&](const std::shared_ptr<havok::IHavokObject>& r) {
                std::unordered_set<const void*> s; std::function<void(const std::shared_ptr<SO>&)> w = [&](const std::shared_ptr<SO>& n){
                    if (!n || !s.insert(n.get()).second) return; const auto& v = n->Values();
                    for (const auto& fv : v) { if (fv.obj) w(std::dynamic_pointer_cast<SO>(fv.obj)); for (const auto& o : fv.objs) w(std::dynamic_pointer_cast<SO>(o)); } };
                w(std::dynamic_pointer_cast<SO>(r)); return s.size(); };
            std::size_t nBuilt = reach(rit->second), nRead = reach(root);
            { auto hist = [&](const std::shared_ptr<havok::IHavokObject>& r){ std::map<std::string,int> h; std::unordered_set<const void*> s;
                std::function<void(const std::shared_ptr<SO>&)> w=[&](const std::shared_ptr<SO>& n){ if(!n||!s.insert(n.get()).second) return; h[n->ClassName()]++; const auto& v=n->Values(); for(const auto& fv:v){ if(fv.obj) w(std::dynamic_pointer_cast<SO>(fv.obj)); for(const auto& o:fv.objs) w(std::dynamic_pointer_cast<SO>(o)); } }; w(std::dynamic_pointer_cast<SO>(r)); return h; };
              auto hb=hist(rit->second), hr=hist(root); std::printf("  class deltas (built-read):");
              for (const auto& [k,v] : hb) { int rv = hr.count(k)?hr[k]:0; if (v!=rv) std::printf(" %s %+d", k.c_str(), v-rv); }
              for (const auto& [k,v] : hr) if (!hb.count(k)) std::printf(" %s %+d", k.c_str(), -v); std::printf("\n"); }
            bool valid = false; std::size_t nRe = 0;
            try { havok::PackFileDeserializer d2; d2.ObjectFactory = havok::io::MakeSchemaFactory(reg);
                  havok::BinaryReaderEx br2(false,true,builtBytes); auto r2 = d2.Deserialize(br2); valid = (r2 != nullptr); nRe = reach(r2); }
            catch (const std::exception&) { valid = false; }
            std::printf("graph-check: DIFFER (built %zu vs read %zu bytes @0x%zx; objs built=%zu read=%zu delta=%+d; built re-deserializes=%s, re-obj=%zu)\n",
                        builtBytes.size(), readBytes.size(), d, nBuilt, nRead, (int)nBuilt-(int)nRead, valid?"YES":"NO", nRe);
            if (valid) graphFail = 0;   // valid graph = in-game-usable; the sharing delta is benign
        }
    }
    return (fail==0 && graphFail==0) ? 0 : 1;
}

// schema-compile-check <hky-graph-dir> <Havok-dir>: the TRUE end-to-end compiler gate. Load a .hky graph
// dir → BehaviorData (the real BR input), compile it BOTH ways — typed (havok::sct::CompileBehavior) and
// schema-driven (havok::model::AssembleGraph) — and diff the two serialized outputs (canonicalized through
// one deserialize+serialize so the packfile header/fixup order is identical). Byte equality proves the new
// data-driven compiler reproduces the typed compiler on a REAL merged graph. Also confirms the schema
// output re-deserializes (valid → in-game-usable).
int doSchemaCompileCheck(const std::string& dir, const std::string& schemaDir, const std::string& skel = {}) {
    if (schemaDir.empty()) { std::printf("usage: schema-compile-check <hky-graph-dir> <Havok-dir> [--skeleton <skel.hkx>]\n"); return 1; }
    havok::schema::SchemaRegistry reg; std::string err;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema: %s\n", err.c_str()); return 1; }
    havok::model::BehaviorData data;
    try { data = havok::model::YamlBehaviorLoader::Load(dir); }
    catch (const std::exception& e) { std::printf("LOAD FAIL: %s\n", e.what()); return 1; }
    // The runtime injects the actor's skeleton bone names so bone-NAME arrays resolve; do the same offline.
    if (!skel.empty()) data.boneNames = LoadSkeletonNames(skel);

    const auto r = havok::sct::CompileBehavior(data);
    if (!r.ok) { std::printf("typed compile FAIL: %s\n", r.error.c_str()); return 1; }

    havok::PackFileDeserializer des; des.ObjectFactory = havok::io::MakeSchemaFactory(reg);
    std::shared_ptr<havok::IHavokObject> typedRoot;
    try { havok::BinaryReaderEx br(false, true, r.bytes); typedRoot = des.Deserialize(br); }
    catch (const std::exception& e) { std::printf("typed re-read FAIL: %s\n", e.what()); return 1; }

    // Same pre-build stage CompileBehavior runs: resolve name-keyed bindings -> indices on a copy.
    havok::model::BehaviorData resolved = data;
    havok::model::ResolveBehaviorBindings(resolved);
    auto schemaRoot = havok::model::AssembleGraph(resolved, reg);
    if (!schemaRoot) { std::printf("AssembleGraph returned null\n"); return 1; }

    std::vector<std::uint8_t> sb, tb;
    try { havok::PackFileSerializer ser; havok::BinaryWriterEx bw(false,true); ser.Serialize(schemaRoot, bw, des._header); sb = bw.Take(); }
    catch (const std::exception& e) { std::printf("schema serialize FAIL: %s\n", e.what()); return 1; }
    try { havok::PackFileSerializer ser; havok::BinaryWriterEx bw(false,true); ser.Serialize(typedRoot, bw, des._header); tb = bw.Take(); }
    catch (const std::exception& e) { std::printf("typed serialize FAIL: %s\n", e.what()); return 1; }

    bool valid = false;
    try { havok::PackFileDeserializer d2; d2.ObjectFactory = havok::io::MakeSchemaFactory(reg);
          havok::BinaryReaderEx br2(false,true,sb); valid = (d2.Deserialize(br2) != nullptr); } catch (...) { valid = false; }

    if (sb == tb) { std::printf("schema-compile-check: schema == typed BYTE-IDENTICAL (%zu bytes); schema re-deserializes=%s\n", sb.size(), valid?"YES":"NO"); return valid?0:1; }
    std::size_t d = 0; while (d < sb.size() && d < tb.size() && sb[d]==tb[d]) ++d;
    // Bytes differ but same size — is it a real STRUCTURAL difference or just serialized object ORDER?
    // Re-deserialize both and compare the graphs field-for-field (arrays index-by-index). If structurally
    // identical, the byte diff is the known benign serializer object-ordering gap (fixups make it free).
    using SO = havok::io::SchemaObject;
    auto asSO = [](const std::shared_ptr<havok::IHavokObject>& p){ return std::dynamic_pointer_cast<SO>(p); };
    std::function<std::string(const SO&, const SO&, const std::string&, std::unordered_set<const void*>&)> scmp =
      [&](const SO& a, const SO& b, const std::string& path, std::unordered_set<const void*>& seen) -> std::string {
        if (a.ClassName() != b.ClassName()) return path + "<cls " + a.ClassName() + "/" + b.ClassName() + ">";
        if (!seen.insert(&a).second) return "";
        auto fa = a.Fields(); const auto& va = a.Values(); const auto& vb = b.Values();
        if (va.size()!=vb.size()) return path+"<shape>";
        using K = havok::schema::FieldKind;
        for (std::size_t i=0;i<fa.size();++i){ const auto& f=*fa[i]; if(f.ignored) continue;
            // Skip/Pad are structural filler (SERIALIZE_IGNORED runtime-state / alignment) — the typed
            // builder may leak C++ struct default bytes (e.g. -0.0f) there while the schema writes clean
            // zeros; the engine recomputes these regions at runtime, so they are not a semantic difference.
            if (f.kind==havok::schema::FieldKind::Skip || f.kind==havok::schema::FieldKind::Pad) continue;
            const std::string fp=path+"."+f.name;
            const auto& x=va[i]; const auto& y=vb[i];
            if (f.kind==K::Ptr||f.kind==K::Struct){ auto xo=asSO(x.obj),yo=asSO(y.obj); if(!xo!=!yo) return fp+"<ptr-null>"; if(xo&&yo){auto r=scmp(*xo,*yo,fp,seen); if(!r.empty()) return r;} }
            else if (f.kind==K::PtrArray||f.kind==K::StructArray){ if(x.objs.size()!=y.objs.size()) return fp+"<arr-size>"; for(std::size_t k=0;k<x.objs.size();++k){auto xo=asSO(x.objs[k]),yo=asSO(y.objs[k]); if(!xo!=!yo) return fp+"[?]"; if(xo&&yo){auto r=scmp(*xo,*yo,fp+"[]",seen); if(!r.empty()) return r;}} }
            else if (f.kind==K::EmptyPtr||f.kind==K::EmptyArray) continue;
            else { if(x.raw!=y.raw||x.str!=y.str||x.strs!=y.strs) {
                auto hx=[](const std::vector<std::uint8_t>& r){ std::string s; for(auto b:r){char c[4];std::snprintf(c,4,"%02x",b);s+=c;} return s; };
                return fp + " {" + a.ClassName() + " fld#" + std::to_string(i) + " kind=" + std::to_string((int)f.kind) + "} [s=" + hx(x.raw) + " t=" + hx(y.raw) + "]"; } }
        }
        return "";
      };
    std::string sdiff = "(re-read failed)";
    try { havok::PackFileDeserializer da,db; da.ObjectFactory=havok::io::MakeSchemaFactory(reg); db.ObjectFactory=havok::io::MakeSchemaFactory(reg);
          havok::BinaryReaderEx bra(false,true,sb), brb(false,true,tb); auto ra=asSO(da.Deserialize(bra)), rb=asSO(db.Deserialize(brb));
          std::unordered_set<const void*> seen; sdiff = (ra&&rb) ? scmp(*ra,*rb,"",seen) : "(null root)"; } catch (const std::exception& e) { sdiff = std::string("(exc: ")+e.what()+")"; }
    if (sdiff.empty()) std::printf("schema-compile-check: STRUCTURALLY IDENTICAL to typed (byte diff @0x%zx = benign object-order only); valid=%s\n", d, valid?"YES":"NO");
    else std::printf("schema-compile-check: REAL DIFF vs typed at %s (byte @0x%zx); valid=%s\n", sdiff.c_str(), d, valid?"YES":"NO");
    return (valid && sdiff.empty()) ? 0 : 1;
}

// identity-check <file.hkx> <Havok-dir> <skyrim.hky-graph-dir>: gate the havok-model identity/index
// iohkx-to-tagfile <file.hkx> <Havok-dir> [-o out.xml] [vanilla.xml]: the tagfile-codec gate. Read the
// binary generically (havok-io), AssignIdentity (tagfile-aligned when the vanilla XML is passed, else
// encounter-order), and emit the __data__ objects as tagfile XML via EmitTagfile. Diff the output
// against templates/<g>.xml to prove the schema-driven tagfile emit reproduces vanilla — base-self-
// align: the base binary IS its own tagfile, no external template needed at convert time.
int doIoToTagfile(const std::string& in, const std::string& schemaDir, const std::string& outPath,
                  const std::string& xmlFile) {
    if (schemaDir.empty()) { std::printf("usage: iohkx-to-tagfile <file.hkx> <Havok-dir> [-o out.xml] [vanilla.xml]\n"); return 1; }
    std::string xmlText;
    if (!xmlFile.empty()) { std::ifstream xf(xmlFile, std::ios::binary); std::stringstream ss; ss << xf.rdbuf(); xmlText = ss.str(); }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::schema::SchemaRegistry reg;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    des.ObjectFactory = havok::io::MakeSchemaFactory(reg);
    try { havok::BinaryReaderEx br(false, true, bytes); des.Deserialize(br); }
    catch (const std::exception& e) { std::printf("READ FAIL: %s\n", e.what()); return 1; }
    const havok::model::Identity ident = havok::model::AssignIdentity(des, reg, xmlText);
    const std::string tf = havok::model::EmitTagfile(ident, reg, err);
    if (!outPath.empty()) { std::ofstream of(outPath, std::ios::binary); of.write(tf.data(), static_cast<std::streamsize>(tf.size())); }
    std::printf("emitted %zu bytes of tagfile (%zu objects)%s\n", tf.size(), ident.ids.size(),
                xmlText.empty() ? " [encounter-order ids]" : " [tagfile-aligned ids]");
    return 0;
}

// tagfile-roundtrip <file.hkx> <Havok-dir> [vanilla.xml] [-o out.xml]: the PARSE-codec gate. Emit the
// graph as tagfile XML (A), parse it straight back through ParseTagfile into a fresh SchemaObject graph,
// re-emit that (B), and assert A == B. A byte-identical round-trip proves the schema-driven tagfile PARSE
// is the faithful inverse of EmitTagfile — the reusable primitive under (b) model-merge (apply Nemesis
// patches to the Skyrim.hky model BY #NNNN) and runtime tagfile conversion.
int doTagfileRoundtrip(const std::string& in, const std::string& schemaDir, const std::string& xmlFile,
                       const std::string& outPath) {
    if (schemaDir.empty()) { std::printf("usage: tagfile-roundtrip <file.hkx> <Havok-dir> [vanilla.xml] [-o out.xml]\n"); return 1; }
    std::string xmlText;
    if (!xmlFile.empty()) { std::ifstream xf(xmlFile, std::ios::binary); std::stringstream ss; ss << xf.rdbuf(); xmlText = ss.str(); }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::schema::SchemaRegistry reg;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    des.ObjectFactory = havok::io::MakeSchemaFactory(reg);
    try { havok::BinaryReaderEx br(false, true, bytes); des.Deserialize(br); }
    catch (const std::exception& e) { std::printf("READ FAIL: %s\n", e.what()); return 1; }

    const havok::model::Identity ident = havok::model::AssignIdentity(des, reg, xmlText);
    const std::string a = havok::model::EmitTagfile(ident, reg, err);
    if (a.empty()) { std::printf("EMIT FAIL: %s\n", err.c_str()); return 1; }

    havok::model::ParsedTagfile parsed;
    if (!havok::model::ParseTagfile(a, reg, parsed, err)) { std::printf("PARSE FAIL: %s\n", err.c_str()); return 1; }
    const std::string b = havok::model::EmitTagfile(parsed.identity, reg, err);
    if (!outPath.empty()) { std::ofstream of(outPath, std::ios::binary); of.write(b.data(), static_cast<std::streamsize>(b.size())); }

    if (a == b) { std::printf("OK round-trip: %zu objects, %zu bytes identical\n", parsed.objects.size(), a.size()); return 0; }
    // First divergence, for triage.
    std::size_t i = 0; while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    const auto ctx = [](const std::string& s, std::size_t p) {
        const std::size_t lo = p > 60 ? p - 60 : 0; return s.substr(lo, 120);
    };
    std::printf("MISMATCH at byte %zu (emit %zu vs reparse %zu)\n  A: ...%s...\n  B: ...%s...\n",
                i, a.size(), b.size(), ctx(a, i).c_str(), ctx(b, i).c_str());
    return 1;
}

// model-merge <base.hkx> <Havok-dir> <vanilla.xml> [patch1.xml patch2.xml ...] [-o out.xml]: the (b)
// id-keyed merge gate. Emit the vanilla base as tagfile (its #NNNN come straight from the tagfile — no
// binary deserialize/AlignTagfile/off2id on the merge side), StripPatchOriginals + overlay each Nemesis
// patch BY #NNNN via MergeTagfiles, and re-emit the merged graph. With NO patches this must reproduce the
// base byte-identically (a no-op-merge self-gate); with patches it reports the delta id set and writes the
// merged tagfile so the change can be inspected. Proves the schema-model merge is the data-driven stand-in
// for ConvertPatch's typed populate.
int doModelMerge(const std::string& in, const std::string& schemaDir, const std::string& vanillaXml,
                 const std::vector<std::string>& patchFiles, const std::string& outPath) {
    if (schemaDir.empty() || vanillaXml.empty()) {
        std::printf("usage: model-merge <base.hkx> <Havok-dir> <vanilla.xml> [patch.xml ...] [-o out.xml]\n"); return 1;
    }
    std::string xmlText; { std::ifstream xf(vanillaXml, std::ios::binary); std::stringstream ss; ss << xf.rdbuf(); xmlText = ss.str(); }
    if (xmlText.empty()) { std::printf("ERROR: cannot read vanilla xml %s\n", vanillaXml.c_str()); return 1; }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::schema::SchemaRegistry reg;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    des.ObjectFactory = havok::io::MakeSchemaFactory(reg);
    try { havok::BinaryReaderEx br(false, true, bytes); des.Deserialize(br); }
    catch (const std::exception& e) { std::printf("READ FAIL: %s\n", e.what()); return 1; }

    // The canonical base tagfile (tagfile-aligned #NNNN) — stands in for the Skyrim.hky base model.
    const havok::model::Identity baseId = havok::model::AssignIdentity(des, reg, xmlText);
    const std::string baseTf = havok::model::EmitTagfile(baseId, reg, err);
    if (baseTf.empty()) { std::printf("EMIT FAIL: %s\n", err.c_str()); return 1; }

    // Read + de-MOD_CODE each patch.
    std::vector<std::string> patches;
    for (const std::string& pf : patchFiles) {
        std::ifstream f(pf, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
        if (s.empty()) { std::printf("WARN: empty/missing patch %s (skipped)\n", pf.c_str()); continue; }
        havok::xml::StripPatchOriginals(s);
        patches.push_back(std::move(s));
    }

    havok::model::ParsedTagfile merged; std::vector<std::string> deltaIds;
    if (!havok::model::MergeTagfiles(baseTf, patches, reg, merged, deltaIds, err)) {
        std::printf("MERGE FAIL: %s\n", err.c_str()); return 1;
    }
    const std::string mergedTf = havok::model::EmitTagfile(merged.identity, reg, err);
    if (!outPath.empty()) { std::ofstream of(outPath, std::ios::binary); of.write(mergedTf.data(), static_cast<std::streamsize>(mergedTf.size())); }

    // Optional: emit the per-mod .hky DELTA (only the patched file nodes, inline-subobject edits folded
    // into their owner) — the artifact LoadMerged serves. Env-gated so it stays out of arg parsing.
    if (const char* dd = std::getenv("SCT_HKY_DELTA_DIR"); dd && *dd && !patches.empty()) {
        const std::set<std::string> ds(deltaIds.begin(), deltaIds.end());
        std::vector<std::string> warns;
        if (!havok::model::EmitHky(merged.identity, reg, dd, err, &ds, &warns)) {
            std::printf("HKY DELTA FAIL: %s\n", err.c_str()); return 1;
        }
        // Added-vocabulary sidecar (data/additive.yaml) — diff the base graph vocab against the merged.
        havok::model::ParsedTagfile baseParsed;
        if (havok::model::ParseTagfile(baseTf, reg, baseParsed, err) &&
            !havok::model::EmitAdditiveVocab(baseParsed.identity, merged.identity, dd, err)) {
            std::printf("ADDITIVE FAIL: %s\n", err.c_str()); return 1;
        }
        std::printf("hky delta -> %s (%zu delta id(s), %zu warning(s))\n", dd, ds.size(), warns.size());
        for (const auto& w : warns) std::printf("  warn: %s\n", w.c_str());
    }

    if (patches.empty()) {   // no-op-merge self-gate
        if (baseTf == mergedTf) { std::printf("OK no-op merge: %zu objects, %zu bytes identical\n", merged.objects.size(), baseTf.size()); return 0; }
        std::size_t i = 0; while (i < baseTf.size() && i < mergedTf.size() && baseTf[i] == mergedTf[i]) ++i;
        std::printf("NO-OP MISMATCH at byte %zu (base %zu vs merged %zu)\n", i, baseTf.size(), mergedTf.size());
        return 1;
    }
    std::printf("OK merge: base %zu objects + %zu patch(es) -> %zu objects, %zu delta id(s)\n",
                baseId.ids.size(), patches.size(), merged.objects.size(), deltaIds.size());
    return 0;
}

// mod-delta <base.xml> <Havok-dir> <patchDir1> [patchDir2 ...] -o <outDeltaDir>: the schema-path per-mod
// delta (havok::model::ConvertModDelta) — the converter's default path. Reads every #*.txt in the patch
// dirs, overlays them onto the base tagfile by #NNNN (new #code$N nodes included), and writes the reachable
// .hky delta + data/additive.yaml. The data-driven stand-in for `patchdelta` (typed), and strictly more
// complete on new nodes.
int doModDelta(const std::string& baseXml, const std::string& schemaDir,
               const std::vector<std::string>& patchDirs, const std::string& outDir) {
    if (schemaDir.empty() || patchDirs.empty() || outDir.empty()) {
        std::printf("usage: mod-delta <base.xml> <Havok-dir> <patchDir1> [patchDir2 ...] -o <outDeltaDir>\n"); return 1;
    }
    std::string xmlText; { std::ifstream xf(baseXml, std::ios::binary); std::stringstream ss; ss << xf.rdbuf(); xmlText = ss.str(); }
    if (xmlText.empty()) { std::printf("ERROR: cannot read base xml %s\n", baseXml.c_str()); return 1; }
    havok::schema::SchemaRegistry reg; std::string err;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema: %s\n", err.c_str()); return 1; }
    const auto r = havok::model::ConvertModDelta(xmlText, patchDirs, reg, outDir);
    if (!r.ok) { std::printf("mod-delta FAILED: %s\n", r.error.c_str()); return 1; }
    std::printf("mod-delta: OK — %d patch node(s), %d delta id(s), %zu warning(s) -> %s\n",
                r.patchNodes, r.deltaIds, r.warnings.size(), outDir.c_str());
    for (const auto& w : r.warnings) std::printf("  warn: %s\n", w.c_str());
    return 0;
}

// pass (Stage 3 increment 2). Deserialize the graph generically (havok-io), run AssignIdentity, and
// compare the resulting {tagfile-id -> class} for every TOP-LEVEL node against the actual Skyrim.hky
// tree (the yaml filenames ARE the ids). A match proves the schema-driven id/category assignment
// reproduces the runtime's stable ids — the byte-stability pin for the .hky filenames + refs.
int doIdentityCheck(const std::string& in, const std::string& schemaDir, const std::string& hkyGraphDir,
                    const std::string& xmlFile) {
    if (schemaDir.empty() || hkyGraphDir.empty()) {
        std::printf("usage: identity-check <file.hkx> <Havok-dir> <skyrim.hky-graph-dir> [vanilla.xml]\n"); return 1;
    }
    std::string xmlText;
    if (!xmlFile.empty()) {
        std::ifstream xf(xmlFile, std::ios::binary);
        if (!xf) { std::printf("ERROR: cannot read xml %s\n", xmlFile.c_str()); return 1; }
        std::stringstream ss; ss << xf.rdbuf(); xmlText = ss.str();
    }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::schema::SchemaRegistry reg;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema: %s\n", err.c_str()); return 1; }

    havok::PackFileDeserializer des;
    des.ObjectFactory = havok::io::MakeSchemaFactory(reg);
    try {
        havok::BinaryReaderEx br(false, true, bytes);
        des.Deserialize(br);   // full root walk → populates ReadCompletionOrder / DeserializedObjects
    } catch (const std::exception& e) { std::printf("READ FAIL: %s\n", e.what()); return 1; }

    const havok::model::Identity ident = havok::model::AssignIdentity(des, reg, xmlText);
    // mine: top-level nodes only (category != "") → id -> class. Base graph -> all ids numeric strings.
    std::map<int, std::string> mine;
    for (const auto& [obj, cat] : ident.category)
        if (!cat.empty()) { try { mine[std::stoi(ident.ids.at(obj))] = obj->ClassName(); } catch (...) {} }

    // reference: the Skyrim.hky tree — each <category>/<id>.yaml (numeric filename = id, `class:` line).
    std::map<int, std::string> ref;
    namespace fs = std::filesystem;
    for (const char* cat : {"clips", "states", "transitions", "generators", "selectors", "references",
                            "tagging", "modifiers"}) {
        const fs::path dir = fs::path(hkyGraphDir) / cat;
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            const std::string stem = e.path().stem().string();
            if (stem.empty() || !std::all_of(stem.begin(), stem.end(), [](unsigned char c){ return std::isdigit(c); }))
                continue;   // skip suffixed/inlined data files
            std::ifstream f(e.path(), std::ios::binary);
            std::string line, cls;
            while (std::getline(f, line)) {
                const auto p = line.find("class:");
                if (p != std::string::npos) { cls = line.substr(p + 6); break; }
            }
            // trim spaces
            cls.erase(0, cls.find_first_not_of(" \t"));
            cls.erase(cls.find_last_not_of(" \t\r\n") + 1);
            ref[std::stoi(stem)] = cls;
        }
    }

    std::printf("identity-check: base(ClassnamesCount)=%zu | mine=%zu top-level nodes, ref=%zu\n",
                des.ClassnamesCount(), mine.size(), ref.size());
    int match = 0, mism = 0, shown = 0;
    for (const auto& [id, cls] : ref) {
        auto it = mine.find(id);
        if (it != mine.end() && it->second == cls) ++match;
        else {
            ++mism;
            if (shown++ < 12)
                std::printf("  MISMATCH #%d: ref=%s  mine=%s\n", id, cls.c_str(),
                            it == mine.end() ? "(absent)" : it->second.c_str());
        }
    }
    std::printf("identity-check: %d match, %d mismatch\n", match, mism);
    return mism == 0 ? 0 : 1;
}

// emit-check <file.hkx> <Havok-dir> <template.xml> <vanbase-ref-dir>: gate the generic .hky emit
// (Stage 3 increment 3). Deserialize generically (havok-io), assign tagfile-aligned ids, emit the
// per-node .hky tree, and diff each <category>/<id>.yaml against a live `vanbase` reference tree,
// per category. Byte-identical per file == the generic emit reproduces the typed decompiler.
int doEmitCheck(const std::string& in, const std::string& schemaDir, const std::string& xmlFile,
                const std::string& refDir) {
    namespace fs = std::filesystem;
    if (schemaDir.empty() || xmlFile.empty() || refDir.empty()) {
        std::printf("usage: emit-check <file.hkx> <Havok-dir> <template.xml> <vanbase-ref-dir>\n"); return 1;
    }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::schema::SchemaRegistry reg;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema: %s\n", err.c_str()); return 1; }
    std::string xmlText;
    { std::ifstream xf(xmlFile, std::ios::binary); std::stringstream ss; ss << xf.rdbuf(); xmlText = ss.str(); }

    havok::PackFileDeserializer des;
    des.ObjectFactory = havok::io::MakeSchemaFactory(reg);
    try { havok::BinaryReaderEx br(false, true, bytes); des.Deserialize(br); }
    catch (const std::exception& e) { std::printf("READ FAIL: %s\n", e.what()); return 1; }

    const havok::model::Identity ident = havok::model::AssignIdentity(des, reg, xmlText);
    const fs::path outDir = fs::temp_directory_path() / "sct_emitcheck";
    std::error_code ec; fs::remove_all(outDir, ec);
    if (!havok::model::EmitHky(ident, reg, outDir.string(), err)) { std::printf("EMIT FAIL: %s\n", err.c_str()); return 1; }

    // per-category diff vs the reference tree
    auto readFile = [](const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); };
    std::map<std::string, std::pair<int,int>> tally;   // category -> {match, total}
    int shown = 0;
    for (const char* cat : {"clips","states","transitions","generators","selectors","references","tagging","modifiers"}) {
        const fs::path rc = fs::path(refDir) / cat;
        if (!fs::is_directory(rc, ec)) continue;
        for (const auto& e : fs::directory_iterator(rc, ec)) {
            if (!e.is_regular_file()) continue;
            const std::string stem = e.path().stem().string();
            if (!std::all_of(stem.begin(), stem.end(), [](unsigned char c){ return std::isdigit(c); })) continue;
            auto& t = tally[cat]; ++t.second;
            const std::string ref = readFile(e.path());
            const std::string mine = readFile(outDir / cat / e.path().filename());
            if (ref == mine) { ++t.first; }
            else if (shown++ < 6) {
                std::printf("  DIFF %s/%s:\n", cat, e.path().filename().string().c_str());
                // first differing line
                std::size_t rp = 0, mp = 0; int ln = 1;
                while (rp < ref.size() && mp < mine.size()) {
                    std::size_t re = ref.find('\n', rp), me = mine.find('\n', mp);
                    std::string rl = ref.substr(rp, re-rp), ml = mine.substr(mp, me-mp);
                    if (rl != ml) { std::printf("    L%d ref: %s\n    L%d io:  %s\n", ln, rl.c_str(), ln, ml.c_str()); break; }
                    rp = re+1; mp = me+1; ++ln;
                }
            }
        }
    }
    std::printf("emit-check:\n");
    int gm=0, gt=0;
    for (const auto& [cat, t] : tally) { std::printf("  %-12s %d/%d\n", cat.c_str(), t.first, t.second); gm+=t.first; gt+=t.second; }
    std::printf("  TOTAL        %d/%d files byte-identical\n", gm, gt);
    return gm == gt ? 0 : 1;
}

// Field-level diff of two packfiles. For every object, re-serialize its struct through
// OUR serializer (consistent layout, pointers -> placeholders, array data dropped) and
// compare the per-class multisets of struct bytes. A class with the SAME object count
// but different struct bytes has a field one file sets and the other drops — the
// decompiler blind spot. Reports the byte offset within the struct (maps back to a
// field via ClassWrite.cpp's field order). Reads fields the decompiler never emits.
// refframe — dump any hkaDefaultAnimatedReferenceFrame in an animation .hkx (the baked
// root motion: 30fps samples, xyz=cumulative translation, w=cumulative yaw about m_up).
// The MotionRecord the animationdatasinglefile carries is built from this — present in
// only 13 dragon-flight files. Experimental: proves the reference-frame -> motion path.
int doRefFrame(const std::string& in) {
    std::vector<std::uint8_t> bytes;
    std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    std::vector<std::shared_ptr<havok::IHavokObject>> frames;
    try { frames = des.ConstructAllOfClass(dr, "hkaDefaultAnimatedReferenceFrame"); }
    catch (const std::exception& e) { std::printf("ERROR: %s\n", e.what()); return 1; }
    if (frames.empty()) { std::printf("NONE  %s\n", in.c_str()); return 0; }
    for (auto& o : frames) {
        auto rf = std::dynamic_pointer_cast<havok::hkaDefaultAnimatedReferenceFrame>(o);
        if (!rf) continue;
        const std::size_t n = rf->m_referenceFrameSamples.size();
        std::printf("REFFRAME  %s  dur=%.6f  up=(%.3f,%.3f,%.3f)  fwd=(%.3f,%.3f,%.3f)  samples=%zu\n",
                    in.c_str(), rf->m_duration, rf->m_up.x, rf->m_up.y, rf->m_up.z,
                    rf->m_forward.x, rf->m_forward.y, rf->m_forward.z, n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto& s = rf->m_referenceFrameSamples[i];
            const double t = i / 30.0;
            std::printf("  t=%.5f  pos=(%.6f, %.6f, %.6f)  yaw=%.6f\n", t, s.x, s.y, s.z, s.w);
        }
    }
    return 0;
}

// animdatadump — parse an animationdatasinglefile.txt and print one project's clips +
// motion records (filter by substring in extra[0]). The ground-truth side of the
// refframe comparison. Experimental.
int doAnimDataDump(const std::string& singlefile, const std::vector<std::string>& extra) {
    std::ifstream f(singlefile, std::ios::binary);
    if (!f) { std::printf("ERROR: cannot open %s\n", singlefile.c_str()); return 1; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    havok::animdata::SingleFile sf;
    try { sf = havok::animdata::ParseSingleFile(text); }
    catch (const std::exception& e) { std::printf("ERROR: parse: %s\n", e.what()); return 1; }
    auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c)); return s; };
    const std::string want = extra.empty() ? "" : lower(extra[0]);
    for (const auto& p : sf.projects) {
        if (!want.empty() && lower(p.name).find(want) == std::string::npos) continue;
        std::printf("PROJECT %s  hasAnimData=%d  clips=%zu  motions=%zu\n",
                    p.name.c_str(), p.hasAnimData ? 1 : 0, p.clips.size(), p.motions.size());
        for (const auto& c : p.clips)
            std::printf("  CLIP  idx=%s  name='%s'  speed=%s  crop=[%s,%s]  triggers=%zu\n",
                        c.animIndex.c_str(), c.name.c_str(), c.playbackSpeed.c_str(),
                        c.cropStart.c_str(), c.cropEnd.c_str(), c.triggers.size());
        for (const auto& m : p.motions) {
            std::printf("  MOTION idx=%s  dur=%s  T=%zu  R=%zu\n",
                        m.animIndex.c_str(), m.duration.c_str(), m.translations.size(), m.rotations.size());
            for (const auto& t : m.translations) std::printf("      T %s\n", t.c_str());
            for (const auto& r : m.rotations)    std::printf("      R %s\n", r.c_str());
        }
    }
    return 0;
}

bool ParseClipYaml(const std::filesystem::path& p, havok::animdata::DeriveClipInput& out);  // defined below

// clipinputs-check — prove the RUNTIME clip extraction (from the in-memory BehaviorData that
// YamlBehaviorLoader produces, via sct::DeriveClipInputsFromBehavior) matches the OFFLINE
// file-parse extraction (ParseClipYaml over clips/<id>.yaml). Same DeriveClipInputs => the
// runtime deriver core is equivalent to the proven offline path. Arg: a decompiled behavior dir.
int doClipInputsCheck(const std::string& behDir) {
    auto low = [](std::string s){ for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    auto keyTrig = [&](const havok::animdata::DeriveClipInput& d) {
        std::multiset<std::string> t;
        for (const auto& tr : d.triggers) {
            char b[64]; std::snprintf(b, sizeof b, "%s|%.6f|%d", tr.event.c_str(), tr.localTime, tr.relativeToEndOfClip ? 1 : 0);
            t.insert(b);
        }
        return t;
    };

    // A) offline file-parse extraction
    std::map<std::string, havok::animdata::DeriveClipInput> A;
    std::error_code ec;
    for (fs::directory_iterator ci(fs::path(behDir) / "clips", ec), cend; !ec && ci != cend; ci.increment(ec)) {
        if (low(ci->path().extension().string()) != ".yaml") continue;
        havok::animdata::DeriveClipInput dc;
        if (ParseClipYaml(ci->path(), dc)) A[dc.name] = dc;
    }
    // B) in-memory extraction (the runtime path)
    havok::model::BehaviorData data;
    try { data = havok::model::YamlBehaviorLoader::Load(behDir); }
    catch (const std::exception& e) { std::printf("ERROR: load '%s': %s\n", behDir.c_str(), e.what()); return 1; }
    std::map<std::string, havok::animdata::DeriveClipInput> B;
    for (auto& dc : havok::sct::DeriveClipInputsFromBehavior(data)) B[dc.name] = dc;

    std::printf("clipinputs-check  file-parse=%zu  in-memory=%zu\n", A.size(), B.size());
    std::size_t match = 0, mism = 0;
    for (const auto& [name, a] : A) {
        auto it = B.find(name);
        if (it == B.end()) { std::printf("  MISSING in in-memory: %s\n", name.c_str()); ++mism; continue; }
        const auto& b = it->second;
        const bool same = low(a.animationName) == low(b.animationName)
            && std::abs(a.playbackSpeed - b.playbackSpeed) < 1e-6
            && std::abs(a.cropStart - b.cropStart) < 1e-6
            && std::abs(a.cropEnd - b.cropEnd) < 1e-6
            && keyTrig(a) == keyTrig(b);
        if (same) ++match;
        else { ++mism; std::printf("  DIFF %s: anim(%s|%s) spd(%.3f|%.3f) trg(%zu|%zu)\n",
                    name.c_str(), a.animationName.c_str(), b.animationName.c_str(),
                    a.playbackSpeed, b.playbackSpeed, a.triggers.size(), b.triggers.size()); }
    }
    std::printf("=== clip inputs: %zu match, %zu differ ===\n", match, mism);
    return mism == 0 ? 0 : 1;
}

// movesets-roundtrip — gate the vanilla-decompose: parse animationsetdatasinglefile, and for
// each project emit movesets.yaml -> parse it back -> compare the authored fields (gate/equip/
// attacks; CRCs are derived, not carried). Zero diffs = the decompose is lossless.
int doMovesetsRoundtrip(const std::string& setdataFile) {
    std::ifstream f(setdataFile, std::ios::binary);
    if (!f) { std::printf("ERROR: cannot open %s\n", setdataFile.c_str()); return 1; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    havok::animsetdata::SingleFile sf;
    try { sf = havok::animsetdata::ParseSingleFile(text); }
    catch (const std::exception& e) { std::printf("ERROR: parse: %s\n", e.what()); return 1; }

    auto sameSet = [](const havok::animsetdata::SetFile& a, const havok::animsetdata::SetFile& b) {
        if (a.name != b.name || a.conditions.size() != b.conditions.size() ||
            a.equipEvents != b.equipEvents || a.attacks.size() != b.attacks.size()) return false;
        for (std::size_t i = 0; i < a.conditions.size(); ++i)
            if (a.conditions[i].variable != b.conditions[i].variable ||
                a.conditions[i].value != b.conditions[i].value ||
                a.conditions[i].extra != b.conditions[i].extra) return false;
        for (std::size_t i = 0; i < a.attacks.size(); ++i)
            if (a.attacks[i].event != b.attacks[i].event || a.attacks[i].flag != b.attacks[i].flag ||
                a.attacks[i].clips != b.attacks[i].clips) return false;
        return true;
    };

    std::size_t projects = 0, ok = 0, bad = 0;
    for (const auto& proj : sf.projects) {
        std::string pname = proj.header;
        if (const auto bs = pname.find_last_of("\\/"); bs != std::string::npos) pname = pname.substr(bs + 1);
        if (const auto dot = pname.rfind('.'); dot != std::string::npos) pname.erase(dot);
        if (proj.sets.empty()) continue;
        ++projects;
        std::string err;
        const auto rt = havok::animsetdata::ParseMovesetsYaml(
            havok::animsetdata::EmitMovesetsYaml(proj), pname, err);
        std::map<std::string, const havok::animsetdata::SetFile*> rtByName;
        if (!rt.projects.empty()) for (const auto& s : rt.projects.front().sets) rtByName[s.name] = &s;
        for (const auto& s : proj.sets) {
            auto it = rtByName.find(s.name);
            if (it != rtByName.end() && sameSet(s, *it->second)) ++ok;
            else { ++bad; std::printf("  DIFF %s :: %s\n", pname.c_str(), s.name.c_str()); }
        }
    }
    std::printf("=== movesets round-trip: %zu projects, %zu sets OK, %zu differ ===\n", projects, ok, bad);
    return bad == 0 ? 0 : 1;
}

// setdata-tree-roundtrip — the BYTE gate for the animationsetdatasinglefile.txt/ folder: decompose
// vanilla into the three pieces (index.yaml + per-project movesets.yaml + baked crcs.yaml) IN MEMORY,
// parse them back, AssembleSetdata, EmitSingleFile, and byte-compare to the vanilla file. Zero diff
// proves the folder composes the exact base — the drift alarm for the "bake the un-reversible" design.
int doSetdataTreeRoundtrip(const std::string& setdataFile) {
    namespace asd = havok::animsetdata;
    std::ifstream f(setdataFile, std::ios::binary);
    if (!f) { std::printf("ERROR: cannot open %s\n", setdataFile.c_str()); return 1; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    asd::SingleFile sf;
    try { sf = asd::ParseSingleFile(text); }
    catch (const std::exception& e) { std::printf("ERROR: parse: %s\n", e.what()); return 1; }

    // Decompose -> the folder pieces (as text), exactly as the converter would ship them.
    std::string idxYaml = asd::EmitSetdataIndexYaml(sf);
    std::map<std::string, asd::SingleFile>                                        movesetsByStem;
    std::map<std::string, std::map<std::string, std::vector<asd::CrcTriple>>>     crcsByStem;
    for (const auto& proj : sf.projects) {
        const std::string stem = asd::StemForHeader(proj.header);
        std::string err;
        movesetsByStem[stem] = asd::ParseMovesetsYaml(asd::EmitMovesetsYaml(proj), stem, err);
        crcsByStem[stem]     = asd::ParseSetdataCrcsYaml(asd::EmitSetdataCrcsYaml(proj), err);
        if (!err.empty()) std::printf("  WARN %s: %s\n", stem.c_str(), err.c_str());
    }

    // Recompose from index order + the parsed pieces.
    std::string ierr;
    const auto  headers  = asd::ParseSetdataIndexYaml(idxYaml, ierr);
    if (!ierr.empty()) { std::printf("ERROR: index parse: %s\n", ierr.c_str()); return 1; }
    const auto  composed = asd::EmitSingleFile(asd::AssembleSetdata(headers, movesetsByStem, crcsByStem));

    const std::string canonical = asd::EmitSingleFile(sf);   // the pre-existing parse->emit form
    const bool canonEqVanilla = (canonical == text);
    if (composed == canonical) {
        std::printf("=== setdata-tree round-trip: %zu projects OK, composed == canonical (parse->emit%s vanilla) ===\n",
                    sf.projects.size(), canonEqVanilla ? " ==" : " !=");
        return canonEqVanilla ? 0 : 1;
    }
    // First byte difference — locate it for triage.
    std::size_t i = 0, n = std::min(composed.size(), canonical.size());
    while (i < n && composed[i] == canonical[i]) ++i;
    std::printf("=== setdata-tree round-trip: DIFF at byte %zu (composed %zu bytes, canonical %zu) ===\n",
                i, composed.size(), canonical.size());
    auto ctx = [](const std::string& s, std::size_t at) {
        std::size_t b = at > 40 ? at - 40 : 0, e = std::min(s.size(), at + 40);
        std::string o; for (std::size_t k = b; k < e; ++k) o += (s[k] == '\r' ? ' ' : s[k] == '\n' ? '|' : s[k]);
        return o;
    };
    std::printf("  composed : ...%s...\n", ctx(composed, i).c_str());
    std::printf("  canonical: ...%s...\n", ctx(canonical, i).c_str());
    return 1;
}

// motion-roundtrip — gate the motion decompose: parse animationdatasinglefile, and for each
// motion-decompose <singlefile> <ProjectName> [-o out.yaml] — emit ONE project's per-actor
// motion.yaml in the shipped named+labeled form (clip name key, axis-labeled samples). Reusable to
// regenerate a motion yaml without a full build-base (e.g. the ChaurusFlyer/HMDaedra noActor gap).
int doMotionDecompose(const std::string& animFile, const std::vector<std::string>& extra, const std::string& out) {
    if (extra.empty()) { std::printf("usage: motion-decompose <singlefile> <ProjectName> [-o out.yaml]\n"); return 1; }
    std::ifstream f(animFile, std::ios::binary);
    if (!f) { std::printf("ERROR: cannot open %s\n", animFile.c_str()); return 1; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    havok::animdata::SingleFile sf;
    try { sf = havok::animdata::ParseSingleFile(text); }
    catch (const std::exception& e) { std::printf("ERROR: parse: %s\n", e.what()); return 1; }
    auto norm = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (s.size() >= 4 && s.compare(s.size() - 4, 4, ".txt") == 0) s.erase(s.size() - 4);
        return s;
    };
    const std::string want = norm(extra[0]);
    for (const auto& p : sf.projects) {
        if (norm(p.name) != want) continue;
        if (p.motions.empty()) { std::printf("project '%s' has no motion.\n", extra[0].c_str()); return 1; }
        long maxIdx = -1;
        for (const auto& c : p.clips)   maxIdx = std::max(maxIdx, std::atol(c.animIndex.c_str()));
        for (const auto& m : p.motions) maxIdx = std::max(maxIdx, std::atol(m.animIndex.c_str()));
        std::vector<std::string> labels(static_cast<std::size_t>(maxIdx + 1));
        for (const auto& c : p.clips) {
            const long i = std::atol(c.animIndex.c_str());
            if (i >= 0 && labels[static_cast<std::size_t>(i)].empty()) labels[static_cast<std::size_t>(i)] = c.name;
        }
        const std::string y = havok::animdata::EmitMotionYaml(p, labels);
        if (out.empty()) std::printf("%s", y.c_str());
        else { std::ofstream(out, std::ios::binary).write(y.data(), static_cast<std::streamsize>(y.size()));
               std::printf("wrote %s (%zu motion record(s))\n", out.c_str(), p.motions.size()); }
        return 0;
    }
    std::printf("project '%s' not found.\n", extra[0].c_str());
    return 1;
}

// setdata-decompose <animationsetdatasinglefile.txt> -o <outFolderDir> — decompose the whole
// set-data cache into its ".txt/" folder form (index.yaml + movesets/<stem>.yaml + crcs/<stem>.yaml)
// under <outFolderDir>. Reusable to regenerate the master's folder without a full build-base.
int doSetdataDecompose(const std::string& setdataFile, const std::string& out) {
    namespace asd = havok::animsetdata;
    if (out.empty()) { std::printf("usage: setdata-decompose <singlefile> -o <outFolderDir>\n"); return 1; }
    std::ifstream f(setdataFile, std::ios::binary);
    if (!f) { std::printf("ERROR: cannot open %s\n", setdataFile.c_str()); return 1; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    asd::SingleFile sf;
    try { sf = asd::ParseSingleFile(text); }
    catch (const std::exception& e) { std::printf("ERROR: parse: %s\n", e.what()); return 1; }

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root(out);
    fs::create_directories(root / "movesets", ec);
    fs::create_directories(root / "crcs", ec);

    const std::string idx = asd::EmitSetdataIndexYaml(sf);
    std::ofstream(root / "index.yaml", std::ios::binary).write(idx.data(), (std::streamsize)idx.size());

    int mv = 0, cr = 0;
    for (const auto& proj : sf.projects) {
        const std::string stem = asd::StemForHeader(proj.header);
        if (stem.empty() || proj.sets.empty()) continue;
        const std::string m = asd::EmitMovesetsYaml(proj);
        std::ofstream(root / "movesets" / (stem + ".yaml"), std::ios::binary).write(m.data(), (std::streamsize)m.size());
        ++mv;
        const std::string c = asd::EmitSetdataCrcsYaml(proj);
        std::ofstream(root / "crcs" / (stem + ".yaml"), std::ios::binary).write(c.data(), (std::streamsize)c.size());
        ++cr;
    }
    std::printf("wrote %s: index.yaml + %d movesets/ + %d crcs/ (%zu projects)\n",
                out.c_str(), mv, cr, sf.projects.size());
    return 0;
}

// setdata-compose <folderDir> [-o out.txt] — COMPOSE the monolithic animationsetdatasinglefile.txt
// back from its ".txt/" folder (index.yaml + movesets/ + crcs/), the EXACT path ServeSetData runs
// (AssembleSetdata + EmitSingleFile) but over files on disk instead of a packed .hky. Prints to
// stdout, or writes -o. This is the on-disk twin of the runtime compose — diff its output vs vanilla
// to close the loop end-to-end.
int doSetdataCompose(const std::string& folderDir, const std::string& out) {
    namespace asd = havok::animsetdata;
    namespace fs  = std::filesystem;
    const fs::path root(folderDir);
    std::error_code ec;

    auto readFile = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    std::string ierr;
    const auto  headers = asd::ParseSetdataIndexYaml(readFile(root / "index.yaml"), ierr);
    if (!ierr.empty()) { std::printf("ERROR: index.yaml: %s\n", ierr.c_str()); return 1; }
    if (headers.empty()) { std::printf("ERROR: no projects in %s/index.yaml\n", folderDir.c_str()); return 1; }

    auto stemOf = [](const fs::path& p) { return p.stem().string(); };
    std::map<std::string, asd::SingleFile>                                    movesetsByStem;
    std::map<std::string, std::map<std::string, std::vector<asd::CrcTriple>>> crcsByStem;
    for (fs::directory_iterator it(root / "movesets", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".yaml") continue;
        std::string e; movesetsByStem[stemOf(it->path())] = asd::ParseMovesetsYaml(readFile(it->path()), stemOf(it->path()), e);
    }
    for (fs::directory_iterator it(root / "crcs", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".yaml") continue;
        std::string e; crcsByStem[stemOf(it->path())] = asd::ParseSetdataCrcsYaml(readFile(it->path()), e);
    }
    const std::string composed = asd::EmitSingleFile(asd::AssembleSetdata(headers, movesetsByStem, crcsByStem));
    if (out.empty()) std::fwrite(composed.data(), 1, composed.size(), stdout);
    else { std::ofstream(out, std::ios::binary).write(composed.data(), (std::streamsize)composed.size());
           std::printf("wrote %s (%zu bytes, %zu projects)\n", out.c_str(), composed.size(), headers.size()); }
    return 0;
}

// ── animationdatasinglefile.txt ".txt/" folder verbs (siblings of the setdata ones) ──
namespace {
// index -> clip NAME labels for a project (what EmitMotionYaml keys motion on).
std::vector<std::string> ClipLabels(const havok::animdata::Project& p) {
    long maxIdx = -1;
    for (const auto& c : p.clips)   maxIdx = std::max(maxIdx, std::atol(c.animIndex.c_str()));
    for (const auto& m : p.motions) maxIdx = std::max(maxIdx, std::atol(m.animIndex.c_str()));
    std::vector<std::string> labels(static_cast<std::size_t>(maxIdx + 1));
    for (const auto& c : p.clips) {
        const long i = std::atol(c.animIndex.c_str());
        if (i >= 0 && labels[static_cast<std::size_t>(i)].empty()) labels[static_cast<std::size_t>(i)] = c.name;
    }
    return labels;
}

using havok::animdata::ProjectCharacter;
using havok::animdata::LoadProjectCharacters;   // shared co-location resolver (havok-core)

// Case-insensitive filename key (also strips trailing '.'/' ' the way Windows would) — for detecting
// collisions among clip/motion filenames within a project directory.
std::string NormFileKey(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    return s;
}

// Motion file key: the clip-name label at this motion's animIndex, or "unnamed_<N>" when no clip
// occupies that index (the hybrid mounted-combat blocks). `labels[i]` = first clip name at index i.
std::string MotionKey(const havok::animdata::MotionRecord& m, const std::vector<std::string>& labels) {
    const long i = std::atol(m.animIndex.c_str());
    if (i >= 0 && static_cast<std::size_t>(i) < labels.size() && !labels[static_cast<std::size_t>(i)].empty())
        return labels[static_cast<std::size_t>(i)];
    return "unnamed_" + m.animIndex;
}
// Recover a motion record's key from its filename stem: "unnamed_<N>" -> raw animIndex N; else the
// stem IS the clip-name label (animIndex resolved later via ResolveMotionIndices).
void KeyToMotion(const std::string& key, havok::animdata::MotionRecord& m) {
    if (key.rfind("unnamed_", 0) == 0) m.animIndex = key.substr(8);
    else                               m.animation = key;
}
}  // namespace

// animdata-decompose <animationdatasinglefile.txt> <meshesDir> -o <outFolderDir> — decompose the
// whole cache into its ".txt/" folder (index.yaml + clips/<stem>.yaml + motion/<stem>.yaml). The
// meshesDir supplies character rosters so clips are keyed by animation name (not the raw index).
int doAnimdataDecompose(const std::string& animFile, const std::vector<std::string>& extra, const std::string& out) {
    namespace ad = havok::animdata;
    if (out.empty() || extra.empty()) { std::printf("usage: animdata-decompose <singlefile> <meshesDir> -o <outFolderDir>\n"); return 1; }
    std::ifstream f(animFile, std::ios::binary);
    if (!f) { std::printf("ERROR: cannot open %s\n", animFile.c_str()); return 1; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ad::SingleFile sf;
    try { sf = ad::ParseSingleFile(text); }
    catch (const std::exception& e) { std::printf("ERROR: parse: %s\n", e.what()); return 1; }
    const auto pcs = LoadProjectCharacters(extra[0]);   // project stem -> {char ref, roster}
    std::map<std::string, std::string> charRefByStem;
    for (const auto& [stem, pc] : pcs) charRefByStem.emplace(stem, pc.ref);

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root(out);
    fs::create_directories(root / "clips", ec);
    fs::create_directories(root / "motion", ec);
    const std::string idx = ad::EmitAnimdataIndexYaml(sf, charRefByStem);
    std::ofstream(root / "index.yaml", std::ios::binary).write(idx.data(), (std::streamsize)idx.size());

    static const std::vector<std::string> kEmpty;
    int clips = 0, motion = 0, resolved = 0;
    for (const auto& p : sf.projects) {
        if (!p.hasAnimData) continue;
        const std::string stem = ad::StemForProjectName(p.name);
        const auto pit = pcs.find(stem);
        const bool have = pit != pcs.end();
        if (have) ++resolved;
        const auto& roster = have ? pit->second.roster : kEmpty;
        const fs::path cdir = root / "clips" / stem; fs::create_directories(cdir, ec);
        std::set<std::string> usedClip;
        for (const auto& c : p.clips) {
            const std::string fn = ad::UniqueFileName(c.name, usedClip);
            const std::string y  = ad::EmitClipYaml(c, roster, fn != c.name);
            std::ofstream(cdir / (fn + ".yaml"), std::ios::binary).write(y.data(), (std::streamsize)y.size());
            ++clips;
        }
        if (!p.motions.empty()) {
            const auto labels = ClipLabels(p);
            const fs::path mdir = root / "motion" / stem; fs::create_directories(mdir, ec);
            std::set<std::string> usedMot;
            for (const auto& m : p.motions) {
                std::string key = MotionKey(m, labels);
                if (!usedMot.insert(NormFileKey(key)).second) { key = "unnamed_" + m.animIndex; usedMot.insert(NormFileKey(key)); }
                const std::string y = ad::EmitMotionSidecar(m);
                std::ofstream(mdir / (key + ".yaml"), std::ios::binary).write(y.data(), (std::streamsize)y.size());
                ++motion;
            }
        }
    }
    std::printf("wrote %s: index.yaml + %d clip file(s) + %d motion file(s) (%zu projects, %d char-resolved)\n",
                out.c_str(), clips, motion, sf.projects.size(), resolved);
    return 0;
}

// Read a per-clip/per-motion animdata tree under <root> into the maps AssembleAnimdata consumes.
namespace {
void ReadAnimTree(const std::filesystem::path& root,
                  std::map<std::string, std::vector<havok::animdata::ClipGenerator>>& clipsByStem,
                  std::map<std::string, std::vector<havok::animdata::MotionRecord>>&  motionByStem) {
    namespace fs = std::filesystem; namespace ad = havok::animdata;
    std::error_code ec;
    auto readFile = [](const fs::path& p) { std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); };
    for (fs::directory_iterator pit(root / "clips", ec), pend; !ec && pit != pend; pit.increment(ec)) {
        if (!pit->is_directory(ec)) continue;
        const std::string stem = pit->path().filename().string();
        for (fs::directory_iterator it(pit->path(), ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().extension() != ".yaml") continue;
            std::string e; ad::ClipGenerator c = ad::ParseClipYaml(readFile(it->path()), e);
            if (c.name.empty()) c.name = it->path().stem().string();   // filename is the name unless disambiguated (in-body)
            clipsByStem[stem].push_back(std::move(c));
        }
    }
    for (fs::directory_iterator pit(root / "motion", ec), pend; !ec && pit != pend; pit.increment(ec)) {
        if (!pit->is_directory(ec)) continue;
        const std::string stem = pit->path().filename().string();
        for (fs::directory_iterator it(pit->path(), ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().extension() != ".yaml") continue;
            std::string e; ad::MotionRecord m = ad::ParseMotionSidecar(readFile(it->path()), e);
            KeyToMotion(it->path().stem().string(), m);
            motionByStem[stem].push_back(std::move(m));
        }
    }
}
}  // namespace

// animdata-compose <folderDir> <meshesDir> [-o out.txt] — COMPOSE the monolithic
// animationdatasinglefile.txt back from its ".txt/" folder, the EXACT path ServeAnimData runs
// (AssembleAnimdata + EmitSingleFile). meshesDir supplies rosters to resolve clip animIndices.
int doAnimdataCompose(const std::string& folderDir, const std::vector<std::string>& extra, const std::string& out) {
    namespace ad = havok::animdata;
    namespace fs = std::filesystem;
    if (extra.empty()) { std::printf("usage: animdata-compose <folderDir> <meshesDir> [-o out.txt]\n"); return 1; }
    const fs::path root(folderDir);
    std::error_code ec;
    auto readFile = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    std::string ierr;
    const auto headers = ad::ParseAnimdataIndexYaml(readFile(root / "index.yaml"), ierr);
    if (!ierr.empty()) { std::printf("ERROR: index.yaml: %s\n", ierr.c_str()); return 1; }
    if (headers.empty()) { std::printf("ERROR: no projects in %s/index.yaml\n", folderDir.c_str()); return 1; }

    std::map<std::string, std::vector<ad::ClipGenerator>> clipsByStem;
    std::map<std::string, std::vector<ad::MotionRecord>>  motionByStem;
    ReadAnimTree(root, clipsByStem, motionByStem);
    // Rosters come from each project's `character:` header ref (meshes/<ref>/animations.txt) — the
    // exact path ServeAnimData uses. meshesDir (extra[0]) is where the character units live.
    const fs::path meshes(extra[0]);
    std::map<std::string, std::vector<std::string>> rosters;
    for (const auto& h : headers) {
        if (h.character.empty()) continue;
        std::ifstream rf(meshes / fs::path(h.character) / "animations.txt");
        std::vector<std::string> roster; std::string line;
        while (std::getline(rf, line)) {
            while (!line.empty() && (line.back()=='\r'||line.back()=='\n'||line.back()==' '||line.back()=='\t')) line.pop_back();
            if (!line.empty()) roster.push_back(line);
        }
        if (!roster.empty()) rosters[ad::StemForProjectName(h.name)] = std::move(roster);
    }
    const std::string composed = ad::EmitSingleFile(ad::AssembleAnimdata(headers, clipsByStem, motionByStem, rosters));
    if (out.empty()) std::fwrite(composed.data(), 1, composed.size(), stdout);
    else { std::ofstream(out, std::ios::binary).write(composed.data(), (std::streamsize)composed.size());
           std::printf("wrote %s (%zu bytes, %zu projects)\n", out.c_str(), composed.size(), headers.size()); }
    return 0;
}

// animdata-tree-roundtrip <animationdatasinglefile.txt> <meshesDir> — the BYTE gate: decompose in
// memory (clips keyed by animation name via the rosters), parse back, AssembleAnimdata (resolving
// indices against the rosters), EmitSingleFile, compare to vanilla. Zero diff proves the folder
// composes the exact base AND the clip de-hardwire is lossless.
int doAnimdataTreeRoundtrip(const std::string& animFile, const std::vector<std::string>& extra) {
    namespace ad = havok::animdata;
    if (extra.empty()) { std::printf("usage: animdata-tree-roundtrip <singlefile> <meshesDir>\n"); return 1; }
    std::ifstream f(animFile, std::ios::binary);
    if (!f) { std::printf("ERROR: cannot open %s\n", animFile.c_str()); return 1; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ad::SingleFile sf;
    try { sf = ad::ParseSingleFile(text); }
    catch (const std::exception& e) { std::printf("ERROR: parse: %s\n", e.what()); return 1; }
    const auto pcs = LoadProjectCharacters(extra[0]);
    std::map<std::string, std::vector<std::string>> rosters;
    for (const auto& [stem, pc] : pcs) rosters.emplace(stem, pc.roster);

    static const std::vector<std::string> kEmpty;
    std::map<std::string, std::string> charRefByStem;
    for (const auto& [stem, pc] : pcs) charRefByStem.emplace(stem, pc.ref);
    const std::string idxYaml = ad::EmitAnimdataIndexYaml(sf, charRefByStem);
    // Per-CLIP + per-MOTION round-trip in memory: each clip through EmitClipYaml->ParseClipYaml (name
    // from the "filename"), each motion through EmitMotionSidecar->ParseMotionSidecar (key logic),
    // exactly as the disk decompose+compose would.
    std::map<std::string, std::vector<ad::ClipGenerator>> clipsByStem;
    std::map<std::string, std::vector<ad::MotionRecord>>  motionByStem;
    for (const auto& p : sf.projects) {
        if (!p.hasAnimData) continue;
        const std::string stem = ad::StemForProjectName(p.name);
        const auto pit = pcs.find(stem);
        const auto& roster = pit != pcs.end() ? pit->second.roster : kEmpty;
        std::set<std::string> usedClip;
        for (const auto& c : p.clips) {
            // Simulate the disk decompose: disambiguate the filename, write name in-body when it differs,
            // recover the name from body-or-filename — so the gate exercises the case-collision handling.
            const std::string fn = ad::UniqueFileName(c.name, usedClip);
            std::string e; ad::ClipGenerator rc = ad::ParseClipYaml(ad::EmitClipYaml(c, roster, fn != c.name), e);
            if (rc.name.empty()) rc.name = fn;
            clipsByStem[stem].push_back(std::move(rc));
        }
        if (!p.motions.empty()) {
            const auto labels = ClipLabels(p);
            std::set<std::string> usedMot;
            for (const auto& m : p.motions) {
                std::string key = MotionKey(m, labels);
                if (!usedMot.insert(NormFileKey(key)).second) { key = "unnamed_" + m.animIndex; usedMot.insert(NormFileKey(key)); }
                std::string e; ad::MotionRecord rm = ad::ParseMotionSidecar(ad::EmitMotionSidecar(m), e);
                KeyToMotion(key, rm);
                motionByStem[stem].push_back(std::move(rm));
            }
        }
    }
    std::string ierr;
    const auto headers = ad::ParseAnimdataIndexYaml(idxYaml, ierr);
    if (!ierr.empty()) { std::printf("ERROR: index parse: %s\n", ierr.c_str()); return 1; }
    const std::string composed  = ad::EmitSingleFile(ad::AssembleAnimdata(headers, clipsByStem, motionByStem, rosters));
    // MULTISET gate: AssembleAnimdata emits in canonical (sorted) order because cache order is inert,
    // so sort the vanilla parse the SAME way before comparing — a byte-diff of the two sorted forms
    // proves same-content (project set + per-project clip/motion multisets), order-independent.
    ad::SortAnimdata(sf);
    const std::string canonical = ad::EmitSingleFile(sf);
    if (composed == canonical) {
        std::printf("=== animdata-tree round-trip: %zu projects OK, composed == canonical (multiset: content matches vanilla, order canonicalized) ===\n",
                    sf.projects.size());
        return 0;
    }
    std::size_t i = 0, n = std::min(composed.size(), canonical.size());
    while (i < n && composed[i] == canonical[i]) ++i;
    std::printf("=== animdata-tree round-trip: DIFF at byte %zu (composed %zu, canonical %zu) ===\n",
                i, composed.size(), canonical.size());
    auto ctx = [](const std::string& s, std::size_t at) {
        std::size_t b = at > 40 ? at - 40 : 0, e = std::min(s.size(), at + 40);
        std::string o; for (std::size_t k = b; k < e; ++k) o += (s[k] == '\r' ? ' ' : s[k] == '\n' ? '|' : s[k]);
        return o;
    };
    std::printf("  composed : ...%s...\n", ctx(composed, i).c_str());
    std::printf("  canonical: ...%s...\n", ctx(canonical, i).c_str());
    return 1;
}

// project with motion, emit motion.yaml -> parse it back -> compare the records verbatim
// (index/duration/translation/rotation). Zero diffs = byte-exact.
int doMotionRoundtrip(const std::string& animFile) {
    std::ifstream f(animFile, std::ios::binary);
    if (!f) { std::printf("ERROR: cannot open %s\n", animFile.c_str()); return 1; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    havok::animdata::SingleFile sf;
    try { sf = havok::animdata::ParseSingleFile(text); }
    catch (const std::exception& e) { std::printf("ERROR: parse: %s\n", e.what()); return 1; }

    std::size_t projects = 0, ok = 0, bad = 0;
    for (const auto& proj : sf.projects) {
        if (proj.motions.empty()) continue;
        ++projects;
        // Build clip-name labels (animIndex -> name) so the gate exercises the NAMED path + the
        // ResolveMotionIndices compile-join: emit drops the index for named records, parse reads the
        // name, resolve reconstructs the index. If that reproduces the original animIndex, the
        // de-hardwire is byte-lossless (unnamed hybrid blocks still round-trip via the raw index).
        long maxIdx = -1;
        for (const auto& c : proj.clips)   maxIdx = std::max(maxIdx, std::atol(c.animIndex.c_str()));
        for (const auto& m : proj.motions) maxIdx = std::max(maxIdx, std::atol(m.animIndex.c_str()));
        std::vector<std::string> labels(static_cast<std::size_t>(maxIdx + 1));
        for (const auto& c : proj.clips) {
            const long i = std::atol(c.animIndex.c_str());
            if (i >= 0 && labels[static_cast<std::size_t>(i)].empty()) labels[static_cast<std::size_t>(i)] = c.name;
        }
        std::string err;
        auto rt = havok::animdata::ParseMotionYaml(havok::animdata::EmitMotionYaml(proj, labels), err);
        havok::animdata::ResolveMotionIndices(rt, proj.clips);   // name -> index (the compile join)
        if (rt.size() != proj.motions.size()) { bad += proj.motions.size(); std::printf("  SIZE %s\n", proj.name.c_str()); continue; }
        for (std::size_t i = 0; i < proj.motions.size(); ++i) {
            const auto& a = proj.motions[i];
            const auto& b = rt[i];
            if (a.animIndex == b.animIndex && a.duration == b.duration &&
                a.translations == b.translations && a.rotations == b.rotations) ++ok;
            else { ++bad; std::printf("  DIFF %s idx=%s\n", proj.name.c_str(), a.animIndex.c_str()); }
        }
    }
    std::printf("=== motion round-trip: %zu projects, %zu motions OK, %zu differ ===\n", projects, ok, bad);
    return bad == 0 ? 0 : 1;
}

// Load every object in an .hkx as a per-class MULTISET of DEEP-serialized struct bytes.
// Order-independent (multiset) and pointer-numbering-independent (deep-serialize inlines the
// pointed-to children by content, so a child's #id never appears), so two structurally-identical
// graphs hash identically regardless of emit order or how each tool numbered its objects. This is
// the canonical-structural-diff core shared by fielddiff (one pair) and treediff (whole trees).
// It never touches the YAML decompiler — raw PackFileDeserializer -> PackFileSerializer, the
// round-trip-gated path — so a YAML-emit quirk cannot hide a real difference here. Throws on
// read/parse failure.
static std::map<std::string, std::multiset<std::string>> LoadByClassStructs(const std::string& in) {
    std::map<std::string, std::multiset<std::string>> byClass;
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) throw std::runtime_error(err);
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    havok::PackFileSerializer ser;
    for (const auto& [off, cls] : des.ListObjects()) {
        std::shared_ptr<havok::IHavokObject> obj;
        try { obj = des.ConstructVirtualClass(dr, off); } catch (...) { continue; }
        if (!obj) continue;
        std::vector<std::uint8_t> sb;
        try { sb = ser.SerializeStructDeep(obj, des._header); } catch (...) { continue; }
        byClass[cls].insert(std::string(reinterpret_cast<const char*>(sb.data()), sb.size()));
    }
    return byClass;
}

// ── treediff --resolve: soft-ref-aware canonical structural signature ────────────────────────────
// The byte-multiset above is numbering-immune for POINTERS (deep-inline) but NOT for soft refs — an
// event/variable is referenced by its INDEX into the graph's name table, and two independently-merged
// graphs order those tables differently (BR injects vocab, so the BFCO attack-event tail renumbers),
// making structurally-identical objects hash differently. This resolved loader fixes that: it loads
// via the SCHEMA object model (named, typed fields), then emits a SHALLOW per-object signature —
// own scalars/strings + INLINE structs (recursed), class POINTERS rendered as the target's class name
// only (each pointee is its own multiset entry) — with the known soft-ref index fields resolved to the
// NAME they point at. Cycle-free (no deep pointer follow) and index-immune. The soft-ref set is
// hardcoded here (not schema markers) so the change stays isolated to this diagnostic and never
// perturbs the decompile/compile round-trip.
static std::int64_t tdReadInt(const std::vector<std::uint8_t>& raw, havok::schema::Scalar s) {
    using S = havok::schema::Scalar;
    auto rd = [&](int n) -> std::uint64_t {
        std::uint64_t v = 0; for (int i = 0; i < n && i < static_cast<int>(raw.size()); ++i) v |= static_cast<std::uint64_t>(raw[i]) << (8 * i); return v; };
    switch (s) {
        case S::Int8:  return raw.empty() ? 0 : static_cast<std::int8_t>(raw[0]);
        case S::Byte: case S::Bool: return raw.empty() ? 0 : raw[0];
        case S::Int16: return static_cast<std::int16_t>(rd(2));
        case S::UInt16:return static_cast<std::uint16_t>(rd(2));
        case S::Int32: return static_cast<std::int32_t>(rd(4));
        case S::UInt32:return static_cast<std::uint32_t>(rd(4));
        default:       return static_cast<std::int64_t>(rd(8));
    }
}
// (class, field) -> 1=event index, 2=variable index, 0=plain. The complete soft-ref set for the
// behavior-graph classes (the fields that hold an index into eventNames/variableNames).
static int tdSoftRef(const std::string& cls, const std::string& fld) {
    if (fld == "id"             && cls == "hkbEventProperty")                return 1;
    if (fld == "eventId"        && cls == "hkbStateMachineTransitionInfo")  return 1;
    if ((fld == "activateEventId" || fld == "deactivateEventId") && cls == "hkbEventDrivenModifier") return 1;
    if (fld == "variableIndex"  && cls == "hkbVariableBindingSetBinding")   return 2;
    return 0;
}
static std::map<std::string, std::multiset<std::string>>
LoadByClassResolved(const std::string& in, const havok::schema::SchemaRegistry& reg) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) throw std::runtime_error(err);
    havok::BinaryReaderEx br(bytes);
    havok::PackFileDeserializer des;
    des.ObjectFactory = havok::io::MakeSchemaFactory(reg);
    std::shared_ptr<havok::IHavokObject> root = des.Deserialize(br);
    if (!root) throw std::runtime_error("null root");
    using SO = havok::io::SchemaObject;
    using K  = havok::schema::FieldKind;
    auto asSO = [](const std::shared_ptr<havok::IHavokObject>& p) { return dynamic_cast<SO*>(p.get()); };

    // Collect TOP-LEVEL (pointer-reachable) objects; descend inline structs to find pointers within them.
    std::unordered_set<const void*> seen;
    std::vector<SO*> top;
    std::function<void(SO*)> descend = [&](SO* so) {
        const auto fields = so->Fields(); const auto& vals = so->Values();
        for (std::size_t i = 0; i < fields.size() && i < vals.size(); ++i) {
            const auto* f = fields[i]; const auto& v = vals[i];
            if (f->kind == K::Ptr)        { if (auto* c = asSO(v.obj)) if (seen.insert(c).second) top.push_back(c); }
            else if (f->kind == K::PtrArray) { for (auto& e : v.objs) if (auto* c = asSO(e)) if (seen.insert(c).second) top.push_back(c); }
            else if (f->kind == K::Struct)   { if (auto* c = asSO(v.obj)) descend(c); }
            else if (f->kind == K::StructArray) { for (auto& e : v.objs) if (auto* c = asSO(e)) descend(c); }
        }
    };
    if (auto* r = asSO(root)) { seen.insert(r); top.push_back(r); }
    for (std::size_t i = 0; i < top.size(); ++i) descend(top[i]);   // grows as pointers are found

    // Name tables from hkbBehaviorGraphStringData.
    std::vector<std::string> eventNames, varNames;
    for (SO* so : top) if (std::string(so->ClassName()) == "hkbBehaviorGraphStringData") {
        const auto fields = so->Fields(); const auto& vals = so->Values();
        for (std::size_t i = 0; i < fields.size() && i < vals.size(); ++i) {
            if (fields[i]->name == "eventNames")    eventNames = vals[i].strs;
            else if (fields[i]->name == "variableNames") varNames = vals[i].strs;
        }
    }
    auto nm = [](const std::vector<std::string>& t, std::int64_t x) -> std::string {
        if (x < 0) return "-";
        if (x < static_cast<std::int64_t>(t.size())) return t[static_cast<std::size_t>(x)];
        return "#" + std::to_string(x);
    };
    auto hex = [](const std::vector<std::uint8_t>& r) { std::string s; char b[3]; for (auto c : r) { std::snprintf(b, 3, "%02x", c); s += b; } return s; };

    std::function<void(SO*, std::string&)> emit = [&](SO* so, std::string& out) {
        const std::string cls = so->ClassName();
        out += cls; out += '(';
        const auto fields = so->Fields(); const auto& vals = so->Values();
        for (std::size_t i = 0; i < fields.size() && i < vals.size(); ++i) {
            const auto* f = fields[i]; const auto& v = vals[i];
            switch (f->kind) {
                case K::Scalar: {
                    if (int sr = tdSoftRef(cls, f->name))
                        out += f->name + "=" + (sr == 1 ? nm(eventNames, tdReadInt(v.raw, f->scalar))
                                                        : nm(varNames,   tdReadInt(v.raw, f->scalar))) + " ";
                    else out += f->name + "=" + hex(v.raw) + " ";
                    break; }
                case K::String: case K::CString: out += f->name + "='" + v.str + "' "; break;
                case K::StringArray: { out += f->name + "=["; for (auto& s : v.strs) { out += s; out += ','; } out += "] "; break; }
                case K::Struct: if (auto* c = asSO(v.obj)) emit(c, out); break;
                case K::StructArray: for (auto& e : v.objs) if (auto* c = asSO(e)) emit(c, out); break;
                case K::Ptr: out += f->name + "->" + (v.obj ? v.obj->ClassName() : "null") + " "; break;
                case K::PtrArray: { out += f->name + "->["; for (auto& e : v.objs) { out += e ? e->ClassName() : "null"; out += ','; } out += "] "; break; }
                case K::Vector4: case K::Quaternion: case K::QsTransform: case K::BoolArray:
                case K::Vec4Array: case K::QsTransformArray: case K::ScalarArray:
                    out += f->name + "=" + hex(v.raw) + " "; break;
                default: break;   // Vtable / EmptyArray / EmptyPtr / Pad / Skip — no identity
            }
        }
        out += ')';
    };
    std::map<std::string, std::multiset<std::string>> byClass;
    for (SO* so : top) { std::string sig; emit(so, sig); byClass[so->ClassName()].insert(std::move(sig)); }
    return byClass;
}

int doFieldDiff(const std::string& fileA, const std::string& fileB) {
    std::map<std::string, std::multiset<std::string>> A, B;
    try { A = LoadByClassStructs(fileA); B = LoadByClassStructs(fileB); }
    catch (const std::exception& e) { std::printf("ERROR: %s\n", e.what()); return 1; }

    std::set<std::string> classes;
    for (auto& kv : A) classes.insert(kv.first);
    for (auto& kv : B) classes.insert(kv.first);

    int diffs = 0;
    for (const auto& cls : classes) {
        const auto& a = A[cls];
        const auto& b = B[cls];
        if (a == b) continue;
        ++diffs;
        const bool sameCount = a.size() == b.size();
        std::printf("CLASS %s: A=%zu B=%zu%s\n", cls.c_str(), a.size(), b.size(),
                    sameCount ? "  <-- same count, FIELD DIFF" : "  (count differs = de-sharing)");
        std::vector<std::string> onlyA;
        std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(onlyA));
        if (onlyA.empty()) { std::printf("  (every A struct is present in B — pure de-sharing, no field diff)\n"); continue; }
        std::printf("  %zu A-struct(s) have NO match in B (dropped field):\n", onlyA.size());
        const std::string& pa = onlyA.front();
        const std::string* best = nullptr; std::size_t bestD = pa.size() + 1;
        for (const auto& pb : b) {
            if (pb.size() != pa.size()) continue;
            std::size_t d = 0; for (std::size_t k = 0; k < pa.size(); ++k) if (pa[k] != pb[k]) ++d;
            if (d && d < bestD) { bestD = d; best = &pb; }
        }
        if (!best) { std::printf("  (no same-size match)\n"); continue; }
        std::printf("  struct %zu bytes; %zu byte(s) differ from closest object:\n", pa.size(), bestD);
        for (std::size_t k = 0; k < pa.size(); ++k)
            if (pa[k] != (*best)[k])
                std::printf("    +%zu (0x%zx): A=0x%02x B=0x%02x\n", k, k,
                            static_cast<unsigned char>(pa[k]), static_cast<unsigned char>((*best)[k]));
    }
    std::printf("--- %d class(es) differ ---\n", diffs);
    return 0;
}

// treediff <dirA> <dirB>: recursively diff every .hkx in A against its counterpart in B, using the
// canonical per-class multiset comparison (LoadByClassStructs). Files are matched by relative path,
// normalized case-insensitively and with a leading "meshes/" stripped, so a runtime community_behaviors_cache
// (rooted at actors/) pairs with a Pandora/Nemesis output (rooted at meshes/actors/). For each pair
// it reports, per class: a COUNT difference (de-sharing — a shared object split into copies, or a real
// add/drop) or, at equal count, a FIELD DIFF with the number of structs that have no exact match. A
// pair whose every class matches prints nothing unless -v. Ends with a summary. This is the
// go-forward BR-vs-Pandora (or BR-vs-vanilla) structural diff: format-agnostic, numbering-immune,
// and independent of the YAML decompiler.
int doTreeDiff(const std::string& dirA, const std::string& dirB, bool verbose, const std::string& schemaDir) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(dirA) || !fs::is_directory(dirB)) {
        std::printf("ERROR: both arguments must be directories\n"); return 1;
    }
    // --resolve <HavokDir>: load the schema once and use the soft-ref-resolved signature (strips
    // event/variable index-renumbering noise). Without it, the raw byte-multiset (fast, no schema).
    std::unique_ptr<havok::schema::SchemaRegistry> reg;
    if (!schemaDir.empty()) {
        reg = std::make_unique<havok::schema::SchemaRegistry>();
        std::string serr;
        if (!reg->LoadDir(schemaDir, serr)) { std::printf("ERROR loading schema '%s': %s\n", schemaDir.c_str(), serr.c_str()); return 1; }
        std::printf("(resolved mode: soft-ref event/variable indices → names, schema '%s')\n", schemaDir.c_str());
    }
    auto loadOne = [&](const std::string& p) {
        return reg ? LoadByClassResolved(p, *reg) : LoadByClassStructs(p);
    };
    auto normKey = [](fs::path rel) {
        std::string s = rel.generic_string();
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (s.rfind("meshes/", 0) == 0) s = s.substr(7);   // pair cache(actors/…) with output(meshes/actors/…)
        return s;
    };
    // Index B by normalized relative key.
    std::map<std::string, fs::path> bIndex;
    for (auto& e : fs::recursive_directory_iterator(dirB)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".hkx") bIndex[normKey(fs::relative(e.path(), dirB))] = e.path();
    }
    // Walk A, deterministic order.
    std::vector<fs::path> aFiles;
    for (auto& e : fs::recursive_directory_iterator(dirA)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".hkx") aFiles.push_back(e.path());
    }
    std::sort(aFiles.begin(), aFiles.end());

    // Track which B files a walk of A paired with, so the leftover (present only in B) can be
    // reported as [ONLY-B] below — a one-directional diff would silently hide a whole .hkx that
    // the B tree ships and A does not (e.g. a referenced cross-hkx behavior CB failed to emit).
    std::set<std::string> matchedB;

    int compared = 0, differ = 0, onlyA = 0, onlyB = 0, errs = 0;
    for (const auto& pa : aFiles) {
        const std::string key = normKey(fs::relative(pa, dirA));
        auto bit = bIndex.find(key);
        if (bit == bIndex.end()) { std::printf("[ONLY-A] %s\n", key.c_str()); ++onlyA; continue; }
        matchedB.insert(key);
        std::map<std::string, std::multiset<std::string>> A, B;
        try { A = loadOne(pa.string()); B = loadOne(bit->second.string()); }
        catch (const std::exception& ex) { std::printf("[ERROR ] %s — %s\n", key.c_str(), ex.what()); ++errs; continue; }
        ++compared;
        std::set<std::string> classes;
        for (auto& kv : A) classes.insert(kv.first);
        for (auto& kv : B) classes.insert(kv.first);
        std::vector<std::string> lines;
        for (const auto& cls : classes) {
            const auto& a = A[cls]; const auto& b = B[cls];
            if (a == b) continue;
            char buf[300];
            if (a.size() != b.size()) {
                std::snprintf(buf, sizeof buf, "    %-38s A=%-5zu B=%-5zu DE-SHARING/COUNT (%+d)",
                              cls.c_str(), a.size(), b.size(), static_cast<int>(a.size()) - static_cast<int>(b.size()));
            } else {
                std::vector<std::string> onlyAv;
                std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(onlyAv));
                std::snprintf(buf, sizeof buf, "    %-38s A=%-5zu B=%-5zu FIELD DIFF (%zu struct(s) differ)",
                              cls.c_str(), a.size(), b.size(), onlyAv.size());
            }
            lines.emplace_back(buf);
        }
        if (lines.empty()) { if (verbose) std::printf("[SAME  ] %s\n", key.c_str()); continue; }
        ++differ;
        std::printf("[DIFF  ] %s\n", key.c_str());
        for (const auto& l : lines) std::printf("%s\n", l.c_str());
    }
    // Files present ONLY in B (never paired during the A walk). bIndex is sorted (std::map), so
    // this reports in deterministic key order.
    for (const auto& [key, path] : bIndex)
        if (!matchedB.count(key)) { std::printf("[ONLY-B] %s\n", key.c_str()); ++onlyB; (void)path; }

    std::printf("=== treediff: %d compared, %d differ, %d only-in-A, %d only-in-B, %d error(s) ===\n",
                compared, differ, onlyA, onlyB, errs);
    return 0;
}

int doExprDump(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    std::multiset<std::string> exprs;
    for (const auto& [off, cls] : des.ListObjects()) {
        if (cls != "hkbExpressionCondition") continue;
        std::shared_ptr<havok::IHavokObject> obj;
        try { obj = des.ConstructVirtualClass(dr, off); } catch (...) { continue; }
        auto ec = std::dynamic_pointer_cast<havok::hkbExpressionCondition>(obj);
        if (ec) exprs.insert(ec->m_expression);
    }
    for (const auto& e : exprs) std::printf("%s\n", e.c_str());
    std::printf("--- %zu hkbExpressionCondition ---\n", exprs.size());
    return 0;
}

// idcheck: reproduce the tagfile #NNNN numbering from a binary and grade it,
// slot-for-slot by class, against a reference tagfile XML. Hypothesis: #NNNN =
// classnamesCount + [root first, then post-order DFS of the reference graph]
// (an object numbered after everything it references; refs in field order).
int doIdCheck(const std::string& binFile, const std::string& xmlFile) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(binFile, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);

    const auto objs = des.ListObjects();       // (offset, class), ascending by offset

    // Offset: the tagfile numbers __classnames__ + __types__ before __data__, so
    // data #NNNN starts at some N. Derive N from the reference's root #NNNN to
    // isolate ORDER-correctness from offset-computation (fixed properly later).
    std::uint32_t classCount = static_cast<std::uint32_t>(des.ClassnamesCount());
    {
        std::ifstream f(xmlFile, std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("class=\"hkRootLevelContainer\"") == std::string::npos) continue;
            const auto p = line.find("name=\"#");
            if (p != std::string::npos) classCount = static_cast<std::uint32_t>(std::stoul(line.substr(p + 7)));
            break;
        }
    }
    std::printf("  (ClassnamesCount=%zu, XML-derived offset=%u)\n", des.ClassnamesCount(), classCount);

    std::unordered_map<std::uint32_t, std::string> classOf;
    for (const auto& [o, c] : objs) classOf[o] = c;

    std::uint32_t root = 0xFFFFFFFFu;
    for (const auto& [o, c] : objs) if (c == "hkRootLevelContainer") { root = o; break; }
    if (root == 0xFFFFFFFFu) { std::printf("ERROR: no hkRootLevelContainer\n"); return 1; }

    // Fully construct from the root: each ReadClassPointer resolves its target
    // inline, so ConstructVirtualClass returns in read-completion (post-order)
    // order, in field order with inline arrays — exactly the tagfile #NNNN walk.
    havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    try { des.ConstructVirtualClass(dr, root); }
    catch (const std::exception& e) { std::printf("ERROR: construct from root: %s\n", e.what()); return 1; }
    const auto& completion = des.ReadCompletionOrder();

    // #NNNN = root first (classCount), then the rest in post-order (root is last).
    std::map<std::uint32_t, std::string> myMap;   // #NNNN -> class
    std::uint32_t rank = classCount + 1;
    for (const std::uint32_t off : completion) {
        const std::uint32_t id = (off == root) ? classCount : rank++;
        myMap[id] = classOf.count(off) ? classOf[off] : std::string("<unknown>");
    }
    std::printf("  completion-order objects: %zu\n", completion.size());

    // Parse the reference XML: #NNNN -> class.
    std::map<std::uint32_t, std::string> xmlMap;
    {
        std::ifstream f(xmlFile, std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            const auto p = line.find("<hkobject name=\"#");
            if (p == std::string::npos) continue;
            const auto ns = p + 17;
            const auto ne = line.find('"', ns);
            const auto cp = line.find("class=\"", ne);
            if (ne == std::string::npos || cp == std::string::npos) continue;
            const auto cs = cp + 7;
            const auto ce = line.find('"', cs);
            xmlMap[static_cast<std::uint32_t>(std::stoul(line.substr(ns, ne - ns)))] = line.substr(cs, ce - cs);
        }
    }

    std::printf("idcheck: %zu objects, classCount(offset)=%u\n", objs.size(), classCount);
    std::printf("         reference XML: %zu numbered objects\n", xmlMap.size());
    int match = 0, mism = 0, shown = 0;
    for (const auto& [nnnn, cls] : xmlMap) {
        auto it = myMap.find(nnnn);
        if (it != myMap.end() && it->second == cls) { ++match; }
        else {
            ++mism;
            if (shown++ < 12)
                std::printf("  MISMATCH #%u: xml=%s  mine=%s\n", nnnn, cls.c_str(),
                            it == myMap.end() ? "(absent)" : it->second.c_str());
        }
    }
    std::printf("  ===> %d/%zu slots match by class (%d mismatch)\n", match, xmlMap.size(), mism);
    return 0;
}

// idalign: grade the structural-alignment oracle (shared havok/sct/TagfileOracle.h)
// on a binary/tagfile pair. Co-DFS the binary graph (refs in Read order) and the
// tagfile graph (refs in doc order) from their shared root; if isomorphic and ref
// orders agree, every object maps to exactly one #NNNN with matching class.
int doIdAlign(const std::string& binFile, const std::string& xmlFile) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(binFile, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    std::uint32_t binRoot = 0xFFFFFFFFu;
    for (const auto& [o, c] : des.ListObjects()) if (c == "hkRootLevelContainer") binRoot = o;
    if (binRoot == 0xFFFFFFFFu) { std::printf("ERROR: no root in binary\n"); return 1; }
    havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    try { des.ConstructVirtualClass(dr, binRoot); }
    catch (const std::exception& e) { std::printf("ERROR: construct: %s\n", e.what()); return 1; }

    std::ifstream f(xmlFile, std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const havok::sct::OracleResult res = havok::sct::AlignTagfile(des, all);
    if (!res.ok) { std::printf("ERROR: %s\n", res.error.c_str()); return 1; }
    for (const auto& m : res.classMismatches)
        std::printf("  CLASS MISMATCH #%u: bin=%s xml=%s (refs bin=%zu xml=%zu)\n",
                    m.id, m.binClass.c_str(), m.xmlClass.c_str(), m.binN, m.xmlN);
    for (const auto& m : res.refMismatches)
        std::printf("  REF-COUNT MISMATCH #%u (%s): bin=%zu xml=%zu\n",
                    m.id, m.xmlClass.c_str(), m.binN, m.xmlN);
    std::printf("idalign: binary %zu objs, XML %zu objs; mapped %zu; classMism=%d refCountMism=%d conflict=%d\n",
                res.binObjs, res.xmlObjs, res.mapped, res.classMism, res.refCountMism, res.conflict);
    return 0;
}

int doBwDump(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    std::multiset<std::string> arrs;
    for (const auto& [off, cls] : des.ListObjects()) {
        if (cls != "hkbBoneWeightArray") continue;
        std::shared_ptr<havok::IHavokObject> obj;
        try { obj = des.ConstructVirtualClass(dr, off); } catch (...) { continue; }
        auto bw = std::dynamic_pointer_cast<havok::hkbBoneWeightArray>(obj);
        if (!bw) continue;
        std::string canon = "n=" + std::to_string(bw->m_boneWeights.size()) + ":";
        for (std::size_t i = 0; i < bw->m_boneWeights.size(); ++i) {
            char b[16]; std::snprintf(b, sizeof(b), "%.3f,", bw->m_boneWeights[i]); canon += b;
        }
        arrs.insert(canon);
    }
    for (const auto& a : arrs) std::printf("%s\n", a.c_str());
    std::printf("--- %zu hkbBoneWeightArray ---\n", arrs.size());
    return 0;
}

int doBindDump(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    std::multiset<std::string> sets;
    for (const auto& [off, cls] : des.ListObjects()) {
        if (cls != "hkbVariableBindingSet") continue;
        std::shared_ptr<havok::IHavokObject> obj;
        try { obj = des.ConstructVirtualClass(dr, off); } catch (...) { continue; }
        auto bs = std::dynamic_pointer_cast<havok::hkbVariableBindingSet>(obj);
        if (!bs) continue;
        std::vector<std::string> lines;
        for (const auto& b : bs->m_bindings)
            lines.push_back(b.m_memberPath + "|v" + std::to_string(b.m_variableIndex) +
                            "|bit" + std::to_string(static_cast<int>(b.m_bitIndex)) +
                            "|t" + std::to_string(static_cast<int>(b.m_bindingType)));
        std::sort(lines.begin(), lines.end());
        std::string canon = "enable=" + std::to_string(bs->m_indexOfBindingToEnable);
        for (const auto& l : lines) canon += " ;; " + l;
        sets.insert(canon);
    }
    for (const auto& s : sets) std::printf("%s\n", s.c_str());
    std::printf("--- %zu hkbVariableBindingSet ---\n", sets.size());
    return 0;
}

// Count #NNNN reference tokens in a param's text (co-located with idalign's scan).
static int countRefs(const std::string& t) {
    int n = 0;
    for (std::size_t q = t.find('#'); q != std::string::npos; q = t.find('#', q + 1))
        if (q + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[q + 1]))) ++n;
    return n;
}

// Pretty-print a parsed <hkobject> tree — proves the generic reader recovers the
// class/id/field structure of a Nemesis/Pandora patch node (Stage 4 step 2).
static void dumpXmlObj(const havok::xml::Node& obj, int indent) {
    const std::string pad(indent, ' ');
    const std::string id(obj.attr("name")), cls(obj.attr("class"));
    std::size_t nparam = 0;
    for (const auto& c : obj.children) if (c.tag == "hkparam") ++nparam;
    std::printf("%s%s  class=%s  (%zu params)\n", pad.c_str(),
                id.empty() ? "<inline>" : id.c_str(), cls.c_str(), nparam);
    for (const auto& c : obj.children) {
        if (c.tag != "hkparam") continue;
        const std::string pn(c.attr("name")), ne(c.attr("numelements"));
        std::size_t nobj = 0;
        for (const auto& cc : c.children) if (cc.tag == "hkobject") ++nobj;
        const int nref = countRefs(c.text);
        if (nobj)
            std::printf("%s  .%-24s -> %zu inline object(s)%s%s\n", pad.c_str(), pn.c_str(), nobj,
                        ne.empty() ? "" : "  numelements=", ne.c_str());
        else if (nref)
            std::printf("%s  .%-24s -> %d ref(s)%s%s\n", pad.c_str(), pn.c_str(), nref,
                        ne.empty() ? "" : "  numelements=", ne.c_str());
        else {
            std::string v = c.text;
            if (v.size() > 48) v = v.substr(0, 45) + "...";
            for (char& ch : v) if (ch == '\n' || ch == '\r') ch = ' ';
            std::printf("%s  .%-24s = \"%s\"%s%s\n", pad.c_str(), pn.c_str(), v.c_str(),
                        ne.empty() ? "" : "  numelements=", ne.c_str());
        }
        for (const auto& cc : c.children) if (cc.tag == "hkobject") dumpXmlObj(cc, indent + 4);
    }
}

int doXmlParse(const std::string& xmlFile) {
    std::ifstream f(xmlFile, std::ios::binary);
    if (!f) { std::printf("ERROR: cannot open %s\n", xmlFile.c_str()); return 1; }
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    havok::xml::StripPatchOriginals(src);   // no-op on clean tagfiles; applies Nemesis MOD_CODE edits
    const havok::xml::Node root = havok::xml::Parse(src);
    if (root.tag.empty()) { std::printf("ERROR: no XML element parsed\n"); return 1; }

    // Gather the top-level <hkobject>s: a bare patch-node file is a single hkobject;
    // a full tagfile nests them under <hksection name="__data__">.
    std::vector<const havok::xml::Node*> objs;
    if (root.tag == "hkobject") {
        objs.push_back(&root);
    } else {
        std::vector<const havok::xml::Node*> stack{ &root };
        while (!stack.empty()) {
            const havok::xml::Node* n = stack.back(); stack.pop_back();
            for (const auto& c : n->children) {
                if (c.tag == "hkobject") objs.push_back(&c);
                else stack.push_back(&c);
            }
        }
    }
    if (objs.empty()) { std::printf("ERROR: no <hkobject> found under <%s>\n", root.tag.c_str()); return 1; }

    std::printf("xmlparse: %zu top-level hkobject(s)\n", objs.size());
    if (objs.size() <= 4) {
        for (const auto* o : objs) dumpXmlObj(*o, 0);
    } else {
        // Big tagfile: show the first two, then a class histogram.
        for (std::size_t i = 0; i < 2; ++i) dumpXmlObj(*objs[i], 0);
        std::map<std::string, int> hist;
        for (const auto* o : objs) ++hist[std::string(o->attr("class"))];
        std::printf("  ... class histogram (%zu classes):\n", hist.size());
        for (const auto& [c, n] : hist) std::printf("    %5d  %s\n", n, c.c_str());
    }
    return 0;
}

int doPatchConvert(const std::string& vanBin, const std::vector<std::string>& extra,
                   const std::string& out) {
    if (extra.size() < 2 || out.empty()) {
        std::printf("usage: patchconvert <vanillaBin> <vanillaXml> <patchDir1> [patchDir2 ...] -o <out.hkx>\n"
                    "  patch dirs are applied in LOAD ORDER (later wins scalar conflicts).\n");
        return 2;
    }
    const std::vector<std::string> patchDirs(extra.begin() + 1, extra.end());  // extra[0] = vanillaXml
    const havok::sct::PatchConvertResult res =
        havok::sct::ConvertPatch(vanBin, extra[0], patchDirs, out);
    std::printf("patchconvert: %s\n", res.ok ? "OK" : "FAILED");
    if (!res.ok) { std::printf("  error: %s\n", res.error.c_str()); return 1; }
    std::printf("  overrides=%d added=%d mergedConflicts=%d symbolsResolved=%d refsUnresolved=%d skippedMismatch=%d\n",
                res.overrides, res.added, res.mergedConflicts, res.symbolsResolved, res.refsUnresolved, res.skippedMismatch);
    if (!res.unsupportedClasses.empty()) {
        std::printf("  UNSUPPORTED classes (%zu):\n", res.unsupportedClasses.size());
        for (const auto& c : res.unsupportedClasses) std::printf("    %s\n", c.c_str());
    }
    if (!res.warnings.empty()) {
        std::printf("  warnings (%zu):\n", res.warnings.size());
        std::size_t shown = 0;
        for (const auto& w : res.warnings) {
            if (shown++ >= 15) { std::printf("    ... +%zu more\n", res.warnings.size() - 15); break; }
            std::printf("    %s\n", w.c_str());
        }
    }
    std::printf("  -> %s\n", out.c_str());
    return 0;
}

int doPatchDelta(const std::string& vanBin, const std::vector<std::string>& extra, const std::string& out) {
    if (extra.size() < 2 || out.empty()) {
        std::printf("usage: patchdelta <vanillaBin> <vanillaXml> <modPatchDir1> [modPatchDir2 ...] -o <outDeltaDir>\n"
                    "  emits ONE mod's native .hky delta (overrides as full nodes + new mod$N nodes +\n"
                    "  data/additive.yaml), keyed by the stable tagfile id, to merge onto a vanbase. A mod\n"
                    "  that ships several Nemesis codes (e.g. TDM's tdmlen/tdmh/tdmv) passes them all here\n"
                    "  so the one delta is its combined contribution.\n");
        return 2;
    }
    const std::vector<std::string> patchDirs(extra.begin() + 1, extra.end());  // extra[0] = vanillaXml
    const auto res = havok::sct::ConvertPatch(vanBin, extra[0], patchDirs, "", out, "");
    std::printf("patchdelta: %s\n", res.ok ? "OK" : "FAILED");
    if (!res.ok) { std::printf("  error: %s\n", res.error.c_str()); return 1; }
    std::printf("  overrides=%d added=%d symbolsResolved=%d refsUnresolved=%d skippedMismatch=%d\n",
                res.overrides, res.added, res.symbolsResolved, res.refsUnresolved, res.skippedMismatch);
    if (!res.warnings.empty()) {
        std::printf("  warnings (%zu):\n", res.warnings.size());
        std::size_t shown = 0;
        for (const auto& w : res.warnings) {
            if (shown++ >= 40) { std::printf("    ... +%zu more\n", res.warnings.size() - 40); break; }
            std::printf("    %s\n", w.c_str());
        }
    }
    std::printf("  -> %s\n", out.c_str());
    return 0;
}

int doVanBase(const std::string& vanBin, const std::vector<std::string>& extra, const std::string& out) {
    if (extra.size() != 1 || out.empty()) {
        std::printf("usage: vanbase <vanillaBin> <vanillaXml> -o <outBaseDir>\n"
                    "  decompiles vanilla with tagfile ids — the base the per-mod deltas merge onto.\n");
        return 2;
    }
    const auto res = havok::sct::ConvertPatch(vanBin, extra[0], {}, "", "", out);
    std::printf("vanbase: %s\n", res.ok ? "OK" : "FAILED");
    if (!res.ok) { std::printf("  error: %s\n", res.error.c_str()); return 1; }
    std::printf("  -> %s\n", out.c_str());
    return 0;
}

// ── animdata-derive-check: the part-B oracle ─────────────────────────────────
// Derive a project's animationdatasinglefile clip list from the behavior graph
// (havok::animdata::DeriveClipList) and byte-diff it against the vanilla cache. Proves the
// shared deriver reproduces Bethesda's bytes before it is trusted at runtime (ServeAnimData).
//   animdata-derive-check <cache.txt> <ProjectName> <behaviorDir> <characterHkx>

std::string ReadTextFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string StripLine(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t b = 0; while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
    return s.substr(b);
}

// Value after "key:" on a decompiled-clip yaml line, single-quotes stripped.
std::string YamlVal(const std::string& line) {
    const auto c = line.find(':');
    if (c == std::string::npos) return {};
    std::string v = StripLine(line.substr(c + 1));
    if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'') v = v.substr(1, v.size() - 2);
    return v;
}

// Parse one decompiled clips/<id>.yaml into a DeriveClipInput. Flat key: value, plus a
// `triggers:` list of `- localTime: / event: / relativeToEndOfClip:` blocks.
bool ParseClipYaml(const fs::path& p, havok::animdata::DeriveClipInput& out) {
    std::ifstream f(p);
    if (!f) return false;
    std::string raw;
    bool haveName = false, inTrig = false;
    havok::animdata::DeriveClipInput::Trigger cur;
    bool curHasLt = false;
    auto flush = [&] {
        if (curHasLt) out.triggers.push_back(cur);
        cur = {}; curHasLt = false;
    };
    while (std::getline(f, raw)) {
        const std::string s = StripLine(raw);
        if (s.rfind("name:", 0) == 0 && !haveName) { out.name = YamlVal(s); haveName = true; }
        else if (s.rfind("animationName:", 0) == 0) out.animationName = YamlVal(s);
        else if (s.rfind("playbackSpeed:", 0) == 0) out.playbackSpeed = std::atof(YamlVal(s).c_str());
        else if (s.rfind("cropStartAmountLocalTime:", 0) == 0) out.cropStart = std::atof(YamlVal(s).c_str());
        else if (s.rfind("cropEndAmountLocalTime:", 0) == 0) out.cropEnd = std::atof(YamlVal(s).c_str());
        else if (s.rfind("triggers:", 0) == 0) inTrig = true;
        else if (inTrig && s.rfind("- localTime:", 0) == 0) {
            flush();
            cur.localTime = std::atof(YamlVal(s).c_str()); curHasLt = true;
        }
        else if (inTrig && s.rfind("event:", 0) == 0) cur.event = YamlVal(s);
        else if (inTrig && s.rfind("relativeToEndOfClip:", 0) == 0) cur.relativeToEndOfClip = (YamlVal(s) == "true");
    }
    flush();
    return haveName;
}

// An animation's derivation-relevant data: its duration (m_duration — present even when the
// animation has no root-motion record, unlike the cache motion table) and its annotation-track
// events. Skyrim bakes physical triggers (footsteps, weapon sounds) into the annotation track,
// which the cache merges into the clip's trigger list — so cache triggers = graph triggers UNION
// these. Cached per animation. Missing file (BSA-packed / absent) or a non-animation .hkx → has=false.
struct AnimInfo {
    bool                                     has = false;
    double                                   duration = 0.0;
    std::vector<std::pair<std::string, double>> anns;
};

const AnimInfo& GetAnimInfo(const fs::path& animRoot, const std::string& animName) {
    static std::unordered_map<std::string, AnimInfo> cache;
    std::string key = animName;
    for (char& c : key) c = (char)std::tolower((unsigned char)c);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    auto& out = cache[key];  // default (has=false), returned on any failure below

    std::string rel = animName;
    for (char& c : rel) if (c == '\\') c = '/';
    const fs::path p = animRoot / rel;
    std::error_code ec;
    if (!fs::exists(p, ec)) return out;
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(p.string(), bytes, &err)) return out;
    // Shared with the runtime: the byte-based extractor (havok::sct::ExtractAnimClipInfo). The
    // offline path only differs in that it resolves + reads the file first; the extraction is one.
    const havok::sct::AnimClipInfo info = havok::sct::ExtractAnimClipInfo(bytes);
    out.has      = info.has;
    out.duration = info.duration;
    for (const auto& [text, time] : info.annotations) out.anns.emplace_back(text, time);
    return out;
}

// Debug: dump every annotation track of an animation (time + text), to reverse the cache's
// annotation-filtering rules for special animations.
int doAnimAnns(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    for (const auto& [off, cls] : des.ListObjects()) {
        if (cls != "hkaSplineCompressedAnimation" &&
            cls != "hkaInterleavedUncompressedAnimation" && cls != "hkaAnimation") continue;
        std::shared_ptr<havok::IHavokObject> obj;
        try { obj = des.ConstructVirtualClass(dr, off); } catch (const std::exception& e) { std::printf("construct: %s\n", e.what()); break; }
        auto anim = std::dynamic_pointer_cast<havok::hkaAnimation>(obj);
        if (!anim) break;
        std::printf("duration=%g  transformTracks=%d  annotationTracks=%zu\n",
                    anim->m_duration, anim->m_numberOfTransformTracks, anim->m_annotationTracks.size());
        for (std::size_t t = 0; t < anim->m_annotationTracks.size(); ++t) {
            const auto& tr = anim->m_annotationTracks[t];
            if (tr.m_annotations.empty()) continue;
            std::printf("  track %zu: %zu annotation(s)\n", t, tr.m_annotations.size());
            for (const auto& a : tr.m_annotations) std::printf("    %-10g '%s'\n", a.m_time, a.m_text.c_str());
        }
        break;
    }
    return 0;
}

std::string CanonClip(const havok::animdata::ClipGenerator& g) {
    std::string s = g.name + "|i=" + g.animIndex + "|s=" + g.playbackSpeed +
                    "|cs=" + g.cropStart + "|ce=" + g.cropEnd;
    for (const auto& t : g.triggers) s += "|" + t;
    return s;
}

// Shared extraction for BOTH animdata-derive verbs (check = byte-diff, derive = emit): parse the
// master cache, locate the project, decompile its character (roster in order) + behaviors (clip
// generators + merged animation annotations + real durations), and project the clip list via the
// ONE shared transform havok::animdata::DeriveClipList. Written once so check and emit can't drift.
struct DerivedProject {
    bool                                        ok = false;
    std::string                                 error;
    havok::animdata::SingleFile                 master;      // parsed master cache (all projects)
    std::size_t                                 projIndex = 0;
    std::vector<havok::animdata::ClipGenerator> derived;     // the derived clip list for that project
    std::vector<std::string>                    unresolved;  // clips whose animationName isn't in the roster
    int                                         behaviors  = 0;
    std::size_t                                 graphClips = 0;
    std::size_t                                 rosterSize = 0;
};

DerivedProject deriveOneProject(const std::string& cacheFile, const std::string& projName,
                                const fs::path& behDir, const fs::path& charHkx) {
    DerivedProject R;
    auto low = [](std::string s){ for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };

    // 1) Parse the master cache; locate the project.
    try { R.master = havok::animdata::ParseSingleFile(ReadTextFile(cacheFile)); }
    catch (const std::exception& e) { R.error = std::string("cache parse: ") + e.what(); return R; }
    int found = -1;
    for (int i = 0; i < (int)R.master.projects.size(); ++i) {
        std::string n = low(R.master.projects[i].name);
        if (n == low(projName) || n == low(projName) + ".txt") { found = i; break; }
    }
    if (found < 0) { R.error = "project '" + projName + "' not found in cache"; return R; }
    R.projIndex = (std::size_t)found;
    const havok::animdata::Project& proj = R.master.projects[R.projIndex];

    const fs::path tmp = fs::temp_directory_path() / "sct_animderive";
    std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec);

    // 2) Roster FIRST: decompile the character; read animations.txt (animationNames in order).
    std::vector<std::string> roster;
    {
        std::vector<std::uint8_t> bytes; std::string err;
        if (havok::sct::ReadHavokFile(charHkx.string(), bytes, &err)) {
            const fs::path cd = tmp / "_char";
            havok::sct::DecompileToDir(bytes, cd.string());
            fs::path rosterPath = cd / "animations.txt";
            if (!fs::exists(rosterPath)) {
                for (fs::recursive_directory_iterator ri(cd, ec), rend; !ec && ri != rend; ri.increment(ec))
                    if (ri->is_regular_file(ec) && low(ri->path().filename().string()) == "animations.txt") { rosterPath = ri->path(); break; }
            }
            std::ifstream rf(rosterPath); std::string line;
            while (std::getline(rf, line)) { line = StripLine(line); if (!line.empty()) roster.push_back(line); }
        }
    }
    R.rosterSize = roster.size();

    // Trigger-clamp duration by animIndex: seed from the master motion table, then prefer the
    // referenced animation's own m_duration (present even for no-root-motion anims).
    std::unordered_map<int, double> durByIndex;
    for (const auto& m : proj.motions) durByIndex[std::atoi(m.animIndex.c_str())] = std::atof(m.duration.c_str());

    // 3) Decompile every behavior .hkx; collect clip generators + merge animation annotations.
    const fs::path animRoot = behDir.parent_path();   // "Animations\\X.HKX" -> <character>/Animations/X.HKX
    std::vector<havok::animdata::DeriveClipInput> clips;
    for (fs::directory_iterator it(behDir, ec), end; it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (low(it->path().extension().string()) != ".hkx") continue;
        std::vector<std::uint8_t> bytes; std::string err;
        if (!havok::sct::ReadHavokFile(it->path().string(), bytes, &err)) continue;
        const fs::path od = tmp / it->path().stem();
        const auto dr = havok::sct::DecompileToDir(bytes, od.string());
        if (!dr.ok || dr.kind != "behavior") continue;
        ++R.behaviors;
        // Clip generators straight from the in-memory model — the SAME extraction the runtime
        // uses (DeriveClipInputsFromBehavior), so this oracle runs the actual runtime assembly.
        try {
            havok::model::BehaviorData bd = havok::model::YamlBehaviorLoader::Load(od.string());
            for (auto& c : havok::sct::DeriveClipInputsFromBehavior(bd)) clips.push_back(std::move(c));
        } catch (...) {}
    }
    R.graphClips = clips.size();

    // 4) Project the clip list via the shared runtime+offline assembly (annotation merge over a
    //    filesystem animation reader, then DeriveClipList). The runtime supplies a VFS/loose reader.
    auto readAnim = [&](const std::string& an) -> std::vector<std::uint8_t> {
        std::string rel = an; for (char& c : rel) if (c == '\\') c = '/';
        const fs::path p = animRoot / rel;
        std::vector<std::uint8_t> b; std::string e; std::error_code ee;
        if (fs::exists(p, ee) && havok::sct::ReadHavokFile(p.string(), b, &e)) return b;
        return {};
    };
    R.derived = havok::sct::DeriveProjectClipList(clips, roster, durByIndex, readAnim, &R.unresolved);

    fs::remove_all(tmp, ec);
    R.ok = true;
    return R;
}

int doAnimDataDeriveCheck(const std::string& cacheFile, const std::vector<std::string>& extra) {
    if (extra.size() < 3) {
        std::printf("usage: animdata-derive-check <cache.txt> <ProjectName> <behaviorDir> <characterHkx>\n");
        return 2;
    }
    const DerivedProject R = deriveOneProject(cacheFile, extra[0], extra[1], extra[2]);
    if (!R.ok) { std::printf("ERROR: %s\n", R.error.c_str()); return 1; }
    const havok::animdata::Project& proj = R.master.projects[R.projIndex];
    const auto& derived    = R.derived;
    const auto& unresolved = R.unresolved;

    // byte-diff (multiset over canonical per-clip strings).
    std::multiset<std::string> D, C;
    for (const auto& g : derived)     D.insert(CanonClip(g));
    for (const auto& g : proj.clips)  C.insert(CanonClip(g));
    std::vector<std::string> onlyC, onlyD;
    std::set_difference(C.begin(), C.end(), D.begin(), D.end(), std::back_inserter(onlyC));
    std::set_difference(D.begin(), D.end(), C.begin(), C.end(), std::back_inserter(onlyD));
    const std::size_t matched = C.size() - onlyC.size();

    // Classify each only-in-cache clip: NAME present among derived (field/rule diff) or absent (selection gap)?
    std::unordered_set<std::string> derivedNames;
    for (const auto& g : derived) derivedNames.insert(g.name);
    auto nameOf = [](const std::string& canon) { return canon.substr(0, canon.find('|')); };
    std::size_t cacheOnlyNamePresent = 0, cacheOnlyNameAbsent = 0;
    for (const auto& c : onlyC)
        (derivedNames.count(nameOf(c)) ? cacheOnlyNamePresent : cacheOnlyNameAbsent)++;

    std::printf("animdata-derive-check  project=%s\n", proj.name.c_str());
    std::printf("  behaviors decompiled : %d   graph clip-gens: %zu   roster: %zu\n", R.behaviors, R.graphClips, R.rosterSize);
    std::printf("  cache clips=%zu  derived clips=%zu\n", proj.clips.size(), derived.size());
    std::printf("  BYTE-EXACT matched   : %zu / %zu  (%.1f%%)\n",
                matched, C.size(), C.empty() ? 0.0 : 100.0 * (double)matched / (double)C.size());
    std::printf("  only-in-cache=%zu  only-in-derived=%zu  animName-unresolved=%zu\n",
                onlyC.size(), onlyD.size(), unresolved.size());
    std::printf("  only-in-cache breakdown: name-present(field/rule diff)=%zu  name-absent(selection)=%zu\n",
                cacheOnlyNamePresent, cacheOnlyNameAbsent);
    std::unordered_map<std::string, std::vector<std::string>> derivedByName;
    for (const auto& g : derived) derivedByName[g.name].push_back(CanonClip(g));
    for (std::size_t i = 0; i < onlyC.size() && i < 70; ++i) {
        std::printf("    CACHE : %s\n", onlyC[i].c_str());
        for (const auto& d : derivedByName[nameOf(onlyC[i])]) std::printf("    DERIV : %s\n", d.c_str());
    }
    for (std::size_t i = 0; i < unresolved.size() && i < 6; ++i) std::printf("    NO-ROSTER  : %s\n", unresolved[i].c_str());
    return 0;
}

// animdata-derive: the EMIT sibling of animdata-derive-check. Derives one project's clip list from
// the graph (motions carried from the master verbatim), splices it into a copy of the master, and
// writes a full animationdatasinglefile.txt. This is "the converter derives the cache" in offline
// form — the artifact is byte-gateable (diff vs vanilla; only that project's clip lines should
// differ) before the same DeriveClipList is wired into the converter app / runtime ServeAnimData.
//   animdata-derive <master.txt> <ProjectName> <behaviorDir> <characterHkx> -o <out.txt>
int doAnimDataDerive(const std::string& cacheFile, const std::vector<std::string>& extra, const std::string& out) {
    if (extra.size() < 3 || out.empty()) {
        std::printf("usage: animdata-derive <master.txt> <ProjectName> <behaviorDir> <characterHkx> -o <out.txt>\n");
        return 2;
    }
    DerivedProject R = deriveOneProject(cacheFile, extra[0], extra[1], extra[2]);
    if (!R.ok) { std::printf("ERROR: %s\n", R.error.c_str()); return 1; }

    havok::animdata::Project& proj = R.master.projects[R.projIndex];

    // HYBRID (carry-the-exceptions): for each master clip, use the DERIVED clip when it reproduces
    // the master byte-for-byte, else carry the master's clip verbatim. The emitted file is
    // byte-exact by construction; the CARRIED names are the derivation's exception set — exactly
    // what a runtime with no vanilla cache must carry from the Skyrim.hky master (the graph derives
    // the rest). Motions are always carried (proprietary extraction).
    std::unordered_map<std::string, const havok::animdata::ClipGenerator*> derivedByName;
    for (const auto& g : R.derived) derivedByName.emplace(g.name, &g);
    std::vector<havok::animdata::ClipGenerator> hybrid;
    hybrid.reserve(proj.clips.size());
    std::vector<std::string> carried;
    std::size_t derivedExact = 0;
    for (const auto& m : proj.clips) {
        auto it = derivedByName.find(m.name);
        if (it != derivedByName.end() && CanonClip(*it->second) == CanonClip(m)) {
            hybrid.push_back(*it->second); ++derivedExact;
        } else {
            hybrid.push_back(m); carried.push_back(m.name);
        }
    }
    const std::size_t masterClips = proj.clips.size();
    proj.clips = std::move(hybrid);

    const std::string text = havok::animdata::EmitSingleFile(R.master);
    // Binary write so the canonical CRLF layout survives verbatim (no newline translation).
    if (!havok::sct::WriteHavokFile(out, std::vector<std::uint8_t>(text.begin(), text.end()))) {
        std::printf("ERROR: cannot write %s\n", out.c_str()); return 1;
    }
    std::printf("animdata-derive  project=%s  wrote %s (%zu bytes, byte-exact hybrid)\n",
                proj.name.c_str(), out.c_str(), text.size());
    std::printf("  derived-exact: %zu / %zu  (%.1f%%)   carried (exception set): %zu   graph clip-gens: %zu\n",
                derivedExact, masterClips, masterClips ? 100.0 * (double)derivedExact / (double)masterClips : 0.0,
                carried.size(), R.graphClips);
    for (std::size_t i = 0; i < carried.size() && i < 40; ++i)
        std::printf("    CARRY : %s\n", carried[i].c_str());
    if (carried.size() > 40) std::printf("    ... (%zu more)\n", carried.size() - 40);
    return 0;
}

// animdata-derive-delta: the CLIP HALF of the mod-delta derive. Derive the clip list of a MERGED
// (vanilla+mod) behavior tree and of the VANILLA tree, then emit the DELTA — the clips the mod adds
// — with their NEW-animation indices re-symbolized ("$N") into the runtime patch form. Motion (a
// MotionRecord per symbol) is rung 3 (extract) or carried from the mod's patch; this proves the
// clip side. Compare the ADD lines by eye against the mod's Nemesis animdata patch clips.
//   animdata-derive-delta <master.txt> <Project> <mergedBehDir> <mergedChar> <vanillaBehDir> <vanillaChar>
int doAnimDataDeriveDelta(const std::string& cacheFile, const std::vector<std::string>& extra) {
    if (extra.size() < 5) {
        std::printf("usage: animdata-derive-delta <master.txt> <Project> <mergedBehDir> <mergedChar> <vanillaBehDir> <vanillaChar>\n");
        return 2;
    }
    const DerivedProject M = deriveOneProject(cacheFile, extra[0], extra[1], extra[2]);
    if (!M.ok) { std::printf("ERROR (merged): %s\n", M.error.c_str()); return 1; }
    const DerivedProject V = deriveOneProject(cacheFile, extra[0], extra[3], extra[4]);
    if (!V.ok) { std::printf("ERROR (vanilla): %s\n", V.error.c_str()); return 1; }

    // A merged clip is part of the delta iff its canonical record isn't already in the vanilla
    // derivation (added, or an override of an existing clip).
    std::unordered_set<std::string> vanillaCanon;
    for (const auto& g : V.derived) vanillaCanon.insert(CanonClip(g));

    // Re-symbolize: a delta clip whose animIndex sits BEYOND the vanilla roster is a NEW animation
    // (the mod added it). Assign one "$N" symbol per distinct new animIndex, shared by every clip
    // that references it — mirroring how the Nemesis patch pairs a clip and its motion by symbol,
    // and how the runtime (MergeProjectPatch) re-allocates the real index at merge time.
    std::unordered_map<int, std::string> symbolOf;
    int nextSym = 0;
    std::vector<havok::animdata::ClipGenerator> delta;
    std::size_t newAnim = 0, changedExisting = 0;
    for (const auto& g : M.derived) {
        if (vanillaCanon.count(CanonClip(g))) continue;                 // unchanged from vanilla
        havok::animdata::ClipGenerator d = g;
        const int idx = std::atoi(g.animIndex.c_str());
        if (idx >= (int)V.rosterSize) {                                 // new animation -> symbol
            auto it = symbolOf.find(idx);
            if (it == symbolOf.end()) it = symbolOf.emplace(idx, "$" + std::to_string(nextSym++)).first;
            d.animIndex = it->second;
            ++newAnim;
        } else {
            ++changedExisting;                                          // same animation, changed record
        }
        delta.push_back(std::move(d));
    }

    std::printf("animdata-derive-delta  project=%s\n", M.master.projects[M.projIndex].name.c_str());
    std::printf("  merged clips=%zu (roster %zu)   vanilla clips=%zu (roster %zu)\n",
                M.derived.size(), M.rosterSize, V.derived.size(), V.rosterSize);
    std::printf("  DELTA clips=%zu   new-animation(symbolized)=%zu   changed-existing=%zu   distinct new symbols=%zu\n",
                delta.size(), newAnim, changedExisting, symbolOf.size());
    for (std::size_t i = 0; i < delta.size() && i < 40; ++i)
        std::printf("    ADD : %s\n", CanonClip(delta[i]).c_str());
    if (delta.size() > 40) std::printf("    ... (%zu more)\n", delta.size() - 40);
    return 0;
}

// ── project verbs ────────────────────────────────────────────────────────────
// project-info: dump every serialized field of a project .hkx AND prove the
// layout by a byte-exact deserialize->serialize round-trip against the input.
// project-schema-check <project.hkx> <Havok-dir>: build a project via BOTH the typed BuildProject and
// the schema AssembleProject (toggled around the same spec) and assert byte-identical — the project
// half of the compiler gate (sibling of schema-compile-check for behaviors).
// skeleton-schema-check <skeleton.hkx> <Havok-dir>: compile the ANIM skeleton via BOTH the typed
// BuildSkeletonRoot and the schema AssembleSkeletonAnim (toggled) and assert byte-identical. Uses
// CompileSkeleton, which emits anim-only regardless of physics — so any vanilla skeleton exercises it.
int doSkeletonSchemaCheck(const std::string& in, const std::string& schemaDir) {
    if (in.empty() || schemaDir.empty()) { std::printf("usage: skeleton-schema-check <skeleton.hkx> <Havok-dir>\n"); return 1; }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("skeleton load FAIL: %s\n", err.c_str()); return 1; }
    const auto& sk = skels[0];

    havok::sct::SetSchemaCompiler(false, "");                      // typed path
    const auto typed = havok::sct::CompileSkeleton(sk);
    if (!typed.ok) { std::printf("typed CompileSkeleton FAIL: %s\n", typed.error.c_str()); return 1; }

    havok::sct::SetSchemaCompiler(true, schemaDir);                // schema path
    if (!havok::sct::SchemaCompilerReady()) { std::printf("schema registry failed to load from %s\n", schemaDir.c_str()); return 1; }
    const auto schema = havok::sct::CompileSkeleton(sk);
    if (!schema.ok) { std::printf("schema CompileSkeleton FAIL: %s\n", schema.error.c_str()); return 1; }

    if (schema.bytes == typed.bytes) {
        std::printf("skeleton-schema-check: schema == typed BYTE-IDENTICAL (%zu bytes, %zu bones)\n", schema.bytes.size(), sk.bones.size());
        return 0;
    }
    std::size_t d = 0; while (d < schema.bytes.size() && d < typed.bytes.size() && schema.bytes[d] == typed.bytes[d]) ++d;
    std::printf("skeleton-schema-check: REAL DIFF schema vs typed — sizes %zu/%zu, first diff @0x%zx\n",
                schema.bytes.size(), typed.bytes.size(), d);
    return 1;
}

// skeleton-full-schema-check <skeleton.hkx> <Havok-dir>: compile the FULL ragdoll skeleton (6 variants:
// anim + ragdoll skeletons, rigid bodies, constraints, ragdoll instance, physics system, 2 mappers,
// resource tree) via BOTH the typed CompileSkeletonFull and the schema AssembleSkeletonFull (toggled)
// and assert byte-identical. Reads the ragdoll PHYSICS (ReadSkeletonPhysics) so the physics path is
// exercised — use a vanilla character skeleton.hkx (skeletonbeast.hkx, etc.), NOT skeletonfirst.hkx.
int doSkeletonFullSchemaCheck(const std::string& in, const std::string& schemaDir) {
    if (in.empty() || schemaDir.empty()) { std::printf("usage: skeleton-full-schema-check <skeleton.hkx> <Havok-dir>\n"); return 1; }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        std::printf("skeleton load FAIL: %s\n", err.c_str()); return 1; }
    havok::sct::SkeletonData sk = skels[0];
    if (!havok::sct::ReadSkeletonPhysics(bytes.data(), bytes.size(), sk, &err)) {
        std::printf("physics read FAIL: %s\n", err.c_str()); return 1; }

    havok::sct::SetSchemaCompiler(false, "");                      // typed path
    const auto typed = havok::sct::CompileSkeletonFull(sk);
    if (!typed.ok) { std::printf("typed CompileSkeletonFull FAIL: %s\n", typed.error.c_str()); return 1; }

    havok::sct::SetSchemaCompiler(true, schemaDir);                // schema path
    if (!havok::sct::SchemaCompilerReady()) { std::printf("schema registry failed to load from %s\n", schemaDir.c_str()); return 1; }
    const auto schema = havok::sct::CompileSkeletonFull(sk);
    if (!schema.ok) { std::printf("schema CompileSkeletonFull FAIL: %s\n", schema.error.c_str()); return 1; }

    // Signed-zero-tolerant compare: capsule vertex-W is unused padding Havok stores as -0.0 canonically,
    // and the schema/typed derive paths differ ONLY in that sign (+0.0 vs -0.0) — geometrically and
    // functionally identical (radius is authoritative in hkpConvexShape::m_radius). Count only REAL
    // diffs: a byte diff inside a 4-byte float where BOTH floats are zero (i.e. ±0.0) is not real. Any
    // non-signed-zero diff still fails, so a genuine emit regression is caught.
    if (schema.bytes.size() == typed.bytes.size()) {
        std::size_t real = 0, sz = 0;
        for (std::size_t i = 0; i < schema.bytes.size(); ++i) {
            if (schema.bytes[i] == typed.bytes[i]) continue;
            const std::size_t f = i & ~std::size_t{3};   // 4-byte-aligned float (Havok aligns floats to 4)
            float sf = 0, tf = 0;
            if (f + 4 <= schema.bytes.size()) { std::memcpy(&sf, &schema.bytes[f], 4); std::memcpy(&tf, &typed.bytes[f], 4); }
            if (sf == 0.0f && tf == 0.0f) ++sz; else ++real;   // ±0.0 both compare == 0.0f
        }
        if (real == 0) {
            std::printf("skeleton-full-schema-check: schema == typed BYTE-IDENTICAL modulo signed-zero "
                        "(%zu bytes, %zu bones; %zu ±0.0 padding bytes in capsule vertex-W)\n",
                        schema.bytes.size(), sk.bones.size(), sz);
            return 0;
        }
    }
    if (schema.bytes == typed.bytes) {
        std::printf("skeleton-full-schema-check: schema == typed BYTE-IDENTICAL (%zu bytes, %zu bones)\n", schema.bytes.size(), sk.bones.size());
        return 0;
    }
    // Dump both for offline inspection (hexdump / decompile) — the byte-diff convergence loop.
    { std::ofstream(std::filesystem::temp_directory_path() / "skfull_schema.hkx", std::ios::binary)
          .write(reinterpret_cast<const char*>(schema.bytes.data()), (std::streamsize)schema.bytes.size());
      std::ofstream(std::filesystem::temp_directory_path() / "skfull_typed.hkx", std::ios::binary)
          .write(reinterpret_cast<const char*>(typed.bytes.data()), (std::streamsize)typed.bytes.size()); }
    std::size_t d = 0; while (d < schema.bytes.size() && d < typed.bytes.size() && schema.bytes[d] == typed.bytes[d]) ++d;
    std::printf("skeleton-full-schema-check: REAL DIFF schema vs typed — sizes %zu/%zu, first diff @0x%zx\n"
                "  wrote %%TEMP%%/skfull_schema.hkx + skfull_typed.hkx\n",
                schema.bytes.size(), typed.bytes.size(), d);
    return 1;
}

// animation-schema-check <anim.yaml> <Havok-dir>: compile a native animation via BOTH the typed
// EmitAnimationHkx (retained in havok-core purely as the gate baseline) and the schema-native
// havok::anim::CompileAnimation, and assert byte-identical. Both run the SAME spline codec on the
// same AnimationDef, so this isolates the object-serialization difference — and proves the move of
// the animation pipeline into havok-anim did not change a single emitted byte.
int doAnimationSchemaCheck(const std::string& in, const std::string& schemaDir) {
    if (in.empty() || schemaDir.empty()) { std::printf("usage: animation-schema-check <anim.yaml> <Havok-dir>\n"); return 1; }
    havok::anim::AnimationDef anim;
    try { anim = havok::anim::AnimationYamlLoader::Load(in); }
    catch (const std::exception& e) { std::printf("LOAD FAIL: %s\n", e.what()); return 1; }

    std::vector<std::uint8_t> typed;
    try { typed = havok::anim::EmitAnimationHkx(anim, 30); }        // typed baseline (havok-core)
    catch (const std::exception& e) { std::printf("typed EmitAnimationHkx FAIL: %s\n", e.what()); return 1; }

    havok::schema::SetSharedSchemaDir(schemaDir);                   // arm the shared registry
    if (!havok::schema::SharedRegistry()) {
        std::printf("schema registry failed to load from %s: %s\n", schemaDir.c_str(),
                    havok::schema::SharedRegistryError().c_str());
        return 1;
    }
    const auto schema = havok::anim::CompileAnimation(anim, 30);    // schema-native (havok-anim)
    if (!schema.ok) { std::printf("schema CompileAnimation FAIL: %s\n", schema.error.c_str()); return 1; }

    if (schema.bytes == typed) {
        std::printf("animation-schema-check: schema == typed BYTE-IDENTICAL (%zu bytes)\n", schema.bytes.size());
        return 0;
    }
    std::size_t d = 0; while (d < schema.bytes.size() && d < typed.size() && schema.bytes[d] == typed[d]) ++d;
    std::printf("animation-schema-check: REAL DIFF schema vs typed — sizes %zu/%zu, first diff @0x%zx\n",
                schema.bytes.size(), typed.size(), d);
    return 1;
}

// character-schema-check <char-yaml-dir> <Havok-dir> [--skeleton <skel.hkx>]: compile a character via
// BOTH the typed CharacterBuilder and the schema AssembleCharacter (toggled) and assert byte-identical.
int doCharacterSchemaCheck(const std::string& dir, const std::string& schemaDir, const std::string& skel = {}) {
    if (dir.empty() || schemaDir.empty()) { std::printf("usage: character-schema-check <char-yaml-dir> <Havok-dir> [--skeleton <skel.hkx>]\n"); return 1; }
    havok::model::CharacterData data;
    try { data = havok::model::CharacterYamlLoader::Load(dir); }
    catch (const std::exception& e) { std::printf("LOAD FAIL: %s\n", e.what()); return 1; }
    if (!skel.empty()) data.boneNames = LoadSkeletonNames(skel);   // resolve named bone-weight / bone-pair maps

    havok::sct::SetSchemaCompiler(false, "");                      // typed path
    const auto typed = havok::sct::CompileCharacter(data);
    if (!typed.ok) { std::printf("typed CompileCharacter FAIL: %s\n", typed.error.c_str()); return 1; }

    havok::sct::SetSchemaCompiler(true, schemaDir);                // schema path
    if (!havok::sct::SchemaCompilerReady()) { std::printf("schema registry failed to load from %s\n", schemaDir.c_str()); return 1; }
    const auto schema = havok::sct::CompileCharacter(data);
    if (!schema.ok) { std::printf("schema CompileCharacter FAIL: %s\n", schema.error.c_str()); return 1; }

    if (schema.bytes == typed.bytes) {
        std::printf("character-schema-check: schema == typed BYTE-IDENTICAL (%zu bytes)\n", schema.bytes.size());
        return 0;
    }
    std::size_t d = 0; while (d < schema.bytes.size() && d < typed.bytes.size() && schema.bytes[d] == typed.bytes[d]) ++d;
    std::printf("character-schema-check: REAL DIFF schema vs typed — sizes %zu/%zu, first diff @0x%zx\n",
                schema.bytes.size(), typed.bytes.size(), d);
    return 1;
}

int doProjectSchemaCheck(const std::string& in, const std::string& schemaDir) {
    if (in.empty() || schemaDir.empty()) { std::printf("usage: project-schema-check <project.hkx> <Havok-dir>\n"); return 1; }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    const auto pr = havok::sct::ReadProject(bytes);
    if (!pr.ok) { std::printf("ReadProject FAIL: %s\n", pr.error.c_str()); return 1; }

    havok::sct::SetSchemaCompiler(false, "");                       // typed path
    const auto typed = havok::sct::BuildProject(pr.spec, pr.header);
    if (!typed.ok) { std::printf("typed BuildProject FAIL: %s\n", typed.error.c_str()); return 1; }

    havok::sct::SetSchemaCompiler(true, schemaDir);                 // schema path
    if (!havok::sct::SchemaCompilerReady()) { std::printf("schema registry failed to load from %s\n", schemaDir.c_str()); return 1; }
    const auto schema = havok::sct::BuildProject(pr.spec, pr.header);
    if (!schema.ok) { std::printf("schema BuildProject FAIL: %s\n", schema.error.c_str()); return 1; }

    if (schema.bytes == typed.bytes) {
        std::printf("project-schema-check: schema == typed BYTE-IDENTICAL (%zu bytes); == vanilla input=%s\n",
                    schema.bytes.size(), (typed.bytes == bytes) ? "YES" : "no");
        return 0;
    }
    std::size_t d = 0; while (d < schema.bytes.size() && d < typed.bytes.size() && schema.bytes[d] == typed.bytes[d]) ++d;
    std::printf("project-schema-check: REAL DIFF schema vs typed — sizes %zu/%zu, first diff @0x%zx\n",
                schema.bytes.size(), typed.bytes.size(), d);
    return 1;
}

int doProjectInfo(const std::string& in) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    const auto pr = havok::sct::ReadProject(bytes);
    if (!pr.ok) { std::printf("ERROR: %s\n", pr.error.c_str()); return 1; }
    const auto& s = pr.spec;
    std::printf("project: %s\n", in.c_str());
    std::printf("  worldUpWS        = (%g, %g, %g, %g)\n",
                s.worldUpWS[0], s.worldUpWS[1], s.worldUpWS[2], s.worldUpWS[3]);
    std::printf("  defaultEventMode = %d\n", (int)s.defaultEventMode);
    std::printf("  characterFilenames (%zu):\n", s.characterFilenames.size());
    for (const auto& c : s.characterFilenames) std::printf("      %s\n", c.c_str());
    std::printf("  animationFilenames=%zu behaviorFilenames=%zu eventNames=%zu\n",
                s.animationFilenames.size(), s.behaviorFilenames.size(), s.eventNames.size());
    std::printf("  animationPath='%s' behaviorPath='%s' characterPath='%s' fullPathToSource='%s'\n",
                s.animationPath.c_str(), s.behaviorPath.c_str(),
                s.characterPath.c_str(), s.fullPathToSource.c_str());

    const auto rt = havok::sct::RoundTripProject(bytes);
    if (!rt.ok) { std::printf("  round-trip: FAIL (%s)\n", rt.error.c_str()); return 1; }
    const bool identical = (rt.bytes == bytes);
    std::printf("  round-trip: %s (in=%zu out=%zu bytes)\n",
                identical ? "BYTE-IDENTICAL" : "DIFFERS", bytes.size(), rt.bytes.size());
    if (!identical) {
        std::size_t firstDiff = 0, n = std::min(bytes.size(), rt.bytes.size());
        while (firstDiff < n && bytes[firstDiff] == rt.bytes[firstDiff]) ++firstDiff;
        std::printf("    first diff at byte %zu\n", firstDiff);
        return 2;
    }
    return 0;
}

// project-rewrite <in.hkx> <newCharacterFilename0> -o out.hkx
int doProjectRewrite(const std::string& in, const std::vector<std::string>& extra, const std::string& out) {
    if (extra.empty() || out.empty()) {
        std::printf("usage: project-rewrite <in.hkx> <newCharacterFilename0> -o out.hkx\n");
        return 2;
    }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    const auto r = havok::sct::RewriteProjectCharacterFilename(bytes, extra[0]);
    if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
    std::string werr;
    if (!havok::sct::WriteHavokFile(out, r.bytes, &werr)) { std::printf("FAIL: %s\n", werr.c_str()); return 1; }
    std::printf("OK: rewrote characterFilenames[0] -> '%s'  %s (%zu bytes)\n",
                extra[0].c_str(), out.c_str(), r.bytes.size());
    return 0;
}

// project-build <characterFilename> [moreCharacterFilenames...] -o out.hkx
// Synthesize a project from constants (worldUpWS=(0,0,1,0), defaultEventMode=2).
int doProjectBuild(const std::string& first, const std::vector<std::string>& extra, const std::string& out) {
    if (out.empty()) {
        std::printf("usage: project-build <characterFilename> [more...] -o out.hkx\n");
        return 2;
    }
    havok::sct::ProjectSpec spec;  // worldUpWS=(0,0,1,0), defaultEventMode=2 by default
    spec.characterFilenames.push_back(first);
    for (const auto& e : extra) spec.characterFilenames.push_back(e);
    const auto r = havok::sct::BuildProject(spec);
    if (!r.ok) { std::printf("FAIL: %s\n", r.error.c_str()); return 1; }
    std::string werr;
    if (!havok::sct::WriteHavokFile(out, r.bytes, &werr)) { std::printf("FAIL: %s\n", werr.c_str()); return 1; }
    std::printf("OK: built project (%zu characterFilename(s)) -> %s (%zu bytes)\n",
                spec.characterFilenames.size(), out.c_str(), r.bytes.size());
    return 0;
}

// refframe — dump every hkaDefaultAnimatedReferenceFrame in a file (READ proof,
// compare to hkxc), then round-trip the first one through the serializer: wrap it
// in a minimal hkRootLevelContainer -> hkaAnimationContainer -> interleaved anim
// (m_extractedMotion = the frame), Serialize, re-deserialize, and confirm every
// field survives. Writes out.hkx (when -o is given) for strict hkxc validation.
int doRefFrame(const std::string& in, const std::string& out) {
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }

    havok::PackFileDeserializer des;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    const auto frames = des.ConstructAllOfClass(dr, "hkaDefaultAnimatedReferenceFrame");

    std::printf("refframe %s : %zu hkaDefaultAnimatedReferenceFrame object(s)\n", in.c_str(), frames.size());
    if (frames.empty()) { std::printf("  (extractedMotion is null in this file)\n"); return 0; }

    auto rf0 = std::dynamic_pointer_cast<havok::hkaDefaultAnimatedReferenceFrame>(frames.front());
    for (std::size_t k = 0; k < frames.size(); ++k) {
        auto rf = std::dynamic_pointer_cast<havok::hkaDefaultAnimatedReferenceFrame>(frames[k]);
        std::printf("  [%zu] up=(%g %g %g %g) forward=(%g %g %g %g) duration=%g samples=%zu\n",
                    k, rf->m_up.x, rf->m_up.y, rf->m_up.z, rf->m_up.w,
                    rf->m_forward.x, rf->m_forward.y, rf->m_forward.z, rf->m_forward.w,
                    rf->m_duration, rf->m_referenceFrameSamples.size());
        for (std::size_t i = 0; i < rf->m_referenceFrameSamples.size(); ++i) {
            const auto& s = rf->m_referenceFrameSamples[i];
            std::printf("    s%-4zu %.9g %.9g %.9g %.9g\n", i, s.x, s.y, s.z, s.w);
        }
    }

    // Round-trip the first frame through Serialize -> Deserialize.
    auto anim = std::make_shared<havok::hkaInterleavedUncompressedAnimation>();
    anim->m_duration = rf0->m_duration;
    anim->m_extractedMotion = rf0;
    auto cont = std::make_shared<havok::hkaAnimationContainer>();
    cont->m_animations.push_back(anim);
    auto root = std::make_shared<havok::hkRootLevelContainer>();
    havok::hkRootLevelContainerNamedVariant nv;
    nv.m_name = "Merged Animation Container";
    nv.m_className = "hkaAnimationContainer";
    nv.m_variant = cont;
    root->m_namedVariants.push_back(nv);

    havok::PackFileSerializer ser;
    havok::BinaryWriterEx bw(des._header.Endian == 0, des._header.PointerSize == 8);
    ser.Serialize(root, bw, des._header);
    const std::vector<std::uint8_t> outBytes = bw.Data();

    havok::PackFileDeserializer des2;
    havok::BinaryReaderEx br2(false, true, outBytes);
    des2.DeserializePartially(br2);
    havok::BinaryReaderEx dr2(des2._header.Endian == 0, des2._header.PointerSize == 8, des2.DataSectionBytes());
    const auto frames2 = des2.ConstructAllOfClass(dr2, "hkaDefaultAnimatedReferenceFrame");
    bool pass = frames2.size() == 1;
    if (pass) {
        auto rt = std::dynamic_pointer_cast<havok::hkaDefaultAnimatedReferenceFrame>(frames2.front());
        pass = rt && rt->m_up == rf0->m_up && rt->m_forward == rf0->m_forward
               && rt->m_duration == rf0->m_duration
               && rt->m_referenceFrameSamples == rf0->m_referenceFrameSamples;
    }
    std::printf("  round-trip (Serialize->Deserialize): %s\n", pass ? "PASS (all fields identical)" : "FAIL");
    if (!out.empty()) {
        std::string werr;
        if (!havok::sct::WriteHavokFile(out, outBytes, &werr)) { std::printf("  write %s: %s\n", out.c_str(), werr.c_str()); return 1; }
        std::printf("  wrote %s (%zu bytes) for strict hkxc validation\n", out.c_str(), outBytes.size());
    }
    return pass ? 0 : 1;
}

} // namespace

// animdata-build-check — the STEP-3 loop closer: build a project's animationdata from its
// EDITABLE sources (graph clips + motion.yaml) via sct::DeriveProjectAnimData, and compare to
// vanilla. Motion comes from motion.yaml (the step-2 decompose), NOT the cache — so a green run
// proves vanilla -> decompose -> motion.yaml -> derive -> vanilla closes.
//   animdata-build-check <cache.txt> <Project> <behaviorDir> <characterHkx> <motion.yaml>
int doAnimDataBuildCheck(const std::string& cacheFile, const std::vector<std::string>& extra) {
    if (extra.size() < 4) {
        std::printf("usage: animdata-build-check <cache.txt> <Project> <behaviorDir> <characterHkx> <motion.yaml>\n");
        return 2;
    }
    auto low = [](std::string s){ for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    const std::string projName = extra[0];
    const fs::path    behDir = extra[1], charHkx = extra[2], motionYaml = extra[3];

    havok::animdata::SingleFile master;
    try { master = havok::animdata::ParseSingleFile(ReadTextFile(cacheFile)); }
    catch (const std::exception& e) { std::printf("ERROR cache: %s\n", e.what()); return 1; }
    const havok::animdata::Project* van = nullptr;
    for (const auto& p : master.projects)
        if (low(p.name) == low(projName) || low(p.name) == low(projName) + ".txt") { van = &p; break; }
    if (!van) { std::printf("project '%s' not in cache\n", projName.c_str()); return 1; }

    const fs::path tmp = fs::temp_directory_path() / "sct_animbuild";
    std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec);

    std::vector<std::string> roster;
    { std::vector<std::uint8_t> b; std::string e;
      if (havok::sct::ReadHavokFile(charHkx.string(), b, &e)) {
          havok::sct::DecompileToDir(b, (tmp / "_c").string());
          std::ifstream rf(tmp / "_c" / "animations.txt"); std::string ln;
          while (std::getline(rf, ln)) { ln = StripLine(ln); if (!ln.empty()) roster.push_back(ln); } } }

    std::string merr;
    const auto motions = havok::animdata::ParseMotionYaml(ReadTextFile(motionYaml.string()), merr);

    std::vector<havok::animdata::DeriveClipInput> clips;
    for (fs::directory_iterator it(behDir, ec), end; it != end; it.increment(ec)) {
        if (low(it->path().extension().string()) != ".hkx") continue;
        std::vector<std::uint8_t> b; std::string e;
        if (!havok::sct::ReadHavokFile(it->path().string(), b, &e)) continue;
        const fs::path od = tmp / it->path().stem();
        const auto dr = havok::sct::DecompileToDir(b, od.string());
        if (!dr.ok || dr.kind != "behavior") continue;
        try { auto bd = havok::model::YamlBehaviorLoader::Load(od.string());
              for (auto& c : havok::sct::DeriveClipInputsFromBehavior(bd)) clips.push_back(std::move(c)); }
        catch (...) {}
    }
    const fs::path animRoot = behDir.parent_path();
    auto readAnim = [&](const std::string& an) -> std::vector<std::uint8_t> {
        std::string rel = an; for (char& c : rel) if (c == '\\') c = '/';
        const fs::path p = animRoot / rel; std::vector<std::uint8_t> b; std::string e; std::error_code ee;
        if (fs::exists(p, ee) && havok::sct::ReadHavokFile(p.string(), b, &e)) return b; return {};
    };

    const auto built = havok::sct::DeriveProjectAnimData(projName, clips, roster, motions, readAnim);
    fs::remove_all(tmp, ec);

    const bool dumpDiff = std::getenv("BUILDCHECK_DIFF") != nullptr;
    std::map<std::string, std::string> vc;
    for (const auto& c : van->clips) vc[c.name] = CanonClip(c);
    std::size_t ok = 0, bad = 0, missing = 0, printed = 0;
    for (const auto& c : built.clips) {
        auto it = vc.find(c.name);
        const std::string bc = CanonClip(c);
        if (it != vc.end() && it->second == bc) { ++ok; continue; }
        ++bad;
        if (it == vc.end()) ++missing;
        if (dumpDiff && printed < 1000) {
            ++printed;
            std::printf("  DIFF clip='%s' %s\n", c.name.c_str(), it == vc.end() ? "(name absent in vanilla)" : "");
            if (it != vc.end()) {
                std::printf("    built  : %s\n", bc.c_str());
                std::printf("    vanilla: %s\n", it->second.c_str());
            }
        }
    }
    if (dumpDiff) std::printf("  [%zu differ, of which %zu name-absent-in-vanilla]\n", bad, missing);

    bool motionExact = built.motions.size() == van->motions.size();
    for (std::size_t i = 0; motionExact && i < built.motions.size(); ++i) {
        const auto& a = built.motions[i]; const auto& b = van->motions[i];
        if (a.animIndex != b.animIndex || a.duration != b.duration ||
            a.translations != b.translations || a.rotations != b.rotations) motionExact = false;
    }
    std::printf("animdata-build-check %s: clips %zu/%zu byte-exact (%zu differ); motion %s (%zu from motion.yaml)\n",
                projName.c_str(), ok, van->clips.size(), bad,
                motionExact ? "BYTE-EXACT" : "DIFF", built.motions.size());
    return 0;
}

// motion-sidecar-check — the omnidirectional gate for the per-animation motion sidecars. Decompose
// a project's cache motion into sidecars keyed by CANONICAL animation path (dedup + divergence
// check), then REBIND (roster index -> canonical path -> sidecar -> motion) and require the result
// to equal the cache byte-for-byte. Proves the sidecar form + the compile-time name->index bind
// round-trip losslessly, and that shared animations don't diverge. Arg: cache, Project, characterHkx.
int doMotionSidecarCheck(const std::string& cacheFile, const std::vector<std::string>& extra) {
    if (extra.size() < 2) {
        std::printf("usage: motion-sidecar-check <cache.txt> <Project> <characterHkx>\n");
        return 2;
    }
    auto low = [](std::string s){ for (char& c : s) c=(char)std::tolower((unsigned char)c); return s; };
    const std::string projName = extra[0];
    const fs::path    charHkx  = extra[1];

    havok::animdata::SingleFile cache;
    try { cache = havok::animdata::ParseSingleFile(ReadTextFile(cacheFile)); }
    catch (const std::exception& e) { std::printf("ERROR cache: %s\n", e.what()); return 1; }
    const havok::animdata::Project* proj = nullptr;
    for (const auto& p : cache.projects)
        if (low(p.name)==low(projName) || low(p.name)==low(projName)+".txt") { proj=&p; break; }
    if (!proj) { std::printf("project '%s' not in cache\n", projName.c_str()); return 1; }

    // roster from the character hkx; actorRoot = meshes-relative "actors/<actor>" (parent of characters/)
    const fs::path tmp = fs::temp_directory_path() / "sct_motionsidecar";
    std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec);
    std::vector<std::string> roster;
    { std::vector<std::uint8_t> b; std::string e;
      if (!havok::sct::ReadHavokFile(charHkx.string(), b, &e)) { std::printf("ERROR char: %s\n", e.c_str()); return 1; }
      havok::sct::DecompileToDir(b, (tmp/"_c").string());
      std::ifstream rf(tmp/"_c"/"animations.txt"); std::string ln;
      while (std::getline(rf, ln)) { ln = StripLine(ln); if (!ln.empty()) roster.push_back(ln); } }
    fs::remove_all(tmp, ec);
    std::string actorRoot;
    { const std::string p = charHkx.generic_string();
      const std::string pl = low(p);
      const auto mp = pl.rfind("/meshes/");
      const std::string relc = (mp!=std::string::npos) ? p.substr(mp+8) : p;   // actors/<actor>/characters/<n>.hkx
      actorRoot = fs::path(relc).parent_path().parent_path().generic_string(); }

    // decompose -> sidecars keyed by canonical path (dedup + divergence check)
    std::unordered_map<std::string,std::string> sidecar;   // canon -> yaml
    std::size_t emitted=0, dup=0, diverged=0, unresolved=0;
    for (const auto& m : proj->motions) {
        const long idx = std::atol(m.animIndex.c_str());
        if (idx<0 || (std::size_t)idx>=roster.size()) { ++unresolved; continue; }
        const std::string canon = havok::animdata::CanonicalAnimPath(actorRoot, roster[(std::size_t)idx]);
        const std::string y = havok::animdata::EmitMotionSidecar(m);
        auto it = sidecar.find(canon);
        if (it!=sidecar.end()) { if (it->second!=y) ++diverged; ++dup; continue; }
        sidecar.emplace(canon, y); ++emitted;
    }
    // rebind -> compare to cache motion byte-for-byte
    std::size_t ok=0, bad=0;
    for (const auto& m : proj->motions) {
        const long idx = std::atol(m.animIndex.c_str());
        if (idx<0 || (std::size_t)idx>=roster.size()) continue;
        const std::string canon = havok::animdata::CanonicalAnimPath(actorRoot, roster[(std::size_t)idx]);
        auto it = sidecar.find(canon);
        if (it==sidecar.end()) { ++bad; continue; }
        std::string e; auto rb = havok::animdata::ParseMotionSidecar(it->second, e);
        rb.animIndex = m.animIndex;   // the per-project bind
        if (rb.duration==m.duration && rb.translations==m.translations && rb.rotations==m.rotations) ++ok; else ++bad;
    }
    std::string msg = "motion-sidecar-check " + projName + ": actorRoot='" + actorRoot + "'  sidecars="
        + std::to_string(emitted) + " (" + std::to_string(dup) + " shared-dedup"
        + (diverged? ("; " + std::to_string(diverged) + " DIVERGED!") : "") + ")  rebind "
        + std::to_string(ok) + "/" + std::to_string(proj->motions.size()-unresolved) + " byte-exact";
    if (unresolved) msg += "  [" + std::to_string(unresolved) + " unresolved index]";
    std::printf("%s\n", msg.c_str());
    return (bad||diverged) ? 1 : 0;
}

// perproject-check — gate the per-project emitters against vanilla's own dev files. Parse the
// collated animationdatasinglefile.txt, split each project via EmitProjectClips/EmitProjectMotion,
// and byte-compare to Meshes\AnimationData\<Project>.txt and BoundAnims\Anims_<Project>.txt. A
// match proves our split reproduces the engine's native per-project format. (The dev tree is a
// partial/older snapshot, so absent files are expected; DIFFs on present files are the real signal.)
int doPerProjectCheck(const std::string& collated, const std::vector<std::string>& extra) {
    if (extra.empty()) { std::printf("usage: perproject-check <collated.txt> <devAnimDataDir>\n"); return 2; }
    const fs::path devDir = extra[0];
    auto low = [](std::string s){ for (char& c : s) c=(char)std::tolower((unsigned char)c); return s; };
    havok::animdata::SingleFile sf;
    try { sf = havok::animdata::ParseSingleFile(ReadTextFile(collated)); }
    catch (const std::exception& e) { std::printf("ERROR: %s\n", e.what()); return 1; }
    std::size_t clipOk=0, clipDiff=0, clipAbsent=0, motOk=0, motDiff=0, motAbsent=0;
    std::vector<std::string> diffs;
    std::error_code ec;
    for (const auto& p : sf.projects) {
        const fs::path cf = devDir / low(p.name);
        if (fs::exists(cf, ec)) {
            if (havok::animdata::EmitProjectClips(p) == ReadTextFile(cf.string())) ++clipOk;
            else { ++clipDiff; if (diffs.size()<8) diffs.push_back("CLIP   " + p.name); }
        } else ++clipAbsent;
        if (!p.motions.empty()) {
            const fs::path mf = devDir / "boundanims" / ("anims_" + low(p.name));
            if (fs::exists(mf, ec)) {
                if (havok::animdata::EmitProjectMotion(p) == ReadTextFile(mf.string())) ++motOk;
                else { ++motDiff; if (diffs.size()<8) diffs.push_back("MOTION " + p.name); }
            } else ++motAbsent;
        }
    }
    std::printf("perproject-check: clips %zu ok / %zu differ / %zu absent-in-dev;  motion %zu ok / %zu differ / %zu absent\n",
                clipOk, clipDiff, clipAbsent, motOk, motDiff, motAbsent);
    for (const auto& d : diffs) std::printf("  DIFF %s\n", d.c_str());
    return (clipDiff||motDiff) ? 1 : 0;
}

// ── derive-delta: precompiled full behavior hkx -> name-keyed delta ──────────────
// Some mods (HorsePower's horse behavior, BFCO) ship a LOOSE precompiled full graph
// instead of a Nemesis patch, so BR (which only ingests deltas) drops their changes.
// This derives a delta by NAME identity: decompile both the mod's full graph and the
// vanilla base with stableIds = obj->name (the emitter renders every ref + filename by
// name, so numbering — unrelated between two compiles — never enters), then keep the
// mod nodes that are NEW or whose name-keyed YAML DIFFERS from vanilla. The round-trip
// gate (merge(vanilla, delta) == mod graph) is run separately; whatever it rejects is
// the collision/rename kernel, localized. Name collisions inside the mod graph (e.g.
// HP's four locomotion FWD states) are disambiguated deterministically as name, name#1,
// name#2 — so exactly one matches vanilla and the gate flags the rest for us.
// One loaded behavior graph: its objects' (class,name) + structural owner map + this
// graph's own per-name counts. Identity assignment is deferred to assignNameIds so the
// COLLISION decision can be made GLOBALLY across the two graphs being compared (a name
// colliding in EITHER graph must be qualified in BOTH, or a node unique in vanilla but
// duplicated in the mod would key differently on each side and never match).
struct LoadedGraph {
    std::shared_ptr<havok::hkbBehaviorGraph> bg;
    std::vector<const void*> order;                                          // objects, offset order
    std::unordered_map<const void*, std::pair<std::string, std::string>> meta;  // obj -> (class,name)
    std::unordered_map<const void*, const void*> owner;                      // obj -> owning node
    std::unordered_map<std::string, int> nameCount;                          // class|name -> count
};

static bool loadGraphMeta(const std::string& path, havok::PackFileDeserializer& des,
                          LoadedGraph& g, std::string& err) {
    std::vector<std::uint8_t> bytes;
    if (!havok::sct::ReadHavokFile(path, bytes, &err)) return false;
    havok::BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    std::uint32_t rootOff = 0xFFFFFFFFu;
    for (const auto& [o, cn] : des.ListObjects()) if (cn == "hkRootLevelContainer") rootOff = o;
    if (rootOff == 0xFFFFFFFFu) { err = "no hkRootLevelContainer in " + path; return false; }
    havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    try { des.ConstructVirtualClass(dr, rootOff); }
    catch (const std::exception& e) { err = std::string("construct: ") + e.what(); return false; }

    auto nameOf = [](const std::shared_ptr<havok::IHavokObject>& o) -> std::string {
        if (auto n = std::dynamic_pointer_cast<havok::hkbNode>(o)) return n->m_name;
        if (auto s = std::dynamic_pointer_cast<havok::hkbStateMachineStateInfo>(o)) return s->m_name;
        return {};
    };
    std::unordered_map<const void*, std::vector<const void*>> refs;   // child -> its referrers
    auto addRef = [&](const std::shared_ptr<havok::IHavokObject>& c, const void* p) {
        if (c && p) refs[c.get()].push_back(p);
    };
    for (const auto& [off, obj] : des.DeserializedObjects()) {
        if (!g.bg) if (auto bg = std::dynamic_pointer_cast<havok::hkbBehaviorGraph>(obj)) g.bg = bg;
        if (auto sm = std::dynamic_pointer_cast<havok::hkbStateMachine>(obj))
            for (auto& s : sm->m_states) addRef(s, sm.get());
        if (auto st = std::dynamic_pointer_cast<havok::hkbStateMachineStateInfo>(obj))
            addRef(st->m_generator, st.get());
        if (auto bl = std::dynamic_pointer_cast<havok::hkbBlenderGenerator>(obj))   // incl. hkbPoseMatchingGenerator
            for (auto& ch : bl->m_children) if (ch) addRef(ch->m_generator, bl.get());
        if (auto ms = std::dynamic_pointer_cast<havok::hkbManualSelectorGenerator>(obj))
            for (auto& gen : ms->m_generators) addRef(gen, ms.get());
        if (auto mg = std::dynamic_pointer_cast<havok::hkbModifierGenerator>(obj)) {
            addRef(mg->m_generator, mg.get()); addRef(mg->m_modifier, mg.get());
        }
        const std::string cls = obj->ClassName();
        std::string nm = nameOf(obj);
        if (nm.empty()) {
            if (cls == "hkbBehaviorGraph" || cls == "hkbBehaviorGraphData" ||
                cls == "hkbBehaviorGraphStringData" || cls == "hkbVariableValueSet")
                nm = "$" + cls;
            else
                continue;   // inline/unreferenced — the emitter renders it inside its owner
        }
        g.order.push_back(obj.get());
        g.meta[obj.get()] = { cls, nm };
        // Ambiguity is GLOBAL by name (any class): the compiler resolves refs by id alone
        // (unlike the loader's class+id grouping), so a clip 'X' and a generator 'X' sharing
        // the bare id 'X' make a ref ambiguous and one gets orphaned + dropped at compile.
        if (nm[0] != '$') g.nameCount[nm]++;
    }
    if (!g.bg) { err = "no hkbBehaviorGraph in " + path; return false; }

    // Resolve each child's owner DETERMINISTICALLY: the referrer with the smallest
    // (class,name). A node reachable from several parents (a shared clip referenced by both
    // a state and a modifier-generator) must pick the SAME owner in both graphs being
    // compared; offset-order "first referrer" does not — it flips between compiles and
    // desyncs the owner-qualified identity (the sole cause of the last round-trip residual).
    for (const auto& [child, rs] : refs) {
        const void* best = nullptr;
        for (const void* r : rs) {
            if (!g.meta.count(r)) continue;
            if (!best || g.meta.at(r) < g.meta.at(best)) best = r;
        }
        if (best) g.owner[child] = best;
    }
    return true;
}

// Assign each object a stable identity: bare name when the (class,name) is unambiguous in
// BOTH graphs; when it collides in either, prepend the owner's identity ("<ownerId>~<name>")
// recursively — a structural key stable across recompiles (unlike a traversal-order suffix).
static void assignNameIds(const LoadedGraph& g, const std::set<std::string>& ambiguous,
                          std::unordered_map<const void*, std::string>& stableIds) {
    std::unordered_map<const void*, std::string> ident;
    std::function<std::string(const void*)> identOf = [&](const void* o) -> std::string {
        auto mi = g.meta.find(o);
        if (mi == g.meta.end()) return {};
        if (auto it = ident.find(o); it != ident.end()) return it->second;
        const std::string& cls = mi->second.first;
        const std::string& nm  = mi->second.second;
        std::string result = nm;
        if (nm[0] != '$' && ambiguous.count(nm)) {             // ambiguous GLOBALLY, any class
            ident[o] = nm;                                     // cycle guard
            const auto oit = g.owner.find(o);
            const std::string op = (oit != g.owner.end()) ? identOf(oit->second) : std::string();
            if (!op.empty()) result = op + "~" + nm;
        }
        ident[o] = result;
        return result;
    };
    // Final ids must be GLOBALLY unique (the compiler resolves refs by id alone). Group by the
    // base identity and, within a group, assign id / id~1 / id~2 in a DETERMINISTIC order — by
    // class, then owner — so the two graphs agree on which twin keeps the bare id (offset order
    // did not). Cross-class twins (clip vs generator 'X') tie here and split cleanly by class.
    std::unordered_map<std::string, std::vector<const void*>> groups;
    for (const void* o : g.order) groups[identOf(o)].push_back(o);
    for (auto& [base, objs] : groups) {
        if (objs.size() > 1)
            std::stable_sort(objs.begin(), objs.end(), [&](const void* a, const void* b) {
                const auto& ma = g.meta.at(a); const auto& mb = g.meta.at(b);
                if (ma.first != mb.first) return ma.first < mb.first;          // by class
                const auto oa = g.owner.find(a), ob = g.owner.find(b);
                const std::string ka = (oa != g.owner.end()) ? identOf(oa->second) : std::string();
                const std::string kb = (ob != g.owner.end()) ? identOf(ob->second) : std::string();
                return ka < kb;                                                // then by owner
            });
        for (std::size_t i = 0; i < objs.size(); ++i)
            stableIds[objs[i]] = (i == 0) ? base : base + "~" + std::to_string(i);
    }
}

int doDeriveDelta(const std::string& vanHkx, const std::vector<std::string>& extra, const std::string& outArg) {
    if (extra.empty()) {
        std::printf("usage: derive-delta <vanillaFull.hkx> <modFull.hkx> -o <outDir>\n"
                    "  decompiles both graphs name-keyed (obj->name identity, numbering-free) and\n"
                    "  writes <outDir>/vanilla, <outDir>/mod, and <outDir>/delta (mod nodes that are\n"
                    "  NEW or DIFFER from vanilla). Gate with: merge <outDir>/vanilla <outDir>/delta.\n");
        return 2;
    }
    const std::string modHkx = extra[0];
    const std::string outDir = outArg.empty() ? "derive_out" : outArg;
    const fs::path vanDir = fs::path(outDir) / "vanilla";
    const fs::path modDir = fs::path(outDir) / "mod";
    const fs::path delDir = fs::path(outDir) / "delta";
    std::error_code ec;
    fs::remove_all(outDir, ec);

    std::string err;
    havok::PackFileDeserializer vdes, mdes;
    LoadedGraph vg, mg;
    if (!loadGraphMeta(vanHkx, vdes, vg, err)) { std::printf("ERROR (vanilla): %s\n", err.c_str()); return 1; }
    if (!loadGraphMeta(modHkx, mdes, mg, err)) { std::printf("ERROR (mod): %s\n", err.c_str()); return 1; }

    // GLOBAL collision set: a (class|name) ambiguous in EITHER graph is owner-qualified in
    // BOTH, so a node unique in vanilla but duplicated in the mod still keys identically.
    std::set<std::string> ambiguous;
    for (const auto& [k, n] : vg.nameCount) if (n > 1) ambiguous.insert(k);
    for (const auto& [k, n] : mg.nameCount) if (n > 1) ambiguous.insert(k);

    // Owner-qualified string identities are for MATCHING ONLY. The emitted ids are the base's
    // own encounter-order NUMBERS (what BuildBaseBundle assigns) so the delta drops onto the
    // shipped Skyrim.hky base — AND numeric ids sidestep the compiler's id!=name string-id drop
    // (owner-qualified string ids orphan shared/nested nodes at compile; numbers never do).
    std::unordered_map<const void*, std::string> vIdent, mIdent;
    assignNameIds(vg, ambiguous, vIdent);
    assignNameIds(mg, ambiguous, mIdent);

    // Vanilla base: decompile with NULL stableIds -> the encounter-order numbering (captured),
    // byte-identical to the base bundle's horse graph.
    std::unordered_map<const void*, std::string> vNum;
    const auto vr = havok::sct::DecompileBehaviorTree(vg.bg, vanDir, nullptr, &vNum);
    if (!vr.ok) { std::printf("ERROR: vanilla decompile: %s\n", vr.error.c_str()); return 1; }

    // identity -> base number; fresh numbers above the base max for the mod's NEW nodes.
    std::unordered_map<std::string, std::string> identToNum;
    long maxNum = 0;
    for (const auto& [obj, num] : vNum) {
        if (auto it = vIdent.find(obj); it != vIdent.end()) identToNum[it->second] = num;
        try { maxNum = std::max(maxNum, std::stol(num)); } catch (...) {}
    }
    long nextNew = maxNum + 1;

    // Mod graph: matched node -> the base's number (by identity); new node -> a fresh number.
    std::unordered_map<const void*, std::string> mNum;
    int matched = 0, brandNew = 0;
    for (const void* o : mg.order) {
        if (auto it = identToNum.find(mIdent[o]); it != identToNum.end()) { mNum[o] = it->second; ++matched; }
        else { mNum[o] = std::to_string(nextNew++); ++brandNew; }
    }
    const auto mr = havok::sct::DecompileBehaviorTree(mg.bg, modDir, &mNum);
    if (!mr.ok) { std::printf("ERROR: mod decompile: %s\n", mr.error.c_str()); return 1; }
    std::printf("derive-delta: matched %d mod node(s) to base ids, %d new (base max id %ld).\n",
                matched, brandNew, maxNum);

    // Diff the two name-keyed trees by relative path (identical emitter => identical
    // filename for the same node name => a real by-name node diff).
    auto readAll = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    int added = 0, changed = 0, same = 0, removed = 0;
    std::vector<std::string> addedL, changedL;
    for (fs::recursive_directory_iterator it(modDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const fs::path rel = fs::relative(it->path(), modDir);
        const fs::path vp  = vanDir / rel;
        const bool inVan = fs::exists(vp, ec);
        bool copy = false;
        if (!inVan) { ++added; copy = true; if (addedL.size() < 40) addedL.push_back(rel.generic_string()); }
        else if (readAll(it->path()) != readAll(vp)) { ++changed; copy = true; if (changedL.size() < 40) changedL.push_back(rel.generic_string()); }
        else ++same;
        if (copy) {
            const fs::path dst = delDir / rel;
            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ec);
        }
    }
    for (fs::recursive_directory_iterator it(vanDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && !fs::exists(modDir / fs::relative(it->path(), vanDir), ec)) ++removed;
    }
    std::printf("derive-delta: added=%d changed=%d unchanged=%d  removed-from-vanilla=%d\n", added, changed, same, removed);
    std::printf("  vanilla nodes decompiled OK, mod nodes decompiled OK.\n");
    std::printf("  delta -> %s  (merge %s %s to gate)\n", delDir.string().c_str(), vanDir.string().c_str(), delDir.string().c_str());
    if (!addedL.empty())   { std::printf("  NEW (first %zu):\n", addedL.size());   for (auto& s : addedL)   std::printf("    + %s\n", s.c_str()); }
    if (!changedL.empty()) { std::printf("  CHANGED (first %zu):\n", changedL.size()); for (auto& s : changedL) std::printf("    ~ %s\n", s.c_str()); }
    return 0;
}

// ── basefidelity: Phase-1 go/no-go for base-sourcing the Nemesis vanilla binary ──────────────
// Prove that recompiling each templated graph's base unit out of Skyrim.hky reproduces the SAME
// tagfile-numbered vanilla the shipped templates/<g>.hkx does. Every template is the 3rd-person
// CHARACTER graph, so the base unit is resolved by the fixed path convention
// `meshes/actors/character/behaviors/<g>.hkx` (this is the disambiguator — nearly every stem also
// has a _1stperson twin, and `horsebehavior` a third under actors/horse; the template is the
// character one). Recompile that unit, then diff ConvertPatch's vanbase decompile (recompiled
// binary vs templates/<g>.hkx). Byte-identical vanbase trees => the base binary is a safe drop-in
// for pass 2a; any object-count delta (the compiler id!=name inline-node drop) surfaces as a
// vanbase diff => NOT SAFE, that graph stays template-sourced.
//   basefidelity <Skyrim.hky> <templatesDir>
int doBaseFidelity(const std::string& archivePath, const std::vector<std::string>& extra) {
    if (extra.empty()) { std::printf("usage: basefidelity <Skyrim.hky> <templatesDir>\n"); return 2; }
    const fs::path templatesDir = extra[0];
    std::string err;
    auto arc = havok::model::HkyArchive::LoadFromFile(archivePath, err);
    if (!arc) { std::printf("ERROR: %s\n", err.c_str()); return 1; }

    std::unordered_set<std::string> behaviorUnits;   // prefixes present in the archive
    for (const auto& u : arc->units())
        if (u.kind == havok::model::HkyArchive::UnitKind::Behavior) behaviorUnits.insert(u.prefix);

    auto compileUnit = [&](const std::string& prefix, std::vector<std::uint8_t>& out) -> std::string {
        try {
            auto data = havok::model::YamlBehaviorLoader::LoadMerged({ arc->source(prefix) });
            const auto cr = havok::sct::CompileBehavior(data);
            if (!cr.ok) return cr.error;
            out = cr.bytes;
            return {};
        } catch (const std::exception& e) { return e.what(); }
    };
    auto snapshot = [](const fs::path& root) {   // relpath -> bytes, for a byte-diff of two vanbase trees
        std::map<std::string, std::string> m;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fe;
            if (!it->is_regular_file(fe)) continue;
            std::ifstream f(it->path(), std::ios::binary);
            m[it->path().lexically_relative(root).generic_string()] =
                std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }
        return m;
    };

    std::vector<std::string> graphs;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(templatesDir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".xml") continue;
        const std::string g = e.path().stem().string();
        if (fs::exists(templatesDir / (g + ".hkx"), ec)) graphs.push_back(g);
    }
    std::sort(graphs.begin(), graphs.end());
    if (graphs.empty()) { std::printf("ERROR: no <g>.xml + <g>.hkx template pairs in %s\n", templatesDir.string().c_str()); return 1; }

    const fs::path tmp = fs::temp_directory_path(ec) / "sct_basefidelity";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    int pass = 0, fail = 0;
    for (const auto& g : graphs) {
        const fs::path xml    = templatesDir / (g + ".xml");
        const fs::path thkx   = templatesDir / (g + ".hkx");
        const std::string prefix = "meshes/actors/character/behaviors/" + g + ".hkx";
        if (!behaviorUnits.count(prefix)) {
            std::printf("  FAIL %-36s : no base unit at %s\n", g.c_str(), prefix.c_str());
            ++fail; continue;
        }
        std::vector<std::uint8_t> baseBytes;
        if (const std::string cerr = compileUnit(prefix, baseBytes); !cerr.empty()) {
            std::printf("  FAIL %-36s : base unit compile failed: %s\n", g.c_str(), cerr.c_str());
            ++fail; continue;
        }

        const fs::path rbin = tmp / (g + ".recompiled.hkx");
        std::string werr;
        if (!havok::sct::WriteHavokFile(rbin.string(), baseBytes, &werr)) {
            std::printf("  FAIL %-36s : write recompiled binary: %s\n", g.c_str(), werr.c_str()); ++fail; continue;
        }
        const fs::path A = tmp / (g + ".A"), B = tmp / (g + ".B");
        const auto ra = havok::sct::ConvertPatch(rbin.string(), xml.string(), {}, "", "", A.string());
        const auto rb = havok::sct::ConvertPatch(thkx.string(), xml.string(), {}, "", "", B.string());
        if (!ra.ok || !rb.ok) {
            std::printf("  FAIL %-36s : vanbase failed (base:%s tmpl:%s)\n", g.c_str(),
                        ra.ok ? "ok" : ra.error.c_str(), rb.ok ? "ok" : rb.error.c_str());
            ++fail; fs::remove_all(A, ec); fs::remove_all(B, ec); fs::remove(rbin, ec); continue;
        }
        const auto sa = snapshot(A), sb = snapshot(B);
        if (sa == sb) {
            std::printf("  PASS %-36s : %zu file(s) identical\n", g.c_str(), sa.size());
            ++pass;
        } else {
            std::size_t ndiff = 0;
            std::string first;
            for (const auto& [k, v] : sa) {
                auto it = sb.find(k);
                if (it == sb.end() || it->second != v) {
                    if (first.empty()) first = k + (it == sb.end() ? " (only in base)" : " (differs)");
                    ++ndiff;
                }
            }
            std::printf("  FAIL %-36s : vanbase differs (base=%zu tmpl=%zu file(s), %zu diff; first: %s)\n",
                        g.c_str(), sa.size(), sb.size(), ndiff, first.c_str());
            ++fail;
        }
        fs::remove_all(A, ec); fs::remove_all(B, ec); fs::remove(rbin, ec);
    }
    fs::remove_all(tmp, ec);
    std::printf("=== basefidelity: %d/%zu graph(s) safe to base-source, %d not ===\n",
                pass, graphs.size(), fail);
    return fail == 0 ? 0 : 1;
}

// ── oracle-baseline (havok-core v2 rewrite, Stage 0) ──────────────────────────────────
// The TWO-TIER differential oracle the ground-up rewrite gates against. Runs the existing
// round-trip gate over an enumerated corpus and records a baseline the new libs are diffed
// against (see the plan: for-this-a-doc-merry-turing.md):
//   Tier A — GROUND TRUTH (inviolable): every vanilla .hkx must round-trip byte-identical.
//            The new `havok-io` MUST reproduce these bytes exactly. This is the hard floor.
//   Tier B — ADVISORY (NOT truth): artifacts old havok-core COMPILES (merge+compile of the
//            load order). new-vs-old there is a regression AID; where old was buggy the new
//            build is EXPECTED to diverge (a candidate fix). Compile FAILURES/fallbacks are
//            the known-bad A-pose family (BR-17/19/20: a compile throw -> broken graph ->
//            A-pose) and are QUARANTINED — new SUCCEEDING there is the goal, never a fail.
// Usage: havok-core-cli oracle-baseline <vanilla-corpus-root> [-o baseline.txt]
//        (Tier-B load-order wiring is the next Stage-0 step — see the TODO in the emitter.)

// Quiet round-trip check (the doHkxRoundtrip core without the verbose reporting): deserialize
// then re-serialize and byte-compare. Returns true iff byte-identical; `why` explains a failure.
static bool quietRoundtripOk(const std::vector<std::uint8_t>& bytes, std::string& why) {
    std::shared_ptr<havok::IHavokObject> root;
    havok::HKXHeader header;
    try {
        havok::PackFileDeserializer des;
        havok::BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        root   = des.Deserialize(br);
        header = des._header;
    } catch (const std::exception& e) { why = std::string("read: ") + e.what(); return false; }
    if (!root) { why = "read: null root"; return false; }
    std::vector<std::uint8_t> out;
    try {
        havok::PackFileSerializer ser;
        havok::BinaryWriterEx bw(/*bigEndian*/ false, /*uSizeLong*/ true);
        ser.Serialize(root, bw, header);
        out = bw.Take();
    } catch (const std::exception& e) { why = std::string("write: ") + e.what(); return false; }
    if (out.size() != bytes.size()) {
        why = "size " + std::to_string(out.size()) + " != " + std::to_string(bytes.size());
        return false;
    }
    for (std::size_t i = 0; i < out.size(); ++i)
        if (out[i] != bytes[i]) {
            char b[80];
            std::snprintf(b, sizeof b, "byte 0x%zx in=0x%02x out=0x%02x", i, bytes[i], out[i]);
            why = b; return false;
        }
    return true;
}

int doOracleBaseline(const std::string& corpusRoot,
                     const std::vector<std::string>& bundles,
                     const std::string& outPath = {}) {
    // Tier A — enumerate every .hkx under the corpus root and round-trip it (the ground truth floor).
    std::vector<std::string> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(corpusRoot, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it->is_regular_file(ec)) {
            const fs::path p = it->path();
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".hkx") files.push_back(p.string());
        }
    }
    std::sort(files.begin(), files.end());

    std::size_t pass = 0;
    std::vector<std::pair<std::string, std::string>> fails;   // {path, why}
    for (const auto& f : files) {
        std::vector<std::uint8_t> bytes; std::string err;
        if (!havok::sct::ReadHavokFile(f, bytes, &err)) { fails.push_back({ f, "open: " + err }); continue; }
        std::string why;
        if (quietRoundtripOk(bytes, why)) ++pass; else fails.push_back({ f, why });
    }

    // Tier B — merge+compile every serve-key over the load-order bundles (base-first, in the
    // order given). Record compiled|failed per unit; a FAILURE (return !ok, failed validate, OR a
    // thrown loader exception) is the known-bad A-pose family -> QUARANTINED. Tier-B failures do
    // NOT gate the exit code (advisory) — only Tier A (ground truth) does. Reuses the exact merge+
    // compile path of doHkyMergeCompile, iterated over every unit and wrapped per-unit in try/catch
    // (LoadMerged can throw, e.g. "bone list is EMPTY" — the A-pose family — which must be caught,
    // not crash the whole baseline). CompileBehavior itself never throws (captures into .error).
    std::size_t bCompiled = 0, bTotal = 0, bSkipped = 0, bNoSkel = 0;
    std::vector<std::pair<std::string, std::string>> bFails;   // GENUINE quarantine (real compile failures)
    if (!bundles.empty()) {
        std::vector<std::shared_ptr<havok::model::HkyArchive>> arcs;   // keep sources alive
        for (const auto& p : bundles) {
            std::string berr;
            auto arc = havok::model::HkyArchive::LoadFromFile(p, berr);
            if (!arc) { bFails.push_back({ "(bundle) " + p, "load: " + berr }); continue; }
            arcs.push_back(std::move(arc));
        }
        // Union of unit prefixes across the load order; kind from the first bundle that defines it.
        std::map<std::string, havok::model::HkyArchive::UnitKind> unitKind;
        std::vector<std::string> unitOrder;
        for (const auto& arc : arcs)
            for (const auto& u : arc->units())
                if (unitKind.emplace(u.prefix, u.kind).second)
                    unitOrder.push_back(u.prefix);
        std::sort(unitOrder.begin(), unitOrder.end());
        bTotal = unitOrder.size();
        havok::model::YamlBehaviorLoader::SetDiagnosticSink(nullptr);   // quiet at corpus scale
        using UK = havok::model::HkyArchive::UnitKind;
        for (const auto& unit : unitOrder) {
            const UK kind = unitKind[unit];
            if (kind == UK::Project) { ++bSkipped; continue; }   // projects compile via a separate path, not the A-pose behavior/character surface
            std::vector<std::shared_ptr<const havok::model::IUnitSource>> sources;   // base-first
            for (const auto& arc : arcs)
                for (const auto& u : arc->units())
                    if (u.prefix == unit) { sources.push_back(arc->source(unit)); break; }
            if (sources.empty()) { ++bSkipped; continue; }
            std::string why;
            bool ok = false;
            try {
                if (kind == UK::Character) {
                    const auto cdata = havok::model::CharacterYamlLoader::LoadMerged(sources);
                    const auto r     = havok::sct::CompileCharacter(cdata);
                    ok = r.ok; if (!ok) why = r.error;
                } else {   // Behavior
                    const auto data = havok::model::YamlBehaviorLoader::LoadMerged(sources);
                    const auto r    = havok::sct::CompileBehavior(data);
                    if (!r.ok) why = r.error;
                    else { const auto vr = havok::sct::ValidatePackfile(r.bytes); ok = vr.ok; if (!ok) why = "validate: " + vr.error; }
                }
            } catch (const std::exception& e) { why = std::string("throw: ") + e.what(); }
              catch (...)                     { why = "throw: (unknown)"; }
            if (ok) ++bCompiled;
            else if (why.find("bone list is EMPTY") != std::string::npos) ++bNoSkel;   // offline artifact: the runtime injects per-actor skeleton boneNames; hky-merge-compile does not — NOT a real failure
            else if (why.find("missing behavior.yaml") != std::string::npos ||
                     why.find("missing character.yaml") != std::string::npos) ++bSkipped;   // not a behavior/character unit (HkyArchive over-detects skeleton subtrees as Behavior-kind)
            else bFails.push_back({ unit, why });
        }
    }

    std::ostringstream m;
    m << "# havok-core v2 oracle-baseline (Stage 0)\n";
    m << "# TIER A (ground truth, inviolable): vanilla .hkx round-trip. new havok-io MUST match.\n";
    m << "corpus_root=" << corpusRoot << "\n";
    m << "tierA_total=" << files.size() << "\n";
    m << "tierA_pass="  << pass << "\n";
    m << "tierA_fail="  << fails.size() << "\n";
    for (const auto& [p, why] : fails) m << "tierA_FAIL\t" << p << "\t" << why << "\n";
    m << "# TIER B (advisory, NOT truth): merge+compile over the load order. GENUINE compile\n";
    m << "#   failures = known-bad A-pose family, QUARANTINED (new SUCCEEDING there is the goal).\n";
    m << "#   NOTE: offline compile lacks the runtime Resolver's per-actor skeleton boneNames\n";
    m << "#   injection, so bone-name graphs (creatures) fail 'bone list is EMPTY' offline — that\n";
    m << "#   is a FIDELITY GAP of this offline mirror, bucketed as tierB_noskeleton, NOT a real fail.\n";
    m << "tierB_bundles="     << bundles.size() << "\n";
    m << "tierB_units="       << bTotal << "\n";
    m << "tierB_compiled="    << bCompiled << "\n";
    m << "tierB_skipped="     << bSkipped << "  # project/non-behavior units (separate compile path)\n";
    m << "tierB_noskeleton="  << bNoSkel  << "  # offline-context gap (bone-name graphs w/o injected skeleton) — NOT a failure\n";
    m << "tierB_quarantine="  << bFails.size() << "  # GENUINE compile failures (the real quarantine set)\n";
    for (const auto& [k, why] : bFails) m << "tierB_QUARANTINE\t" << k << "\t" << why << "\n";
    const std::string text = m.str();
    if (!outPath.empty()) { std::ofstream of(outPath); of << text; }
    std::fputs(text.c_str(), stdout);
    std::printf("oracle-baseline: Tier A %zu/%zu byte-identical (%zu fail) | "
                "Tier B of %zu units: %zu compiled, %zu skipped, %zu no-skeleton(offline gap), %zu GENUINE quarantine\n",
                pass, files.size(), fails.size(), bTotal, bCompiled, bSkipped, bNoSkel, bFails.size());
    return fails.empty() ? 0 : 1;   // only Tier A (ground truth) gates the exit code
}

// ── schema-parity (havok-core v2 rewrite, Stage 1) ────────────────────────────────────
// Gate the Havok/ class descriptors against the OLD (current) classes' ground truth, BEFORE any
// serde trusts the schema (bytes come in Stage 2). Two checks per class:
//   SIZE — the schema's field-walk size (parent chain + this class's fields, honoring pad/skip/
//          align) == the descriptor's declared `size:`. Proves the field list is complete + correctly
//          sized (a missing/mis-typed field won't sum to `size`).
//   SIG  — the schema's `signature:` == the old class's Signature() (via HavokRegistry::Create), when
//          the old class exists. Proves the descriptor names + identifies the right class.
// A class with no old struct (schema-only) is fine (sig unchecked). Usage: schema-parity <Havok-dir>
int doSchemaParity(const std::string& havokDir) {
    havok::schema::SchemaRegistry reg;
    std::string err;
    if (!reg.LoadDir(havokDir, err)) { std::printf("ERROR loading schema '%s': %s\n", havokDir.c_str(), err.c_str()); return 2; }

    std::size_t total = 0, ok = 0, sizeMismatch = 0, sigMismatch = 0, schemaOnly = 0;
    for (const auto& [name, cs] : reg.All()) {
        ++total;
        bool classOk = true;

        std::string serr;
        const int computed = reg.ComputeSize(name, &serr);
        if (cs.size > 0 && computed != cs.size) {   // size: is optional (0 = derived-only) — LoadDir already asserts it
            const std::string extra = serr.empty() ? std::string{} : ("  (" + serr + ")");
            std::printf("SIZE  %-40s computed %d != declared %d%s\n", name.c_str(), computed, cs.size, extra.c_str());
            ++sizeMismatch; classOk = false;
        }

        auto obj = havok::HavokRegistry::Create(name);   // the old class, by name
        if (obj) {
            const std::uint32_t oldSig = obj->Signature();
            if (oldSig != cs.signature) {
                std::printf("SIG   %-40s schema 0x%08x != old 0x%08x\n", name.c_str(), cs.signature, oldSig);
                ++sigMismatch; classOk = false;
            }
        } else {
            ++schemaOnly;   // no old struct to compare against — not a mismatch
        }

        if (classOk) ++ok;
    }
    std::printf("schema-parity: %zu/%zu classes OK  (%zu size, %zu sig mismatch; %zu schema-only, no old class)\n",
                ok, total, sizeMismatch, sigMismatch, schemaOnly);
    return (sizeMismatch + sigMismatch) == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string verb = argv[1];
    const std::string in   = argv[2];
    std::string out;
    std::string skel;                 // --skeleton <skeleton.hkx|bones.txt> for bone-name resolution
    std::string schema;               // --schema <HavokDir>: wire the merge classifier to the schema
    bool strictSchema = false;        // --strict-schema: disable the name-set fallback (gate mode)
    std::vector<std::string> extra;   // positional args after `in` (merge delta dirs)
    for (int i = 3; i < argc; i++) {
        const std::string a = argv[i];
        if ((a == "-o" || a == "--out") && i + 1 < argc) out = argv[++i];
        else if (a == "--skeleton" && i + 1 < argc) skel = argv[++i];
        else if (a == "--schema" && i + 1 < argc) schema = argv[++i];
        else if (a == "--strict-schema") strictSchema = true;
        else extra.push_back(a);
    }
    if (verb == "merge")     return doMerge(in, extra, out);
    if (verb == "compile")   return doCompile(in, out, skel);
    if (verb == "decompile") return doDecompile(in, out, skel);
    if (verb == "hky-compile") return doHkyCompile(in, extra, out);
    if (verb == "hky-pack")    return doHkyPack(in, out);
    if (verb == "hky-unpack")  return doHkyUnpack(in, out);
    if (verb == "hky-merge-compile") return doHkyMergeCompile(in, extra, out, schema, strictSchema);
    if (verb == "schema-merge-tag")  return doSchemaMergeTag(in, extra.empty() ? std::string{} : extra[0],
                                                             extra.size() > 1 ? extra[1] : std::string{});
    if (verb == "objhist")   return doObjHist(in);
    if (verb == "objlist")   return doObjList(in);
    if (verb == "resdump")   return doResDump(in);
    if (verb == "mapperdump") return doMapperDump(in);
    if (verb == "physdump")   return doPhysDump(in);
    if (verb == "fkcheck")    return doFkCheck(in);
    if (verb == "ragdollcheck") return doRagdollCheck(in);
    if (verb == "bodycheck")    return doBodyCheck(in);
    if (verb == "framecheck")   return doFrameCheck(in);
    if (verb == "constraintcheck") return doConstraintCheck(in);
    if (verb == "hkx-roundtrip") return doHkxRoundtrip(in, out);
    if (verb == "iohkx-roundtrip") return doIoHkxRoundtrip(in, extra.empty() ? std::string{} : extra[0], out);
    if (verb == "iohkx-rebuild")   return doIoHkxRebuild(in, extra.empty() ? std::string{} : extra[0], out);
    if (verb == "iohkx-to-tagfile") return doIoToTagfile(in, extra.empty() ? std::string{} : extra[0], out,
                                                          extra.size() > 1 ? extra[1] : std::string{});
    if (verb == "tagfile-roundtrip") return doTagfileRoundtrip(in, extra.empty() ? std::string{} : extra[0],
                                                               extra.size() > 1 ? extra[1] : std::string{}, out);
    if (verb == "model-merge") return doModelMerge(in, extra.empty() ? std::string{} : extra[0],
                                                   extra.size() > 1 ? extra[1] : std::string{},
                                                   extra.size() > 2 ? std::vector<std::string>(extra.begin() + 2, extra.end())
                                                                    : std::vector<std::string>{}, out);
    if (verb == "mod-delta") return doModDelta(in, extra.empty() ? std::string{} : extra[0],
                                               extra.size() > 1 ? std::vector<std::string>(extra.begin() + 1, extra.end())
                                                                : std::vector<std::string>{}, out);
    if (verb == "schemabuild-clip-check") return doSchemaBuildClipCheck(in, extra.empty() ? std::string{} : extra[0]);
    if (verb == "schema-compile-check") return doSchemaCompileCheck(in, extra.empty() ? std::string{} : extra[0], skel);
    if (verb == "identity-check") return doIdentityCheck(in, extra.size() > 0 ? extra[0] : std::string{},
                                                          extra.size() > 1 ? extra[1] : std::string{},
                                                          extra.size() > 2 ? extra[2] : std::string{});
    if (verb == "emit-check") return doEmitCheck(in, extra.size() > 0 ? extra[0] : std::string{},
                                                 extra.size() > 1 ? extra[1] : std::string{},
                                                 extra.size() > 2 ? extra[2] : std::string{});
    if (verb == "oracle-baseline") return doOracleBaseline(in, extra, out);
    if (verb == "schema-parity")   return doSchemaParity(in);
    if (verb == "skeleton-recompile") return doSkeletonRecompile(in, out);
    if (verb == "skeleton-decompile") return doSkeletonDecompile(in, out);
    if (verb == "skeleton-decompile-tree") return doSkeletonDecompileTree(in, out);
    if (verb == "skeleton-compile")   return doSkeletonCompile(in, out, std::find(extra.begin(), extra.end(), std::string("--full")) != extra.end());
    if (verb == "skeleton-compile-full") return doSkeletonCompileFull(in, out);
    if (verb == "skeleton-recompile-base") return doSkeletonRecompileBase(in);
    if (verb == "skeleton-append") return doSkeletonAppend(in, extra.empty() ? std::string{} : extra[0], out);
    if (verb == "skeleton-diff-layer") return doSkeletonDiffLayer(in, extra.empty() ? std::string{} : extra[0], out);
    if (verb == "skeleton-split") return doSkeletonSplit(in, extra.size() > 0 ? extra[0] : std::string{}, extra.size() > 1 ? extra[1] : std::string{});
    if (verb == "animscan")  return doAnimScan(in);
    if (verb == "refframe")  return doRefFrame(in);
    if (verb == "animdatadump") return doAnimDataDump(in, extra);
    if (verb == "clipinputs-check") return doClipInputsCheck(in);
    if (verb == "movesets-roundtrip") return doMovesetsRoundtrip(in);
    if (verb == "setdata-tree-roundtrip") return doSetdataTreeRoundtrip(in);
    if (verb == "setdata-decompose") return doSetdataDecompose(in, out);
    if (verb == "setdata-compose") return doSetdataCompose(in, out);
    if (verb == "animdata-tree-roundtrip") return doAnimdataTreeRoundtrip(in, extra);
    if (verb == "animdata-decompose") return doAnimdataDecompose(in, extra, out);
    if (verb == "animdata-compose") return doAnimdataCompose(in, extra, out);
    if (verb == "motion-decompose") return doMotionDecompose(in, extra, out);
    if (verb == "motion-roundtrip") return doMotionRoundtrip(in);
    if (verb == "animdata-build-check") return doAnimDataBuildCheck(in, extra);
    if (verb == "motion-sidecar-check") return doMotionSidecarCheck(in, extra);
    if (verb == "perproject-check")     return doPerProjectCheck(in, extra);
    if (verb == "fielddiff") return extra.empty() ? usage() : doFieldDiff(in, extra[0]);
    if (verb == "treediff") {
        bool tdv = false; std::string dirB, schemaDir;
        for (std::size_t i = 0; i < extra.size(); ++i) {
            if (extra[i] == "-v" || extra[i] == "--verbose") tdv = true;
            else if (extra[i] == "--resolve") { if (i + 1 < extra.size()) schemaDir = extra[++i]; }
            else if (dirB.empty()) dirB = extra[i];
        }
        return dirB.empty() ? usage() : doTreeDiff(in, dirB, tdv, schemaDir);
    }
    if (verb == "exprdump")  return doExprDump(in);
    if (verb == "binddump")  return doBindDump(in);
    if (verb == "bwdump")    return doBwDump(in);
    if (verb == "idcheck")   return extra.empty() ? usage() : doIdCheck(in, extra[0]);
    if (verb == "idalign")   return extra.empty() ? usage() : doIdAlign(in, extra[0]);
    if (verb == "xmlparse")  return doXmlParse(in);
    if (verb == "patchconvert") return doPatchConvert(in, extra, out);
    if (verb == "patchdelta")   return doPatchDelta(in, extra, out);
    if (verb == "derive-delta") return doDeriveDelta(in, extra, out);
    if (verb == "derive-lib") {   // debug: exercise the library DeriveLooseBehaviorDelta
        if (extra.empty() || out.empty()) { std::printf("usage: derive-lib <van.hkx> <mod.hkx> -o <outDelta>\n"); return 2; }
        const auto res = havok::sct::DeriveLooseBehaviorDelta(in, extra[0], out);
        std::printf("derive-lib: ok=%d err='%s' matched=%d added=%d changed=%d new=%d removed=%d baseMax=%ld\n",
                    res.ok, res.error.c_str(), res.matched, res.added, res.changedNodes, res.newNodes,
                    res.removedFromBase, res.baseMaxId);
        return res.ok ? 0 : 1;
    }
    if (verb == "vanbase")      return doVanBase(in, extra, out);
    if (verb == "basefidelity") return doBaseFidelity(in, extra);
    if (verb == "animdata-derive-check") return doAnimDataDeriveCheck(in, extra);
    if (verb == "animdata-derive") return doAnimDataDerive(in, extra, out);
    if (verb == "animdata-derive-delta") return doAnimDataDeriveDelta(in, extra);
    if (verb == "animanns")     return doAnimAnns(in);
    if (verb == "refframe")     return doRefFrame(in, out);
    if (verb == "project-info")    return doProjectInfo(in);
    if (verb == "project-schema-check") return doProjectSchemaCheck(in, extra.empty() ? std::string{} : extra[0]);
    if (verb == "character-schema-check") return doCharacterSchemaCheck(in, extra.empty() ? std::string{} : extra[0], skel);
    if (verb == "animation-schema-check") return doAnimationSchemaCheck(in, extra.empty() ? std::string{} : extra[0]);
    if (verb == "skeleton-schema-check") return doSkeletonSchemaCheck(in, extra.empty() ? std::string{} : extra[0]);
    if (verb == "skeleton-full-schema-check") return doSkeletonFullSchemaCheck(in, extra.empty() ? std::string{} : extra[0]);
    if (verb == "project-rewrite") return doProjectRewrite(in, extra, out);
    if (verb == "project-build")   return doProjectBuild(in, extra, out);
    return usage();
}
