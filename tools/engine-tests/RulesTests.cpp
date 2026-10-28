/// @brief The rule framework (CB::engine::rules) over a small hand-written unit set: descriptor checks, run order, the
///        project -> character -> graph relations, re-emit fidelity, and a reversible test op through the regen's
///        apply / re-emit / reverse-for-the-gate sequence.

#include "TestSupport.h"

#include <decompile/BehaviorDecompile.h>
#include <disk/Disk.h>
#include <codec/resource/PathKey.h>
#include <rules/HoistBoneData.h>
#include <rules/Rules.h>
#include <RymlInclude.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <process.h>
#include <string>
#include <vector>

namespace CB::engine::tests {
namespace {
namespace fs  = std::filesystem;
namespace sch = CB::engine::schema;
namespace dec = CB::engine::decompile;
namespace rl  = CB::engine::rules;

/// @brief Test op: appends `params.suffix` to the name of every behavior graph some character runs; the inverse strips it.
class SuffixOp final : public rl::IOp {
public:
    struct Rec final : rl::Record { std::vector<std::string> units; };

    SuffixOp(const sch::RuleDef& rule, std::string& err) {
        std::string   buf  = rule.text;
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(buf));
        const auto    root = tree.rootref();
        if (root.has_child("params") && root["params"].has_child("suffix")) {
            const auto v = root["params"]["suffix"].val();
            m_suffix.assign(v.str, v.len);
        }
        if (m_suffix.empty()) err = "SuffixOp needs params.suffix";
    }

    bool Apply(rl::Context& ctx, std::unique_ptr<rl::Record>& out, std::string& err) override {
        auto rec = std::make_unique<Rec>();
        for (const rl::Character& c : ctx.Characters()) {
            std::vector<std::string> graphs;
            if (!ctx.GraphsOf(c, graphs, err)) return false;
            for (const auto& g : graphs) {
                if (std::find(rec->units.begin(), rec->units.end(), g) != rec->units.end()) continue;
                if (!Edit(ctx, g, true, err)) return false;
                rec->units.push_back(g);
            }
        }
        if (!rec->units.empty()) out = std::move(rec);
        return true;
    }

    bool Inverse(rl::Context& ctx, const rl::Record& record, std::string& err) override {
        for (const auto& g : static_cast<const Rec&>(record).units)
            if (!Edit(ctx, g, false, err)) return false;
        return true;
    }

private:
    bool Edit(rl::Context& ctx, const std::string& key, bool add, std::string& err) {
        dec::ParsedHky* hky = ctx.Unit(key, err);
        if (!hky) return false;
        for (const auto& o : hky->objects) {
            if (!o || std::string(o->ClassName()) != "hkbBehaviorGraph") continue;
            std::string& name = o->Str("name");
            if (add) name += m_suffix;
            else if (name.size() >= m_suffix.size() && name.compare(name.size() - m_suffix.size(), m_suffix.size(), m_suffix) == 0)
                name.resize(name.size() - m_suffix.size());
            else { err = key + ": graph name '" + name + "' does not carry the suffix"; return false; }
        }
        ctx.Touch(key);
        return true;
    }

