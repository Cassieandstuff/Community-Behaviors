// BehaviorDecompiler — import direction for behaviors: an hkbBehaviorGraph object
// graph -> the multi-file behavior source tree (behavior.yaml + data/graphdata.yaml
// + per-node YAML in typed subdirs) that YamlBehaviorLoader reads. Inverse of
// BehaviorBuilder. Node coverage is incremental; an unsupported class aborts with
// a clear message. Verified by a byte-identical compile->decompile->recompile
// round-trip (self-validating, no C# reference).

#include "havok/sct/BehaviorDecompiler.h"    // DecompileBehaviorTree / DecompileNativeDelta
#include "havok/sct/CharacterDecompiler.h"   // DecompileResult

#include "havok/classes/Classes.h"
#include "havok/classes/gen/ClassesGen.h"
#include "havok/model/HavokEnums.h"
#include "havok/sct/BoneNames.h"   // BoneNameTable — bone index -> name (optional)
#include "havok/cross/Cross.h"     // cross-kind membrane (enum reverse; shared with the schema path)

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace havok::sct {
namespace fs = std::filesystem;
namespace en = havok::model::enums;
namespace {

std::string fstr(float v) { char b[32]; std::snprintf(b, sizeof b, "%.9g", v); return b; }
std::string boolstr(bool v) { return v ? "true" : "false"; }
std::string revEnum(const std::unordered_map<std::string, long>& t, long v, const char* fb) {
    for (const auto& [k, val] : t) if (val == v) return k;
    return fb;
}
// Enum value -> name, else the raw number (ResolveEnum parses numbers back), so it always
// round-trips. Via the shared membrane codec (havok::cross::enumName) — one rule with the schema
// path's revNum, and deterministic on aliases (B5).
std::string revNum(const std::unordered_map<std::string, long>& t, long v) {
    std::string nm = havok::cross::enumName(v, t);
    return nm.empty() ? std::to_string(v) : nm;
}
std::string q(const std::string& s) {
    // Fast path: no control chars -> YAML single-quoted (doubling any ' ). Some vanilla
    // creature graphs carry control chars in names (troll ships an event literally named
    // "SoundPlay.NPCTrollAttackB\r\n"); a raw \r\n would break single-quoted YAML, so emit
    // those as double-quoted with escapes, which ryml decodes back byte-for-byte on load.
    bool ctrl = false;
    for (unsigned char c : s) if (c < 0x20) { ctrl = true; break; }
    if (!ctrl) {
        std::string o = "'";
        for (char c : s) { if (c == '\'') o += "''"; else o += c; }
        return o + "'";
    }
    std::string o = "\"";
    char buf[8];
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) { std::snprintf(buf, sizeof buf, "\\x%02X", c); o += buf; }
                else o += static_cast<char>(c);
        }
    }
    return o + "\"";
}
// Behavior generic-modifier vec/quat params are "(x y z w)" strings (parsed by
// pv4/pq4), NOT [a,b,c,d] seqs (that's the character loader).
std::string pvec(const Vector4& v) { return "'(" + fstr(v.x) + " " + fstr(v.y) + " " + fstr(v.z) + " " + fstr(v.w) + ")'"; }
std::string pquat(const Quaternion& v) { return "'(" + fstr(v.x) + " " + fstr(v.y) + " " + fstr(v.z) + " " + fstr(v.w) + ")'"; }
// m_payload is the base hkbEventPayload; the string lives on hkbStringEventPayload.
std::string payloadStr(const std::shared_ptr<hkbEventPayload>& p) {
    if (auto sp = std::dynamic_pointer_cast<hkbStringEventPayload>(p)) return sp->m_data;
    return "";
}
void writeText(const fs::path& p, const std::string& s) {
    std::error_code ec; fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

struct BehaviorEmitter {
    fs::path dir;
    const hkbBehaviorGraphData* gd = nullptr;
    std::set<const void*> visited;

    // Optional skeleton: when set, bone-index fields emit the bone NAME; when null they emit the
    // raw number (byte-identical to a no-skeleton decompile). A -1 / out-of-range index always
    // stays numeric.
    const havok::sct::BoneNameTable* bones = nullptr;
    std::string boneRef(int idx) const {
        if (bones)
            if (const std::string* nm = bones->NameOf(idx); nm && !nm->empty()) return q(*nm);
        return std::to_string(idx);
    }

    // Roster index -> name, via the shared membrane primitive (havok::cross::rosterName) — one
    // bounds-checked lookup rule with the schema path's NameResolver.
    std::string eventName(int id) const {
        return (gd && gd->m_stringData) ? havok::cross::rosterName(id, gd->m_stringData->m_eventNames) : "";
    }
    std::string variableName(int idx) const {
        return (gd && gd->m_stringData) ? havok::cross::rosterName(idx, gd->m_stringData->m_variableNames) : "";
    }
    std::string charPropName(int idx) const {
        return (gd && gd->m_stringData) ? havok::cross::rosterName(idx, gd->m_stringData->m_characterPropertyNames) : "";
    }

    // Safe-name variants for emitting SYMBOLIC event/variable references in deltas: the
    // name is returned ONLY if resolving it (first occurrence wins, mirroring the
    // builder's resolveEventId/resolveVariableIndex) maps BACK to the same id — so a
    // duplicate name can never silently remap an id on round-trip. The raw id is only
    // valid for this-bundle-on-vanilla; the runtime merge shifts ids when other mods'
    // vocab additions insert first (bfco's ~6k 1hm interval refs shifted 19 slots ->
    // combo windows opened on the wrong events -> light attacks fell back to vanilla;
    // the same shift on Precision's refs in the other order preceded the char-setup
    // crash). The builder prefers the name against the MERGED vocabulary and falls back
    // to the raw id when absent, exactly like the binding `variable:` names.
    std::unordered_map<std::string, int> evFirst, varFirst;
    bool nameMapsBuilt = false;
    void buildNameMaps() {
        if (nameMapsBuilt || !gd || !gd->m_stringData) return;
        const auto& sd = *gd->m_stringData;
        for (int i = 0; i < static_cast<int>(sd.m_eventNames.size()); ++i)
            evFirst.try_emplace(sd.m_eventNames[i], i);
        for (int i = 0; i < static_cast<int>(sd.m_variableNames.size()); ++i)
            varFirst.try_emplace(sd.m_variableNames[i], i);
        nameMapsBuilt = true;
    }
    std::string safeEventName(int id) {
        const std::string n = eventName(id);
        if (n.empty()) return "";
        buildNameMaps();
        const auto it = evFirst.find(n);
        return (it != evFirst.end() && it->second == id) ? n : "";
    }
    std::string safeVariableName(int idx) {
        const std::string n = variableName(idx);
        if (n.empty()) return "";
        buildNameMaps();
        const auto it = varFirst.find(n);
        return (it != varFirst.end() && it->second == idx) ? n : "";
    }
    // Append `<key>: '<name>'` for an event/variable id when it is safely nameable.
    void evLine(std::string& y, const std::string& ind, const char* key, int id) {
        const std::string n = safeEventName(id);
        if (!n.empty()) y += ind + key + (": " + q(n)) + "\n";
    }
    void varLine(std::string& y, const std::string& ind, const char* key, int idx) {
        const std::string n = safeVariableName(idx);
        if (!n.empty()) y += ind + key + (": " + q(n)) + "\n";
    }
    // Append a nested `event: '<name>'` line at the given indent when the id is safely
    // nameable — the merge-safe companion to a raw `id:` inside an inline-event map
    // (ungroundedEvent / EventToCrossBlend / notify events / generic modifier events).
    // The builder prefers this name against the MERGED event vocab and only falls back to
    // the raw id when absent, so a load-order vocab shift can't misfire the event.
    void inlEvLine(std::string& y, const char* ind, int id) {
        const std::string n = safeEventName(id);
        if (!n.empty()) y += std::string(ind) + "event: " + q(n) + "\n";
    }

    // Inline `bindings:` block from a variable binding set. `ind` is the base indent.
    std::string bindingsBlock(const std::shared_ptr<hkbVariableBindingSet>& bs, const std::string& ind) {
        if (!bs || bs->m_bindings.empty()) return "";
        own(bs.get(), curOwner);   // binding sets are separate binary objects, inlined here
        std::string y = ind + "bindings:\n";
        for (int i = 0; i < static_cast<int>(bs->m_bindings.size()); ++i) {
            const auto& b = bs->m_bindings[i];
            y += ind + "  - memberPath: " + q(b.m_memberPath) + "\n";
            y += ind + "    variableIndex: " + std::to_string(b.m_variableIndex) + "\n";
            // Emit the bound variable / character-property BY NAME. The raw variableIndex is
            // only valid for THIS bundle-on-vanilla; the runtime merge shifts indices when
            // other mods add variables ahead of this one (e.g. Precision's Collision_* push
            // TDM's lean vars up, so a baked index reads the wrong variable → stuck lean).
            // buildBindingSet prefers `variable` and resolves it against the MERGED vocabulary,
            // falling back to variableIndex only when the name is absent. bindingType 1 =
            // CHARACTER_PROPERTY, else VARIABLE. Omit when nameless (nothing to resolve by).
            {
                const std::string vn = (b.m_bindingType == 1)
                    ? charPropName(b.m_variableIndex)
                    : variableName(b.m_variableIndex);
                if (!vn.empty()) y += ind + "    variable: " + q(vn) + "\n";
            }
            y += ind + "    bitIndex: " + std::to_string(b.m_bitIndex) + "\n";
            y += ind + "    bindingType: " + revNum(en::BindingType(), b.m_bindingType) + "\n";
            // m_indexOfBindingToEnable points at this binding — preserve it (the
            // builder can't re-derive it: it's -1 even for sets with an `enable`
            // binding far more often than not).
            if (i == bs->m_indexOfBindingToEnable)
                y += ind + "    enableTarget: true\n";
        }
        return y;
    }

    std::string intervalBlock(const hkbStateMachineTimeInterval& iv, const std::string& ind) {
        std::string y;
        y += ind + "  enterEventId: " + std::to_string(iv.m_enterEventId) + "\n";
        evLine(y, ind + "  ", "enterEvent", iv.m_enterEventId);
        y += ind + "  exitEventId: " + std::to_string(iv.m_exitEventId) + "\n";
        evLine(y, ind + "  ", "exitEvent", iv.m_exitEventId);
        y += ind + "  enterTime: " + fstr(iv.m_enterTime) + "\n";
        y += ind + "  exitTime: " + fstr(iv.m_exitTime) + "\n";
        return y;
    }

    std::string transitionsBlock(const std::shared_ptr<hkbStateMachineTransitionInfoArray>& arr, const std::string& ind);
    void        effect(const std::shared_ptr<hkbTransitionEffect>& e);

    void node(const std::shared_ptr<hkbNode>& n);
    void stateInfo(const std::shared_ptr<hkbStateMachineStateInfo>& s);   // NOT an hkbNode

    // ── Object identity (id-identity refactor Stage 2, docs/id-identity-refactor.md) ──
    // write() uses the object's stable id (from uq, just below) as BOTH the filename and
    // the id: field. Ids are unique, so there is no filename dedup; the name lives in y
    // as a plain label.
    // Delta emit: when `sink` is set, write() stores id -> (sub, full-yaml) in memory
    // instead of touching the filesystem, so the converter can post-process (prune an
    // override to its changed params, keep only this mod's nodes) before writing the
    // native per-mod delta. Null for a normal decompile (writes files exactly as before).
    std::unordered_map<std::string, std::pair<std::string, std::string>>* sink = nullptr;
    std::unordered_map<std::string, int> fileSeq;   // per-sub filename counter — cosmetic; identity is the id: field
    void write(const char* sub, const std::string& id, const std::string& y) {
        if (dryRun) return;                       // pass 1 only assigns editorIds; emits nothing
        const std::string full = "id: " + id + "\n" + y;
        if (sink) { (*sink)[id] = { sub, full }; return; }
        // Filename is a numeric counter, NOT the id: a (class,name) editorId contains ':' and spaces,
        // which are illegal/awkward in filenames. The loader keys off the id: field's content, never
        // the filename, so this is purely cosmetic and collision-free.
        writeText(dir / sub / (std::to_string(fileSeq[sub]++) + ".yaml"), full);
    }

    // Object identity: a stable numeric id per object, assigned on first encounter
    // (definition OR reference, whichever the DFS reaches first). uq() returns that id
    // as a string — used for every filename and reference. Node NAMES are pure labels
    // now (raw m_name, may repeat across objects); identity is the id, so there is no
    // name uniquification. Events/variables stay name-keyed (their identity IS the name).
    std::unordered_map<const void*, int> objIds;
    int nextId = 0;
    // Delta emit: injected stable ids — a vanilla object maps to its tagfile id, a new
    // object to a `mod$N` symbol. When set, uq() returns these instead of the encounter-
    // order numeric id, so refs + filenames use the identity the runtime merges by. Null
    // for a normal decompile (byte-identical to before).
    const std::unordered_map<const void*, std::string>* stableIds = nullptr;

    // ── (class, name) editorID identity (breezy-gliding-nebula plan) ──
    // A node's stable identity is its NAME, not a number. A state's is its owning state machine's name
    // + '_' + the state's name (state names are unique only within their SM). This dissolves the numeric
    // id-space mismatch between the base master and per-mod deltas that made merges collide. Built by a
    // DRY pre-pass over the same DFS (pass 1 assigns editorIds, writes nothing), then used by uq() as the
    // identity for every filename/id/ref in the real emit (pass 2). editorIds supersedes any injected
    // numeric stableIds; anything unnamed falls back to the encounter-order number (never a ref target).
    bool                                          dryRun = false;
    bool                                          useEditorIds = false;   // base path on; delta path off (still tagfile) until converted
    std::unordered_map<const void*, std::string>  editorIds;
    std::string                                   curSMName;   // owning SM name while emitting its states
    void setEditorId(const void* obj, const std::string& eid) { if (obj) editorIds.try_emplace(obj, eid); }

    // Delta emit: sub-object -> the top-level node whose YAML INLINES it. Several
    // binary objects have no YAML node of their own — a state/SM's
    // hkbStateMachineTransitionInfoArray and hkbStateMachineEventPropertyArray, a
    // transition's expression/string condition, a blender/bone-switch child and its
    // hkbBoneWeightArray — they render as blocks inside their owner. A Nemesis patch
    // that overrides such an object BY ITS OWN #NNNN (BFCO edits the vanilla ready/
    // attack states' transition arrays this way) leaves the owner's fields unchanged,
    // so only the sub-object's id lands in deltaIds — and it has no node to write.
    // DecompileNativeDelta uses this map to emit the OWNER instead (whose YAML carries
    // the full corrected array); without it the mod's edit silently vanishes (BFCO's
    // from-neutral light attacks played vanilla). Registered at each inline site,
    // always to the TOP-LEVEL node (one hop).
    std::unordered_map<const void*, const void*> subOwner;
    const void* curOwner = nullptr;   // the top-level node currently being emitted
    void own(const void* sub, const void* owner) {
        if (sub && owner) subOwner.try_emplace(sub, owner);
    }
    void ownTransitions(const std::shared_ptr<hkbStateMachineTransitionInfoArray>& arr, const void* owner) {
        if (!arr) return;
        own(arr.get(), owner);
        for (const auto& t : arr->m_transitions) own(t.m_condition.get(), owner);
    }
    int idNum(const void* obj) {
        if (!obj) return -1;
        auto [it, ins] = objIds.try_emplace(obj, nextId);
        if (ins) ++nextId;
        return it->second;
    }
    std::string uq(const void* obj, const std::string& = {}) {
        if (!obj) return "null";
        if (useEditorIds) { if (auto it = editorIds.find(obj); it != editorIds.end()) return it->second; }  // (class,name) identity
        if (stableIds) { if (auto it = stableIds->find(obj); it != stableIds->end()) return it->second; }
        return std::to_string(idNum(obj));
    }
    template <class T>
    std::string uq(const std::shared_ptr<T>& p) { return uq(p.get()); }
};

std::string BehaviorEmitter::transitionsBlock(const std::shared_ptr<hkbStateMachineTransitionInfoArray>& arr, const std::string& ind) {
    if (!arr || arr->m_transitions.empty()) return "";
    std::string y = ind + "transitions:\n";
    for (const auto& t : arr->m_transitions) {
        y += ind + "  - triggerInterval:\n" + intervalBlock(t.m_triggerInterval, ind + "    ");
        y += ind + "    initiateInterval:\n" + intervalBlock(t.m_initiateInterval, ind + "    ");
        if (t.m_transition) { y += ind + "    transition: " + uq(t.m_transition) + "\n"; effect(t.m_transition); }
        if (auto ec = std::dynamic_pointer_cast<hkbExpressionCondition>(t.m_condition))
            y += ind + "    condition: " + q(ec->m_expression) + "\n";
        else if (auto sc = std::dynamic_pointer_cast<hkbStringCondition>(t.m_condition))
            y += ind + "    conditionString: " + q(sc->m_conditionString) + "\n";
        y += ind + "    eventId: " + std::to_string(t.m_eventId) + "\n";
        evLine(y, ind + "    ", "event", t.m_eventId);
        y += ind + "    toStateId: " + std::to_string(t.m_toStateId) + "\n";
        y += ind + "    fromNestedStateId: " + std::to_string(t.m_fromNestedStateId) + "\n";
        y += ind + "    toNestedStateId: " + std::to_string(t.m_toNestedStateId) + "\n";
        y += ind + "    priority: " + std::to_string(t.m_priority) + "\n";
        y += ind + "    flags: " + en::FormatFlags(t.m_flags, en::TransitionFlags()) + "\n";
    }
    return y;
}

void BehaviorEmitter::effect(const std::shared_ptr<hkbTransitionEffect>& e) {
    if (!e) return;
    if (!visited.insert(e.get()).second) return;
    // effect() runs MID-BUILD of its referencing owner (from transitionsBlock) —
    // save/restore curOwner so the owner's later inline blocks attribute correctly.
    const void* savedOwner = curOwner;
    curOwner = e.get();
    setEditorId(e.get(), std::string(e->ClassName()) + ":" + e->m_name);   // (class, name) identity
    const auto be = std::dynamic_pointer_cast<hkbBlendingTransitionEffect>(e);
    if (!be) throw std::runtime_error("behavior decompile: unsupported transition effect '" + std::string(e->ClassName()) + "'");
    std::string y;
    y += "class: hkbBlendingTransitionEffect\n";
    y += "name: " + q(be->m_name) + "\n";
    y += "userData: " + std::to_string(be->m_userData) + "\n";
    y += "selfTransitionMode: " + revNum(en::SelfTransitionMode(), be->m_selfTransitionMode) + "\n";
    y += "eventMode: " + revNum(en::EventMode(), be->m_eventMode) + "\n";
    y += "duration: " + fstr(be->m_duration) + "\n";
    y += "toGeneratorStartTimeFraction: " + fstr(be->m_toGeneratorStartTimeFraction) + "\n";
    y += "flags: " + en::FormatFlags(be->m_flags, en::FlagBits()) + "\n";
    y += "endMode: " + revNum(en::EndMode(), be->m_endMode) + "\n";
    y += "blendCurve: " + revNum(en::BlendCurve(), be->m_blendCurve) + "\n";
    // Root-motion-relevant flags dropped historically; initializeCharacterPose resets
    // world-from-model at transition, which BFCO's wind-down blends depend on.
    y += "applySelfTransition: " + boolstr(be->m_applySelfTransition) + "\n";
    y += "initializeCharacterPose: " + boolstr(be->m_initializeCharacterPose) + "\n";
    y += bindingsBlock(be->m_variableBindingSet, "");
    write("transitions", uq(be), y);
    curOwner = savedOwner;
}

void BehaviorEmitter::stateInfo(const std::shared_ptr<hkbStateMachineStateInfo>& s) {
    if (!s) return;
    if (!visited.insert(s.get()).second) return;
    curOwner = s.get();
    // Identity = owning-SM name + '_' + state name (state names are unique only within their SM).
    setEditorId(s.get(), "hkbStateMachineStateInfo:" + (curSMName.empty() ? s->m_name : (curSMName + "_" + s->m_name)));
    std::string y;
    y += "class: hkbStateMachineStateInfo\n";
    y += "name: " + q(s->m_name) + "\n";
    y += "stateId: " + std::to_string(s->m_stateId) + "\n";
    y += "probability: " + fstr(s->m_probability) + "\n";
    y += "enable: " + boolstr(s->m_enable) + "\n";
    if (s->m_generator) y += "generator: " + uq(s->m_generator) + "\n";
    auto notify = [&](const std::shared_ptr<hkbStateMachineEventPropertyArray>& arr, const char* key) {
        if (!arr || arr->m_events.empty()) return;
        y += std::string(key) + ":\n";
        for (const auto& e : arr->m_events) {
            own(e.m_payload.get(), s.get());
            y += "  - id: " + std::to_string(e.m_id) + "\n";
            inlEvLine(y, "    ", e.m_id);   // merge-safe name (buildEventArray prefers it)
            if (e.m_payload) y += "    payload: " + q(payloadStr(e.m_payload)) + "\n";
        }
    };
    notify(s->m_enterNotifyEvents, "enterNotifyEvents");
    notify(s->m_exitNotifyEvents, "exitNotifyEvents");
    y += transitionsBlock(s->m_transitions, "");
    y += bindingsBlock(s->m_variableBindingSet, "");
    own(s->m_enterNotifyEvents.get(), s.get());
    own(s->m_exitNotifyEvents.get(), s.get());
    ownTransitions(s->m_transitions, s.get());
    write("states", uq(s), y);
    node(s->m_generator);
}

void BehaviorEmitter::node(const std::shared_ptr<hkbNode>& n) {
    if (!n) return;
    if (!visited.insert(n.get()).second) return;
    curOwner = n.get();
    // Identity = (class, name): CLASS-qualified so a clip and a state machine that share a name (vanilla
    // has both an "IdleChiselKneeling" clip AND SM) stay distinct — a generator ref then resolves to the
    // right one. Name-only would collapse them and drop the shadowed subtree.
    setEditorId(n.get(), std::string(n->ClassName()) + ":" + n->m_name);

    const std::string cls = n->ClassName();

    if (cls == "hkbStateMachine") {
        const auto& sm = static_cast<const hkbStateMachine&>(*n);
        std::string y;
        y += "class: hkbStateMachine\n";
        y += "name: " + q(n->m_name) + "\n";
        y += "userData: " + std::to_string(sm.m_userData) + "\n";
        y += "startStateId: " + std::to_string(sm.m_startStateId) + "\n";
        // The event the SM fires on any state/transition change (BFCO drives motion
        // lock off it). Vanilla is -1; BFCO sets it, and losing it floats the wind-down.
        y += "eventToSendWhenStateOrTransitionChanges: " + std::to_string(sm.m_eventToSendWhenStateOrTransitionChanges.m_id) + "\n";
        evLine(y, "", "eventToSendWhenStateOrTransitionChangesEvent", sm.m_eventToSendWhenStateOrTransitionChanges.m_id);
        own(sm.m_eventToSendWhenStateOrTransitionChanges.m_payload.get(), n.get());
        y += "returnToPreviousStateEventId: " + std::to_string(sm.m_returnToPreviousStateEventId) + "\n";
        evLine(y, "", "returnToPreviousStateEvent", sm.m_returnToPreviousStateEventId);
        y += "randomTransitionEventId: " + std::to_string(sm.m_randomTransitionEventId) + "\n";
        evLine(y, "", "randomTransitionEvent", sm.m_randomTransitionEventId);
        y += "transitionToNextHigherStateEventId: " + std::to_string(sm.m_transitionToNextHigherStateEventId) + "\n";
        evLine(y, "", "transitionToNextHigherStateEvent", sm.m_transitionToNextHigherStateEventId);
        y += "transitionToNextLowerStateEventId: " + std::to_string(sm.m_transitionToNextLowerStateEventId) + "\n";
        evLine(y, "", "transitionToNextLowerStateEvent", sm.m_transitionToNextLowerStateEventId);
        y += "syncVariableIndex: " + std::to_string(sm.m_syncVariableIndex) + "\n";
        varLine(y, "", "syncVariable", sm.m_syncVariableIndex);
        y += "wrapAroundStateId: " + boolstr(sm.m_wrapAroundStateId) + "\n";
        y += "maxSimultaneousTransitions: " + std::to_string(sm.m_maxSimultaneousTransitions) + "\n";
        y += "startStateMode: " + revNum(en::StartStateMode(), sm.m_startStateMode) + "\n";
        y += "selfTransitionMode: " + revNum(en::SmSelfTransitionMode(), sm.m_selfTransitionMode) + "\n";
        y += bindingsBlock(sm.m_variableBindingSet, "");
        // The loader reads the SM's WILDCARD transitions from the `transitions:`
        // key (states use the same key for their own transitions).
        y += transitionsBlock(sm.m_wildcardTransitions, "");
        ownTransitions(sm.m_wildcardTransitions, n.get());
        // States are identified within THIS SM's namespace (curSMName), so uq(state) and each
        // stateInfo() below produce "<SMname>_<stateName>". Save/restore for nested SMs.
        const std::string prevSM = curSMName;
        curSMName = sm.m_name;
        for (const auto& s : sm.m_states) if (s) setEditorId(s.get(), "hkbStateMachineStateInfo:" + curSMName + "_" + s->m_name);
        y += "states:\n";
        for (const auto& s : sm.m_states) if (s) y += "  - " + uq(s) + "\n";
        write("states", uq(n), y);
        for (const auto& s : sm.m_states) stateInfo(s);
        curSMName = prevSM;
        return;
    }

    if (cls == "hkbClipGenerator") {
        const auto& c = static_cast<const hkbClipGenerator&>(*n);
        std::string y;
        y += "class: hkbClipGenerator\n";
        y += "name: " + q(n->m_name) + "\n";
        y += "userData: " + std::to_string(c.m_userData) + "\n";
        y += "animationName: " + q(c.m_animationName) + "\n";
        y += "cropStartAmountLocalTime: " + fstr(c.m_cropStartAmountLocalTime) + "\n";
        y += "cropEndAmountLocalTime: " + fstr(c.m_cropEndAmountLocalTime) + "\n";
        y += "startTime: " + fstr(c.m_startTime) + "\n";
        y += "playbackSpeed: " + fstr(c.m_playbackSpeed) + "\n";
        y += "enforcedDuration: " + fstr(c.m_enforcedDuration) + "\n";
        y += "userControlledTimeFraction: " + fstr(c.m_userControlledTimeFraction) + "\n";
        y += "animationBindingIndex: " + std::to_string(c.m_animationBindingIndex) + "\n";
        y += "mode: " + revNum(en::PlaybackMode(), c.m_mode) + "\n";
        y += "flags: " + en::FormatFlags(c.m_flags, en::ClipGeneratorFlags()) + "\n";
        own(c.m_triggers.get(), n.get());   // hkbClipTriggerArray inlines here (register even when emptied)
        if (c.m_triggers && !c.m_triggers->m_triggers.empty()) {
            y += "triggers:\n";
            for (const auto& t : c.m_triggers->m_triggers) {
                own(t.m_event.m_payload.get(), n.get());
                y += "  - localTime: " + fstr(t.m_localTime) + "\n";
                const std::string ev = eventName(t.m_event.m_id);
                if (!ev.empty()) y += "    event: " + q(ev) + "\n";
                if (t.m_event.m_payload) y += "    payload: " + q(payloadStr(t.m_event.m_payload)) + "\n";
                y += "    relativeToEndOfClip: " + boolstr(t.m_relativeToEndOfClip) + "\n";
                y += "    acyclic: " + boolstr(t.m_acyclic) + "\n";
                y += "    isAnnotation: " + boolstr(t.m_isAnnotation) + "\n";
            }
        }
        y += bindingsBlock(c.m_variableBindingSet, "");
        write("clips", uq(n), y);
        return;
    }

    if (cls == "hkbBlenderGenerator") {
        const auto& b = static_cast<const hkbBlenderGenerator&>(*n);
        std::string y;
        y += "class: hkbBlenderGenerator\n";
        y += "name: " + q(n->m_name) + "\n";
        y += "userData: " + std::to_string(b.m_userData) + "\n";
        y += "referencePoseWeightThreshold: " + fstr(b.m_referencePoseWeightThreshold) + "\n";
        y += "blendParameter: " + fstr(b.m_blendParameter) + "\n";
        y += "minCyclicBlendParameter: " + fstr(b.m_minCyclicBlendParameter) + "\n";
        y += "maxCyclicBlendParameter: " + fstr(b.m_maxCyclicBlendParameter) + "\n";
        y += "indexOfSyncMasterChild: " + std::to_string(b.m_indexOfSyncMasterChild) + "\n";
        y += "flags: " + en::FormatFlags(b.m_flags, en::BlenderFlags()) + "\n";
        y += "subtractLastChild: " + boolstr(b.m_subtractLastChild) + "\n";
        y += bindingsBlock(b.m_variableBindingSet, "");
        y += "children:\n";
        for (const auto& ch : b.m_children) {
            if (!ch) continue;
            own(ch.get(), n.get());
            own(ch->m_boneWeights.get(), n.get());
            y += "  - generator: " + uq(ch->m_generator) + "\n";
            y += "    weight: " + fstr(ch->m_weight) + "\n";
            y += "    worldFromModelWeight: " + fstr(ch->m_worldFromModelWeight) + "\n";
            if (ch->m_boneWeights) {
                y += "    boneWeights:\n";
                y += "      count: " + std::to_string(ch->m_boneWeights->m_boneWeights.size()) + "\n";
                std::string vals;
                for (std::size_t i = 0; i < ch->m_boneWeights->m_boneWeights.size(); ++i) { if (i) vals += ' '; vals += fstr(ch->m_boneWeights->m_boneWeights[i]); }
                y += "      values: " + q(vals) + "\n";
            }
            // hkbBlenderGeneratorChild is bindable — Pandora binds per-child weight/
            // boneWeights to variables (e.g. BFCO). Emit at the child's 4-space indent.
            y += bindingsBlock(ch->m_variableBindingSet, "    ");
        }
        write("generators", uq(n), y);
        for (const auto& ch : b.m_children) if (ch) node(ch->m_generator);
        return;
    }

    if (cls == "BSCyclicBlendTransitionGenerator") {
        const auto& cb = static_cast<const BSCyclicBlendTransitionGenerator&>(*n);
        std::string y;
        y += "class: BSCyclicBlendTransitionGenerator\n";
        y += "name: " + q(n->m_name) + "\n";
        y += "userData: " + std::to_string(cb.m_userData) + "\n";
        if (cb.m_pBlenderGenerator) y += "pBlenderGenerator: " + uq(cb.m_pBlenderGenerator) + "\n";
        y += "EventToFreezeBlendValue:\n    id: " + std::to_string(cb.m_EventToFreezeBlendValue.m_id) + "\n";
        inlEvLine(y, "    ", cb.m_EventToFreezeBlendValue.m_id);
        y += "EventToCrossBlend:\n    id: " + std::to_string(cb.m_EventToCrossBlend.m_id) + "\n";
        inlEvLine(y, "    ", cb.m_EventToCrossBlend.m_id);
        y += "fBlendParameter: " + fstr(cb.m_fBlendParameter) + "\n";
        y += "fTransitionDuration: " + fstr(cb.m_fTransitionDuration) + "\n";
        y += "eBlendCurve: " + revNum(en::BlendCurve(), cb.m_eBlendCurve) + "\n";
        y += bindingsBlock(cb.m_variableBindingSet, "");
        write("generators", uq(n), y);
        node(cb.m_pBlenderGenerator);
        return;
    }

    if (cls == "hkbManualSelectorGenerator") {
        const auto& s = static_cast<const hkbManualSelectorGenerator&>(*n);
        std::string y = "class: hkbManualSelectorGenerator\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(s.m_userData) + "\n";
        y += "selectedGeneratorIndex: " + std::to_string(s.m_selectedGeneratorIndex) + "\n";
        y += "currentGeneratorIndex: " + std::to_string(s.m_currentGeneratorIndex) + "\n";
        y += bindingsBlock(s.m_variableBindingSet, "");
        y += "generators:\n";
        for (const auto& g : s.m_generators) y += "  - " + uq(g) + "\n";
        write("selectors", uq(n), y);
        for (const auto& g : s.m_generators) node(g);
        return;
    }

    if (cls == "BSBoneSwitchGenerator") {
        const auto& b = static_cast<const BSBoneSwitchGenerator&>(*n);
        std::string y = "class: BSBoneSwitchGenerator\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(b.m_userData) + "\n";
        if (b.m_pDefaultGenerator) y += "pDefaultGenerator: " + uq(b.m_pDefaultGenerator) + "\n";
        y += bindingsBlock(b.m_variableBindingSet, "");
        y += "children:\n";
        for (const auto& c : b.m_ChildrenA) {
            if (!c) continue;
            own(c.get(), n.get());
            own(c->m_spBoneWeight.get(), n.get());
            y += "  - pGenerator: " + uq(c->m_pGenerator) + "\n";
            if (c->m_spBoneWeight) {
                y += "    boneWeights:\n      count: " + std::to_string(c->m_spBoneWeight->m_boneWeights.size()) + "\n";
                std::string v; for (std::size_t i = 0; i < c->m_spBoneWeight->m_boneWeights.size(); ++i) { if (i) v += ' '; v += fstr(c->m_spBoneWeight->m_boneWeights[i]); }
                y += "      values: " + q(v) + "\n";
            }
            y += bindingsBlock(c->m_variableBindingSet, "    ");
        }
        write("generators", uq(n), y);
        node(b.m_pDefaultGenerator);
        for (const auto& c : b.m_ChildrenA) if (c) node(c->m_pGenerator);
        return;
    }

    if (cls == "BSOffsetAnimationGenerator") {
        const auto& g = static_cast<const BSOffsetAnimationGenerator&>(*n);
        std::string y = "class: BSOffsetAnimationGenerator\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(g.m_userData) + "\n";
        if (g.m_pDefaultGenerator)    y += "pDefaultGenerator: " + uq(g.m_pDefaultGenerator) + "\n";
        if (g.m_pOffsetClipGenerator) y += "pOffsetClipGenerator: " + uq(g.m_pOffsetClipGenerator) + "\n";
        y += "fOffsetVariable: " + fstr(g.m_fOffsetVariable) + "\n";
        y += "fOffsetRangeStart: " + fstr(g.m_fOffsetRangeStart) + "\n";
        y += "fOffsetRangeEnd: " + fstr(g.m_fOffsetRangeEnd) + "\n";
        y += bindingsBlock(g.m_variableBindingSet, "");
        write("generators", uq(n), y);
        node(g.m_pDefaultGenerator);
        node(g.m_pOffsetClipGenerator);
        return;
    }

    if (cls == "BSSynchronizedClipGenerator") {
        const auto& g = static_cast<const BSSynchronizedClipGenerator&>(*n);
        std::string y = "class: BSSynchronizedClipGenerator\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(g.m_userData) + "\n";
        if (g.m_pClipGenerator) y += "pClipGenerator: " + uq(g.m_pClipGenerator) + "\n";
        y += "SyncAnimPrefix: " + q(g.m_SyncAnimPrefix) + "\n";
        y += "bSyncClipIgnoreMarkPlacement: " + boolstr(g.m_bSyncClipIgnoreMarkPlacement) + "\n";
        y += "fGetToMarkTime: " + fstr(g.m_fGetToMarkTime) + "\nfMarkErrorThreshold: " + fstr(g.m_fMarkErrorThreshold) + "\n";
        y += "bLeadCharacter: " + boolstr(g.m_bLeadCharacter) + "\nbReorientSupportChar: " + boolstr(g.m_bReorientSupportChar) + "\n";
        y += "bApplyMotionFromRoot: " + boolstr(g.m_bApplyMotionFromRoot) + "\n";
        y += "sAnimationBindingIndex: " + std::to_string(g.m_sAnimationBindingIndex) + "\n";
        y += bindingsBlock(g.m_variableBindingSet, "");
        write("generators", uq(n), y);
        node(g.m_pClipGenerator);
        return;
    }

    if (cls == "hkbPoseMatchingGenerator") {
        const auto& g = static_cast<const hkbPoseMatchingGenerator&>(*n);
        std::string y = "class: hkbPoseMatchingGenerator\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(g.m_userData) + "\n";
        y += "referencePoseWeightThreshold: " + fstr(g.m_referencePoseWeightThreshold) + "\nblendParameter: " + fstr(g.m_blendParameter) + "\n";
        y += "minCyclicBlendParameter: " + fstr(g.m_minCyclicBlendParameter) + "\nmaxCyclicBlendParameter: " + fstr(g.m_maxCyclicBlendParameter) + "\n";
        y += "indexOfSyncMasterChild: " + std::to_string(g.m_indexOfSyncMasterChild) + "\nflags: " + en::FormatFlags(g.m_flags, en::BlenderFlags()) + "\n";
        y += "subtractLastChild: " + boolstr(g.m_subtractLastChild) + "\n";
        y += bindingsBlock(g.m_variableBindingSet, "");
        y += "children:\n";
        for (const auto& ch : g.m_children) {
            if (!ch) continue;
            own(ch.get(), n.get());
            own(ch->m_boneWeights.get(), n.get());
            y += "  - generator: " + uq(ch->m_generator) + "\n";
            y += "    weight: " + fstr(ch->m_weight) + "\n    worldFromModelWeight: " + fstr(ch->m_worldFromModelWeight) + "\n";
            if (ch->m_boneWeights) {
                y += "    boneWeights:\n";
                y += "      count: " + std::to_string(ch->m_boneWeights->m_boneWeights.size()) + "\n";
                std::string vals;
                for (std::size_t i = 0; i < ch->m_boneWeights->m_boneWeights.size(); ++i) { if (i) vals += ' '; vals += fstr(ch->m_boneWeights->m_boneWeights[i]); }
                y += "      values: " + q(vals) + "\n";
            }
            y += bindingsBlock(ch->m_variableBindingSet, "    ");
        }
        y += "worldFromModelRotation: " + pquat(g.m_worldFromModelRotation) + "\n";
        y += "blendSpeed: " + fstr(g.m_blendSpeed) + "\nminSpeedToSwitch: " + fstr(g.m_minSpeedToSwitch) + "\n";
        y += "minSwitchTimeNoError: " + fstr(g.m_minSwitchTimeNoError) + "\nminSwitchTimeFullError: " + fstr(g.m_minSwitchTimeFullError) + "\n";
        y += "startPlayingEventId: " + std::to_string(g.m_startPlayingEventId) + "\n";
        evLine(y, "", "startPlayingEvent", g.m_startPlayingEventId);
        y += "startMatchingEventId: " + std::to_string(g.m_startMatchingEventId) + "\n";
        evLine(y, "", "startMatchingEvent", g.m_startMatchingEventId);
        // Pose matcher is a TYPED generator Def (loader parses intField before the skeleton loads),
        // so its bone fields stay numeric until it moves to the generic setBone path — deferred.
        y += "rootBoneIndex: " + std::to_string(g.m_rootBoneIndex) + "\notherBoneIndex: " + std::to_string(g.m_otherBoneIndex) + "\n";
        y += "anotherBoneIndex: " + std::to_string(g.m_anotherBoneIndex) + "\npelvisIndex: " + std::to_string(g.m_pelvisIndex) + "\n";
        y += "mode: " + std::string(g.m_mode == 1 ? "MODE_PLAY" : "MODE_MATCH") + "\n";
        write("generators", uq(n), y);
        for (const auto& ch : g.m_children) if (ch) node(ch->m_generator);
        return;
    }

    if (cls == "hkbBehaviorReferenceGenerator") {
        const auto& g = static_cast<const hkbBehaviorReferenceGenerator&>(*n);
        std::string y = "class: hkbBehaviorReferenceGenerator\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(g.m_userData) + "\n";
        y += "behaviorName: " + q(g.m_behaviorName) + "\n";
        y += bindingsBlock(g.m_variableBindingSet, "");
        write("references", uq(n), y);
        return;
    }

    if (cls == "hkbReferencePoseGenerator") {
        const auto& g = static_cast<const hkbReferencePoseGenerator&>(*n);
        std::string y = "class: hkbReferencePoseGenerator\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(g.m_userData) + "\n";
        y += bindingsBlock(g.m_variableBindingSet, "");
        write("generators", uq(n), y);
        return;
    }

    if (cls == "BGSGamebryoSequenceGenerator") {
        const auto& g = static_cast<const BGSGamebryoSequenceGenerator&>(*n);
        std::string y = "class: BGSGamebryoSequenceGenerator\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(g.m_userData) + "\n";
        y += "sequence: " + q(g.m_pSequence) + "\n";
        y += "blendModeFunction: " + revNum(en::BlendModeFunction(), g.m_eBlendModeFunction) + "\n";
        y += "percent: " + fstr(g.m_fPercent) + "\n";
        y += bindingsBlock(g.m_variableBindingSet, "");
        write("generators", uq(n), y);
        return;
    }

    if (cls == "BSiStateTaggingGenerator") {
        const auto& g = static_cast<const BSiStateTaggingGenerator&>(*n);
        std::string y = "class: BSiStateTaggingGenerator\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(g.m_userData) + "\n";
        if (g.m_pDefaultGenerator) y += "pDefaultGenerator: " + uq(g.m_pDefaultGenerator) + "\n";
        y += "iStateToSetAs: " + std::to_string(g.m_iStateToSetAs) + "\n";
        y += "iPriority: " + std::to_string(g.m_iPriority) + "\n";
        y += bindingsBlock(g.m_variableBindingSet, "");
        write("tagging", uq(n), y);
        node(g.m_pDefaultGenerator);
        return;
    }

    if (cls == "hkbModifierGenerator") {
        const auto& m = static_cast<const hkbModifierGenerator&>(*n);
        std::string y = "class: hkbModifierGenerator\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(m.m_userData) + "\n";
        if (m.m_modifier)  y += "modifier: "  + uq(m.m_modifier) + "\n";
        if (m.m_generator) y += "generator: " + uq(m.m_generator) + "\n";
        y += bindingsBlock(m.m_variableBindingSet, "");
        write("modifiers", uq(n), y);
        node(std::static_pointer_cast<hkbNode>(m.m_modifier));
        node(m.m_generator);
        return;
    }

    if (cls == "hkbModifierList") {
        const auto& m = static_cast<const hkbModifierList&>(*n);
        std::string y = "class: hkbModifierList\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(m.m_userData) + "\nenable: " + boolstr(m.m_enable) + "\n";
        y += bindingsBlock(m.m_variableBindingSet, "");
        y += "modifiers:\n";
        for (const auto& e : m.m_modifiers) y += "  - " + uq(e) + "\n";
        write("modifiers", uq(n), y);
        for (const auto& e : m.m_modifiers) node(std::static_pointer_cast<hkbNode>(e));
        return;
    }

    if (cls == "BSIsActiveModifier") {
        const auto& m = static_cast<const BSIsActiveModifier&>(*n);
        std::string y = "class: BSIsActiveModifier\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(m.m_userData) + "\nenable: " + boolstr(m.m_enable) + "\n";
        y += "bIsActive0: " + boolstr(m.m_bIsActive0) + "\nbInvertActive0: " + boolstr(m.m_bInvertActive0) + "\n";
        y += "bIsActive1: " + boolstr(m.m_bIsActive1) + "\nbInvertActive1: " + boolstr(m.m_bInvertActive1) + "\n";
        y += "bIsActive2: " + boolstr(m.m_bIsActive2) + "\nbInvertActive2: " + boolstr(m.m_bInvertActive2) + "\n";
        y += "bIsActive3: " + boolstr(m.m_bIsActive3) + "\nbInvertActive3: " + boolstr(m.m_bInvertActive3) + "\n";
        y += "bIsActive4: " + boolstr(m.m_bIsActive4) + "\nbInvertActive4: " + boolstr(m.m_bInvertActive4) + "\n";
        y += bindingsBlock(m.m_variableBindingSet, "");
        write("modifiers", uq(n), y);
        return;
    }

    if (cls == "hkbEvaluateExpressionModifier") {
        const auto& m = static_cast<const hkbEvaluateExpressionModifier&>(*n);
        std::string y = "class: hkbEvaluateExpressionModifier\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(m.m_userData) + "\nenable: " + boolstr(m.m_enable) + "\n";
        y += "expressions: null\n";   // linked by naming convention (<name>_expressions)
        y += bindingsBlock(m.m_variableBindingSet, "");
        write("modifiers", uq(n), y);
        // Emit the referenced expression data array as a data/ sidecar; its name
        // follows the naming convention (== the modifier name).
        own(m.m_expressions.get(), n.get());
        if (m.m_expressions) {
            // hkbExpressionData::ExpressionEventMode (sbyte) — inverse of the
            // builder's kExpressionEventMode. Distinct from the hkbEventBase
            // EventMode used on transitions; the two tables do not share values.
            auto exprModeStr = [](std::int8_t v) -> std::string {
                switch (v) {
                    case 1:  return "EVENT_MODE_SEND_ON_TRUE";
                    case 2:  return "EVENT_MODE_SEND_ON_FALSE_TO_TRUE";
                    case 3:  return "EVENT_MODE_SEND_EVERY_FRAME_ONCE_TRUE";
                    default: return "EVENT_MODE_SEND_ONCE";
                }
            };
            std::string d = "class: hkbExpressionDataArray\nname: " + q(n->m_name) + "\nexpressionsData:\n";
            for (const auto& e : m.m_expressions->m_expressionsData) {
                d += "  - expression: " + q(e.m_expression) + "\n";
                d += "    assignmentVariableIndex: " + std::to_string(e.m_assignmentVariableIndex) + "\n";
                varLine(d, "    ", "assignmentVariable", e.m_assignmentVariableIndex);
                d += "    assignmentEventIndex: " + std::to_string(e.m_assignmentEventIndex) + "\n";
                evLine(d, "    ", "assignmentEvent", e.m_assignmentEventIndex);
                d += "    eventMode: " + exprModeStr(e.m_eventMode) + "\n";
            }
            write("data", uq(n) + "_expressions", d);
        }
        return;
    }

    if (cls == "hkbEventsFromRangeModifier") {
        const auto& m = static_cast<const hkbEventsFromRangeModifier&>(*n);
        std::string y = "class: hkbEventsFromRangeModifier\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(m.m_userData) + "\nenable: " + boolstr(m.m_enable) + "\n";
        y += "inputValue: " + fstr(m.m_inputValue) + "\nlowerBound: " + fstr(m.m_lowerBound) + "\neventRanges: null\n";
        y += bindingsBlock(m.m_variableBindingSet, "");
        write("modifiers", uq(n), y);
        // Referenced data array as a data/ sidecar (named == the modifier).
        own(m.m_eventRanges.get(), n.get());
        if (m.m_eventRanges) {
            auto modeStr = [](std::int8_t v) -> std::string {
                switch (v) {
                    case 1:  return "EVENT_MODE_SEND_ON_TRUE";
                    case 2:  return "EVENT_MODE_SEND_ON_FALSE_TO_TRUE";
                    case 3:  return "EVENT_MODE_SEND_EVERY_FRAME_ONCE_TRUE";
                    default: return "EVENT_MODE_SEND_ONCE";
                }
            };
            std::string d = "class: hkbEventRangeDataArray\nname: " + q(n->m_name) + "\neventData:\n";
            for (const auto& e : m.m_eventRanges->m_eventData) {
                own(e.m_event.m_payload.get(), n.get());
                d += "  - upperBound: " + fstr(e.m_upperBound) + "\n";
                d += "    event: " + q(eventName(e.m_event.m_id)) + "\n";
                if (e.m_event.m_payload) d += "    payload: " + q(payloadStr(e.m_event.m_payload)) + "\n";
                d += "    eventMode: " + modeStr(e.m_eventMode) + "\n";
            }
            write("data", uq(n) + "_eventRanges", d);
        }
        return;
    }

    if (cls == "hkbEventDrivenModifier") {
        const auto& m = static_cast<const hkbEventDrivenModifier&>(*n);
        std::string y = "class: hkbEventDrivenModifier\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(m.m_userData) + "\nenable: " + boolstr(m.m_enable) + "\n";
        if (m.m_modifier) y += "modifier: " + uq(m.m_modifier) + "\n";
        y += "activateEventId: " + std::to_string(m.m_activateEventId) + "\n";
        evLine(y, "", "activateEvent", m.m_activateEventId);
        y += "deactivateEventId: " + std::to_string(m.m_deactivateEventId) + "\n";
        evLine(y, "", "deactivateEvent", m.m_deactivateEventId);
        y += "activeByDefault: " + boolstr(m.m_activeByDefault) + "\n";
        y += bindingsBlock(m.m_variableBindingSet, "");
        write("modifiers", uq(n), y);
        node(std::static_pointer_cast<hkbNode>(m.m_modifier));
        return;
    }

    if (cls == "hkbFootIkControlsModifier") {
        const auto& m = static_cast<const hkbFootIkControlsModifier&>(*n);
        const auto& g = m.m_controlData.m_gains;
        std::string y = "class: hkbFootIkControlsModifier\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(m.m_userData) + "\nenable: " + boolstr(m.m_enable) + "\n";
        y += bindingsBlock(m.m_variableBindingSet, "");
        y += "controlData:\n  gains:\n";
        y += "    onOffGain: " + fstr(g.m_onOffGain) + "\n    groundAscendingGain: " + fstr(g.m_groundAscendingGain) + "\n";
        y += "    groundDescendingGain: " + fstr(g.m_groundDescendingGain) + "\n    footPlantedGain: " + fstr(g.m_footPlantedGain) + "\n";
        y += "    footRaisedGain: " + fstr(g.m_footRaisedGain) + "\n    footUnlockGain: " + fstr(g.m_footUnlockGain) + "\n";
        y += "    worldFromModelFeedbackGain: " + fstr(g.m_worldFromModelFeedbackGain) + "\n    errorUpDownBias: " + fstr(g.m_errorUpDownBias) + "\n";
        y += "    alignWorldFromModelGain: " + fstr(g.m_alignWorldFromModelGain) + "\n    hipOrientationGain: " + fstr(g.m_hipOrientationGain) + "\n";
        y += "    maxKneeAngleDifference: " + fstr(g.m_maxKneeAngleDifference) + "\n    ankleOrientationGain: " + fstr(g.m_ankleOrientationGain) + "\n";
        y += "errorOutTranslation: " + pvec(m.m_errorOutTranslation) + "\n";
        y += "alignWithGroundRotation: " + pquat(m.m_alignWithGroundRotation) + "\n";
        // Per-leg config (one entry per foot). groundPosition/verticalError/hit
        // flags are runtime state (usually zero in shipped files) but serialized,
        // so round-trip them; ungroundedEvent is the authored event.
        if (!m.m_legs.empty()) {
            y += "legs:\n";
            for (const auto& leg : m.m_legs) {
                y += "  - groundPosition: " + pvec(leg.m_groundPosition) + "\n";
                y += "    ungroundedEvent:\n      id: " + std::to_string(leg.m_ungroundedEvent.m_id) + "\n";
                inlEvLine(y, "      ", leg.m_ungroundedEvent.m_id);   // merge-safe foot-IK unground event
                if (leg.m_ungroundedEvent.m_payload)
                    y += "      payload: " + q(payloadStr(leg.m_ungroundedEvent.m_payload)) + "\n";
                y += "    verticalError: " + fstr(leg.m_verticalError) + "\n";
                y += "    hitSomething: " + boolstr(leg.m_hitSomething) + "\n";
                y += "    isPlantedMS: " + boolstr(leg.m_isPlantedMS) + "\n";
            }
        }
        write("modifiers", uq(n), y);
        return;
    }

    if (cls == "hkbFootIkModifier") {
        const auto& m = static_cast<const hkbFootIkModifier&>(*n);
        const auto& g = m.m_gains;
        std::string y = "class: hkbFootIkModifier\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(m.m_userData) + "\nenable: " + boolstr(m.m_enable) + "\n";
        y += bindingsBlock(m.m_variableBindingSet, "");
        y += "gains:\n";
        y += "  onOffGain: " + fstr(g.m_onOffGain) + "\n  groundAscendingGain: " + fstr(g.m_groundAscendingGain) + "\n";
        y += "  groundDescendingGain: " + fstr(g.m_groundDescendingGain) + "\n  footPlantedGain: " + fstr(g.m_footPlantedGain) + "\n";
        y += "  footRaisedGain: " + fstr(g.m_footRaisedGain) + "\n  footUnlockGain: " + fstr(g.m_footUnlockGain) + "\n";
        y += "  worldFromModelFeedbackGain: " + fstr(g.m_worldFromModelFeedbackGain) + "\n  errorUpDownBias: " + fstr(g.m_errorUpDownBias) + "\n";
        y += "  alignWorldFromModelGain: " + fstr(g.m_alignWorldFromModelGain) + "\n  hipOrientationGain: " + fstr(g.m_hipOrientationGain) + "\n";
        y += "  maxKneeAngleDifference: " + fstr(g.m_maxKneeAngleDifference) + "\n  ankleOrientationGain: " + fstr(g.m_ankleOrientationGain) + "\n";
        y += "raycastDistanceUp: " + fstr(m.m_raycastDistanceUp) + "\nraycastDistanceDown: " + fstr(m.m_raycastDistanceDown) + "\n";
        y += "originalGroundHeightMS: " + fstr(m.m_originalGroundHeightMS) + "\nerrorOut: " + fstr(m.m_errorOut) + "\n";
        y += "errorOutTranslation: " + pvec(m.m_errorOutTranslation) + "\nalignWithGroundRotation: " + pquat(m.m_alignWithGroundRotation) + "\n";
        y += "verticalOffset: " + fstr(m.m_verticalOffset) + "\ncollisionFilterInfo: " + std::to_string(m.m_collisionFilterInfo) + "\n";
        y += "forwardAlignFraction: " + fstr(m.m_forwardAlignFraction) + "\nsidewaysAlignFraction: " + fstr(m.m_sidewaysAlignFraction) + "\n";
        y += "sidewaysSampleWidth: " + fstr(m.m_sidewaysSampleWidth) + "\n";
        y += "useTrackData: " + boolstr(m.m_useTrackData) + "\nlockFeetWhenPlanted: " + boolstr(m.m_lockFeetWhenPlanted) + "\n";
        y += "useCharacterUpVector: " + boolstr(m.m_useCharacterUpVector) + "\nalignMode: " + std::to_string(static_cast<int>(m.m_alignMode)) + "\n";
        if (!m.m_legs.empty()) {
            y += "legs:\n";
            for (const auto& leg : m.m_legs) {
                y += "  - prevAnkleRotLS: " + pquat(leg.m_prevAnkleRotLS) + "\n";
                y += "    kneeAxisLS: " + pvec(leg.m_kneeAxisLS) + "\n    footEndLS: " + pvec(leg.m_footEndLS) + "\n";
                y += "    ungroundedEvent:\n      id: " + std::to_string(leg.m_ungroundedEvent.m_id) + "\n";
                inlEvLine(y, "      ", leg.m_ungroundedEvent.m_id);   // merge-safe foot-IK unground event
                if (leg.m_ungroundedEvent.m_payload)
                    y += "      payload: " + q(payloadStr(leg.m_ungroundedEvent.m_payload)) + "\n";
                y += "    footPlantedAnkleHeightMS: " + fstr(leg.m_footPlantedAnkleHeightMS) + "\n    footRaisedAnkleHeightMS: " + fstr(leg.m_footRaisedAnkleHeightMS) + "\n";
                y += "    maxAnkleHeightMS: " + fstr(leg.m_maxAnkleHeightMS) + "\n    minAnkleHeightMS: " + fstr(leg.m_minAnkleHeightMS) + "\n";
                y += "    maxKneeAngleDegrees: " + fstr(leg.m_maxKneeAngleDegrees) + "\n    minKneeAngleDegrees: " + fstr(leg.m_minKneeAngleDegrees) + "\n";
                y += "    verticalError: " + fstr(leg.m_verticalError) + "\n    maxAnkleAngleDegrees: " + fstr(leg.m_maxAnkleAngleDegrees) + "\n";
                y += "    hipIndex: " + std::to_string(leg.m_hipIndex) + "\n    kneeIndex: " + std::to_string(leg.m_kneeIndex) + "\n    ankleIndex: " + std::to_string(leg.m_ankleIndex) + "\n";
                y += "    hitSomething: " + boolstr(leg.m_hitSomething) + "\n    isPlantedMS: " + boolstr(leg.m_isPlantedMS) + "\n";
                y += "    isOriginalAnkleTransformMSSet: " + boolstr(leg.m_isOriginalAnkleTransformMSSet) + "\n";
            }
        }
        write("modifiers", uq(n), y);
        return;
    }

    if (cls == "BSIStateManagerModifier") {
        const auto& m = static_cast<const BSIStateManagerModifier&>(*n);
        std::string y = "class: BSIStateManagerModifier\nname: " + q(n->m_name) + "\nuserData: " + std::to_string(m.m_userData) + "\nenable: " + boolstr(m.m_enable) + "\n";
        y += "iStateVar: " + std::to_string(m.m_iStateVar) + "\n";
        varLine(y, "", "iStateVariable", m.m_iStateVar);
        y += bindingsBlock(m.m_variableBindingSet, "");
        if (!m.m_stateData.empty()) {
            y += "stateData:\n";
            for (const auto& sd : m.m_stateData) {
                y += "  - pStateMachine: " + uq(sd.m_pStateMachine) + "\n";
                y += "    StateID: " + std::to_string(sd.m_StateID) + "\n";
                y += "    iStateToSetAs: " + std::to_string(sd.m_iStateToSetAs) + "\n";
            }
        }
        write("modifiers", uq(n), y);
        // Recurse into the referenced state machines so they get emitted (node()
        // dedups against the generator-tree traversal via `visited`).
        for (const auto& sd : m.m_stateData) node(std::static_pointer_cast<hkbNode>(sd.m_pStateMachine));
        return;
    }

    // ── generic modifiers (params mirror BehaviorBuilder::buildGenericModifier) ──
    if (const auto mod = std::dynamic_pointer_cast<hkbModifier>(n)) {
        auto ev = [&](const char* k, const hkbEventProperty& e) {
            own(e.m_payload.get(), mod.get());
            std::string s = std::string(k) + ":\n    id: " + std::to_string(e.m_id) + "\n";
            inlEvLine(s, "    ", e.m_id);   // merge-safe name (fillEventBase prefers it)
            if (e.m_payload) s += "    payload: " + q(payloadStr(e.m_payload)) + "\n";
            return s;
        };
        // A ragdoll/keyframe bone list is emitted as a data/ sidecar named by the
        // convention <modifier><suffix>, with RAW indices (our loader accepts both
        // names and raw indices; the decompiler can't reverse-resolve to names
        // without the skeleton). The modifier keeps its `null` field.
        auto emitBoneIdx = [&](const char* suffix, const std::shared_ptr<hkbBoneIndexArray>& arr) {
            if (!arr) return;
            own(arr.get(), mod.get());
            std::string d = "class: hkbBoneIndexArray\nname: " + q(uq(mod) + suffix) + "\nboneIndices:\n";
            for (auto idx : arr->m_boneIndices) d += "  - " + boneRef(idx) + "\n";
            write("data", uq(mod) + suffix, d);
        };
        std::string y = "class: " + cls + "\nname: " + q(mod->m_name) + "\nuserData: " + std::to_string(mod->m_userData) + "\nenable: " + boolstr(mod->m_enable) + "\n";
        y += bindingsBlock(mod->m_variableBindingSet, "");

        if (cls == "hkbTwistModifier") {
            const auto& m = static_cast<const hkbTwistModifier&>(*n);
            y += "axisOfRotation: " + pvec(m.m_axisOfRotation) + "\ntwistAngle: " + fstr(m.m_twistAngle) + "\n";
            y += "startBoneIndex: " + boneRef(m.m_startBoneIndex) + "\nendBoneIndex: " + boneRef(m.m_endBoneIndex) + "\n";
            y += "setAngleMethod: " + revNum(en::SetAngleMethod(), m.m_setAngleMethod) + "\n";
            y += "rotationAxisCoordinates: " + revNum(en::RotationAxisCoordinates(), m.m_rotationAxisCoordinates) + "\n";
            y += "isAdditive: " + boolstr(m.m_isAdditive) + "\n";
        } else if (cls == "hkbDampingModifier") {
            const auto& m = static_cast<const hkbDampingModifier&>(*n);
            y += "kP: " + fstr(m.m_kP) + "\nkI: " + fstr(m.m_kI) + "\nkD: " + fstr(m.m_kD) + "\n";
            y += "enableScalarDamping: " + boolstr(m.m_enableScalarDamping) + "\nenableVectorDamping: " + boolstr(m.m_enableVectorDamping) + "\n";
            y += "rawValue: " + fstr(m.m_rawValue) + "\ndampedValue: " + fstr(m.m_dampedValue) + "\n";
            y += "rawVector: " + pvec(m.m_rawVector) + "\ndampedVector: " + pvec(m.m_dampedVector) + "\n";
            y += "vecErrorSum: " + pvec(m.m_vecErrorSum) + "\nvecPreviousError: " + pvec(m.m_vecPreviousError) + "\n";
            y += "errorSum: " + fstr(m.m_errorSum) + "\npreviousError: " + fstr(m.m_previousError) + "\n";
        } else if (cls == "hkbRotateCharacterModifier") {
            const auto& m = static_cast<const hkbRotateCharacterModifier&>(*n);
            y += "degreesPerSecond: " + fstr(m.m_degreesPerSecond) + "\nspeedMultiplier: " + fstr(m.m_speedMultiplier) + "\naxisOfRotation: " + pvec(m.m_axisOfRotation) + "\n";
        } else if (cls == "hkbGetUpModifier") {
            const auto& m = static_cast<const hkbGetUpModifier&>(*n);
            y += "groundNormal: " + pvec(m.m_groundNormal) + "\nduration: " + fstr(m.m_duration) + "\nalignWithGroundDuration: " + fstr(m.m_alignWithGroundDuration) + "\n";
            y += "rootBoneIndex: " + boneRef(m.m_rootBoneIndex) + "\notherBoneIndex: " + boneRef(m.m_otherBoneIndex) + "\nanotherBoneIndex: " + boneRef(m.m_anotherBoneIndex) + "\n";
        } else if (cls == "BSDirectAtModifier") {
            const auto& m = static_cast<const BSDirectAtModifier&>(*n);
            y += "directAtTarget: " + boolstr(m.m_directAtTarget) + "\n";
            y += "sourceBoneIndex: " + boneRef(m.m_sourceBoneIndex) + "\nstartBoneIndex: " + boneRef(m.m_startBoneIndex) + "\nendBoneIndex: " + boneRef(m.m_endBoneIndex) + "\n";
            y += "limitHeadingDegrees: " + fstr(m.m_limitHeadingDegrees) + "\nlimitPitchDegrees: " + fstr(m.m_limitPitchDegrees) + "\n";
            y += "offsetHeadingDegrees: " + fstr(m.m_offsetHeadingDegrees) + "\noffsetPitchDegrees: " + fstr(m.m_offsetPitchDegrees) + "\n";
            y += "onGain: " + fstr(m.m_onGain) + "\noffGain: " + fstr(m.m_offGain) + "\ntargetLocation: " + pvec(m.m_targetLocation) + "\n";
            y += "userInfo: " + std::to_string(m.m_userInfo) + "\ndirectAtCamera: " + boolstr(m.m_directAtCamera) + "\n";
            y += "directAtCameraX: " + fstr(m.m_directAtCameraX) + "\ndirectAtCameraY: " + fstr(m.m_directAtCameraY) + "\ndirectAtCameraZ: " + fstr(m.m_directAtCameraZ) + "\n";
            y += "active: " + boolstr(m.m_active) + "\ncurrentHeadingOffset: " + fstr(m.m_currentHeadingOffset) + "\ncurrentPitchOffset: " + fstr(m.m_currentPitchOffset) + "\n";
        } else if (cls == "BSEventOnDeactivateModifier") {
            const auto& m = static_cast<const BSEventOnDeactivateModifier&>(*n);
            y += ev("event", m.m_event);
        } else if (cls == "BSEventOnFalseToTrueModifier") {
            const auto& m = static_cast<const BSEventOnFalseToTrueModifier&>(*n);
            y += "bEnableEvent1: " + boolstr(m.m_bEnableEvent1) + "\nbVariableToTest1: " + boolstr(m.m_bVariableToTest1) + "\n" + ev("EventToSend1", m.m_EventToSend1);
            y += "bEnableEvent2: " + boolstr(m.m_bEnableEvent2) + "\nbVariableToTest2: " + boolstr(m.m_bVariableToTest2) + "\n" + ev("EventToSend2", m.m_EventToSend2);
            y += "bEnableEvent3: " + boolstr(m.m_bEnableEvent3) + "\nbVariableToTest3: " + boolstr(m.m_bVariableToTest3) + "\n" + ev("EventToSend3", m.m_EventToSend3);
        } else if (cls == "BSSpeedSamplerModifier") {
            const auto& m = static_cast<const BSSpeedSamplerModifier&>(*n);
            y += "state: " + std::to_string(m.m_state) + "\ndirection: " + fstr(m.m_direction) + "\ngoalSpeed: " + fstr(m.m_goalSpeed) + "\nspeedOut: " + fstr(m.m_speedOut) + "\n";
        } else if (cls == "BSModifyOnceModifier") {
            const auto& m = static_cast<const BSModifyOnceModifier&>(*n);
            if (m.m_pOnActivateModifier)   y += "pOnActivateModifier: " + uq(m.m_pOnActivateModifier) + "\n";
            if (m.m_pOnDeactivateModifier) y += "pOnDeactivateModifier: " + uq(m.m_pOnDeactivateModifier) + "\n";
            write("modifiers", uq(mod), y);
            node(std::static_pointer_cast<hkbNode>(m.m_pOnActivateModifier));
            node(std::static_pointer_cast<hkbNode>(m.m_pOnDeactivateModifier));
            return;
        } else if (cls == "hkbTimerModifier") {
            const auto& m = static_cast<const hkbTimerModifier&>(*n);
            y += "alarmTimeSeconds: " + fstr(m.m_alarmTimeSeconds) + "\n";
            y += ev("alarmEvent", m.m_alarmEvent);
        } else if (cls == "BSRagdollContactListenerModifier") {
            const auto& m = static_cast<const BSRagdollContactListenerModifier&>(*n);
            y += ev("contactEvent", m.m_contactEvent);
            emitBoneIdx("_bones", m.m_bones);
        } else if (cls == "hkbRigidBodyRagdollControlsModifier") {
            const auto& m = static_cast<const hkbRigidBodyRagdollControlsModifier&>(*n);
            y += "durationToBlend: " + fstr(m.m_controlData.m_durationToBlend) + "\n";
            // keyFrameHierarchyControlData (12 gains) — flat keys, read back via setF.
            const auto& kfh = m.m_controlData.m_keyFrameHierarchyControlData;
            y += "hierarchyGain: " + fstr(kfh.m_hierarchyGain) + "\nvelocityDamping: " + fstr(kfh.m_velocityDamping) + "\n";
            y += "accelerationGain: " + fstr(kfh.m_accelerationGain) + "\nvelocityGain: " + fstr(kfh.m_velocityGain) + "\n";
            y += "positionGain: " + fstr(kfh.m_positionGain) + "\npositionMaxLinearVelocity: " + fstr(kfh.m_positionMaxLinearVelocity) + "\n";
            y += "positionMaxAngularVelocity: " + fstr(kfh.m_positionMaxAngularVelocity) + "\nsnapGain: " + fstr(kfh.m_snapGain) + "\n";
            y += "snapMaxLinearVelocity: " + fstr(kfh.m_snapMaxLinearVelocity) + "\nsnapMaxAngularVelocity: " + fstr(kfh.m_snapMaxAngularVelocity) + "\n";
            y += "snapMaxLinearDistance: " + fstr(kfh.m_snapMaxLinearDistance) + "\nsnapMaxAngularDistance: " + fstr(kfh.m_snapMaxAngularDistance) + "\n";
            emitBoneIdx("_bones", m.m_bones);
        } else if (cls == "hkbPoweredRagdollControlsModifier") {
            const auto& m = static_cast<const hkbPoweredRagdollControlsModifier&>(*n);
            y += "maxForce: " + fstr(m.m_controlData.m_maxForce) + "\ntau: " + fstr(m.m_controlData.m_tau) + "\n";
            y += "damping: " + fstr(m.m_controlData.m_damping) + "\n";
            y += "proportionalRecoveryVelocity: " + fstr(m.m_controlData.m_proportionalRecoveryVelocity) + "\n";
            y += "constantRecoveryVelocity: " + fstr(m.m_controlData.m_constantRecoveryVelocity) + "\n";
            // worldFromModelModeData (3 pose-matching bones + mode) — flat keys.
            const auto& wfm = m.m_worldFromModelModeData;
            y += "poseMatchingBone0: " + std::to_string(wfm.m_poseMatchingBone0) + "\n";
            y += "poseMatchingBone1: " + std::to_string(wfm.m_poseMatchingBone1) + "\n";
            y += "poseMatchingBone2: " + std::to_string(wfm.m_poseMatchingBone2) + "\n";
            y += "worldFromModelMode: " + std::to_string(static_cast<int>(wfm.m_mode)) + "\n";
            emitBoneIdx("_bones", m.m_bones);
        } else if (cls == "hkbKeyframeBonesModifier") {
            const auto& m = static_cast<const hkbKeyframeBonesModifier&>(*n);
            if (!m.m_keyframeInfo.empty()) {
                y += "keyframeInfo:\n";
                for (const auto& k : m.m_keyframeInfo) {
                    y += "  - keyframedPosition: " + pvec(k.m_keyframedPosition) + "\n";
                    y += "    keyframedRotation: " + pquat(k.m_keyframedRotation) + "\n";
                    y += "    boneIndex: " + boneRef(k.m_boneIndex) + "\n    isValid: " + boolstr(k.m_isValid) + "\n";
                }
            }
            emitBoneIdx("_keyframedBonesList", m.m_keyframedBonesList);
        } else if (cls == "BSLookAtModifier") {
            const auto& m = static_cast<const BSLookAtModifier&>(*n);
            y += "lookAtTarget: " + boolstr(m.m_lookAtTarget) + "\n";
            auto emitBones = [&](const char* key, const std::vector<BSLookAtModifierBoneData>& v) {
                if (v.empty()) return;
                y += std::string(key) + ":\n";
                for (const auto& b : v) {
                    y += "  - index: " + std::to_string(b.m_index) + "\n    fwdAxisLS: " + pvec(b.m_fwdAxisLS) + "\n";
                    y += "    limitAngleDegrees: " + fstr(b.m_limitAngleDegrees) + "\n    onGain: " + fstr(b.m_onGain) + "\n    offGain: " + fstr(b.m_offGain) + "\n    enabled: " + boolstr(b.m_enabled) + "\n";
                }
            };
            emitBones("bones", m.m_bones);
            emitBones("eyeBones", m.m_eyeBones);
            y += "limitAngleDegrees: " + fstr(m.m_limitAngleDegrees) + "\nlimitAngleThresholdDegrees: " + fstr(m.m_limitAngleThresholdDegrees) + "\n";
            y += "continueLookOutsideOfLimit: " + boolstr(m.m_continueLookOutsideOfLimit) + "\nonGain: " + fstr(m.m_onGain) + "\noffGain: " + fstr(m.m_offGain) + "\n";
            y += "useBoneGains: " + boolstr(m.m_useBoneGains) + "\ntargetLocation: " + pvec(m.m_targetLocation) + "\ntargetOutsideLimits: " + boolstr(m.m_targetOutsideLimits) + "\n";
            y += ev("targetOutOfLimitEvent", m.m_targetOutOfLimitEvent);
            y += "lookAtCamera: " + boolstr(m.m_lookAtCamera) + "\nlookAtCameraX: " + fstr(m.m_lookAtCameraX) + "\nlookAtCameraY: " + fstr(m.m_lookAtCameraY) + "\nlookAtCameraZ: " + fstr(m.m_lookAtCameraZ) + "\n";
        } else if (cls == "BSInterpValueModifier") {
            const auto& m = static_cast<const BSInterpValueModifier&>(*n);
            y += "source: " + fstr(m.m_source) + "\ntarget: " + fstr(m.m_target) +
                 "\nresult: " + fstr(m.m_result) + "\ngain: " + fstr(m.m_gain) + "\n";
        } else if (cls == "BSEventEveryNEventsModifier") {
            const auto& m = static_cast<const BSEventEveryNEventsModifier&>(*n);
            y += ev("eventToCheckFor", m.m_eventToCheckFor);
            y += ev("eventToSend", m.m_eventToSend);
            y += "numberOfEventsBeforeSend: " + std::to_string(static_cast<int>(m.m_numberOfEventsBeforeSend)) + "\n";
            y += "minimumNumberOfEventsBeforeSend: " + std::to_string(static_cast<int>(m.m_minimumNumberOfEventsBeforeSend)) + "\n";
            y += "randomizeNumberOfEvents: " + boolstr(m.m_randomizeNumberOfEvents) + "\n";
        } else if (cls == "BSGetTimeStepModifier") {
            const auto& m = static_cast<const BSGetTimeStepModifier&>(*n);
            y += "timeStep: " + fstr(m.m_timeStep) + "\n";
        } else if (cls == "hkbTransformVectorModifier") {
            const auto& m = static_cast<const hkbTransformVectorModifier&>(*n);
            y += "rotation: " + pquat(m.m_rotation) + "\ntranslation: " + pvec(m.m_translation) + "\n";
            y += "vectorIn: " + pvec(m.m_vectorIn) + "\nvectorOut: " + pvec(m.m_vectorOut) + "\n";
            y += "rotateOnly: " + boolstr(m.m_rotateOnly) + "\ninverse: " + boolstr(m.m_inverse) + "\n";
            y += "computeOnActivate: " + boolstr(m.m_computeOnActivate) + "\ncomputeOnModify: " + boolstr(m.m_computeOnModify) + "\n";
        } else if (cls == "BSDecomposeVectorModifier") {
            const auto& m = static_cast<const BSDecomposeVectorModifier&>(*n);
            y += "vector: " + pvec(m.m_vector) + "\n";
            y += "x: " + fstr(m.m_x) + "\ny: " + fstr(m.m_y) + "\nz: " + fstr(m.m_z) + "\nw: " + fstr(m.m_w) + "\n";
        } else if (cls == "BSLimbIKModifier") {
            const auto& m = static_cast<const BSLimbIKModifier&>(*n);
            y += "limitAngleDegrees: " + fstr(m.m_limitAngleDegrees) + "\n";
            y += "startBoneIndex: " + boneRef(m.m_startBoneIndex) + "\nendBoneIndex: " + boneRef(m.m_endBoneIndex) + "\n";
            y += "gain: " + fstr(m.m_gain) + "\nboneRadius: " + fstr(m.m_boneRadius) + "\ncastOffset: " + fstr(m.m_castOffset) + "\n";
        } else if (cls == "BSTweenerModifier") {
            const auto& m = static_cast<const BSTweenerModifier&>(*n);
            y += "tweenPosition: " + boolstr(m.m_tweenPosition) + "\ntweenRotation: " + boolstr(m.m_tweenRotation) + "\n";
            y += "useTweenDuration: " + boolstr(m.m_useTweenDuration) + "\ntweenDuration: " + fstr(m.m_tweenDuration) + "\n";
            y += "targetPosition: " + pvec(m.m_targetPosition) + "\ntargetRotation: " + pquat(m.m_targetRotation) + "\n";
        } else if (cls == "BSPassByTargetTriggerModifier") {
            const auto& m = static_cast<const BSPassByTargetTriggerModifier&>(*n);
            y += "targetPosition: " + pvec(m.m_targetPosition) + "\nradius: " + fstr(m.m_radius) + "\n";
            y += "movementDirection: " + pvec(m.m_movementDirection) + "\n";
            y += ev("triggerEvent", m.m_triggerEvent);
        } else if (cls == "BSTimerModifier") {
            const auto& m = static_cast<const BSTimerModifier&>(*n);
            y += "alarmTimeSeconds: " + fstr(m.m_alarmTimeSeconds) + "\n";
            y += ev("alarmEvent", m.m_alarmEvent);
            y += "resetAlarm: " + boolstr(m.m_resetAlarm) + "\n";
        } else if (cls != "hkbModifier") {
            throw std::runtime_error("behavior decompile: unsupported modifier class '" + cls + "'");
        }
        write("modifiers", uq(mod), y);
        return;
    }

    throw std::runtime_error("behavior decompile: unsupported node class '" + cls +
                             "' (M-D behavior coverage is incremental)");
}

std::string emitGraphData(const hkbBehaviorGraphData* gd) {
    std::string y;
    const auto sd = gd ? gd->m_stringData : nullptr;
    const std::size_t nVars = sd ? sd->m_variableNames.size() : 0;
    if (nVars == 0) {
        y += "variables: []\n\n";
    } else {
        y += "variables:\n";
        for (std::size_t i = 0; i < nVars; ++i) {
            y += "  - name: " + q(sd->m_variableNames[i]) + "\n";
            std::int8_t type = (i < gd->m_variableInfos.size()) ? gd->m_variableInfos[i].m_type : 0;
            const std::string typeStr = revNum(en::VariableType(), type);
            y += "    type: " + typeStr + "\n";
            long val = 0;
            if (gd->m_variableInitialValues && i < gd->m_variableInitialValues->m_wordVariableValues.size())
                val = gd->m_variableInitialValues->m_wordVariableValues[i].m_value;
            y += "    value: " + std::to_string(val) + "\n";
            // Vector/quaternion variables store their word value as an index into the
            // quad-value array; emit the actual Vector4 so it round-trips (else it
            // defaults to zero/identity).
            if (typeStr == "VARIABLE_TYPE_VECTOR4" || typeStr == "VARIABLE_TYPE_QUATERNION" ||
                typeStr == "VARIABLE_TYPE_VECTOR3") {
                const auto& vvs = gd->m_variableInitialValues;
                if (vvs && val >= 0 && static_cast<std::size_t>(val) < vvs->m_quadVariableValues.size())
                    y += "    quadValue: " + pvec(vvs->m_quadVariableValues[val]) + "\n";
            }
        }
        y += "\n";
    }
    const std::size_t nEvents = sd ? sd->m_eventNames.size() : 0;
    if (nEvents == 0) {
        y += "events: []\n";
    } else {
        y += "events:\n";
        for (std::size_t i = 0; i < nEvents; ++i) {
            y += "  - name: " + q(sd->m_eventNames[i]) + "\n";
            std::uint32_t flags = (i < gd->m_eventInfos.size()) ? gd->m_eventInfos[i].m_flags : 0;
            y += "    flags: " + en::FormatFlags(static_cast<long>(flags), en::EventInfoFlags()) + "\n";
        }
    }
    // characterPropertyNames — name/type/flags per property. Flags are symbolic (RoleFlags),
    // round-tripping byte-exactly: ResolveEnum parses names OR a bare number, and FormatFlags
    // appends any unnamed leftover bits as 0x-hex.
    const std::size_t nCP = sd ? sd->m_characterPropertyNames.size() : 0;
    if (nCP) {
        y += "\ncharacterPropertyNames:\n";
        for (std::size_t i = 0; i < nCP; ++i) {
            y += "  - name: " + q(sd->m_characterPropertyNames[i]) + "\n";
            std::int8_t type = (i < gd->m_characterPropertyInfos.size()) ? gd->m_characterPropertyInfos[i].m_type : 0;
            y += "    type: " + revNum(en::VariableType(), type) + "\n";
            std::int16_t flags = (i < gd->m_characterPropertyInfos.size()) ? gd->m_characterPropertyInfos[i].m_role.m_flags : 0;
            y += "    flags: " + en::FormatFlags(static_cast<long>(flags), en::RoleFlags()) + "\n";
        }
    }
    return y;
}

} // namespace

