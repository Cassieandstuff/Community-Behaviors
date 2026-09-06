// DeltaDeriver.cpp — see havok/sct/DeltaDeriver.h for the design.

#include "havok/sct/DeltaDeriver.h"

#include "havok/core/PackFileDeserializer.h"
#include "havok/sct/BehaviorDecompiler.h"   // DecompileBehaviorTree (+ outIds capture)
#include "havok/sct/HavokFile.h"            // ReadHavokFile
#include "havok/classes/Base.h"             // hkbNode
#include "havok/classes/Graph.h"            // hkbBehaviorGraph
#include "havok/classes/Generators.h"       // blender / selector children
#include "havok/classes/StateMachine.h"     // hkbStateMachine / hkbStateMachineStateInfo
#include "havok/classes/gen/ClassesGen.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace havok::sct {
namespace {

// One loaded behavior graph: its objects' (class,name) + structural owner map + per-name
// counts. Identity assignment is DEFERRED to assignNameIds so the collision decision can be
// made GLOBALLY across the two graphs being compared (a name colliding in EITHER must be
// owner-qualified in BOTH, else a node unique in vanilla but duplicated in the mod keys
// differently on each side and never matches).
struct LoadedGraph {
    std::shared_ptr<havok::hkbBehaviorGraph> bg;
    std::vector<const void*> order;                                             // objects, offset order
    std::unordered_map<const void*, std::pair<std::string, std::string>> meta;  // obj -> (class,name)
    std::unordered_map<const void*, const void*> owner;                         // obj -> owning node
    std::unordered_map<std::string, int> nameCount;                             // name -> count (any class)
};

bool loadGraphMeta(const std::string& path, havok::PackFileDeserializer& des,
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
    // Collect every referrer of each node; resolve the owner deterministically below.
    std::unordered_map<const void*, std::vector<const void*>> refs;
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
            // Referenceable singletons need a stable identity; inline/unreferenced objects
            // are rendered inside their owner, so they can be left out (emitter inlines them).
            if (cls == "hkbBehaviorGraph" || cls == "hkbBehaviorGraphData" ||
                cls == "hkbBehaviorGraphStringData" || cls == "hkbVariableValueSet")
                nm = "$" + cls;
            else
                continue;
        }
        g.order.push_back(obj.get());
        g.meta[obj.get()] = { cls, nm };
        if (nm[0] != '$') g.nameCount[nm]++;
    }
    if (!g.bg) { err = "no hkbBehaviorGraph in " + path; return false; }

    // Deterministic owner = the referrer with the smallest (class,name). A node reachable
    // from several parents (a shared clip) must pick the SAME owner in both graphs; offset
    // order does not — it flips between compiles and desyncs the owner-qualified identity.
    for (const auto& [child, rs] : refs) {
        const void* best = nullptr;
        for (const void* rf : rs) {
            if (!g.meta.count(rf)) continue;
            if (!best || g.meta.at(rf) < g.meta.at(best)) best = rf;
        }
        if (best) g.owner[child] = best;
    }
    return true;
}

