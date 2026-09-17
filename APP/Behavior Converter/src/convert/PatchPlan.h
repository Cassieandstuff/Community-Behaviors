#pragma once
// PatchPlan — the converter's normalization prelude.
//
// Pandora/Nemesis reorganizes the behavior tree to fight MAX_PATH (a different choice than CB's
// short-path approach), so a patch's owning actor / graph / character is IMPLICIT in the raw load
// order and today re-derived inconsistently across the converter's legs (the root cause of the horse
// `animationnames` regression). `BuildPlan` runs BEFORE any node conversion, resolves every change
// against the base archive (ground truth = the shipped Skyrim.hky), and produces one authoritative
// `PatchPlan`: what changed, for which actor/graph/character, and where it goes in CB's tree. The
// converter then EXECUTES the plan and never re-derives paths or attribution.
//
// Decision A (locked 2026-09-17): the prelude plans STRUCTURE/attribution (real serve paths, actor,
// character targets, origins) up front; roster VALUES are gathered during/after graph convert
// (reusing the existing clip-name extraction) but PLACED per the plan. No new on-disk format — the
// plan is in-memory, with a debug dump to D:\cb-diffs\ for inspectability.

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havok::model { class HkyArchive; }

namespace bconv {

// Base-derived resolution maps (ground truth = Skyrim.hky's real unit tree). All keys lowercased,
// matching HkyArchive::Unit::prefix (already lower/'/'-normalized).
struct BaseMaps {
    // third-person behavior graph stem -> its real meshes-relative serve path + actor path.
    std::unordered_map<std::string, std::string> graphServePath;  // "horsebehavior" -> "meshes/actors/horse/behaviors/horsebehavior.hkx"
    std::unordered_map<std::string, std::string> graphActorPath;  // "horsebehavior" -> "actors/horse"
    // first-person behavior graph stem -> real serve path + actor (separate namespace: _1stperson/behaviors).
    std::unordered_map<std::string, std::string> fpGraphServePath;
    std::unordered_map<std::string, std::string> fpGraphActorPath;
    // actor path -> the character unit serve path(s) under it (defaultmale + defaultfemale for
    // actors/character incl. the "characters female/" one; a single "horse" for actors/horse).
    std::unordered_map<std::string, std::vector<std::string>> actorCharacters;  // "actors/horse" -> ["meshes/actors/horse/characters/horse.hkx"]
    std::unordered_set<std::string>                           vanillaGraphStems;  // behavior stems present in base (derive-vs-own-new)
};

BaseMaps BuildBaseMaps(const havok::model::HkyArchive& baseArc);

enum class GraphOrigin { NemesisPatch, PrecompiledGraph, FirstPerson };

struct GraphChange {
    std::string              graphStem;
    std::string              servePath;      // real meshes-relative (from BaseMaps; "" when unresolved/new)
    std::string              actorPath;      // "actors/horse" (from BaseMaps; "" when unresolved/new)
    GraphOrigin              origin = GraphOrigin::NemesisPatch;
    std::vector<std::string> patchDirs;      // NemesisPatch / FirstPerson: the #*.txt patch dirs to merge
    std::string              precompiledHkx; // PrecompiledGraph: the meshes-relative prefix of the loose .hkx
    bool                     isVanilla = false;  // in base (derive) vs a new standalone graph (own+serve)
    bool                     resolved  = false;  // servePath/actor were found in the base maps
};

struct RosterChange {
    std::string              characterServePath;  // where the data/animations.yaml delta goes
    std::vector<std::string> addedAnimations;     // filled in Phase 1 (decision A) — empty in Phase 0
};

struct BundlePlan {
    std::string               bundleName;   // owning mod (attributed) or the raw code
    std::vector<std::string>  codes;
    std::vector<GraphChange>  graphs;
    std::vector<RosterChange> rosters;
};

struct PatchPlan {
    std::vector<BundlePlan> bundles;
    std::vector<std::string> warnings;      // unresolved graphs, stem collisions, etc. (surfaced in the dump)
};

// Inputs are the structures ConvertLoadOrder has already computed at the call site (after bundle
// grouping) — BuildPlan does not re-discover, it normalizes. `patchDir(code, graph)` returns the
// Nemesis patch dir path for a graph in a code (empty string when absent); `precompiledByBundle`
// maps a bundle name to its precompiled loose-graph meshes-relative prefixes (lowercase).
struct PlanInputs {
    const BaseMaps*                                                  base = nullptr;
    const std::vector<std::string>*                                  graphs = nullptr;    // third-person template stems
    const std::vector<std::string>*                                  fpGraphs = nullptr;  // first-person template stems
    const std::vector<std::string>*                                  bundleOrder = nullptr;
    const std::unordered_map<std::string, std::vector<std::string>>* bundleCodes = nullptr;
    const std::unordered_map<std::string, std::vector<std::string>>* precompiledByBundle = nullptr;
    std::function<std::string(const std::string& code, const std::string& graph)>   patchDir;
    std::function<std::string(const std::string& code, const std::string& fpGraph)> fpPatchDir;
};

PatchPlan BuildPlan(const PlanInputs& in);

// Human-readable dump for inspection (D:\cb-diffs\patchplan.txt). Behavior-neutral.
void DumpPlan(const PatchPlan& plan, const std::string& outFile);

}  // namespace bconv