    std::string m_suffix;
};
CB_REGISTER_RULE_OP("TestSuffix", SuffixOp);

const char* kProject =
    "id: 0000x0001\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbProjectData':\n    className: 'hkbProjectData'\n"
    "    variant:\n      class: hkbProjectData\n      worldUpWS: '(0 0 1 0)'\n      stringData:\n"
    "        characterFilenames: ['Characters\\TestChar.hkx']\n      defaultEventMode: 2\n";
const char* kCharacter =
    "id: 0000x0001\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbCharacterData':\n    className: 'hkbCharacterData'\n"
    "    variant:\n      class: hkbCharacterData\n      modelUpMS: '(0 0 1 0)'\n      stringData:\n"
    "        animationNames: ['Animations\\Idle.hkx']\n        name: 'TestChar'\n"
    "        rigName: 'Character Assets\\skeleton.HKX'\n        behaviorFilename: 'Behaviors\\Root.hkx'\n"
    "      scale: 1\n      characterProperties: {}\n";
std::string Graph(const std::string& name) {
    return "id: 0000x0001\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbBehaviorGraph':\n    className: 'hkbBehaviorGraph'\n"
           "    variant:\n      class: hkbBehaviorGraph\n      userData: 0\n      name: '" + name + "'\n"
           "      variableMode: VARIABLE_MODE_DISCARD_WHEN_INACTIVE\n      rootGenerator: 0000x0002\n      data:\n"
           "        variables: {}\n        characterProperties: {}\n        events: {}\n        attributes: {}\n";
}
const char* kReference =
    "id: 0000x0002\nclass: hkbBehaviorReferenceGenerator\nuserData: 0\nname: 'ChildBFR'\nbehaviorName: 'Behaviors\\Child.hkx'\n";
const char* kClip =
    "id: 0000x0002\nclass: hkbClipGenerator\nuserData: 0\nname: 'Idle'\nanimationName: 'Animations\\Idle.hkx'\n"
    "playbackSpeed: 1\nanimationBindingIndex: user\nmode: MODE_LOOPING\nflags: 0\n";

const char* kProjectKey = "meshes/actors/test/testproject.hkx";
const char* kCharKey    = "meshes/actors/test/characters/testchar.hkx";
const char* kRootKey    = "meshes/actors/test/behaviors/root.hkx";
const char* kChildKey   = "meshes/actors/test/behaviors/child.hkx";
const char* kOrphanKey  = "meshes/actors/test/behaviors/orphan.hkx";

bool WriteFixture(const fs::path& root) {
    const std::vector<std::pair<std::string, std::string>> files{
        { std::string(kProjectKey) + "/graph.yaml", kProject },
        { std::string(kCharKey) + "/graph.yaml", kCharacter },
        { std::string(kRootKey) + "/graph.yaml", Graph("Root.hkb") },
        { std::string(kRootKey) + "/references/0000x0002.yaml", kReference },
        { std::string(kChildKey) + "/graph.yaml", Graph("Child.hkb") },
        { std::string(kChildKey) + "/clips/0000x0002.yaml", kClip },
        { std::string(kOrphanKey) + "/graph.yaml", Graph("Orphan.hkb") },
        { std::string(kOrphanKey) + "/clips/0000x0002.yaml", kClip },
    };
    for (const auto& [rel, text] : files)
        if (!CB::engine::disk::WriteFile(root / rel, text)) return false;
    return true;
}

/// @brief Every file under `dir`, relative path -> contents.
std::map<std::string, std::string> Tree(const fs::path& dir) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file()) out[fs::relative(it->path(), dir).generic_string()] = CB::engine::disk::ReadFile(it->path()).value_or("");
    return out;
}

void DescriptorChecks() {
    const auto parses = [](const std::string& text) { sch::RuleDef rd; std::string e; return sch::ParseRuleDef(text, rd, e); };
    Check(parses("name: r\nkind: rule\nop: X\nruns: [regen]\n"), "a minimal rule parses");
    Check(!parses("name: r\nkind: rule\nruns: [regen]\n"), "a rule without op is refused");
    Check(!parses("name: r\nkind: rule\nop: X\n"), "a rule without runs is refused");
    Check(!parses("name: r\nkind: rule\nop: X\nruns: [build]\n"), "an unknown stage is refused");
    Check(!parses("name: r\nkind: rule\nop: X\nruns: [regen]\nselect: [bones]\n"), "an unknown select token is refused");
    sch::RuleDef rd;
    std::string  e;
    Check(sch::ParseRuleDef("name: r\nkind: rule\nop: X\nruns: [regen, converter]\norder: { after: [a], before: [b] }\n"
                            "select: [boneweights, boneref]\n", rd, e)
              && rd.after == std::vector<std::string>{ "a" } && rd.before == std::vector<std::string>{ "b" }
              && rd.runs.size() == 2 && rd.select.size() == 2,
          "a rule's order, stages and selector parse: " + e);
}

