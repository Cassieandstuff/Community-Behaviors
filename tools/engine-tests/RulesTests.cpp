/// @brief The rule framework (CB::engine::rules) over a small hand-written unit set: descriptor checks, run order, the
///        project -> character -> graph relations, re-emit fidelity, and a reversible test op through the regen's
///        apply / re-emit / reverse-for-the-gate sequence.

#include "TestSupport.h"

#include <decompile/BehaviorDecompile.h>
#include <disk/Disk.h>
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
}  // namespace

void RunRulesTests(const schema::SchemaRegistry& reg) {
    const fs::path tmp = fs::temp_directory_path() / ("cb-engine-tests-" + std::to_string(_getpid()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    DescriptorChecks();
    OrderChecks(tmp);
    UnitSetChecks(reg, tmp);
    GateRoundTrip(reg, tmp);
    fs::remove_all(tmp, ec);
}

}  // namespace CB::engine::tests
