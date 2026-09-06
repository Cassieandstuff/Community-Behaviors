#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// AnimationSetData — a first-party model + parser/emitter for Skyrim's
// animationsetdatasinglefile.txt (and its split "<project>data\<set>.txt" form).
//
// This is the moveset table: the 4th layer of an actor's animation system, after
// (1) the behavior graph HKX (logic), (2) the character HKX (animationNames roster),
// and (3) animationdatasinglefile.txt (clip metadata). The set-data file tells the
// engine, per weapon-equip state, which attack events map to which clips, plus the
// path CRCs the engine matches loaded animations against.
//
// The format is a purely POSITIONAL, count-prefixed text format (CRLF line endings,
// every line CRLF-terminated including the last). There are NO delimiters or keys —
// a single miscount shifts everything downstream — so this parser is validated by a
// parse -> emit -> byte-identical round-trip against real vanilla data. The model is
// pure C++ (std only, no SKSE/CommonLib) so it can be unit-tested standalone and
// linked into the plugin unchanged.
//
// Grammar (verified against vanilla animationsetdatasinglefile.txt, 49 projects):
//
//   SINGLE FILE := N                       // project count
//                  header x N              // "ChickenProjectData\ChickenProject.txt"
//                  projectBlock x N
//
//   projectBlock := M                      // set-file count
//                   setName x M            // "1HMDual.txt" ... (ALL names first)
//                   setContent x M         // ... THEN all contents (grouped, in order)
//
//   setContent   := "V3"
//                   E   ; equipEvent x E                       // equip/trigger events
//                   C   ; (var, int, int) x C                  // weapon-type conditions
//                   A   ; (event, flag, K, clip x K) x A       // attacks (variable clips)
//                   R   ; (folderCrc, fileCrc, extCrc) x R     // animation path CRCs
//
// The split form stores each project as a directory: an index file "<project>.txt"
// listing the set names (one per line, no count), plus one "<set>.txt" per set holding
// exactly a setContent. BR bundles ship deltas in the split form under
// meshes\animationsetdata\<project>data\; the engine reads the concatenated single file.
namespace havok::animsetdata {

    // A weapon-type condition gate: "when iLeftHandType == value (type)". Two trailing
    // integers, both preserved verbatim (semantics: value is the compared type id, the
    // second appears to be a category/count and is always 4 in vanilla humanoid sets).
    struct TypeCondition {
        std::string  variable;  // e.g. "iLeftHandType", "iRightHandType"
        std::int32_t value = 0;
        std::int32_t extra = 0;
    };

    // One attack-event -> clip(s) mapping. clips may hold 0..K names (vanilla has 1 and 2).
    struct Attack {
        std::string              event;  // e.g. "attackStart", "attackPowerStart_Sprint"
        std::int32_t             flag = 0;  // 0/1 (unknown semantics; preserved verbatim)
        std::vector<std::string> clips;     // K clip names, e.g. "1HM_AttackLeft"
    };

    // One animation registration: (folderCrc, fileCrc, extCrc). folder+ext are constant
    // within a set (folder = the animation dir, ext = 7891816 for ".hkx"); file varies.
    struct CrcTriple {
        std::uint32_t folder = 0;
        std::uint32_t file = 0;
        std::uint32_t ext = 0;
    };

    // One set file's content (a "V3" record). In the single-file form it carries its
    // own `name` ("1HMDual.txt"); in the split form the name comes from the filename and
    // `name` is left empty by ParseSetFile.
    struct SetFile {
        std::string              name;             // "1HMDual.txt" (identity within project)
        std::string              version = "V3";   // only "V3" is supported
        std::vector<std::string> equipEvents;
        std::vector<TypeCondition> conditions;
        std::vector<Attack>      attacks;
        std::vector<CrcTriple>   crcs;
    };

    struct Project {
        std::string          header;  // "ChickenProjectData\ChickenProject.txt"
        std::vector<SetFile> sets;    // each carries its own name; order is significant
    };

    struct SingleFile {
        std::vector<Project> projects;  // order is significant (the engine indexes by it)
    };