void OrderChecks(const fs::path& tmp) {
    const auto load = [&](const std::string& sub, const std::vector<std::pair<std::string, std::string>>& rules,
                          sch::SchemaRegistry& reg, std::string& err) {
        const fs::path dir = tmp / sub;
        for (const auto& [name, extra] : rules)
            CB::engine::disk::WriteFile(dir / (name + ".yaml"),
                "name: " + name + "\nkind: rule\nop: TestSuffix\nparams: { suffix: '_x' }\n" +
                (extra.find("runs:") == std::string::npos ? "runs: [regen]\n" : "") + extra);
        return reg.LoadDir(dir.string(), err);
    };
    {
        sch::SchemaRegistry reg;
        std::string         err;
        Check(load("order", { { "a", "order: { after: [b] }\n" }, { "b", "" }, { "c", "order: { before: [b] }\n" },
                              { "d", "runs: [converter]\n" } }, reg, err), "ordered rules load: " + err);
        std::vector<const sch::RuleDef*> order;
        Check(rl::RulesFor(reg, rl::Stage::Regen, order, err), "RulesFor: " + err);
        std::string names;
        for (const auto* r : order) names += r->name;
        Check(names == "cba", "rules run c, b, a by their order (got " + names + ")");
    }
    {
        sch::SchemaRegistry reg;
        std::string         err;
        Check(load("cycle", { { "a", "order: { after: [b] }\n" }, { "b", "order: { after: [a] }\n" } }, reg, err), "cyclic rules load");
        std::vector<const sch::RuleDef*> order;
        Check(!rl::RulesFor(reg, rl::Stage::Regen, order, err), "an order cycle is refused");
    }
    {
        sch::SchemaRegistry reg;
        std::string         err;
        Check(!load("dangling", { { "a", "order: { after: [nope] }\n" } }, reg, err), "ordering against a missing rule fails the load");
    }
    {
        sch::SchemaRegistry reg;
        std::string         err;
        const fs::path dir = tmp / "noop";
        CB::engine::disk::WriteFile(dir / "a.yaml", "name: a\nkind: rule\nop: NoSuchOp\nruns: [regen]\n");
        Check(reg.LoadDir(dir.string(), err), "a rule naming an unknown op still loads");
        std::vector<const sch::RuleDef*> order;
        Check(!rl::RulesFor(reg, rl::Stage::Regen, order, err), "an unregistered op is refused when the rules run");
    }
}

void UnitSetChecks(const sch::SchemaRegistry& reg, const fs::path& tmp) {
    const fs::path src = tmp / "fixture";
    Check(WriteFixture(src), "write the fixture");
    rl::DirUnitStore store(src.string(), nullptr);
    rl::Context      ctx(store, reg);
    std::string      err;

    Check(ctx.Units(rl::UnitKind::Behavior).size() == 3 && ctx.Units(rl::UnitKind::Character).size() == 1
              && ctx.Units(rl::UnitKind::Project).size() == 1,
          "the store finds 3 behaviors, 1 character, 1 project");
    Check(ctx.Find("Meshes\\Actors\\Test\\Behaviors\\ROOT.hkx") == kRootKey, "Find resolves by path identity");

    const auto& chars = ctx.Characters();
    Check(chars.size() == 1 && chars[0].key == kCharKey && chars[0].project == kProjectKey && chars[0].behavior == kRootKey,
          "the project's character and its root behavior resolve");
    if (chars.size() != 1) return;
    std::vector<std::string> graphs;
    Check(ctx.GraphsOf(chars[0], graphs, err), "GraphsOf: " + err);
    Check(graphs == std::vector<std::string>{ kRootKey, kChildKey }, "the walk reaches root then child, not the orphan");
    std::vector<const rl::Character*> running;
    Check(ctx.RunningCharacters(kChildKey, running, err) && running.size() == 1 && running[0]->key == kCharKey,
          "the child graph's running character");
    Check(ctx.RunningCharacters(kOrphanKey, running, err) && running.empty(), "nobody runs the orphan");

    sch::RuleDef userSel;
    userSel.select = { "user" };
    const dec::ParsedHky* child = ctx.Unit(kChildKey, err);
    int hits = 0;
    if (child)
        rl::ForEachSelected(*child, userSel, [&](CB::engine::codec::io::SchemaObject& so, std::size_t i) {
            hits += so.Fields()[i]->name == "animationBindingIndex";
        });
    Check(hits == 1, "select: [user] finds the clip's animationBindingIndex");
}

