#pragma once
// AnimDataYaml — the editable motion form (motion/*.yaml) <-> the animationdata motion model.
//
// A project's ROOT-MOTION records are the un-derivable half of the animationdata cache (clips
// derive from the graph; motion comes from the animation). This decomposes them to per-project
// YAML, keyed by animationName (roster[animIndex]) so a reader can bind a motion to the clip
// that plays it. Values are carried VERBATIM (the cache's own %g strings), so the round-trip is
// byte-exact — no quaternion<->yaw float drift. The yaw-editable authoring form is a future
// convenience layered on top; the vanilla decompose ships the exact form.

#include "havok/anim/AnimationData.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace havok::animdata {

    // One project's motion records -> motion.yaml text. `roster` maps each motion's animIndex to its
    // clip-generator NAME (pass the project's clip labels by index). A named record is keyed ONLY by
    // `animation:` (the hardwired index is dropped — it's resolved from the name at compile); a record
    // whose index has no name (unnamed hybrid block) falls back to `index:`.
    std::string EmitMotionYaml(const Project& project, const std::vector<std::string>& roster);

    // motion.yaml -> the motion records (verbatim), for the round-trip gate + the runtime merge. Named
    // records carry `animation` with animIndex empty (resolve via ResolveMotionIndices); unnamed carry
    // animIndex. Never throws — malformed input leaves `err` set and returns what parsed.
    std::vector<MotionRecord> ParseMotionYaml(const std::string& text, std::string& err);

    // Resolve each motion's animIndex from its `animation` (clip NAME) against `clips` (the project's
    // clip generators, name -> animIndex). A record with no name (unnamed block) or an already-set
    // index is left untouched. Returns the count whose name was NOT found in `clips` (an authored
    // motion for a clip that doesn't exist — a real error the caller should surface). This is the
    // de-hardwire join: the name is the stable key, the index is derived here so it can't drift.
    std::size_t ResolveMotionIndices(std::vector<MotionRecord>& motions, const std::vector<ClipGenerator>& clips);

    // ── Per-animation motion sidecars ────────────────────────────────────────────
    // Motion is a property of the animation FILE (proven project-invariant), so the clean form is
    // ONE sidecar per animation, keyed by the animation's canonical path, with the animIndex bound
    // per-project at COMPILE time from the roster. A shared animation (e.g. sharedkillmoves) then
    // yields a single sidecar that every referencing project binds to its own index.

    // One motion record -> sidecar YAML. Carries duration + translation/rotation VERBATIM; drops
    // animIndex (the sidecar's PATH is the animation key, the index is a per-project bind).
    std::string EmitMotionSidecar(const MotionRecord& m);

    // sidecar YAML -> one motion record (animIndex left empty; the caller binds it from the roster
    // position). Never throws — malformed input leaves `err` set and returns what parsed.
    MotionRecord ParseMotionSidecar(const std::string& text, std::string& err);

    // Normalize ONE labeled motion sample ("t: 0, x: 1, …" or the legacy bare "t x y z") back to the
    // canonical space-joined verbatim token the model + .txt carry. Idempotent on bare input. Exposed
    // so an out-of-file parser (e.g. the animation.yaml `motion:` block in AnimationYamlLoader) stays
    // byte-identical to ParseMotionSidecar without re-implementing the unlabel rule.
    std::string UnlabelSample(const std::string& labeledOrBare);

    // Inverse of UnlabelSample: present a canonical "t x y z" verbatim sample with axis labels
    // ("t: <t>, x: <x>, …") for readable, byte-stable YAML. Exposed so an out-of-file EMITTER (the
    // animation.yaml inline `motion:` block emitted by hky-utils) stays byte-identical to
    // EmitMotionSidecar without re-implementing the label rule.
    std::string LabelSample(const std::string& verbatim);

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
    //       index.yaml           per-PROJECT manifest: name + assets + hasAnimData (ALL 429; the
    //                            380 header-only projects are fully described here, no body file)
    //       clips/<stem>.yaml    BAKED residue: the clip generators (49 projects). Baked until the
    //                            Part-B graph-derivation covers the base; then this shrinks away.
    //       motion/<stem>.yaml   AUTHORED: root motion (EmitMotionYaml, keyed by clip name)
    //
    // "Author what reverses, bake what doesn't": motion round-trips to editable YAML; the clip list
    // (and the project header) is baked verbatim for now. Assemble(index+clips+motion) -> EmitSingleFile
    // reproduces vanilla byte-for-byte (gate: havok-core-cli animdata-tree-roundtrip). `<stem>` is the
    // lowercased project name without ".txt" ("DefaultMale.txt" -> "defaultmale").

    // One project's structural header (everything in section A except the clip generators). The 380
    // header-only projects ARE just this; the 49 with hasAnimData carry clips/ + motion/ bodies too.
    struct ProjectHeader {
        std::string              name;              // "DefaultMale.txt" (identity + order)
        std::string              fieldX = "1";      // constant leading "1"; carried only if it differs
        std::vector<std::string> assets;            // "Behaviors\\X.hkx", "Character Assets\\skeleton.HKX"
        bool                     hasAnimData = false;
        std::string              character;          // meshes-rel character unit (roster source) for the
                                                     // clip de-hardwire; empty if unresolved/no anim data
    };

    // The lowercased project name without ".txt" — the key clips/ and motion/ are named by.
    std::string StemForProjectName(const std::string& projectName);

    // One project's resolved character: the meshes-relative character-unit ref (e.g.
    // "actors/ambient/chicken/characters/chickencharater.hkx") + its animationNames roster.
    struct ProjectCharacter { std::string ref; std::vector<std::string> roster; };

    // Map each project stem -> its character (roster source) by ACTOR-FOLDER co-location, over a
    // DECOMPOSED tree under <treeMeshesDir> (reads animations.txt directly, no decompile). Resolution
    // order: (1) a character unit named "<P>.hkx" (humanoid: DefaultMale -> characters/defaultmale.hkx);
    // (2) a PROJECT unit "<P>.hkx" (has project.yaml) locates the actor root, use its co-located
    // character (creature: ChickenProject -> chickenproject.hkx -> actors/ambient/chicken ->
    // characters/chickencharater.hkx, beating the name/typo mismatch); (3) "<P>project|data" char;
    // (4) an actor FOLDER named "<P>" (ChaurusFlyer/HMDaedra: char is "chaurusflyercharacter" but lives
    // in .../chaurusflyer/). A project resolving to none (e.g. rosterless canine tagfile chars) is
    // absent from the map -> its clips keep raw indices. Shared by the converter (decompose over
    // stageHky) and the havok-core-cli tools; the runtime instead reads the `character:` header the
    // decompose writes and loads that roster directly.
    std::map<std::string, ProjectCharacter> LoadProjectCharacters(const std::string& treeMeshesDir);

    // index.yaml: the per-project manifest — name + assets + hasAnimData + `character:` (the roster the
    // project's clip indices key into, meshes-relative). `charRefByStem` supplies the character ref per
    // project stem (from LoadProjectCharacters); a stem absent from it emits no `character:` (its clips
    // then keep raw indices). Order is inert (SortAnimdata) but preserved here for free.
    std::string                EmitAnimdataIndexYaml(const SingleFile& sf,
                                                     const std::map<std::string, std::string>& charRefByStem);
    std::vector<ProjectHeader> ParseAnimdataIndexYaml(const std::string& text, std::string& err);

    // ── Per-CLIP files (clips/<project>/<clipname>.yaml) ─────────────────────────
    // Each clip generator is ONE file, keyed by its name (the FILENAME — unique within a project,
    // filesystem-legal), so an edit touches one clip. The body carries animation|index / speed / crop /
    // triggers; the clip's NAME is the filename (not repeated in the body), and its animIndex is
    // resolved from the roster at compose. Motion decomposes the same way (motion/<project>/<key>.yaml)
    // via the existing EmitMotionSidecar/ParseMotionSidecar. Cache order is inert, so no order manifest.

    // One clip generator -> its file body. Keyed by animation name (roster[animIndex]); falls back to
    // `index:` when the index has no unique roster entry. `roster` may be empty (all -> index). Set
    // `writeName` to also emit `name:` in the body — needed when the clip's filename had to be
    // disambiguated (two clip names collide on a case-insensitive filesystem, e.g. "Crossbow_IdleHeld"
    // vs "CrossBow_IdleHeld"), so the true name survives regardless of the filename.
    std::string   EmitClipYaml(const ClipGenerator& clip, const std::vector<std::string>& roster,
                               bool writeName = false);

    // One clip file body -> a ClipGenerator. `name` is filled ONLY if the body carries `name:` (the
    // disambiguated case); otherwise it's left empty for the caller to set from the filename. animIndex
    // is resolved later via ResolveClipIndices. Never throws; malformed leaves `err` set.
    ClipGenerator ParseClipYaml(const std::string& text, std::string& err);

    // A collision-free filename stem for `base` given the stems already used in a directory. Normalizes
    // case-insensitively (+ strips trailing '.'/' ' the way Windows would) so distinct names that would
    // clash on a case-insensitive filesystem (e.g. "Crossbow_IdleHeld" vs "CrossBow_IdleHeld") get
    // distinct files; on clash appends "__2", "__3", …. Inserts the result's normalized key into `used`.
    // Returns `base` unchanged in the common (no-clash) case, so the caller can detect disambiguation
    // (result != base) and persist the true name in-body.
    std::string UniqueFileName(const std::string& base, std::set<std::string>& used);

    // Resolve each clip's animIndex from its `animation` (roster path) against `roster` (the character's
    // animationNames): animIndex = first index of `animation` in `roster`. A clip with no `animation`
    // (raw-index fallback) or already-set index is left untouched. Returns the count whose animation
    // was NOT found in the roster (a real error the caller should surface). The de-hardwire join for
    // clips — the name is the stable key, the index derives here so it can't drift.
    std::size_t ResolveClipIndices(std::vector<ClipGenerator>& clips, const std::vector<std::string>& roster);

    // Reorder a SingleFile into a canonical, order-INDEPENDENT form: projects by name, and within each
    // Sorts clips by name + motions by numeric animIndex WITHIN each project (both inert — clips bind by
    // name, motions by explicit animIndex value, never by list position). PROJECT order is NOT inert:
    // an in-game test proved it (composing the cache in alphabetical project order A-posed every actor
    // + garbled root motion — the engine binds an actor's animationdata block by project INDEX). The old
    // "429 vs 49 rules out positional cross-reference" argument only disproves animdata<->setdata
    // parallelism, not animdata SELF-indexing. So `sortProjects` defaults true only for the CLI's
    // order-independent multiset GATE; the compose path passes false to KEEP the source index.yaml
    // (vanilla) project order.
    void SortAnimdata(SingleFile& sf, bool sortProjects = true);

    // Stitch the decomposed folder back into a SingleFile. `headers` gives the full project manifest;
    // `clipsByStem`/`motionByStem` supply the bodies for the hasAnimData projects;
    // `rostersByStem` supplies each project's character roster (animationNames). Clip animIndices are
    // resolved against the roster (ResolveClipIndices), THEN motion animIndices against the resolved
    // clips (ResolveMotionIndices) — the two de-hardwire joins. EmitSingleFile on the result
    // reproduces vanilla byte-for-byte.
    SingleFile AssembleAnimdata(
        const std::vector<ProjectHeader>& headers,
        const std::map<std::string, std::vector<ClipGenerator>>& clipsByStem,
        const std::map<std::string, std::vector<MotionRecord>>& motionByStem,
        const std::map<std::string, std::vector<std::string>>& rostersByStem);

}  // namespace havok::animdata
