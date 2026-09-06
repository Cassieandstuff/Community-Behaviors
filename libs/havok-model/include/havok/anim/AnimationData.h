#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// AnimationData — a first-party model + parser/emitter for Skyrim's
// animationdatasinglefile.txt (clip-generator + root-motion metadata), plus a
// reader for the Nemesis per-project patch form and a load-order merge.
//
// This is the 4th animation-system leg (after behavior graphs, the character
// animationNames roster, and animationsetdata). It is what Nemesis/Pandora
// regenerate to register a mod's new clips. The format is purely POSITIONAL and
// count-prefixed (CRLF, every line CRLF-terminated incl. the last, no BOM), so a
// single miscount shifts everything — the parser is validated by a parse -> emit ->
// byte-identical round-trip against real vanilla data. Pure C++ (std only), so it
// unit-tests standalone and links into the plugin unchanged.
//
// Grammar (verified against vanilla animationdatasinglefile.txt — 429 projects,
// 10,597 clip generators, 6,725 motion records):
//
//   FILE     := N CRLF  name x N  projectData x N       // N=project count; data in name order
//   projectData := sectionA [ sectionB ]                // B present iff hasAnimData==1
//
//   sectionA := aLineCount CRLF  aBody                  // aBody is EXACTLY aLineCount lines
//   aBody    := "1"                                     // fieldX — constant 1 in vanilla
//               K ; assetPath x K                       // K asset paths (may contain spaces)
//               hasAnimData                             // 0 or 1
//               clipGenerator x *                       // iff hasAnimData==1; fills rest of aBody
//   sectionB := bLineCount CRLF  bBody                  // bBody is EXACTLY bLineCount lines
//   bBody    := motionRecord x *
//
//   clipGenerator := name; animIndex; playbackSpeed; cropStart; cropEnd;
//                    T ; trigger x T ("Event:time"); BLANK          // 7 + T lines
//   motionRecord  := animIndex; duration; TC ; tKey x TC ("t x y z");
//                    RC ; rKey x RC ("t x y z w"); BLANK            // 5 + TC + RC lines
//
// aLineCount/bLineCount count the body LINES that follow, INCLUDING the blank
// separator after every record (incl. the last). Header-only projects (hasAnimData
// ==0, 380 of 429) have aLineCount == K+3 and no section B. animIndex is the internal
// cross-reference key linking a clip generator to its motion record (same index).
//
// NUMERIC FIELDS ARE CARRIED VERBATIM (never reparsed/reformatted) — floats appear as
// "0.014", "34.7115", and scientific notation "5.96046e-008"; only counts are recomputed.
namespace havok::animdata {

    struct ClipGenerator {
        std::string              name;           // "MainIdle", "Idle Fulbody2[mirror]"
        std::string              animIndex;      // verbatim int (main) or symbol "$" (patch)
        std::string              playbackSpeed;  // verbatim float
        std::string              cropStart;      // verbatim float
        std::string              cropEnd;        // verbatim float
        std::vector<std::string> triggers;       // "Event:time" lines, verbatim
        // The animation this clip plays (== roster[animIndex], e.g. "Animations\\X.hkx"). The editable
        // clips.yaml keys on this NAME, not the hardwired animIndex — the index is resolved from the
        // name against the character's roster (animationNames) at compile (ResolveClipIndices), so it
        // can't drift when the roster shifts. Empty when animIndex has no unique roster entry (a rare
        // duplicate path or an out-of-range index), which keeps its raw animIndex. Not in the .txt — a
        // YAML-authoring field only.
        std::string              animation;
    };

    struct MotionRecord {
        std::string              animIndex;      // verbatim int (main) or symbol "$" (patch)
        std::string              duration;       // verbatim float
        std::vector<std::string> translations;   // "t x y z" lines (4 floats), verbatim
        std::vector<std::string> rotations;      // "t x y z w" lines (5 floats), verbatim
        // The clip-generator NAME this motion belongs to (== the clip at animIndex). The editable
        // motion.yaml keys on this NAME, not the hardwired animIndex — the index is resolved from the
        // name against the project's clips at compile (ResolveMotionIndices), so it can't drift when
        // the roster shifts. Empty for the rare unnamed hybrid blocks (mounted combat), which keep
        // their animIndex as the only key. Not part of the .txt — a YAML-authoring field only.
        std::string              animation;
    };

    struct Project {
        std::string                name;         // "DefaultMale.txt" (identity + order)
        std::string                fieldX = "1"; // constant leading "1"; kept verbatim
        std::vector<std::string>   assetPaths;   // "Behaviors\\X.hkx", "Character Assets\\skeleton.HKX"
        bool                       hasAnimData = false;  // fieldY
        std::vector<ClipGenerator> clips;        // section A body (iff hasAnimData)
        std::vector<MotionRecord>  motions;      // section B body (iff hasAnimData)
    };

    struct SingleFile {
        std::vector<Project> projects;           // order is significant (indexed positionally)
    };