/// @brief The regen's sequence: canonical units, the rules applied and the changed units re-emitted, then a fresh
///        context over the emitted units reverses them and serves every unit as the source did.
void GateRoundTrip(const sch::SchemaRegistry& reg, const fs::path& tmp) {
    const fs::path src = tmp / "fixture", canonDir = tmp / "canon", reemit = tmp / "reemit";
    std::string err;

    std::map<std::string, std::vector<std::uint8_t>> canon;
    {
        rl::DirUnitStore store(src.string(), nullptr);
        rl::Context      ctx(store, reg);
        for (const auto& u : store.Units()) {
            std::vector<std::uint8_t> bytes;
            if (!ctx.Serve(u.prefix, bytes, err)) { Check(false, "serve fixture " + err); return; }
            canon[u.prefix] = bytes;
            Check(dec::DecompileBehaviorReflective(bytes, reg, (canonDir / u.prefix).string(), err, nullptr, u.prefix),
                  "decompile " + u.prefix + ": " + err);
        }
    }

    {   /// re-emit fidelity: a parsed canonical unit emits back to the same files (ids included)
        rl::DirUnitStore store(canonDir.string(), nullptr);
        rl::Context      ctx(store, reg);
        for (const auto& u : store.Units()) {
            Check(ctx.Unit(u.prefix, err) && ctx.Emit(u.prefix, (reemit / u.prefix).string(), err), "re-emit " + u.prefix + ": " + err);
            Check(Tree(canonDir / u.prefix) == Tree(reemit / u.prefix), "re-emit of " + u.prefix + " reproduces its files");
        }
    }

    sch::RuleDef rule;
    Check(sch::ParseRuleDef("name: suffix\nkind: rule\nop: TestSuffix\nruns: [regen]\nparams: { suffix: '_rt' }\n", rule, err),
          "parse the test rule: " + err);
    rule.file = "test";
    rl::RunResult run;
    {
        rl::DirUnitStore store(canonDir.string(), nullptr);
        rl::Context      ctx(store, reg);
        Check(rl::ApplyRules(ctx, { &rule }, run, err), "apply: " + err);
        Check(ctx.Touched() == std::set<std::string>{ kRootKey, kChildKey }, "the rule touches the two running graphs");
        for (const auto& k : ctx.Touched()) Check(ctx.Emit(k, store.Dir(k), err), "emit " + k + ": " + err);
    }
    Check(run.applied.size() == 1 && run.applied[0].record, "the rule recorded its change");

    {
        rl::DirUnitStore store(canonDir.string(), nullptr);
        rl::Context      ctx(store, reg);
        std::vector<std::uint8_t> got;
        Check(ctx.Serve(kRootKey, got, err) && got != canon[kRootKey], "unreversed, the changed graph no longer serves its source");
    }
    {
        rl::DirUnitStore store(canonDir.string(), nullptr);
        rl::Context      ctx(store, reg);
        Check(rl::InverseRules(ctx, run, err), "inverse: " + err);
        for (const auto& [key, want] : canon) {
            std::vector<std::uint8_t> got;
            Check(ctx.Serve(key, got, err) && got == want, "reversed, " + key + " serves byte-identical to its source");
        }
    }
}
/// @brief A directory store whose units parse with fixed bones and whose rigs it knows itself.
class FixtureStore final : public rl::IUnitStore {
public:
    FixtureStore(const fs::path& root, std::map<std::string, std::vector<std::string>> bones,
                 std::map<std::string, std::vector<std::string>> rigs)
        : m_dir(root.string(), nullptr), m_bones(std::move(bones)), m_rigs(std::move(rigs)) {}
    std::vector<CB::engine::codec::HkyArchive::Unit>   Units() const override { return m_dir.Units(); }
    std::shared_ptr<const CB::engine::common::IUnitSource> Source(const std::string& key) const override { return m_dir.Source(key); }
    const std::vector<std::string>* Bones(const std::string& key) override {
        const auto it = m_bones.find(key);
        return it == m_bones.end() ? nullptr : &it->second;
    }
    const std::vector<std::string>* RigBones(const std::string& key) override {
        const auto it = m_rigs.find(CB::engine::codec::resource::PathKey(key));
        return it == m_rigs.end() ? nullptr : &it->second;
    }
    std::string Dir(const std::string& key) const { return m_dir.Dir(key); }

private:
    rl::DirUnitStore                                m_dir;
    std::map<std::string, std::vector<std::string>> m_bones, m_rigs;
};

