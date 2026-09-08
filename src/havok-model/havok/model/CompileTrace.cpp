#include "havok/model/CompileTrace.h"

#include "havok/model/BehaviorData.h"

#include <string>

namespace havok::model::trace {

namespace {
    Sink        g_sink;      // null by default -> disabled
    std::string g_filter;    // empty -> no filter
}

void SetSink(Sink sink) { g_sink = std::move(sink); }

bool Enabled() noexcept { return static_cast<bool>(g_sink); }

void SetFilter(std::string substr) { g_filter = std::move(substr); }

void Line(std::string_view line) {
    if (g_sink) g_sink(line);
}

void Rec(std::string_view phase, std::string_view unit, std::string_view cls,
         std::string_view name, std::string_view field, std::string_view action,
         std::string_view detail) {
    if (!g_sink) return;
    if (!g_filter.empty()) {
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
template <class M>
void dumpBindings(const M& m, const char* cls, std::string_view unit) {
    for (const auto& [name, def] : m) {
        if (!def.bindings) continue;
        for (const auto& b : *def.bindings) {
            std::string d = "var='";
            d += (b.variable ? *b.variable : std::string("-"));
            d += "' idx=" + std::to_string(b.variableIndex);
            trace::Rec("bind", unit, cls, name, b.memberPath, "binding", d);
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
}

} // namespace havok::model
