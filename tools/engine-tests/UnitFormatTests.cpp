/// @brief The unit format: a unit is graph.yaml plus a flat list of YAML files read by what they hold. Random folder layouts
///        give identical bytes from disk and zip; duplicates and non-nodes are refused; rosters patch inline; a skeleton
///        merges per bone.

#include "TestSupport.h"

#include <codec/serialization/HkyArchive.h>
#include <decompile/BehaviorDecompile.h>
#include <disk/Disk.h>

#include <filesystem>
#include <map>
#include <process.h>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace CB::engine::tests {

namespace {

namespace dec = decompile;
namespace fs  = std::filesystem;
using Files = std::vector<std::pair<std::string, std::string>>;

std::string Clip(const std::string& id, const std::string& name, const std::string& triggers = {}) {
    return "id: " + id + "\nclass: hkbClipGenerator\nuserData: 0\nname: '" + name + "'\nanimationName: 'Animations\\" + name + ".hkx'\n"
           "playbackSpeed: 1\nanimationBindingIndex: user\nmode: MODE_LOOPING\nflags: 0\n" + triggers;
}
std::string Trigger(const std::string& key, const std::string& time, const std::string& indent) {
    return indent + "'" + key + "':\n" + indent + "  localTime: " + time + "\n" + indent + "  event:\n" + indent + "    id: 0\n" +
           indent + "    event: 'Go'\n" + indent + "  relativeToEndOfClip: false\n" + indent + "  acyclic: false\n" + indent + "  isAnnotation: false\n";
}
std::string Selector(const std::string& children) {
    return "id: 0000x0002\nclass: hkbManualSelectorGenerator\nvariableBindingSet:\n  indexOfBindingToEnable: -1\n  bindings:\n"
           "    'selectedGeneratorIndex':\n      variableIndex: 0\n      variable: 'Sel'\n      bitIndex: -1\n      bindingType: BINDING_TYPE_VARIABLE\n"
           "userData: 0\nname: 'Sel_MSG'\nselectedGeneratorIndex: 0\ncurrentGeneratorIndex: 0\ngenerators:\n" + children;
}
const char* kGraph =
    "id: 0000x0001\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbBehaviorGraph':\n    className: 'hkbBehaviorGraph'\n"
    "    variant:\n      class: hkbBehaviorGraph\n      userData: 0\n      name: 'T.hkb'\n"
    "      variableMode: VARIABLE_MODE_DISCARD_WHEN_INACTIVE\n      rootGenerator: 0000x0002\n      data:\n"
    "        variables:\n          'Sel': {type: VARIABLE_TYPE_INT32, value: 0}\n"
    "        characterProperties: {}\n        events:\n          'Go': {flags: 0}\n        attributes: {}\n";
const std::string kTwoChildren = "  '0000x0000': 0000x0003\n  '0000x0001': 0000x0004\n";
const std::string kTriggerArray = "id: 0000x0009\nclass: hkbClipTriggerArray\ntriggers:\n" + Trigger("0000x0000", "0.5", "  ");

/// @brief Two clips sharing one trigger list (so it is a node), under a selector.
Files Behavior() {
    return { { "graph.yaml", kGraph }, { "0000x0002.yaml", Selector(kTwoChildren) },
             { "0000x0003.yaml", Clip("0000x0003", "A", "triggers: 0000x0009\n") },
             { "0000x0004.yaml", Clip("0000x0004", "B", "triggers: 0000x0009\n") },
             { "0000x0009.yaml", kTriggerArray } };
}

const char* kSkeletonGraph =
    "id: 0000x0001\nclass: hkRootLevelContainer\nnamedVariants:\n  - name: 'Merged Animation Container'\n"
    "    className: 'hkaAnimationContainer'\n    variant:\n      class: hkaAnimationContainer\n      skeletons:\n        - animation\n";
std::string SkeletonNode(const std::string& bones) {
    return "id: animation\nclass: hkaSkeleton\nname: 'Root'\nextraBones: [" + bones + "]\n";
}
std::string Bone(const std::string& name, const std::string& parent, const std::string& x, const std::string& lock = "false") {
    return "class: hkaBone\nskeleton: animation\nname: '" + name + "'\nparent: " + parent + "\nlockTranslation: " + lock + "\npose:\n"
           "  translation: '(" + x + " 0 0 0)'\n  rotation: '(0 0 0 1)'\n  scale: '(1 1 1 0)'\n";
}
Files Skeleton() {
    return { { "graph.yaml", kSkeletonGraph }, { "animation/skeleton.yaml", SkeletonNode("'Root', 'Child'") },
             { "animation/bones/Root.yaml", Bone("Root", "null", "0") }, { "animation/bones/Child.yaml", Bone("Child", "'Root'", "1") } };
}

/// @brief A fresh folder under `root` holding `files`.
fs::path WriteUnit(const fs::path& root, const std::string& name, const Files& files) {
    const fs::path dir = root / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    for (const auto& [rel, body] : files) Check(disk::WriteFile(dir / rel, body), "write " + rel);
    return dir;
}

/// @brief `files` with every path but graph.yaml's random: any depth, any folder names, any file name.
Files Shuffled(const Files& files, std::mt19937& rng) {
    static const char* const kFolders[] = { "Combat", "combat2", "A", "Group With Spaces", "x.y", "Deep", "0000x0003", "bones", "states" };
    Files out;
    int n = 0;
    for (const auto& [rel, body] : files) {
        if (rel == "graph.yaml") { out.emplace_back(rel, body); continue; }
        std::string path;
        const int depth = static_cast<int>(rng() % 7);
        for (int d = 0; d < depth; ++d) path += std::string(kFolders[rng() % std::size(kFolders)]) + "/";
        out.emplace_back(path + "f" + std::to_string(n++) + ".yaml", body);
    }
    return out;
}

std::vector<std::uint8_t> Bytes(const common::IUnitSource& src, const schema::SchemaRegistry& reg, const std::string& what) {
    dec::ParsedHky graph;
    std::vector<std::uint8_t> out;
    std::string err;
    const bool ok = dec::ParseHkyReflective(src, reg, graph, err) && dec::SerializeGraph(graph, out, err);
    Check(ok, what + ": " + err);
    return out;
}

/// @brief Serialized bytes never depend on where the files sit, from disk or from a packed .hky, and a re-emit puts each
///        node back in its folder.
void ShuffleChecks(const Files& files, const schema::SchemaRegistry& reg, const fs::path& tmp, const std::string& tag) {
    const auto canon = Bytes(common::DiskUnitSource(WriteUnit(tmp, tag, files)), reg, tag);
    Check(!canon.empty(), tag + ": the fixture serializes");
    std::mt19937 rng(0xCB);
    for (int round = 0; round < 8; ++round) {
        const std::string what = tag + " shuffle " + std::to_string(round);
        const Files shuffled = Shuffled(files, rng);
        const fs::path dir = WriteUnit(tmp, tag + "-shuffled", shuffled);
        Check(Bytes(common::DiskUnitSource(dir), reg, what) == canon, what + ": disk bytes differ");

        std::vector<codec::HkyArchive::PackEntry> entries;
        for (const auto& [rel, body] : shuffled)
            entries.push_back({ "meshes/t/u.hkx/" + rel, [text = body]() -> std::optional<std::string> { return text; } });
        const std::string hky = (tmp / (tag + ".hky")).string();
        std::string err;
        Check(codec::HkyArchive::PackEntries(std::move(entries), hky, err), what + ": pack: " + err);
        const auto arc = codec::HkyArchive::LoadFromFile(hky, err);
        Check(arc != nullptr, what + ": load: " + err);
        if (const auto src = arc ? arc->source("meshes/t/u.hkx") : nullptr) Check(Bytes(*src, reg, what) == canon, what + ": zip bytes differ");
        else Check(false, what + ": no unit in the zip");

        dec::ParsedHky first, second;
        const fs::path again = tmp / (tag + "-again");
        { std::error_code ec; fs::remove_all(again, ec); }
        Check(dec::ParseHkyReflective(common::DiskUnitSource(dir), reg, first, err), what + ": " + err);
        Check(dec::EmitHkyReflective(first.identity, reg, again.string(), err), what + ": emit: " + err);
        Check(dec::ParseHkyReflective(common::DiskUnitSource(again), reg, second, err), what + ": re-parse: " + err);
        const auto folders = [](const dec::ParsedHky& g) {
            std::map<std::string, std::string> m;
            for (const auto& [o, folder] : g.identity.folder)
                if (const auto it = g.identity.ids.find(o); it != g.identity.ids.end() && it->second[0] != '~') m[it->second] = folder;
            return m;
        };
        Check(folders(first) == folders(second), what + ": a re-emit moved a node");
    }
}

bool Parses(const Files& files, const schema::SchemaRegistry& reg, const fs::path& tmp, std::string& err) {
    dec::ParsedHky graph;
    err.clear();
    return dec::ParseHkyReflective(common::DiskUnitSource(WriteUnit(tmp, "strict", files)), reg, graph, err);
}

void StrictnessChecks(const schema::SchemaRegistry& reg, const fs::path& tmp) {
    std::string err;
    Files f = Behavior();
    f.emplace_back("somewhere/else/copy.yaml", Clip("0000x0003", "A"));
    Check(!Parses(f, reg, tmp, err) && err.find("stated twice") != std::string::npos, "a duplicate node file is refused: " + err);

    f = Behavior();
    f.emplace_back("0000x0002/generators.yaml", "'0000x0000': 0000x0003\n'0000x0001': 0000x0004\n");
    Check(!Parses(f, reg, tmp, err) && err.find("not a node") != std::string::npos, "a YAML with no id is refused: " + err);

    f = Behavior();
    f.emplace_back("deep/root.yaml", kGraph);
    Check(!Parses(f, reg, tmp, err) && err.find("the root is graph.yaml") != std::string::npos, "a root outside graph.yaml is refused: " + err);

    f = Behavior();
    f.emplace_back("Notes/readme.txt", "not a node, and not YAML");
    Check(Parses(f, reg, tmp, err), "another extension is ignored: " + err);

    f = Skeleton();
    f.emplace_back("Face/Child.yaml", Bone("Child", "'Root'", "1"));
    Check(!Parses(f, reg, tmp, err) && err.find("stated twice") != std::string::npos, "a duplicate bone file is refused: " + err);
}

/// @brief Base `files` plus `patch` as a mod over it, serialized.
bool Merged(const Files& files, const Files& patch, const schema::SchemaRegistry& reg, const fs::path& tmp,
            std::vector<std::uint8_t>& out, std::string& err) {
    const std::vector<common::LayerSource> layers{
        { std::make_shared<common::DiskUnitSource>(WriteUnit(tmp, "layer0", files)), "base", { "base" } },
        { std::make_shared<common::DiskUnitSource>(WriteUnit(tmp, "layer1", patch)), "mod", { "base", "mod" } },
    };
    dec::ParsedHky graph;
    err.clear();
    return dec::MergeLayersReflective(layers, reg, nullptr, graph, err) && dec::SerializeGraph(graph, out, err);
}

/// @brief The patch over `base` merges to exactly the bytes of `whole`, a complete unit stating the outcome.
void MergesTo(const Files& base, const Files& patch, const Files& whole, const schema::SchemaRegistry& reg, const fs::path& tmp, const std::string& what) {
    std::vector<std::uint8_t> got;
    std::string err;
    Check(Merged(base, patch, reg, tmp, got, err), what + ": " + err);
    Check(got == Bytes(common::DiskUnitSource(WriteUnit(tmp, "whole", whole)), reg, what), what + ": merged bytes differ from the whole unit");
}

void PatchChecks(const schema::SchemaRegistry& reg, const fs::path& tmp) {
    const Files base = Behavior();
    MergesTo(base, { { "Moved/Somewhere/deep/x.yaml", "id: 0000x0003\nclass: hkbClipGenerator\n" } }, base, reg, tmp, "a folder-only patch changes nothing");

    Files whole = base;
    whole[1].second = Selector(kTwoChildren + "  '0000x0002': 0000x0004\n");
    MergesTo(base, { { "any/where.yaml", "id: 0000x0002\nclass: hkbManualSelectorGenerator\ngenerators:\n  '0001x0000': 0000x0004\n" } }, whole, reg, tmp,
             "a roster patched inline appends by key");

    whole = base;
    whole[4].second = "id: 0000x0009\nclass: hkbClipTriggerArray\ntriggers:\n" + Trigger("0000x0000", "0.5", "  ") + Trigger("0000x0001", "1", "  ");
    MergesTo(base, { { "x.yaml", "id: 0000x0003\nclass: hkbClipGenerator\ntriggers:\n  triggers:\n" + Trigger("0001x0000", "1", "    ") } }, whole, reg, tmp,
             "a class-less map onto a node patches the node every owner shares");

    whole = base;
    whole[2].second = Clip("0000x0003", "A", "triggers:\n  triggers:\n" + Trigger("0000x0000", "1", "    "));
    MergesTo(base, { { "x.yaml", "id: 0000x0003\nclass: hkbClipGenerator\ntriggers:\n  class: hkbClipTriggerArray\n  triggers:\n" + Trigger("0001x0000", "1", "    ") } }, whole, reg, tmp,
             "a map stating its class replaces the node for this owner alone");

    std::vector<std::uint8_t> got;
    std::string err;
    Check(!Merged(base, { { "a.yaml", "id: 0000x0003\nclass: hkbClipGenerator\n" }, { "b/c.yaml", "id: 0000x0003\nclass: hkbClipGenerator\n" } }, reg, tmp, got, err) &&
              err.find("stated twice") != std::string::npos,
          "a node stated twice in a patch is refused: " + err);
}

void SkeletonPatchChecks(const schema::SchemaRegistry& reg, const fs::path& tmp) {
    const Files base = Skeleton();

    Files whole = base;
    whole[3].second = Bone("Child", "'Root'", "7");
    MergesTo(base, { { "Body/Arm/Child.yaml", "class: hkaBone\nskeleton: animation\nname: 'Child'\npose:\n  translation: '(7 0 0 0)'\n" } }, whole, reg, tmp,
             "a bone patch overrides what it states");

    MergesTo(base, { { "Face/Child.yaml", "class: hkaBone\nskeleton: animation\nname: 'Child'\n" } }, base, reg, tmp, "a bone moved to another folder changes nothing");

    whole = base;
    whole[1].second = SkeletonNode("'Root', 'Child', 'Hand', 'Finger'");
    whole.emplace_back("Hand.yaml", Bone("Hand", "'Child'", "2"));
    whole.emplace_back("Finger.yaml", Bone("Finger", "'Hand'", "3"));
    const Files added{ { "h/a.yaml", Bone("Finger", "'Hand'", "3") }, { "h/b.yaml", Bone("Hand", "'Child'", "2") } };
    MergesTo(base, added, whole, reg, tmp, "new bones append, each after its parent");

    const std::vector<common::LayerSource> layers{
        { std::make_shared<common::DiskUnitSource>(WriteUnit(tmp, "layer0", base)), "base", { "base" } },
        { std::make_shared<common::DiskUnitSource>(WriteUnit(tmp, "layer1", added)), "mod", { "base", "mod" } },
    };
    const auto names = dec::ReadLayeredSkeletonBones(layers, reg, {});
    Check(names && *names == std::vector<std::string>({ "Root", "Child", "Hand", "Finger" }), "the layered bone names match the merge's order");

    std::vector<std::uint8_t> got;
    std::string err;
    Check(!Merged(base, { { "n.yaml", "class: hkaBone\nskeleton: animation\nname: 'New'\nparent: 'Root'\n" } }, reg, tmp, got, err) && err.find("whole pose") != std::string::npos,
          "a new bone without a pose is refused: " + err);
    Check(!Merged(base, { { "n.yaml", "class: hkaBone\nskeleton: animation\nname: 'Child'\ndeleted: true\n" } }, reg, tmp, got, err) && err.find("removed") != std::string::npos,
          "removing a bone is refused: " + err);
    Check(!Merged(base, { { "n.yaml", Bone("New", "'Nowhere'", "1") } }, reg, tmp, got, err) && err.find("Nowhere") != std::string::npos,
          "a new bone with an unknown parent is refused: " + err);
    Check(!Merged(base, { { "n.yaml", "class: hkaBone\nskeleton: ragdoll\nname: 'Child'\n" } }, reg, tmp, got, err) && err.find("no skeleton") != std::string::npos,
          "a bone of an unknown skeleton is refused: " + err);
}

}  // namespace

void RunUnitFormatTests(const schema::SchemaRegistry& reg) {
    const fs::path tmp = fs::temp_directory_path() / ("cb-unit-format-" + std::to_string(_getpid()));
    ShuffleChecks(Behavior(), reg, tmp, "behavior");
    ShuffleChecks(Skeleton(), reg, tmp, "skeleton");
    StrictnessChecks(reg, tmp);
    PatchChecks(reg, tmp);
    SkeletonPatchChecks(reg, tmp);
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

}  // namespace CB::engine::tests
