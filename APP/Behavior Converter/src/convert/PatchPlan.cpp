#include "PatchPlan.h"

#include <havok/model/yaml/HkyArchive.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace bconv {
namespace {

std::string toLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "meshes/actors/horse/behaviors/horsebehavior.hkx" -> "actors/horse"
// Handles the space-folder variants ("behaviors wolf", "characters female") and _1stperson.
std::string actorOf(const std::string& prefixLower) {
    const auto ap = prefixLower.find("actors/");
    if (ap == std::string::npos) return {};
    const std::string rest = prefixLower.substr(ap + 7);  // after "actors/"
    std::size_t cut = std::string::npos;
    for (const char* seg : { "/behaviors", "/characters", "/_1stperson" }) {
        const auto b = rest.find(seg);
        if (b != std::string::npos && (cut == std::string::npos || b < cut)) cut = b;
    }
    const std::string actorSeg = (cut == std::string::npos) ? rest.substr(0, rest.find('/'))
                                                            : rest.substr(0, cut);
    return "actors/" + actorSeg;
}

std::string stemOf(const std::string& prefixLower) {
    const auto slash = prefixLower.rfind('/');
    std::string fn = (slash == std::string::npos) ? prefixLower : prefixLower.substr(slash + 1);
    const auto dot = fn.rfind(".hkx");
    return (dot == std::string::npos) ? fn : fn.substr(0, dot);
}

const char* originName(GraphOrigin o) {
    switch (o) {
        case GraphOrigin::NemesisPatch:     return "NemesisPatch";
        case GraphOrigin::PrecompiledGraph: return "PrecompiledGraph";
        case GraphOrigin::FirstPerson:      return "FirstPerson";
    }
    return "?";
}

}  // namespace

std::string ActorOfServePath(const std::string& servePathLower) { return actorOf(servePathLower); }

BaseMaps BuildBaseMaps(const havok::model::HkyArchive& baseArc) {
    using UK = havok::model::HkyArchive::UnitKind;
    BaseMaps m;
    for (const auto& u : baseArc.units()) {
        const std::string prefix = toLower(u.prefix);  // already normalized, but be defensive
        if (u.kind == UK::Behavior) {
            const std::string stem  = stemOf(prefix);
            const std::string actor = actorOf(prefix);
            const bool isFp = prefix.find("/_1stperson/behaviors/") != std::string::npos;
            auto& sp = isFp ? m.fpGraphServePath : m.graphServePath;
            auto& ap = isFp ? m.fpGraphActorPath : m.graphActorPath;
            // Templated/Nemesis graphs are the character-family graphs (templates/ mirrors
            // actors/character). A stem can collide across actors — horsebehavior exists at BOTH
            // actors/character (the rider's mounted behavior, 118 vars incl. MC_*) and actors/horse
            // (the horse creature, 76 vars). Prefer actors/character so a Nemesis <g> patch resolves to
            // the rider graph it actually targets; the creature/precompiled path resolves by its real
            // prefix (leg 1d), NOT this stem map.
            if (sp.find(stem) == sp.end() || actor == "actors/character") {
                sp[stem] = prefix;
                ap[stem] = actor;
            }
            m.vanillaGraphStems.insert(stem);
        } else if (u.kind == UK::Character) {
            m.actorCharacters[actorOf(prefix)].push_back(prefix);
        }
    }
    for (auto& [actor, chars] : m.actorCharacters) {
        std::sort(chars.begin(), chars.end());
        chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
    }
    return m;
}

