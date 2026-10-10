#pragma once
// AnimDataFromBehavior — animationdata DeriveClipInputs from a behavior graph's clip generators (the merged
// SchemaObject graph the runtime compiles), plus the per-project derive over them.
//
// This covers the CLIP-GENERATOR half of a DeriveClipInput (name, animationName, playback speed, crop, and the
// clip generator's OWN triggers). The animation ANNOTATION triggers (merged from each animation's annotation
// track) and the roster are supplied by the caller — the graph reader does not read animation files.

#include <compile/AnimDataDeriver.h>   // animdata::DeriveClipInput, ClipGenerator, DeriveClipList
#include <codec/serialization/packfile/IHavokObject.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CB::engine::compile {

    /// @brief One DeriveClipInput per hkbClipGenerator reachable from a merged graph's `root` (the reflective path's
    ///        SchemaObject graph), trigger event ids resolved through the graph's eventNames; in clip-name order.
    std::vector<CB::engine::animdata::DeriveClipInput> DeriveClipInputsFromGraph(
        const std::shared_ptr<CB::engine::codec::IHavokObject>& root);

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
    std::vector<CB::engine::animdata::ClipGenerator> DeriveProjectClipList(
        std::vector<CB::engine::animdata::DeriveClipInput>&                       clips,
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
    CB::engine::animdata::Project DeriveProjectAnimData(
        const std::string&                                                  projectName,   // e.g. "DefaultMale" (".txt" appended)
        std::vector<CB::engine::animdata::DeriveClipInput>&                      clips,
        const std::vector<std::string>&                                     roster,
        const std::vector<CB::engine::animdata::MotionRecord>&                   motions,
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
    CB::engine::animdata::ProjectPatch DeriveProjectPatch(
        const std::string&                                                  projectName,
        std::vector<CB::engine::animdata::DeriveClipInput>&                      clips,
        const std::string&                                                  symbolCode,
        const std::function<std::vector<std::uint8_t>(const std::string&)>& readAnim,
        const std::unordered_map<std::string, CB::engine::animdata::MotionRecord>* animMotions = nullptr);

}  // namespace CB::engine::compile
