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
namespace CB::engine::animdata {

    struct ClipGenerator {
        std::string              name;           // "MainIdle", "Idle Fulbody2[mirror]"
        std::string              animIndex;      // verbatim int (main) or symbol "$" (patch)
        std::string              playbackSpeed;  // verbatim float
        std::string              cropStart;      // verbatim float
        std::string              cropEnd;        // verbatim float
        std::vector<std::string> triggers;       // "Event:time" lines, verbatim
        // The animation this clip plays (== roster[animIndex], e.g. "Animations\\X.hkx"). The editable
        // clips.yaml keys on this NAME, not the hardwired animIndex — the index is resolved from the
        // name against the character's roster (animationNames) at compile, so it
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
        // name against the project's clips at compile, so it can't drift when
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

}  // namespace CB::engine::animdata