DecompileResult DecompileBehaviorTree(const std::shared_ptr<hkbBehaviorGraph>& bg, const fs::path& dir,
                                      const std::unordered_map<const void*, std::string>* stableIds,
                                      std::unordered_map<const void*, std::string>* outIds,
                                      const havok::sct::BoneNameTable* bones) {
    try {
        std::error_code ec; fs::create_directories(dir, ec);

        BehaviorEmitter em;
        em.dir = dir;
        em.gd = bg->m_data.get();
        em.bones = bones;               // null = numeric bone indices; set = bone NAMES
        em.stableIds = stableIds;       // last-resort fallback; editorIds (below) supersede it
        em.useEditorIds = true;         // base path is now (class,name)-keyed
        // Pass 1 (dry): walk the graph assigning (class,name) editorIds — every node its name, every
        // state its owning SM's name + '_' + state name. Writes nothing.
        em.dryRun = true;
        em.node(bg->m_rootGenerator);
        // Pass 2: emit for real. uq() now returns the editorId for every ref target (forward or back).
        em.dryRun = false;
        em.visited.clear();
        em.objIds.clear(); em.nextId = 0;
        em.node(bg->m_rootGenerator);   // writes node files keyed by (class,name)

        // Report the id each object actually received: editorId for every named node/state, plus any
        // unnamed fallback objects. Callers map object -> id from this.
        if (outIds) {
            for (const auto& [obj, eid] : em.editorIds) (*outIds)[obj] = eid;
            for (const auto& [obj, n]   : em.objIds)    outIds->try_emplace(obj, em.uq(obj));
        }

        {
            std::string y;
            y += "packfile:\n  classversion: 8\n  contentsversion: \"hk_2010.2.0-r1\"\n\n";
            y += "behavior:\n";
            y += "  name: " + q(bg->m_name) + "\n";
            y += "  variableMode: " + revNum(en::VariableMode(), bg->m_variableMode) + "\n";
            if (bg->m_rootGenerator) y += "  rootGenerator: " + em.uq(bg->m_rootGenerator) + "\n";
            y += "  data: graphdata\n";
            writeText(dir / "behavior.yaml", y);
        }

        writeText(dir / "data" / "graphdata.yaml", emitGraphData(bg->m_data.get()));

        return { true, "", "behavior" };
    } catch (const std::exception& e) {
        return { false, e.what(), "behavior" };
    }
}