const char* kHoistProject =
    "id: 0000x0001\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbProjectData':\n    className: 'hkbProjectData'\n"
    "    variant:\n      class: hkbProjectData\n      worldUpWS: '(0 0 1 0)'\n      stringData:\n"
    "        characterFilenames: ['Characters\\One.hkx', 'Characters\\Two.hkx']\n      defaultEventMode: 2\n";
std::string HoistCharacter(const std::string& name, const std::string& rig, const std::string& props) {
    return "id: 0000x0001\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbCharacterData':\n    className: 'hkbCharacterData'\n"
           "    variant:\n      class: hkbCharacterData\n      modelUpMS: '(0 0 1 0)'\n      stringData:\n"
           "        name: '" + name + "'\n        rigName: 'Character Assets\\" + rig + "'\n"
           "        behaviorFilename: 'Behaviors\\Root.hkx'\n      scale: 1\n      characterProperties:" + props;
}
const char* kUpperBody =
    "\n        'UpperBody':\n          type: VARIABLE_TYPE_POINTER\n          value:\n            class: hkbBoneWeightArray\n"
    "            boneWeights:\n              count: 4\n              'A': 1\n";
const char* kHoistGraph =
    "id: 0000x0001\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbBehaviorGraph':\n    className: 'hkbBehaviorGraph'\n"
    "    variant:\n      class: hkbBehaviorGraph\n      userData: 0\n      name: 'Root.hkb'\n"
    "      variableMode: VARIABLE_MODE_DISCARD_WHEN_INACTIVE\n      rootGenerator: 0000x0002\n      data:\n"
    "        variables: {}\n        characterProperties:\n          'UpperBody': {type: VARIABLE_TYPE_POINTER}\n"
    "        events: {}\n        attributes: {}\n";
const char* kBlend =
    "id: 0000x0002\nclass: hkbBlenderGenerator\nuserData: 0\nname: 'Blend'\nreferencePoseWeightThreshold: 0\nblendParameter: 1\n"
    "minCyclicBlendParameter: 0\nmaxCyclicBlendParameter: 1\nindexOfSyncMasterChild: -1\nflags: 0\nsubtractLastChild: false\n";
const char* kBlendChildren =
    "'0000x0000':\n  generator: 0000x0003\n  boneWeights:\n    count: 4\n    'A': 1\n  weight: 1\n  worldFromModelWeight: 1\n"
    "'0000x0001':\n  variableBindingSet:\n    bindings:\n      'boneWeights':\n        variableIndex: 0\n"
    "        variable: 'UpperBody'\n        bitIndex: -1\n        bindingType: BINDING_TYPE_CHARACTER_PROPERTY\n"
    "    indexOfBindingToEnable: -1\n  generator: 0000x0004\n  boneWeights:\n    count: 4\n    'B': 0.5\n"
    "  weight: 1\n  worldFromModelWeight: 1\n";
const char* kModGen  = "id: 0000x0003\nclass: hkbModifierGenerator\nuserData: 0\nname: 'MG'\nmodifier: 0000x0005\ngenerator: 0000x0006\n";
const char* kGetUp   =
    "id: 0000x0005\nclass: hkbGetUpModifier\nuserData: 0\nname: 'GetUp'\nenable: true\ngroundNormal: '(0 0 1 0)'\nduration: 1\n"
    "alignWithGroundDuration: 0.25\nrootBoneIndex: 'Root'\notherBoneIndex: 'C'\n";