PatchPlan BuildPlan(const PlanInputs& in) {
    PatchPlan plan;
    if (!in.base || !in.bundleOrder || !in.bundleCodes || !in.graphs) return plan;
    const BaseMaps& base = *in.base;

    for (const std::string& bundle : *in.bundleOrder) {
        BundlePlan bp;
        bp.bundleName = bundle;
        const auto ci = in.bundleCodes->find(bundle);
        if (ci != in.bundleCodes->end()) bp.codes = ci->second;

        // Character serve paths this bundle touches (deduped) -> RosterChange targets (values filled in Phase 1).
        std::unordered_set<std::string> rosterTargets;
        auto addRosterTargets = [&](const std::string& actorPath) {
            if (actorPath.empty()) return;
            if (const auto ai = base.actorCharacters.find(actorPath); ai != base.actorCharacters.end())
                for (const auto& cp : ai->second) rosterTargets.insert(cp);
        };

        // 1) Nemesis-patched third-person graphs — union patch dirs across the bundle's codes per graph.
        for (const std::string& g : *in.graphs) {
            const std::string gl = toLower(g);
            GraphChange gc;
            gc.graphStem = g;
            gc.origin    = GraphOrigin::NemesisPatch;
            for (const std::string& code : bp.codes) {
                std::string pd = in.patchDir ? in.patchDir(code, g) : std::string{};
                if (!pd.empty()) gc.patchDirs.push_back(std::move(pd));
            }
            if (gc.patchDirs.empty()) continue;  // this bundle doesn't patch g
            if (const auto sp = base.graphServePath.find(gl); sp != base.graphServePath.end()) {
                gc.servePath = sp->second;
                gc.actorPath = base.graphActorPath.at(gl);
                gc.isVanilla = true;
                gc.resolved  = true;
                addRosterTargets(gc.actorPath);
            } else {
                plan.warnings.push_back("bundle '" + bundle + "': Nemesis graph '" + g +
                                        "' not found in base — servePath/actor unresolved.");
            }
            bp.graphs.push_back(std::move(gc));
        }

        // 2) Nemesis-patched first-person graphs (separate namespace).
        if (in.fpGraphs)
            for (const std::string& g : *in.fpGraphs) {
                if (toLower(g) == "firstperson") continue;  // the FP character, not a behavior graph
                const std::string gl = toLower(g);
                GraphChange gc;
                gc.graphStem = g;
                gc.origin    = GraphOrigin::FirstPerson;
                for (const std::string& code : bp.codes) {
                    std::string pd = in.fpPatchDir ? in.fpPatchDir(code, g) : std::string{};
                    if (!pd.empty()) gc.patchDirs.push_back(std::move(pd));
                }
                if (gc.patchDirs.empty()) continue;
                if (const auto sp = base.fpGraphServePath.find(gl); sp != base.fpGraphServePath.end()) {
                    gc.servePath = sp->second;
                    gc.actorPath = base.fpGraphActorPath.at(gl);
                    gc.isVanilla = true;
                    gc.resolved  = true;
                    // First-person rosters bind to the FirstPerson character, handled by the set-data path.
                }
                bp.graphs.push_back(std::move(gc));
            }

        // 3) Precompiled loose behavior graphs shipped by this bundle (the horse's real path).
        if (in.precompiledByBundle)
            if (const auto pi = in.precompiledByBundle->find(bundle); pi != in.precompiledByBundle->end())
                for (const std::string& prefixRaw : pi->second) {
                    const std::string prefix = toLower(prefixRaw);
                    GraphChange gc;
                    gc.graphStem      = stemOf(prefix);
                    gc.origin         = GraphOrigin::PrecompiledGraph;
                    gc.precompiledHkx = prefix;
                    gc.servePath      = prefix;                 // the loose graph IS its own serve path
                    gc.actorPath      = actorOf(prefix);
                    gc.isVanilla      = base.vanillaGraphStems.count(gc.graphStem) != 0;
                    gc.resolved       = !gc.actorPath.empty();
                    addRosterTargets(gc.actorPath);
                    bp.graphs.push_back(std::move(gc));
                }

        for (const auto& cp : rosterTargets) bp.rosters.push_back(RosterChange{ cp, {} });
        std::sort(bp.rosters.begin(), bp.rosters.end(),
                  [](const RosterChange& a, const RosterChange& b) { return a.characterServePath < b.characterServePath; });

        if (!bp.graphs.empty() || !bp.rosters.empty()) plan.bundles.push_back(std::move(bp));
    }
    return plan;
}

void DumpPlan(const PatchPlan& plan, const std::string& outFile) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(outFile).parent_path(), ec);
    std::ofstream out(outFile, std::ios::binary);
    if (!out) return;

    out << "# PatchPlan (Phase 0 — structural, behavior-neutral)\n";
    out << "# bundles: " << plan.bundles.size() << "\n\n";
    for (const auto& bp : plan.bundles) {
        out << "== " << bp.bundleName << " ==\n";
        out << "  codes:";
        for (const auto& c : bp.codes) out << " " << c;
        out << "\n";
        for (const auto& gc : bp.graphs) {
            out << "  graph " << gc.graphStem << " [" << originName(gc.origin) << "]"
                << (gc.isVanilla ? " vanilla" : " NEW")
                << (gc.resolved ? "" : " UNRESOLVED") << "\n";
            out << "    servePath: " << (gc.servePath.empty() ? "(?)" : gc.servePath) << "\n";
            out << "    actor:     " << (gc.actorPath.empty() ? "(?)" : gc.actorPath) << "\n";
            if (!gc.precompiledHkx.empty()) out << "    precompiled: " << gc.precompiledHkx << "\n";
            for (const auto& pd : gc.patchDirs) out << "    patchDir: " << pd << "\n";
        }
        for (const auto& rc : bp.rosters)
            out << "  roster -> " << rc.characterServePath << "  (+" << rc.addedAnimations.size() << " anims)\n";
        out << "\n";
    }
    if (!plan.warnings.empty()) {
        out << "== warnings (" << plan.warnings.size() << ") ==\n";
        for (const auto& w : plan.warnings) out << "  " << w << "\n";
    }
}

}  // namespace bconv