DecompileResult DecompileNativeDelta(
    const std::shared_ptr<hkbBehaviorGraph>& bg,
    const std::unordered_map<const void*, std::string>& stableIds,
    const std::set<std::string>& deltaIds,
    const std::set<std::string>& addedEventNames,
    const std::set<std::string>& addedVariableNames,
    const std::set<std::string>& addedCharPropNames,
    const fs::path& outDir,
    std::vector<std::string>* warnings) {
    try {
        std::error_code ec; fs::create_directories(outDir, ec);

        // Full DFS emit into an in-memory sink, with stable (tagfile / mod$N) ids, so
        // refs + filenames match the base bundle the runtime merges this delta onto.
        std::unordered_map<std::string, std::pair<std::string, std::string>> sink;
        BehaviorEmitter em;
        em.gd = bg->m_data.get();
        em.stableIds = &stableIds;
        em.sink = &sink;
        em.node(bg->m_rootGenerator);

        // Fold changed ids with NO node of their own (owner-inlined sub-objects: a
        // state/SM's hkbStateMachineTransitionInfoArray / EventPropertyArray, a
        // transition's condition, a clip trigger array, blend children / bone-weight
        // arrays, binding sets) into their OWNING node — the owner's YAML carries the
        // full corrected inline block. A Nemesis patch edits such objects BY THEIR OWN
        // #NNNN while the owner's fields stay unchanged, so only the sub-object id
        // lands in deltaIds; without this fold the mod's edit silently vanished (BFCO's
        // vanilla-state attack transitions -> from-neutral light attacks played vanilla).
        std::set<std::string> writeIds(deltaIds);
        {
            std::unordered_map<std::string, const void*> idToObj;
            idToObj.reserve(stableIds.size());
            for (const auto& [obj, sid] : stableIds) idToObj.emplace(sid, obj);
            for (const auto& id : deltaIds) {
                if (sink.count(id)) continue;                       // has its own node
                const auto oit = idToObj.find(id);
                const void* obj = (oit != idToObj.end()) ? oit->second : nullptr;
                // owner chain (registered flat to the top-level node; walk defensively)
                int hops = 0;
                const void* owner = obj;
                std::string ownerId;
                while (owner && hops++ < 4) {
                    const auto sub = em.subOwner.find(owner);
                    if (sub == em.subOwner.end()) { owner = nullptr; break; }
                    owner = sub->second;
                    if (const auto sit = stableIds.find(owner);
                        sit != stableIds.end() && sink.count(sit->second)) { ownerId = sit->second; break; }
                }
                if (!ownerId.empty()) {
                    writeIds.insert(ownerId);
                } else if (warnings) {
                    // Every inline site registers its sub-objects, so an unresolvable id
                    // means the object is NOT referenced by the merged graph (another edit
                    // repointed its former owner — e.g. a clip whose trigger array was
                    // replaced). Its edit is moot by construction; still surfaced so a
                    // future unregistered inline class can never hide as "moot".
                    warnings->push_back("delta: changed object '" + id +
                                        "' is not referenced by the merged graph — edit skipped as moot");
                }
            }
        }

        // Write ONLY this mod's nodes (override #NNNN + new mod$N). An override is a full
        // node here, but the merged graph is vanilla + only real changes, so at runtime
        // only the truly-different params merge (rymlEqual filters the rest).
        // Naming-convention data sidecars — hkbEvaluateExpressionModifier's expressions,
        // hkbEventsFromRangeModifier's eventRanges, and the IK modifiers' bone-index arrays —
        // are emitted into the sink keyed <ownerId>_<suffix> (the OWNING node's id, not the
        // array's own patch id), so they never appear in deltaIds. They must ship with their
        // owner: without them a mod-added modifier's array is dropped, the runtime re-link
        // (buildEvaluateExpression: id + "_expressions", etc.) finds nothing, m_expressions
        // stays null, and Havok null-derefs the modifier the moment it runs it (the
        // 2026-08-12 Pitch_Adjustment_Bow_Reset_EEM crash). Emit each delta node AND any
        // sidecar it owns.
        static constexpr const char* kSidecarSuffixes[] = {
            "_expressions", "_eventRanges", "_bones", "_keyframedBonesList"
        };
        for (const auto& id : writeIds) {
            if (auto it = sink.find(id); it != sink.end())
                writeText(outDir / it->second.first / (id + ".yaml"), it->second.second);
            for (const char* suffix : kSidecarSuffixes) {
                const std::string skey = id + suffix;
                if (auto sc = sink.find(skey); sc != sink.end())
                    writeText(outDir / sc->second.first / (skey + ".yaml"), sc->second.second);
            }
        }

        // data/additive.yaml — the mod's ADDED vocabulary (runtime unions it in).
        const auto* gd = bg->m_data.get();
        const auto sd = gd ? gd->m_stringData : nullptr;
        if (sd && (!addedVariableNames.empty() || !addedEventNames.empty() || !addedCharPropNames.empty())) {
            std::string y;
            if (!addedVariableNames.empty()) {
                y += "variables:\n";
                for (std::size_t i = 0; i < sd->m_variableNames.size(); ++i) {
                    if (!addedVariableNames.count(sd->m_variableNames[i])) continue;
                    y += "  - name: " + q(sd->m_variableNames[i]) + "\n";
                    std::int8_t type = (i < gd->m_variableInfos.size()) ? gd->m_variableInfos[i].m_type : 0;
                    const std::string typeStr = revNum(en::VariableType(), type);
                    y += "    type: " + typeStr + "\n";
                    long val = 0;
                    if (gd->m_variableInitialValues && i < gd->m_variableInitialValues->m_wordVariableValues.size())
                        val = gd->m_variableInitialValues->m_wordVariableValues[i].m_value;
                    y += "    value: " + std::to_string(val) + "\n";
                    if (typeStr == "VARIABLE_TYPE_VECTOR4" || typeStr == "VARIABLE_TYPE_QUATERNION" ||
                        typeStr == "VARIABLE_TYPE_VECTOR3") {
                        const auto& vvs = gd->m_variableInitialValues;
                        if (vvs && val >= 0 && static_cast<std::size_t>(val) < vvs->m_quadVariableValues.size())
                            y += "    quadValue: " + pvec(vvs->m_quadVariableValues[val]) + "\n";
                    }
                }
            }
            if (!addedEventNames.empty()) {
                y += "events:\n";
                for (std::size_t i = 0; i < sd->m_eventNames.size(); ++i) {
                    if (!addedEventNames.count(sd->m_eventNames[i])) continue;
                    y += "  - name: " + q(sd->m_eventNames[i]) + "\n";
                    std::uint32_t flags = (i < gd->m_eventInfos.size()) ? gd->m_eventInfos[i].m_flags : 0;
                    y += "    flags: " + en::FormatFlags(static_cast<long>(flags), en::EventInfoFlags()) + "\n";
                }
            }
            if (!addedCharPropNames.empty()) {
                y += "characterPropertyNames:\n";
                for (std::size_t i = 0; i < sd->m_characterPropertyNames.size(); ++i) {
                    if (!addedCharPropNames.count(sd->m_characterPropertyNames[i])) continue;
                    y += "  - name: " + q(sd->m_characterPropertyNames[i]) + "\n";
                    std::int8_t type = (i < gd->m_characterPropertyInfos.size()) ? gd->m_characterPropertyInfos[i].m_type : 0;
                    y += "    type: " + revNum(en::VariableType(), type) + "\n";
                    std::int16_t flags = (i < gd->m_characterPropertyInfos.size())
                                         ? gd->m_characterPropertyInfos[i].m_role.m_flags : 0;
                    y += "    flags: " + en::FormatFlags(static_cast<long>(flags), en::RoleFlags()) + "\n";
                }
            }
            writeText(outDir / "data" / "additive.yaml", y);
        }
        return { true, "", "behavior" };
    } catch (const std::exception& e) {
        return { false, e.what(), "behavior" };
    }
}

} // namespace havok::sct
