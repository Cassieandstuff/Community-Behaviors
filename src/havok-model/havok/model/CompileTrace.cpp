#include "havok/model/CompileTrace.h"

#include "havok/model/BehaviorData.h"

#include <RymlInclude.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace havok::model::trace {

namespace {
    // A loaded probe definition (Havok/core/Schema/debug/*.yaml). Empty criterion = "any".
    struct Probe {
        bool                     enabled = true;
        std::vector<std::string> phases;    // trace phases to keep (vars/bind/edge/merge/visit)
        std::vector<std::string> classes;   // node classes to keep
        std::vector<std::string> match;     // substrings; any must occur in unit/class/name/field/detail
    };

    Sink               g_sink;      // null by default -> disabled
    std::string        g_filter;    // legacy substring filter (used only when no probes are loaded)
    std::vector<Probe> g_probes;    // schema-driven probes; non-empty => they gate Rec()

    bool inList(const std::vector<std::string>& v, std::string_view s) {
        if (v.empty()) return true;   // empty criterion = any
        for (const auto& e : v) if (e == s) return true;
        return false;
    }
    bool anySubstr(const std::vector<std::string>& subs, std::initializer_list<std::string_view> fields) {
        if (subs.empty()) return true;
        for (const auto& sub : subs)
            for (std::string_view f : fields)
                if (f.find(sub) != std::string_view::npos) return true;
        return false;
    }
    bool probesAllow(std::string_view phase, std::string_view unit, std::string_view cls,
                     std::string_view name, std::string_view field, std::string_view detail) {
        for (const auto& p : g_probes) {
            if (!p.enabled) continue;
            if (inList(p.phases, phase) && inList(p.classes, cls) &&
                anySubstr(p.match, { unit, cls, name, field, detail }))
                return true;
        }
        return false;
    }
    // ryml helper: read a scalar or seq child into a string vector.
    void readSeq(const c4::yml::ConstNodeRef& n, const char* key, std::vector<std::string>& out) {
        if (!n.is_map() || !n.has_child(c4::to_csubstr(key))) return;
        auto c = n[c4::to_csubstr(key)];
        if (c.is_seq()) { for (auto e : c) if (e.has_val()) { std::string s; c4::from_chars(e.val(), &s); out.push_back(std::move(s)); } }
        else if (c.has_val()) { std::string s; c4::from_chars(c.val(), &s); if (!s.empty()) out.push_back(std::move(s)); }
    }
}

void SetSink(Sink sink) { g_sink = std::move(sink); }

bool Enabled() noexcept { return static_cast<bool>(g_sink); }

void SetFilter(std::string substr) { g_filter = std::move(substr); }

void        ClearProbes()         { g_probes.clear(); }
std::size_t ProbeCount() noexcept { return g_probes.size(); }

std::size_t LoadProbes(const std::string& debugDir, std::string* warn) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(debugDir, ec)) { if (warn) *warn += "debug dir not found: " + debugDir + "\n"; return 0; }
    std::size_t loaded = 0;
    for (auto it = fs::directory_iterator(debugDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        const std::string ext = p.extension().string();
        if (ext != ".yaml" && ext != ".yml") continue;
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        std::string text = ss.str();
        try {
            c4::yml::Tree t = c4::yml::parse_in_place(c4::to_substr(text));
            auto root = t.rootref();
            Probe pr;
            if (root.is_map() && root.has_child("enabled")) {
                std::string e; c4::from_chars(root["enabled"].val(), &e);
                pr.enabled = (e == "true" || e == "1");
            }
            readSeq(root, "phases",  pr.phases);
            readSeq(root, "classes", pr.classes);
            readSeq(root, "match",   pr.match);
            g_probes.push_back(std::move(pr));
            ++loaded;
        } catch (const std::exception& e) {
            if (warn) *warn += "probe parse failed (" + p.filename().string() + "): " + e.what() + "\n";
        }
    }
    return loaded;
}

void Line(std::string_view line) {
    if (g_sink) g_sink(line);
}

void Rec(std::string_view phase, std::string_view unit, std::string_view cls,
         std::string_view name, std::string_view field, std::string_view action,
         std::string_view detail) {
    if (!g_sink) return;
    if (!g_probes.empty()) {
        // Schema-driven probes gate everything once loaded.
        if (!probesAllow(phase, unit, cls, name, field, detail)) return;
    } else if (!g_filter.empty()) {
        const auto hit = [&](std::string_view s) { return s.find(g_filter) != std::string_view::npos; };
        if (!hit(unit) && !hit(cls) && !hit(name)) return;
    }
    const auto dash = [](std::string_view s) { return s.empty() ? std::string_view{"-"} : s; };
    std::string line = "[CC] ";
    line.append(dash(phase));  line.push_back(' ');
    line.append(dash(unit));   line.push_back(' ');
    line.append(dash(cls));    line.push_back('#');
    line.append(dash(name));   line.push_back(' ');
    line.append(dash(field));  line.push_back(' ');
    line.append(dash(action));
    if (!detail.empty()) { line.push_back(' '); line.append(detail); }
    g_sink(line);
}

} // namespace havok::model::trace