    // Thrown on malformed input (bad/space-off count, truncation, unsupported version).
    // The message carries the 1-based line number where parsing failed.
    struct ParseError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    // Parse a whole animationsetdatasinglefile.txt held in memory. Tolerates LF or CRLF
    // on input. Throws ParseError on malformed data.
    SingleFile ParseSingleFile(std::string_view text);

    // Emit canonical CRLF form (every line CRLF-terminated, including the last), a
    // byte-for-byte reproduction of the vanilla layout.
    std::string EmitSingleFile(const SingleFile& sf);

    // Parse/emit a single split-form set file body ("V3" ...). The returned SetFile.name
    // is empty (the caller assigns it from the filename); EmitSetFile ignores name.
    SetFile ParseSetFile(std::string_view text);
    std::string EmitSetFile(const SetFile& set);

    // Parse a Nemesis PATCH-form set file into a DELTA holding only the mod's additions.
    // Nemesis files interleave insertion/replacement blocks into a copy of the base:
    //     <!-- MOD_CODE ~mod~ OPEN -->   new lines (the mod's additions)
    //     <!-- ORIGINAL -->              the base lines being replaced (optional)
    //     <!-- CLOSE -->
    // and the section counts stay BASE-relative (never bumped for the insertions), so this
    // cannot be parsed as a finished file. This walks the base grammar using those base
    // counts while treating markers transparently: items in OPEN..ORIGINAL (or OPEN..CLOSE
    // when there is no ORIGINAL) are collected as the delta; base and ORIGINAL items are
    // consumed for count alignment and discarded (they already live in the base BR merges
    // onto). Returns a delta with empty name (caller sets it from the filename). Throws
    // asd::ParseError on malformed input. Assumes markers fall on item boundaries (Nemesis
    // wraps whole items) — never mid-item.
    SetFile ParseNemesisSetFile(std::string_view text);

    // Parse/emit a split-form project index ("<set>.txt" per line, no count prefix).
    std::vector<std::string> ParseProjectIndex(std::string_view text);
    std::string EmitProjectIndex(const std::vector<std::string>& setNames);

    // ── CRC / animation registration ─────────────────────────────────────────────
    // The animationsetdata path hash: CRC-32 with the standard reflected polynomial
    // 0xEDB88320 but init=0 and NO final xor (i.e. NOT zlib's variant), over the
    // lowercased byte string. Reverse-engineered and verified against vanilla: all 20
    // chicken file stems + the chicken and DefaultMale folder paths reproduce exactly.
    std::uint32_t Crc32(std::string_view s);

    // Build the (folderCrc, fileCrc, extCrc) registration for an animation from its
    // data-relative path (e.g. "meshes\actors\character\animations\community_behaviors\x.hkx"):
    //   folderCrc = Crc32(lower(dir))    — backslashes, NO trailing separator
    //   fileCrc   = Crc32(lower(stem))   — filename without extension
    //   extCrc    = little-endian packing of the lowercased extension bytes
    //               ("hkx" -> 'h' | 'k'<<8 | 'x'<<16 = 7891816); the ext is stored raw,
    //               not hashed (it is always <=4 chars). Input separators may be / or \.
    CrcTriple TripleForAnimation(std::string_view dataRelativePath);

    // ── Merge (load-order overlay) ───────────────────────────────────────────────
    struct MergeStats {
        std::size_t projectsAdded = 0;
        std::size_t setsAdded = 0;
        std::size_t attacksAdded = 0;   // includes delta-wins replacements of existing events
        std::size_t crcsAdded = 0;
        std::size_t equipEventsAdded = 0;
        std::size_t conditionsAdded = 0;
    };

    // Overlay `delta` onto `base` (mutated in place). Identity is by name,
    // case-insensitive: project by header, set by set name, attack by event; a crc is
    // identified by its exact (folder,file,ext) triple. New items are APPENDED so every
    // pre-existing project/set index and order is preserved (the engine cross-references
    // projects positionally). On an attack-event collision the delta wins (later load
    // order overrides, matching the behavior-graph merge). Self-merging an unchanged file
    // is a no-op (re-emit stays byte-identical) — the merge invariant the tests assert.
    MergeStats MergeInto(SingleFile& base, const SingleFile& delta);

}  // namespace havok::animsetdata
