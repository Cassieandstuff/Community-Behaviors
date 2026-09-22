#pragma once
// AnimDataFromBehavior — extract animationdata DeriveClipInputs from an in-memory
// BehaviorData (the model YamlBehaviorLoader produces and Community Behaviors compiles at
// runtime).
//
// This is the RUNTIME counterpart to the offline animdata-derive path: the offline oracle
// decompiles each behavior .hkx and parses clips/<id>.yaml into DeriveClipInput; the runtime
// already holds the same clip generators in BehaviorData::clips, so it reads them straight
// from the model. Same DeriveClipInput, same downstream DeriveClipList — only the SOURCE of
// the clip-generator fields differs (model vs files). Written here in havok-core so both
// paths share it (the omnidirectional rule: one transform, two callers).
//
// This covers the CLIP-GENERATOR half of a DeriveClipInput (name, animationName, playback
// speed, crop, and the clip generator's OWN triggers). The animation ANNOTATION triggers
// (merged from each animation's annotation track) and the roster are supplied by the caller,
// exactly as the offline deriveOneProject does — this function does not read animation files.

#include "havok/anim/AnimDataDeriver.h"   // animdata::DeriveClipInput, ClipGenerator, DeriveClipList

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace havok::model { struct BehaviorData; }

namespace havok::sct {

    // One DeriveClipInput per hkbClipGenerator in `data` (input order = the map's key order).
    // Triggers here are the clip generator's authored triggers only; the caller appends
    // annotation triggers and passes the roster to animdata::DeriveClipList.
    std::vector<havok::animdata::DeriveClipInput> DeriveClipInputsFromBehavior(
        const havok::model::BehaviorData& data);

    // Read clip generators straight out of a decomposed behavior unit's clips/ directory into
    // DeriveClipInputs — WITHOUT loading (or base-merging) the full graph. A mod's behavior DELTA unit
    // has no behavior.yaml (so YamlBehaviorLoader can't load it standalone), yet its clips/<id>.yaml
    // ARE complete hkbClipGenerator nodes carrying name + animationName + crop + speed + own-triggers.
    // This is how the converter recovers a Nemesis mod's added clips (e.g. BFCO's attacks in
    // 1hm_behavior.hkx/clips/) that a whole-graph load can't reach. Non-clip / malformed files are
    // skipped; never throws.
    std::vector<havok::animdata::DeriveClipInput> ReadClipInputsFromClipsDir(const std::string& clipsDir);

    // An animation's derivation-relevant data: its duration (m_duration — present even when the
    // animation carries no root-motion record) and its annotation-track events (text + time).
    // Skyrim bakes physical triggers (footsteps, weapon sounds) into the annotation track, and
    // the cache merges them into the clip's trigger list — so cache triggers = graph triggers
    // UNION these. The caller feeds each as a DeriveClipInput::Trigger with fromAnnotation=true;
    // animdata::DeriveClipList then applies the cache's annotation post-processing (drop AnimObj*,
    // keep the name before the first '.').
    struct AnimClipInfo {
        bool                                        has = false;    // a valid hkaAnimation was read
        double                                      duration = 0.0;
        std::vector<std::pair<std::string, double>> annotations;    // (event text, time)
    };

    // Extract AnimClipInfo from an animation .hkx's BYTES — the runtime form (Community Behaviors
    // reads animation bytes via the VFS / BSA / loose files, not a path). Same extraction the
    // offline GetAnimInfo does from a file, so both share it. Never throws (any failure ->
    // has=false).
    AnimClipInfo ExtractAnimClipInfo(const std::vector<std::uint8_t>& hkxBytes);

    // Assemble ONE project's derived clip list — the shared runtime+offline transform. For each
    // clip input it merges the animation's annotation triggers + real duration (read via
    // `readAnim`, which returns the animation .hkx bytes for an animationName, or {} if the
    // animation isn't present — a BSA-packed vanilla anim a runtime with only loose access can't
    // read simply yields no annotations), then projects via animdata::DeriveClipList. `clips`
    // triggers are extended in place; `motionDur` (animIndex -> duration, seeded from the master
    // motion table) is updated. Each distinct animation is read+parsed once (cached). The caller
    // supplies the reader (VFS/loose at runtime, filesystem offline) and the roster in order.
    std::vector<havok::animdata::ClipGenerator> DeriveProjectClipList(
        std::vector<havok::animdata::DeriveClipInput>&                       clips,
        const std::vector<std::string>&                                     roster,
        std::unordered_map<int, double>&                                    motionDur,
        const std::function<std::vector<std::uint8_t>(const std::string&)>& readAnim,
        std::vector<std::string>*                                           unresolved = nullptr);

    // BUILD-TIME derive of one project's whole animationdata from its editable sources: the clip
    // list is DERIVED from the graph (clips + annotations via readAnim), and the motion records
    // are carried from `motions` (a mod's motion.yaml, or the vanilla decompose). Motion durations
    // seed the trigger clamp. This is the transform the converter/editor runs at ship time so the
    // bundle carries a ready animationdata delta and the runtime only merges (never derives). The
    // caller supplies the roster + the animation-bytes reader.
    havok::animdata::Project DeriveProjectAnimData(
        const std::string&                                                  projectName,   // e.g. "DefaultMale" (".txt" appended)
        std::vector<havok::animdata::DeriveClipInput>&                      clips,
        const std::vector<std::string>&                                     roster,
        const std::vector<havok::animdata::MotionRecord>&                   motions,
        const std::function<std::vector<std::uint8_t>(const std::string&)>& readAnim);

    // Derive a SYMBOL-FORM ProjectPatch (a mergeable animationdata delta) for a set of ADDED
    // clips — the runtime counterpart to a Nemesis-tool patch, but derived from the graph. The
    // clip RECORDS (name / crop / playback / triggers, incl. annotation triggers from readAnim)
    // come from the shared DeriveProjectClipList so they can't drift from the offline derive; but
    // the animIndex is NOT resolved to the roster (a mod clip's roster position lands in the
    // char-setup crash band) — each addition carries a symbol "<code>$<N>", and the caller merges
    // via animdata::MergeProjectPatch, which allocates the safe HIGH band. `symbolCode` is the delta's
    // prefix (e.g. "fnis"); `projectName` is WITHOUT ".txt".
    //
    // Motion: by DEFAULT an in-place (zero-translation, identity-rotation) sample at the clip's
    // duration — correct for FNIS-style idles. But when `animMotions` is supplied (keyed by the clip's
    // animationName, lowercased with '\\'->'/'), a clip whose animation carries a real root-motion
    // record (a BR-native animation.yaml `motion:` block, extracted into AnimationDef.motion) gets THAT
    // motion instead of the placeholder — so editor-authored root motion aligns with its clip at derive
    // time (the intended "derive high-band-aligns motion with clips" path, not a runtime override).
    havok::animdata::ProjectPatch DeriveProjectPatch(
        const std::string&                                                  projectName,
        std::vector<havok::animdata::DeriveClipInput>&                      clips,
        const std::string&                                                  symbolCode,
        const std::function<std::vector<std::uint8_t>(const std::string&)>& readAnim,
        const std::unordered_map<std::string, havok::animdata::MotionRecord>* animMotions = nullptr);

}  // namespace havok::sct