// Identity = bare name when unambiguous in BOTH graphs; when it collides in either, prepend
// the owner's identity ("<ownerId>~<name>") recursively — a structural key stable across
// recompiles. Final ids are made globally unique via a class-then-owner deterministic order.
void assignNameIds(const LoadedGraph& g, const std::set<std::string>& ambiguous,
                   std::unordered_map<const void*, std::string>& stableIds) {
    std::unordered_map<const void*, std::string> ident;
    std::function<std::string(const void*)> identOf = [&](const void* o) -> std::string {
        auto mi = g.meta.find(o);
        if (mi == g.meta.end()) return {};
        if (auto it = ident.find(o); it != ident.end()) return it->second;
        const std::string& nm = mi->second.second;
        std::string result = nm;
        if (nm[0] != '$' && ambiguous.count(nm)) {
            ident[o] = nm;                                     // cycle guard
            const auto oit = g.owner.find(o);
            const std::string op = (oit != g.owner.end()) ? identOf(oit->second) : std::string();
            if (!op.empty()) result = op + "~" + nm;
        }
        ident[o] = result;
        return result;
    };
    std::unordered_map<std::string, std::vector<const void*>> groups;
    for (const void* o : g.order) groups[identOf(o)].push_back(o);
    for (auto& [base, objs] : groups) {
        if (objs.size() > 1)
            std::stable_sort(objs.begin(), objs.end(), [&](const void* a, const void* b) {
                const auto& ma = g.meta.at(a); const auto& mb = g.meta.at(b);
                if (ma.first != mb.first) return ma.first < mb.first;
                const auto oa = g.owner.find(a), ob = g.owner.find(b);
                const std::string ka = (oa != g.owner.end()) ? identOf(oa->second) : std::string();
                const std::string kb = (ob != g.owner.end()) ? identOf(ob->second) : std::string();
                return ka < kb;
            });
        for (std::size_t i = 0; i < objs.size(); ++i)
            stableIds[objs[i]] = (i == 0) ? base : base + "~" + std::to_string(i);
    }
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

DeriveDeltaResult DeriveLooseBehaviorDelta(const std::string& vanillaBin,
                                           const std::string& fullModBin,
                                           const std::string& outDeltaDir) {
    DeriveDeltaResult r;
    std::string err;

    havok::PackFileDeserializer vdes, mdes;
    LoadedGraph vg, mg;
    if (!loadGraphMeta(vanillaBin, vdes, vg, err)) { r.error = "vanilla: " + err; return r; }
    if (!loadGraphMeta(fullModBin, mdes, mg, err)) { r.error = "mod: " + err; return r; }

    // Global collision set: a name ambiguous in EITHER graph is owner-qualified in BOTH.
    std::set<std::string> ambiguous;
    for (const auto& [k, n] : vg.nameCount) if (n > 1) ambiguous.insert(k);
    for (const auto& [k, n] : mg.nameCount) if (n > 1) ambiguous.insert(k);

    std::unordered_map<const void*, std::string> vIdent, mIdent;
    assignNameIds(vg, ambiguous, vIdent);
    assignNameIds(mg, ambiguous, mIdent);

    // Temp decompile dirs (numeric-keyed) for the structural diff; cleaned before return.
    // MUST live under a SHORT system-temp path, NOT under outDeltaDir — a bundle unit path
    // (…/BehaviorFiles.hky/meshes/actors/horse/behaviors/horsebehavior.hkx/) is already deep,
    // and nesting the intermediate tree under it blows past Windows MAX_PATH, silently failing
    // the decompile writes (0 diffs). The final delta write to outDeltaDir is only as deep as
    // any other bundle, so it is fine.
    std::error_code ec;
    static std::atomic<unsigned> s_ctr{ 0 };
    const fs::path work    = fs::temp_directory_path(ec) / ("hkderive_" + std::to_string(s_ctr++));
    const fs::path vanTmp  = work / "vanilla";
    const fs::path modTmp  = work / "mod";
    fs::remove_all(work, ec);

    // Vanilla base: decompile with NULL stableIds -> the encounter-order numbering BuildBase
    // assigns (captured), byte-identical to the shipped Skyrim.hky base graph.
    std::unordered_map<const void*, std::string> vNum;
    const auto vr = havok::sct::DecompileBehaviorTree(vg.bg, vanTmp, nullptr, &vNum);
    if (!vr.ok) { r.error = "vanilla decompile: " + vr.error; return r; }

    std::unordered_map<std::string, std::string> identToNum;
    for (const auto& [obj, num] : vNum) {
        if (auto it = vIdent.find(obj); it != vIdent.end()) identToNum[it->second] = num;
        try { r.baseMaxId = std::max(r.baseMaxId, std::stol(num)); } catch (...) {}
    }
    long nextNew = r.baseMaxId + 1;

    // Mod graph: matched node -> the base's number (by structural identity); new -> fresh.
    std::unordered_map<const void*, std::string> mNum;
    for (const void* o : mg.order) {
        if (auto it = identToNum.find(mIdent[o]); it != identToNum.end()) { mNum[o] = it->second; ++r.matched; }
        else { mNum[o] = std::to_string(nextNew++); ++r.added; }
    }
    const auto mr = havok::sct::DecompileBehaviorTree(mg.bg, modTmp, &mNum);
    if (!mr.ok) { r.error = "mod decompile: " + mr.error; fs::remove_all(work, ec); return r; }

    // Diff the two numeric trees by relative path (same emitter + same ids => same filename
    // for the same node) and copy each NEW or CHANGED mod node into the delta.
    int copyFail = 0;
    for (fs::recursive_directory_iterator it(modTmp, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const fs::path rel = fs::relative(it->path(), modTmp);
        if (rel.filename() == "behavior.yaml") continue;   // graph header comes from the base
        const fs::path vp = vanTmp / rel;
        const bool isNew = !fs::exists(vp, ec);
        if (!isNew && readAll(it->path()) == readAll(vp)) continue;   // unchanged -> base serves it
        // Count only on a SUCCESSFUL write, so a silent copy failure (e.g. a MAX_PATH-long
        // destination) can never masquerade as a populated delta.
        const fs::path dst = fs::path(outDeltaDir) / rel;
        std::error_code ce;
        fs::create_directories(dst.parent_path(), ce);
        fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ce);
        if (ce) ++copyFail;
        else if (isNew) ++r.newNodes; else ++r.changedNodes;
    }
    if (copyFail) {
        r.error = "failed to write " + std::to_string(copyFail) + " delta node file(s) to '" +
                  outDeltaDir + "' (destination path likely exceeds the OS limit)";
        fs::remove_all(work, ec);
        return r;   // r.ok stays false — never report a partial delta as success
    }
    for (fs::recursive_directory_iterator it(vanTmp, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && !fs::exists(modTmp / fs::relative(it->path(), vanTmp), ec)) ++r.removedFromBase;
    }

    fs::remove_all(work, ec);
    r.ok = true;
    return r;
}

}  // namespace havok::sct
