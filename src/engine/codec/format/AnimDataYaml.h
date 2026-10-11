#pragma once
// AnimDataYaml — the editable motion form (motion/*.yaml) <-> the animationdata motion model.
//
// A project's ROOT-MOTION records are the un-derivable half of the animationdata cache (clips
// derive from the graph; motion comes from the animation). This decomposes them to per-project
// YAML, keyed by animationName (roster[animIndex]) so a reader can bind a motion to the clip
// that plays it. Values are carried VERBATIM (the cache's own %g strings), so the round-trip is
// byte-exact — no quaternion<->yaw float drift. The yaw-editable authoring form is a future
// convenience layered on top; the vanilla decompose ships the exact form.

#include <interface/AnimationData.h>
#include <interface/MotionSample.h>   // Label/UnlabelSample (the sample-label codec) — re-exported for callers
#include <codec/motion/MotionFromAmr.h> // MotionFromAmrAnnotations moved to codec/motion (3f-2), re-exported here

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CB::engine::animdata {

    // One project's motion records -> motion.yaml text. `roster` maps each motion's animIndex to its
    // clip-generator NAME (pass the project's clip labels by index). A named record is keyed ONLY by
    // `animation:` (the hardwired index is dropped — it's resolved from the name at compile); a record
    // whose index has no name (unnamed hybrid block) falls back to `index:`.
    std::string EmitMotionYaml(const Project& project, const std::vector<std::string>& roster);

    // motion.yaml -> the motion records (verbatim), for the round-trip gate + the runtime merge. Named
    // records carry `animation` with animIndex empty; unnamed carry
    // animIndex. Never throws — malformed input leaves `err` set and returns what parsed.
    std::vector<MotionRecord> ParseMotionYaml(const std::string& text, std::string& err);

    // ── Per-animation motion sidecars ────────────────────────────────────────────
    // Motion is a property of the animation FILE (proven project-invariant), so the clean form is
    // ONE sidecar per animation, keyed by the animation's canonical path, with the animIndex bound
    // per-project at COMPILE time from the roster. A shared animation (e.g. sharedkillmoves) then
    // yields a single sidecar that every referencing project binds to its own index.

    // One motion record -> sidecar YAML. Carries duration + translation/rotation VERBATIM; drops
    // animIndex (the sidecar's PATH is the animation key, the index is a per-project bind).
    std::string EmitMotionSidecar(const MotionRecord& m);

    // (MotionFromAmrAnnotations — AMR annotations -> a CB MotionRecord — moved to <codec/motion/
    //  MotionFromAmr.h> in firesale phase 3f-2; re-exported via the include above.)

    // sidecar YAML -> one motion record (animIndex left empty; the caller binds it from the roster
    // position). Never throws — malformed input leaves `err` set and returns what parsed.
    MotionRecord ParseMotionSidecar(const std::string& text, std::string& err);

    // (Label/UnlabelSample — the sample-label codec — moved to <interface/MotionSample.h>, included above.)

    // Resolve a roster animation entry (e.g. "Animations\male\foo.hkx" or the relative
    // "..\sharedkillmoves\x.hkx") against the actor root (the character's meshes-relative dir, e.g.
    // "actors/character") into the canonical meshes-relative key: lowercase, '/'-separated, '.'/'..'
    // collapsed. Two projects referencing the same shared animation resolve to the SAME key, so a
    // shared killmove maps to one sidecar. This is the sidecar path (minus the ".yaml" suffix).
    std::string CanonicalAnimPath(const std::string& actorRoot, const std::string& rosterEntry);

    // ── animationdatasinglefile.txt as a decomposed FOLDER (the ".txt/" tree) ────────
    // The monolithic clip+motion cache decomposes into a folder carrying its own name, the same
    // convention a "<name>.hkx" behavior decomposes into a "<name>.hkx/" folder (and the sibling
    // animationsetdatasinglefile.txt/ folder):
    //
    //     meshes/animationdatasinglefile.txt/
    //       index.yaml           per-PROJECT manifest: name + assets + hasAnimData + `character:`
    //                            (ALL 429 projects) — the only file the base ships
    //
    // Clips and motion are not shipped: the runtime projects them (ProjectAnimData) from the graphs,
    // rosters and animation units, each animation carrying its own root motion. `<stem>` is the
    // lowercased project name without ".txt" ("DefaultMale.txt" -> "defaultmale").

    // One project's structural header (everything in section A except the clip generators). The 380
    // header-only projects ARE just this.
    struct ProjectHeader {
        std::string              name;              // "DefaultMale.txt" (identity + order)
        std::string              fieldX = "1";      // constant leading "1"; carried only if it differs
        std::vector<std::string> assets;            // "Behaviors\\X.hkx", "Character Assets\\skeleton.HKX"
        bool                     hasAnimData = false;
        std::string              character;          // meshes-rel character unit (roster source) for the
                                                     // clip de-hardwire; empty if unresolved/no anim data
    };

    // The lowercased project name without ".txt" — the per-project key (index.yaml `character:` map).
    std::string StemForProjectName(const std::string& projectName);

    // One project's resolved character: the meshes-relative character-unit ref (e.g.
    // "actors/ambient/chicken/characters/chickencharater.hkx") + its animationNames roster.
    struct ProjectCharacter { std::string ref; std::vector<std::string> roster; };

    // Map each project stem -> its character (roster source) by ACTOR-FOLDER co-location, over a
    // DECOMPOSED tree under <treeMeshesDir>. The unit format is the caller's: `rosterOf(unit)` reads a
    // character unit's animationNames, `isProject(unit)` tells a project unit. Resolution
    // order: (1) a character unit named "<P>.hkx" (humanoid: DefaultMale -> characters/defaultmale.hkx);
    // (2) a PROJECT unit "<P>.hkx" locates the actor root, use its co-located
    // character (creature: ChickenProject -> chickenproject.hkx -> actors/ambient/chicken ->
    // characters/chickencharater.hkx, beating the name/typo mismatch); (3) "<P>project|data" char;
    // (4) an actor FOLDER named "<P>" (ChaurusFlyer/HMDaedra: char is "chaurusflyercharacter" but lives
    // in .../chaurusflyer/). A project resolving to none (e.g. rosterless canine tagfile chars) is
    // absent from the map -> its clips keep raw indices. Used by the converter (decompose over
    // stageHky); the runtime instead reads the `character:` header the decompose writes and loads that
    // roster directly.
    std::map<std::string, ProjectCharacter> LoadProjectCharacters(
        const std::string& treeMeshesDir,
        const std::function<std::vector<std::string>(const std::filesystem::path& unit)>& rosterOf,
        const std::function<bool(const std::filesystem::path& unit)>& isProject);

    // index.yaml: the per-project manifest — name + assets + hasAnimData + `character:` (the roster the
    // project's clip indices key into, meshes-relative). `charRefByStem` supplies the character ref per
    // project stem (from LoadProjectCharacters); a stem absent from it emits no `character:` (its clips
    // then keep raw indices). Order is preserved.
    std::string                EmitAnimdataIndexYaml(const SingleFile& sf,
                                                     const std::map<std::string, std::string>& charRefByStem);
    std::vector<ProjectHeader> ParseAnimdataIndexYaml(const std::string& text, std::string& err);

}  // namespace CB::engine::animdata