std::string ClipNode(const char* id) {
    return std::string("id: ") + id + "\nclass: hkbClipGenerator\nuserData: 0\nname: 'Clip'\nanimationName: 'Animations\\Idle.hkx'\n"
           "playbackSpeed: 1\nanimationBindingIndex: user\nmode: MODE_LOOPING\nflags: 0\n";
}

/// @brief HoistBoneData over a graph two characters run, their rigs ordering bones differently: a blend mask, a
///        vanilla-bound mask only one character holds the preset of, and two single bone refs.
void HoistChecks(const sch::SchemaRegistry& reg, const fs::path& tmp) {
    const fs::path src = tmp / "hoist-src", canonDir = tmp / "hoist";
    const std::string proj = "meshes/actors/h/hproject.hkx", one = "meshes/actors/h/characters/one.hkx",
                      two = "meshes/actors/h/characters/two.hkx", root = "meshes/actors/h/behaviors/root.hkx";
    const std::vector<std::string> rigA{ "Root", "A", "B", "C" }, rigB{ "Root", "C", "A", "X", "B" };
    const std::vector<std::pair<std::string, std::string>> files{
        { proj + "/graph.yaml", kHoistProject },
        { one + "/graph.yaml", HoistCharacter("One", "rigA.hkx", kUpperBody) },
        { two + "/graph.yaml", HoistCharacter("Two", "rigB.hkx", " {}\n") },
        { root + "/graph.yaml", kHoistGraph },
        { root + "/generators/0000x0002/0000x0002.yaml", kBlend },
        { root + "/generators/0000x0002/children.yaml", kBlendChildren },
        { root + "/generators/0000x0003.yaml", kModGen },
        { root + "/modifiers/0000x0005.yaml", kGetUp },
        { root + "/clips/0000x0004.yaml", ClipNode("0000x0004") },
        { root + "/clips/0000x0006.yaml", ClipNode("0000x0006") },
    };
    for (const auto& [rel, text] : files) Check(CB::engine::disk::WriteFile(src / rel, text), "write " + rel);
    const std::map<std::string, std::vector<std::string>> bones{ { one, rigA }, { two, rigB }, { root, rigA }, { proj, rigA } };
    const std::map<std::string, std::vector<std::string>> rigs{ { "meshes/actors/h/character assets/riga.hkx", rigA },
                                                                { "meshes/actors/h/character assets/rigb.hkx", rigB } };
    std::string err;

    std::map<std::string, std::vector<std::uint8_t>> canon;
    {
        FixtureStore store(src, bones, rigs);
        rl::Context  ctx(store, reg);
        for (const auto& u : store.Units()) {
            std::vector<std::uint8_t> bytes;
            if (!ctx.Serve(u.prefix, bytes, err)) { Check(false, "serve hoist fixture " + err); return; }
            canon[u.prefix] = bytes;
            Check(dec::DecompileBehaviorReflective(bytes, reg, (canonDir / u.prefix).string(), err, &bones.at(u.prefix), u.prefix),
                  "decompile " + u.prefix + ": " + err);
        }
    }

    const auto it = reg.Rules().find("hoistbonedata");
    Check(it != reg.Rules().end(), "the schema ships the hoistbonedata rule");
    if (it == reg.Rules().end()) return;
    rl::RunResult run;
    {
        FixtureStore store(canonDir, bones, rigs);
        rl::Context  ctx(store, reg);
        Check(rl::ApplyRules(ctx, { &it->second }, run, err), "hoist apply: " + err);
        Check(ctx.Touched() == std::set<std::string>{ root, one, two }, "the hoist touches the graph and both characters");
        std::vector<std::string> left;
        Check(rl::CharPropViolations(ctx, left, err) && left.empty(), "no inline bone data is left: " + (left.empty() ? err : left[0]));
        for (const auto& k : ctx.Touched()) Check(ctx.Emit(k, store.Dir(k), err), "emit " + k + ": " + err);
    }

    const auto text = [&](const std::string& rel) { return CB::engine::disk::ReadFile(canonDir / rel).value_or(""); };
    const auto all = [&](const std::string& unit) { std::string s; for (const auto& [p, t] : Tree(canonDir / unit)) s += t; return s; };
    const std::string children = all(root);
    Check(children.find("boneWeights: { charprop: 'Blend_0' }") != std::string::npos,
          "an unbound mask gets a property of its own, not a preset one character lacks:\n" + children);
    Check(children.find("boneWeights: { charprop: 'UpperBody' }") != std::string::npos, "a vanilla-bound mask keeps its preset");
    Check(children.find("variableBindingSet") == std::string::npos, "folded bindings leave no binding set behind");
    const std::string& getUp = children;
    Check(getUp.find("rootBoneIndex: { charprop: 'Bone_Root' }") != std::string::npos &&
              getUp.find("otherBoneIndex: { charprop: 'Bone_C' }") != std::string::npos,
          "single bone refs bind properties named after the bone:\n" + getUp);
    const std::string c2 = text(two + "/graph.yaml");
    Check(c2.find("'Bone_C': {type: VARIABLE_TYPE_INT32, value: 1}") != std::string::npos,
          "a single bone resolves against the character's own rig:\n" + c2);
    Check(c2.find("'UpperBody':") != std::string::npos, "the character lacking the preset gets it from the inline fallback");

    {
        FixtureStore store(canonDir, bones, rigs);
        rl::Context  ctx(store, reg);
        dec::ParsedHky* hky = ctx.Unit(two, err);
        Check(hky != nullptr, "parse the second character: " + err);
        if (hky) {
            std::map<std::string, std::vector<float>> weights;
            for (const auto& o : hky->objects) {
                if (!o || std::string(o->ClassName()) != "hkbCharacterData") continue;
                const auto& pn = o->Child("stringData")->FieldRef("characterPropertyNames").strs;
                const auto* vs = o->Child("characterPropertyValues");
                for (std::size_t i = 0; vs && i < pn.size(); ++i) {
                    const auto* w = dynamic_cast<const CB::engine::codec::io::SchemaObject*>(vs->Children("wordVariableValues")[i].get());
                    const auto& vv = vs->Children("variantVariableValues");
                    const std::int64_t at = w ? w->GetInt("value") : -1;
                    if (at >= 0 && static_cast<std::size_t>(at) < vv.size())
                        if (const auto* set = dynamic_cast<const CB::engine::codec::io::SchemaObject*>(vv[static_cast<std::size_t>(at)].get());
                            set && set->HasField("boneWeights"))
                            weights[pn[i]] = set->GetArray<float>("boneWeights");
                }
            }
            Check(weights["Blend_0"] == std::vector<float>{ 0, 0, 1, 0, 0 }, "a mask resolves by bone name into another rig");
            Check(weights["UpperBody"] == std::vector<float>{ 0, 0, 0, 0, 0.5f }, "the fallback resolves into the lacking character's rig");
        }
    }

    {
        FixtureStore store(canonDir, bones, rigs);
        rl::Context  ctx(store, reg);
        Check(rl::InverseRules(ctx, run, err), "hoist inverse: " + err);
        for (const auto& [key, want] : canon) {
            std::vector<std::uint8_t> got;
            Check(ctx.Serve(key, got, err) && got == want, "reversed, " + key + " serves byte-identical to its source");
        }
    }

    /// @note The converter's path: the hoisted units, packed, are the base; a mod's graph (the un-hoisted source graph
    ///       with one mask changed and one bone ref added) runs through the forward rules against it.
    const fs::path baseHky = tmp / "hoist-base.hky";
    Check(CB::engine::codec::HkyArchive::PackDirectory(canonDir.string(), baseHky.string(), err), "pack the hoisted base: " + err);
    auto arc = CB::engine::codec::HkyArchive::LoadFromFile(baseHky.string(), err);
    Check(arc != nullptr, "reload the hoisted base: " + err);
    if (!arc) return;
    const auto bonesOf = [&](const std::string& k) -> const std::vector<std::string>* {
        const auto b = bones.find(CB::engine::codec::resource::PathKey(k));
        return b == bones.end() ? nullptr : &b->second;
    };
    const auto rigOf = [&](const std::string& k) -> const std::vector<std::string>* {
        const auto b = rigs.find(CB::engine::codec::resource::PathKey(k));
        return b == rigs.end() ? nullptr : &b->second;
    };
    rl::ModGraphRules mr(arc, bonesOf, reg, rigOf);
    Check(mr.Prepare(err) && mr.Active(), "the converter rules prepare over the base: " + err);

    dec::ParsedHky mod, twin;
    Check(dec::ParsePackfile(canon[root], reg, mod, err), "parse the mod graph's bytes: " + err);
    for (const auto& o : mod.objects) {
        if (std::string(o->ClassName()) == "hkbGetUpModifier") o->SetInt("anotherBoneIndex", 2);   // rig A's 'B'
        if (std::string(o->ClassName()) == "hkbBlenderGeneratorChild" && !o->Child("variableBindingSet"))
            if (auto* w = o->Child("boneWeights")) {
                auto v = w->GetArray<float>("boneWeights");
                v[3] = 1;   // 'C'
                w->SetArray<float>("boneWeights", v);
            }
    }
    std::vector<std::uint8_t> modBytes;
    Check(dec::SerializeGraph(mod, modBytes, err), "serialize the mod graph: " + err);
    Check(dec::ParseHkyReflective(*arc->source(root), reg, twin, err, &rigA, root), "parse the base twin: " + err);
    std::map<std::string, std::unique_ptr<dec::ParsedHky>> touched;
    std::vector<std::string> mlog;
    Check(mr.Apply(root, twin, "MyMod", modBytes, touched, mlog, err), "apply the converter rules to the mod graph: " + err);
    Check(touched.size() == 2, "the mod adds data to both running characters");
    for (const auto& [ck, cu] : touched) {
        std::set<std::string> added;
        dec::ParsedHky base;
        Check(dec::ParseHkyReflective(*arc->source(ck), reg, base, err, &bones.at(ck), ck), "parse base " + ck);
        std::set<std::string> had;
        for (const auto& p : rl::ReadBoneProperties(base)) had.insert(p.name);
        for (const auto& p : rl::ReadBoneProperties(*cu)) if (!had.count(p.name)) added.insert(p.name);
        Check(added == std::set<std::string>{ "MyMod_Blend_0", "Bone_B" },
              ck + ": the mod's new mask takes a bundle-named property, its new bone ref the bone's own");
    }

    dec::HkyPatchStats st;
    const fs::path patchDir = tmp / "hoist-patch";
    Check(dec::DeriveHkyPatch(twin, { "skyrim" }, { "mymod", "skyrim" }, modBytes, reg, patchDir.string(), err, &rigA, &st),
          "derive the mod's patch against the hoisted base: " + err);
    std::string patch;
    for (const auto& [p, t] : Tree(patchDir)) patch += t;
    Check(patch.find("MyMod_Blend_0") != std::string::npos && patch.find("Bone_B") != std::string::npos,
          "the patch binds the new properties:\n" + patch);
    Check(patch.find("Blend_1") == std::string::npos && patch.find("UpperBody") == std::string::npos,
          "the masks the mod left alone derive to no change:\n" + patch);
}
}  // namespace

void RunRulesTests(const schema::SchemaRegistry& reg) {
    const fs::path tmp = fs::temp_directory_path() / ("cb-engine-tests-" + std::to_string(_getpid()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    DescriptorChecks();
    OrderChecks(tmp);
    UnitSetChecks(reg, tmp);
    GateRoundTrip(reg, tmp);
    HoistChecks(reg, tmp);
    fs::remove_all(tmp, ec);
}

}  // namespace CB::engine::tests