namespace havok::model {

namespace {
// Emit one "bind" record per binding of every node in a (name -> Def) map. Any Def carrying `.bindings`
// (all generator/modifier/state Defs do) works uniformly.
// A node's human name (the Def.name field) when present, else the map key (id/symbol). The map key is
// the node ID (e.g. "2816", "bfco$960"); Def.name is the readable name (e.g. "AttackForwardSprint_MG").
template <class Def>
std::string nodeLabel(const std::string& key, const Def& d) {
    return d.name.empty() ? key : d.name;
}

// id/symbol -> human name, for resolving edge TARGETS (which are stored as ids) to readable names.
using IdName = std::unordered_map<std::string, std::string>;
template <class M>
void collectNames(const M& m, IdName& out) {
    for (const auto& [key, d] : m) if (!d.name.empty()) out.emplace(key, d.name);
}

template <class M>
void dumpBindings(const M& m, const char* cls, std::string_view unit) {
    for (const auto& [key, def] : m) {
        if (!def.bindings) continue;
        for (const auto& b : *def.bindings) {
            std::string d = "var='";
            d += (b.variable ? *b.variable : std::string("-"));
            d += "' idx=" + std::to_string(b.variableIndex) + " id=" + key;
            trace::Rec("bind", unit, cls, nodeLabel(key, def), b.memberPath, "binding", d);
        }
    }
}
} // namespace

void TraceGraph(const BehaviorData& bd, std::string_view unit) {
    if (!trace::Enabled()) return;

    // (1) the variable table — the slots bindings index into (idx = position in graphData->variables).
    if (bd.graphData) {
        int i = 0;
        for (const auto& v : bd.graphData->variables) {
            trace::Rec("vars", unit, "-", v.name, "-", "table", "idx=" + std::to_string(i));
            ++i;
        }
    }

    // (2) every node's bindings — name + the index it carries. grep a variable name to see whether the
    //     binding index matches the table slot the name occupies above.
    dumpBindings(bd.clips,                       "hkbClipGenerator",              unit);
    dumpBindings(bd.blenders,                    "hkbBlenderGenerator",          unit);
    dumpBindings(bd.selectors,                   "hkbManualSelectorGenerator",   unit);
    dumpBindings(bd.stateMachines,               "hkbStateMachine",              unit);
    dumpBindings(bd.states,                      "hkbStateMachineStateInfo",     unit);
    dumpBindings(bd.transitionEffects,           "hkbBlendingTransitionEffect",  unit);
    dumpBindings(bd.modifierGenerators,          "hkbModifierGenerator",         unit);
    dumpBindings(bd.isActiveModifiers,           "BSIsActiveModifier",           unit);
    dumpBindings(bd.modifierLists,               "hkbModifierList",              unit);
    dumpBindings(bd.eventDrivenModifiers,        "hkbEventDrivenModifier",       unit);
    dumpBindings(bd.genericModifiers,            "hkbGenerateModifier",          unit);
    dumpBindings(bd.evaluateExpressionModifiers, "hkbEvaluateExpressionModifier",unit);
    dumpBindings(bd.footIkModifiers,             "hkbFootIkModifier",            unit);
    dumpBindings(bd.iStateManagerModifiers,      "BSIStateManagerModifier",      unit);
    dumpBindings(bd.stateTaggingGenerators,      "BSiStateTaggingGenerator",     unit);
    dumpBindings(bd.cyclicBlendGenerators,       "BSCyclicBlendTransitionGenerator", unit);
    dumpBindings(bd.synchronizedClips,           "BSSynchronizedClipGenerator",  unit);
    dumpBindings(bd.boneSwitchGenerators,        "BSBoneSwitchGenerator",        unit);
    dumpBindings(bd.offsetAnimGenerators,        "BSOffsetAnimationGenerator",   unit);
    dumpBindings(bd.poseMatchingGenerators,      "hkbPoseMatchingGenerator",     unit);
    dumpBindings(bd.referencePoseGenerators,     "hkbReferencePoseGenerator",    unit);
    dumpBindings(bd.behaviorReferences,          "hkbBehaviorReferenceGenerator",unit);

    // (3) generator edges / node references — the graph TOPOLOGY. grep an attack state to see whether it
    //     routes to a *ForwardSprint_MG commitment generator or a bare generator; grep a state machine to
    //     see its states + wildcard transitions (event -> toState). Records: "edge" phase.
    // id/symbol -> human name, so edge targets (stored as ids) render readable.
    IdName idName;
    collectNames(bd.clips, idName);                 collectNames(bd.blenders, idName);
    collectNames(bd.selectors, idName);             collectNames(bd.stateMachines, idName);
    collectNames(bd.states, idName);                collectNames(bd.transitionEffects, idName);
    collectNames(bd.modifierGenerators, idName);    collectNames(bd.isActiveModifiers, idName);
    collectNames(bd.modifierLists, idName);         collectNames(bd.eventDrivenModifiers, idName);
    collectNames(bd.genericModifiers, idName);      collectNames(bd.evaluateExpressionModifiers, idName);
    collectNames(bd.footIkModifiers, idName);       collectNames(bd.iStateManagerModifiers, idName);
    collectNames(bd.stateTaggingGenerators, idName);collectNames(bd.cyclicBlendGenerators, idName);
    collectNames(bd.synchronizedClips, idName);     collectNames(bd.boneSwitchGenerators, idName);
    collectNames(bd.offsetAnimGenerators, idName);  collectNames(bd.poseMatchingGenerators, idName);
    collectNames(bd.referencePoseGenerators, idName);collectNames(bd.behaviorReferences, idName);
    const auto tgtLabel = [&](const std::string& id) {
        auto it = idName.find(id);
        return it != idName.end() ? (it->second + " [" + id + "]") : id;
    };
    const auto edge = [&](const char* cls, const std::string& name, const std::string& role, const std::string& target) {
        if (!target.empty() && target != "null") trace::Rec("edge", unit, cls, name, role, "->", tgtLabel(target));
    };
    const auto transTo = [](const TransitionInfoDef& t) {
        return t.toState ? *t.toState : ("state#" + std::to_string(t.toStateId));
    };
    const auto transEv = [](const TransitionInfoDef& t) {
        return t.event ? *t.event : ("event#" + std::to_string(t.eventId));
    };
    // The fields that distinguish otherwise-similar combo-stage transitions (same event+toState): the
    // nested-state stage, priority, effect, and any gating condition / trigger-interval window. Without
    // these two combo transitions look like duplicates when they are not.
    const auto transTail = [](const TransitionInfoDef& t) {
        std::string s = " nested=" + std::to_string(t.toNestedStateId) + " prio=" + std::to_string(t.priority);
        if (!t.transition.empty()) s += " via " + t.transition;
        if (t.condition)           s += " cond=" + *t.condition;
        if (t.triggerInterval.enterEvent) s += " trig=" + *t.triggerInterval.enterEvent;
        return s;
    };
    for (const auto& [n, d] : bd.states) {
        const std::string src = nodeLabel(n, d);
        edge("hkbStateMachineStateInfo", src, "generator", d.generator);
        if (d.parsedTransitions)
            for (const auto& t : *d.parsedTransitions)
                trace::Rec("edge", unit, "hkbStateMachineStateInfo", src, "trans[" + transEv(t) + "]", "->",
                           transTo(t) + transTail(t));
    }
    for (const auto& [n, d] : bd.stateMachines) {
        const std::string src = nodeLabel(n, d);
        for (std::size_t i = 0; i < d.states.size(); ++i)
            edge("hkbStateMachine", src, "states[" + std::to_string(i) + "]", d.states[i]);
        if (d.parsedWildcardTransitions)
            for (const auto& t : *d.parsedWildcardTransitions)
                trace::Rec("edge", unit, "hkbStateMachine", src, "wildcard[" + transEv(t) + "]", "->", transTo(t) + transTail(t));
    }
    for (const auto& [n, d] : bd.modifierGenerators) {
        const std::string src = nodeLabel(n, d);
        edge("hkbModifierGenerator", src, "generator", d.generator);
        edge("hkbModifierGenerator", src, "modifier",  d.modifier);
    }
    for (const auto& [n, d] : bd.modifierLists)
        for (std::size_t i = 0; i < d.modifiers.size(); ++i)
            edge("hkbModifierList", nodeLabel(n, d), "modifiers[" + std::to_string(i) + "]", d.modifiers[i]);
    for (const auto& [n, d] : bd.selectors)
        for (std::size_t i = 0; i < d.generators.size(); ++i)
            edge("hkbManualSelectorGenerator", nodeLabel(n, d), "generators[" + std::to_string(i) + "]", d.generators[i]);
    for (const auto& [n, d] : bd.blenders)
        for (std::size_t i = 0; i < d.children.size(); ++i)
            edge("hkbBlenderGenerator", nodeLabel(n, d), "children[" + std::to_string(i) + "]", d.children[i].generator);
    for (const auto& [n, d] : bd.eventDrivenModifiers)
        edge("hkbEventDrivenModifier", nodeLabel(n, d), "modifier", d.modifier);
}

} // namespace havok::model
