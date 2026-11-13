/// @file
/// @brief The dynamic generators: the pick rules, the condition type table, and the schema shapes the plugin relies on.

#include "TestSupport.h"

#include <compile/DynamicClips.h>
#include <decompile/BehaviorDecompile.h>
#include <relay/ConditionText.h>
#include <relay/DynamicClip.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace CB::engine::tests {

namespace {

namespace rl = relay;

void PickChecks() {
    using C = rl::ClipCandidate;
    const std::vector<C> none{ { 5, 1, 0, false }, { 1, 1, 0, false } };
    Check(rl::PickClip(none, 1) == -1, "no passing row: the fallback");

    const std::vector<C> tiers{ { 0, 100, 0, true }, { 9, 1, 0, true }, { 9, 0, 0, false }, { 3, 50, 0, true } };
    for (std::uint32_t s = 0; s < 64; ++s) Check(rl::PickClip(tiers, rl::PickSeed(7, s, 0)) == 1, "the top priority wins whatever its weight");

    const std::vector<C> draw{ { 2, 1, 0, true }, { 2, 3, 0, true }, { 1, 100, 0, true } };
    int counts[3] = {};
    for (std::uint32_t s = 0; s < 4000; ++s) ++counts[rl::PickClip(draw, rl::PickSeed(42, s, 0))];
    Check(counts[2] == 0, "a lower tier never draws");
    Check(counts[1] > counts[0] * 2 && counts[1] < counts[0] * 4, "weights 1:3 draw about 1:3");
    Check(rl::PickClip(draw, 12345) == rl::PickClip(draw, 12345), "one seed, one pick");
    Check(rl::PickSeed(1, 2, 3) != rl::PickSeed(2, 2, 3) && rl::PickSeed(1, 2, 3) != rl::PickSeed(1, 3, 3), "seeds differ by actor and activation");

    const std::vector<C> zero{ { 0, 0, 0, true }, { 0, 0, 0, true } };
    bool seen[2] = {};
    for (std::uint32_t s = 0; s < 64; ++s) seen[rl::PickClip(zero, rl::PickSeed(0, s, 0))] = true;
    Check(seen[0] && seen[1], "all weights zero: an even draw");
}

void RepickChecks() {
    using C = rl::ClipCandidate;
    using M = rl::Moment;
    std::vector<C> rows{ { 1, 1, 0, true }, { 2, 1, rl::kInterruptible, true } };
    Check(!rl::ShouldRepick(rows, 0, M::Update), "a non-interruptible row plays out");
    Check(!rl::ShouldRepick(rows, 1, M::Update), "an interruptible row at the top keeps playing");
    rows[1].passes = false;
    Check(rl::ShouldRepick(rows, 1, M::Update), "an interruptible row that stops passing gives way");
    rows[0].flags = rl::kInterruptible;
    rows[1]       = { 5, 1, 0, true };
    Check(rl::ShouldRepick(rows, 0, M::Update), "an interruptible row gives way to a higher priority");
    Check(!rl::ShouldRepick(rows, 0, M::Loop), "a loop without RedrawOnLoop keeps the clip");

    std::vector<C> loop{ { 0, 1, rl::kRedrawOnLoop, true }, { 0, 1, 0, true } };
    Check(rl::ShouldRepick(loop, 0, M::Loop), "RedrawOnLoop draws again");
    loop[0].flags |= rl::kKeepRandomOnLoop;
    Check(!rl::ShouldRepick(loop, 0, M::Loop), "KeepRandomOnLoop keeps a clip that still qualifies");
    loop[0].passes = false;
    Check(rl::ShouldRepick(loop, 0, M::Loop), "KeepRandomOnLoop re-picks once the clip stops qualifying");

    const std::vector<C> some{ { 0, 1, 0, true } };
    Check(!rl::ShouldRepick(some, -1, M::Update), "the fallback plays out mid-clip");
    Check(rl::ShouldRepick(some, -1, M::Loop), "the fallback gives way at its loop once a row passes");
    Check(!rl::ShouldRepick(std::vector<C>{ { 0, 1, 0, false } }, -1, M::Loop), "the fallback stays while none passes");
}

void FormKeyChecks() {
    const auto k = rl::ParseFormKey("Skyrim.esm|0x0001E711");
    Check(k && k->plugin == "Skyrim.esm" && k->id == 0x1E711, "a FormKey parses, plugin case kept");
    Check(rl::ParseFormKey("My Mod.esp|ABC").has_value(), "the 0x is optional");
    Check(!rl::ParseFormKey("Skyrim.esm").has_value() && !rl::ParseFormKey("|0x1").has_value() &&
              !rl::ParseFormKey("a.esp|0x1000000").has_value() && !rl::ParseFormKey("a.esp|zz").has_value(),
          "malformed FormKeys are refused");
}

void TypeChecks(const std::string& schemaDir) {
    std::ifstream      in(std::filesystem::path(schemaDir) / "metadata" / "conditions.json", std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    std::string err;
    const auto  types = rl::ParseConditionTypes(text.str(), err);
    Check(types.has_value(), "conditions.json parses: " + err);
    if (!types) return;

    for (const auto& [name, _] : types->scopes)
        Check(std::find(rl::EvaluatedScopes().begin(), rl::EvaluatedScopes().end(), name) != rl::EvaluatedScopes().end(),
              "scope '" + name + "' in conditions.json is one the plugin evaluates");
    for (const auto& [name, _] : types->tests)
        Check(std::find(rl::EvaluatedTests().begin(), rl::EvaluatedTests().end(), name) != rl::EvaluatedTests().end(),
              "test '" + name + "' in conditions.json is one the plugin evaluates");
    Check(types->scopes.size() == rl::EvaluatedScopes().size() && types->tests.size() == rl::EvaluatedTests().size(),
          "every scope and test the plugin evaluates is typed in conditions.json");

    const auto chain = [](std::initializer_list<const char*> k) { return std::vector<std::string>(k.begin(), k.end()); };
    Check(types->Check(chain({ "equipped.right" }), "HasKeyword").empty(), "equipped.right has keywords");
    Check(types->Check(chain({ "combatTarget", "equipped.left" }), "HasKeyword").empty(), "scopes chain through an actor");
    Check(types->Check(chain({ "alias" }), "IsSame").empty(), "an alias compares by IsSame");
    Check(!types->Check(chain({ "alias", "equipped.right" }), "HasKeyword").empty(), "an alias is a reference, not yet an actor");
    Check(!types->Check(chain({ "race", "equipped.right" }), "HasKeyword").empty(), "a race has no equipment");
    Check(!types->Check(chain({ "worn" }), "ActorValue").empty(), "armor has no actor values");
    Check(!types->Check(chain({ "self", "alias" }), "Exists").empty(), "alias starts a chain");
    Check(!types->Check(chain({ "self", "mount", "mount", "mount", "mount" }), "Exists").empty(), "chains are capped");
    Check(types->Check({}, "ActorValue").empty(), "an empty chain is the actor itself");

    using K = rl::ConditionNode::Kind;
    std::string e;
    auto n = rl::ParseConditionText(R"(equipped.right.HasKeyword("Skyrim.esm|0x0001E711"))", *types, e);
    Check(n && n->kind == K::Scoped && n->scope.size() == 1 && n->scope[0].kind == "equipped.right" && n->test == "HasKeyword" &&
              n->arguments == std::vector<std::string>{ "Skyrim.esm|0x0001E711" },
          "a dotted scope then a test: " + e);
    n = rl::ParseConditionText(R"(IsSame(alias("MyScene.esp|0x000D62", 3)) && !(combatTarget.equipped.left.Exists || all.worn(32).HasKeyword("a.esp|0x1")))", *types, e);
    Check(n && n->kind == K::And && n->children.size() == 2, "&& and ! nest: " + e);
    if (n && n->children.size() == 2) {
        const auto& same = n->children[0];
        Check(same.scope.empty() && same.test == "IsSame" && same.other.size() == 1 && same.other[0].kind == "alias" &&
                  same.other[0].arguments == std::vector<std::string>{ "MyScene.esp|0x000D62", "3" },
              "IsSame takes a chain as its other");
        const auto& no = n->children[1];
        Check(no.kind == K::Not && no.children.size() == 1 && no.children[0].kind == K::Or && no.children[0].children.size() == 2 &&
                  no.children[0].children[1].all && no.children[0].children[1].scope[0].arguments == std::vector<std::string>{ "32" },
              "! over a parenthesised ||, all. sets the quantifier");
        const std::string formatted = rl::FormatConditionText(*n);
        const auto        back = rl::ParseConditionText(formatted, *types, e);
        Check(back && rl::FormatConditionText(*back) == formatted, "the text form round-trips: " + formatted + " " + e);
    }
    Check(rl::ParseConditionText("ActorValue(\"Health\", <, 50)", *types, e) && e.empty(), "an empty chain is the actor: " + e);
    Check(!rl::ParseConditionText("race.equipped.right.HasKeyword(\"a.esp|0x1\")", *types, e) && !e.empty(), "a mistyped chain is refused");
    Check(!rl::ParseConditionText("Equipped.right.HasKeyword(\"a.esp|0x1\")", *types, e), "names are byte-exact: no case folding");
    Check(!rl::ParseConditionText("HasKeyword()", *types, e), "a missing argument is refused");
    Check(!rl::ParseConditionText("Exists && ", *types, e), "a dangling && is refused");
}

void SchemaChecks(const schema::SchemaRegistry& reg) {
    std::string err;
    Check(reg.ComputeSize("CBDynamicClipGenerator", &err) == 176, "CBDynamicClipGenerator is 176 bytes: " + err);
    Check(reg.ComputeSize("CBDynamicBehaviorReferenceGenerator", &err) == 104, "CBDynamicBehaviorReferenceGenerator is 104 bytes: " + err);
    Check(reg.ComputeSize("CBDynamicClipRow", &err) == 32, "a row is 32 bytes: " + err);
    Check(reg.ComputeSize("CBScopedCondition", &err) == 80, "CBScopedCondition is 80 bytes: " + err);
    Check(reg.ComputeSize("CBConditionAnd", &err) == 32 && reg.ComputeSize("CBConditionOr", &err) == 32 &&
              reg.ComputeSize("CBConditionNot", &err) == 24,
          "the condition tree classes: " + err);
    Check(!reg.Derives("CBDynamicClipGenerator", "hkbClipGenerator"), "the dynamic clip generator is not a clip generator");
    for (const char* cls : { "CBDynamicClipGenerator", "CBDynamicBehaviorReferenceGenerator", "CBScopedCondition", "CBConditionAnd" }) {
        const schema::ClassSchema* cs = reg.Find(cls);
        Check(cs && cs->signature != 0, std::string(cls) + " has a derived signature");
    }

    const std::vector<int> offsets = reg.OwnFieldOffsets("CBDynamicClipGenerator", &err);
    const schema::ClassSchema* cs  = reg.Find("CBDynamicClipGenerator");
    for (std::size_t i = 0; cs && i < offsets.size() && i < cs->fields.size(); ++i) {
        const std::string& n = cs->fields[i].name;
        if (n == "triggers") Check(offsets[i] == 0x48, "triggers at 0x48");
        if (n == "mode") Check(offsets[i] == 0x68, "mode at 0x68");
        if (n == "folder") Check(offsets[i] == 0x70, "folder at 0x70");
        if (n == "fallback") Check(offsets[i] == 0x78, "fallback at 0x78");
        if (n == "blendDuration") Check(offsets[i] == 0x80, "blendDuration at 0x80");
        if (n == "rows") Check(offsets[i] == 0x88, "rows at 0x88");
        if (n == "fallbackClip") Check(offsets[i] == 0x98, "fallbackClip at 0x98");
    }
}

/// @brief A clip's selection data reads from its animation unit's `selection:` map, its condition tree inline.
void SelectionChecks(const schema::SchemaRegistry& reg) {
    const std::string yaml =
        "priority: 10\n"
        "weight: 2.5\n"
        "flags: FLAG_INTERRUPTIBLE|FLAG_REDRAW_ON_LOOP\n"
        "condition:\n"
        "  class: CBConditionAnd\n"
        "  conditions:\n"
        "    - class: CBScopedCondition\n"
        "      test: IsSame\n"
        "      other:\n"
        "        - kind: alias\n"
        "          arguments: ['MyScene.esp|0x000D62', '3']\n"
        "    - class: CBConditionNot\n"
        "      condition:\n"
        "        class: CBScopedCondition\n"
        "        scope:\n"
        "          - kind: equipped.right\n"
        "        test: HasKeyword\n"
        "        arguments: ['Skyrim.esm|0x1E711']\n";
    std::string err;
    const auto  sel = decompile::ParseClipSelection(yaml, reg, "test", err);
    Check(sel != nullptr, "a selection map parses: " + err);
    if (!sel) return;
    Check(sel->GetInt("priority") == 10, "priority reads");
    Check(sel->GetFloat("weight") == 2.5f, "weight reads");
    Check(sel->GetInt("flags") == 3, "flags read by name");
    const auto* cond = sel->Child("condition");
    Check(cond && std::string(cond->ClassName()) == "CBConditionAnd", std::string("the condition tree is inline, class-tagged: ") + (cond ? cond->ClassName() : "null"));
}


/// @brief The compile fill: one row per clip of the folder, sorted, each a clip generator with the generator's settings.
void FillChecks(const schema::SchemaRegistry& reg) {
    decompile::ParsedHky graph;
    decompile::GraphEdit edit(graph, reg);
    auto gen = edit.Make("CBDynamicClipGenerator");
    Check(gen != nullptr, "the generator is made");
    if (!gen) return;
    graph.root = gen;
    gen->Str("name")     = "Idle";
    gen->Str("folder")   = "Idles/Random";
    gen->Str("fallback") = "Animations/IdleStand.hkx";
    gen->SetFloat("playbackSpeed", 1.5f);

    std::string seenRoot, seenFolder;
    const compile::ClipLister list = [&](std::string_view root, std::string_view folder) {
        seenRoot   = std::string(root);
        seenFolder = std::string(folder);
        return std::vector<compile::FolderClip>{
            { "Animations/Idles/Random/B.hkx", "animation:\n  selection:\n    priority: 4\n    weight: 3\n" },
            { "Animations/Idles/Random/A.hkx", "" },
            { "Animations/Idles/Random/C.hkx", "animation:\n  selection:\n    priority: [\n" },
        };
    };
    const auto report = compile::FillDynamicClipTables(edit, "meshes/actors/character/behaviors/0_master.hkx", list);
    Check(seenRoot == "meshes/actors/character/" && seenFolder == "Idles/Random", "the lister gets the project and the folder as authored");
    Check(report.generators == 1 && report.rows == 3, "one row per clip");
    Check(report.warnings.size() == 1, "a malformed selection warns and keeps the defaults");
    const auto& rows = gen->FieldRef("rows").objs;
    if (rows.size() != 3) return;
    const auto row = [&](std::size_t i) { return std::static_pointer_cast<codec::io::SchemaObject>(rows[i]); };
    const auto clip = [&](std::size_t i) { return row(i)->Child("clip"); };
    Check(clip(0)->Str("animationName") == "Animations/Idles/Random/A.hkx", "rows sorted by name, names byte-exact");
    Check(row(0)->GetInt("priority") == 0 && row(0)->GetFloat("weight") == 1.0f, "a binary clip has the default selection");
    Check(row(1)->GetInt("priority") == 4 && row(1)->GetFloat("weight") == 3.0f, "a unit's selection reaches its row");
    Check(clip(1)->GetFloat("playbackSpeed") == 1.5f, "the clip carries the generator's settings");
    const auto* fb = gen->Child("fallbackClip");
    Check(fb && fb->Str("animationName") == "Animations/IdleStand.hkx", "the fallback clip is made from `fallback`");
    Check(compile::DynamicClipFolders(edit) == std::vector<std::string>{ "Idles/Random" }, "the folders are listed for rostering");
    Check(compile::ProjectRootOf("a.hkx").empty(), "a shallow path has no project");
}

}  // namespace

void RunDynamicClipTests(const schema::SchemaRegistry& reg, const std::string& schemaDir) {
    PickChecks();
    RepickChecks();
    FormKeyChecks();
    TypeChecks(schemaDir);
    SchemaChecks(reg);
    SelectionChecks(reg);
    FillChecks(reg);
}

}  // namespace CB::engine::tests