    struct ParseError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    // Parse / emit the whole animationdatasinglefile.txt. Emit reproduces the canonical
    // CRLF layout byte-for-byte (counts recomputed from structure; content lines verbatim).
    SingleFile  ParseSingleFile(std::string_view text);
    std::string EmitSingleFile(const SingleFile& sf);

    // ── Per-project (dev) form: DirList.txt + <Project>.txt + BoundAnims\Anims_<Project>.txt ──
    // The engine's non-collated loader (ShouldLoadCollatedAnimTextData==0) reads these instead of
    // the single blob. They are the collated per-project blocks with the line-COUNT prefixes
    // stripped (the per-project reader reads each file whole, so no aLineCount/bLineCount): the
    // clip file is the A-section (fieldX, assets, hasAnimData, clips), the motion file is the
    // B-section (motion records) — exactly the layout of vanilla Meshes\AnimationData\.

    std::string EmitDirList(const SingleFile& sf);        // one project name per line
    std::string EmitProjectClips(const Project& p);       // -> AnimationData\<Project>.txt
    std::string EmitProjectMotion(const Project& p);      // -> BoundAnims\Anims_<Project>.txt (empty if no motion)

    // ── Nemesis patch form (per-project dirs) ────────────────────────────────────
    // A mod ships Nemesis_Engine/mod/<code>/animationdatasinglefile/<Project>~<n>/<file>.txt,
    // one whole record per file, with the integer animIndex replaced by a symbol "<code>$<N>":
    //   <ClipName>~<code>$<N>.txt  -> a clipGenerator (line 1 repeats the clip name)
    //   <code>$<N>.txt             -> a motionRecord (stem IS the symbol)
    // Clip and motion sharing a symbol are one logical addition. triggerCount in clip files is
    // frequently WRONG, so triggers are read as the actual "Event:time" lines until blank/EOF.
    struct PatchAddition {
        std::string  symbol;   // "<code>$<N>" — links the pair
        ClipGenerator clip;    // animIndex == symbol
        MotionRecord  motion;  // animIndex == symbol
        bool         hasClip   = false;
        bool         hasMotion = false;
    };

    // One project's worth of additions parsed from a <Project>~<n> patch dir.
    struct ProjectPatch {
        std::string                projectName;  // target project WITHOUT ".txt" (e.g. "DefaultMale")
        std::vector<PatchAddition> additions;    // one per symbol
    };

    // Assemble a ProjectPatch from a <Project>~<n> patch dir's files, passed as (filename,
    // content) pairs (the caller does the filesystem walk). The target project is the dir
    // name up to '~'. A file whose stem contains '~' is a clip generator, otherwise a motion
    // record; the two pair by the "<code>$<N>" symbol each file carries. Additions keep the
    // input file order. Throws ParseError on a malformed file.
    ProjectPatch AssembleProjectPatch(std::string_view projectDirName,
                                      const std::vector<std::pair<std::string, std::string>>& files);

    // Parse a single clip-generator / motion patch file body (symbol-first). Used by the
    // patch-dir loader; exposed for testing. Throws ParseError on malformed input.
    ClipGenerator ParsePatchClip(std::string_view text, std::string& outSymbol);
    MotionRecord  ParsePatchMotion(std::string_view text, std::string& outSymbol);

    // ── Merge (load-order overlay) ───────────────────────────────────────────────
    struct MergeStats {
        std::size_t projectsPatched = 0;
        std::size_t clipsAdded = 0;
        std::size_t motionsAdded = 0;
    };

    // Largest numeric animIndex across EVERY project (clips + motions); -1 if none. Seed the
    // merge counter with this + 1.
    long long GlobalMaxIndex(const SingleFile& base);

    // Overlay one project patch onto base (matched by project name, case-insensitive, with or
    // without ".txt"). Each addition's symbol is assigned the next value of a_nextIndex (post-
    // incremented), substituted into BOTH the clip and its motion, then appended to that
    // project's section A / section B.
    //
    // The animIndex must sit in the HIGH mod-clip band (just under 2^15), NOT continue from the
    // project's local vanilla max: at char-setup a clip's animIndex shares an index space with
    // the character's animation-binding table, so a low index collides with the roster range and
    // crashes (rep stosq, confirmed in-game 2026-08-11: ~1656+ crash, Pandora's ~32568+ work).
    // The engine's motion tables are PER-CHARACTER, so the same high band may be reused across
    // characters (Pandora does — DefaultMale and DefaultFemale ranges overlap). The caller seeds
    // a_nextIndex high (see ServeAnimData: 32768 - totalAdditions) and threads one counter.
    //
    // A patch targeting a header-only project flips hasAnimData and creates section B. Returns
    // false + leaves base untouched (and a_nextIndex unchanged) if the project isn't found.
    bool MergeProjectPatch(SingleFile& base, const ProjectPatch& patch, MergeStats& stats,
                           long long& a_nextIndex);

}  // namespace havok::animdata
