#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// FnisListParser — parse FNIS_*_List.txt files into structured animation declarations.
//
// Pure std (no SKSE/PCH) so it links into standalone CLI tools unchanged.  The format is
// line-oriented; each non-comment, non-empty line is one of:
//
//   <type> [-<option,option,...>] <AnimEvent> <AnimFile> [<AnimObject> ...]
//   AnimVar <VarName> [ BOOL | INT32 | REAL ] <numeric_value>
//   Version <VersionString>
//
// Types: b (basic), s (sequence start), so (sequence optimized), fu (furniture start),
//        fuo (furniture optimized), + (sequence/furniture continuation), o (AnimObject),
//        ofa (offset arm), pa (paired), km (killmove), aa (alternate animation).
//
// Options (after '-', comma-separated):
//   a        acyclic (one-shot, not looping)
//   h        headtracking stays on
//   md       motion driven (has root motion)
//   Tn       transition-next (keep pose after acyclic anim, no IdleForceDefaultState)
//   B<n.m>   blend time in seconds (e.g. B1.5)
//   k        known animation file (skip consistency check)
//   bsa      animation in BSA (skip consistency check)
//   o        has AnimObjects
//   st       sticky AO (don't unequip AO at animation end)
//   D<time>  explicit duration (pa/km only)
//   T<Evt>/<time>  triggered event during animation (may appear multiple times)
//   ac/acs/acr     animated camera flags (rare)
//
// Comments: lines starting with ' or // are comments.  Blank lines are skipped.
// The Version line is recorded but otherwise ignored.
namespace CommunityBehaviors::fnis {

enum class AnimType : std::uint8_t {
    Basic,               // b
    AnimObject,          // o
    Sequence,            // s
    SequenceOptimized,   // so
    Furniture,           // fu
    FurnitureOptimized,  // fuo
    Continuation,        // +  (continuation of s/so/fu/fuo)
    OffsetArm,           // ofa
    Paired,              // pa
    Killmove,            // km
    AlternateAnim,       // aa
};

// A triggered event embedded in a clip (from -T<Event>/<time> or -D<time>).
struct TriggeredEvent {
    std::string event;
    float       time = 0.f;
};

// One variable declared with an AnimVar line.
struct AnimVarDecl {
    std::string name;
    std::string type;   // "BOOL", "INT32", or "REAL"
    std::string value;  // verbatim numeric value
};

// One parsed animation declaration line.
struct AnimDecl {
    AnimType                    type = AnimType::Basic;
    std::string                 event;          // trigger event name
    std::string                 animFile;       // HKX path relative to mod anim folder
    std::vector<std::string>    animObjects;    // AnimObject CK names (o/so/fuo/km types)

    // Flags
    bool                        acyclic       = false;   // -a
    bool                        headtracking  = false;   // -h
    bool                        motionDriven  = false;   // -md
    bool                        transitionNext = false;  // -Tn
    bool                        known         = false;   // -k
    bool                        inBsa         = false;   // -bsa
    bool                        hasAnimObjects = false;  // -o
    bool                        stickyAO      = false;   // -st

    float                       blendTime     = -1.f;    // -Bn.m; <0 = not set
    float                       duration      = -1.f;    // -Dn.m; <0 = not set (pa/km)
    std::vector<TriggeredEvent> triggeredEvents;         // -T<Evt>/<time>
};

// Result of parsing one FNIS_<ModName>_List.txt file.
struct ListFile {
    std::string              modName;     // extracted from filename: FNIS_<modName>_List.txt
    std::string              version;     // from "Version ..." line (empty if absent)
    std::vector<AnimDecl>    anims;       // animation declarations, in file order
    std::vector<AnimVarDecl> vars;        // AnimVar declarations
};

// Parse the text content of a single FNIS_*_List.txt file.  `modName` is typically
// extracted from the filename by the caller (ScanForLists does this).  Malformed lines
// are skipped with a warning appended to `warnings` (one per line).  Never throws.
ListFile ParseListFile(std::string_view         text,
                       const std::string&        modName,
                       std::vector<std::string>* warnings = nullptr);

// Result of scanning a mod tree for FNIS list files.
struct ScanResult {
    std::string              actor;     // actor root, e.g. "character" (from path)
    std::vector<ListFile>    lists;     // one per FNIS_*_List.txt found
};

// Scan `animationsDir` (e.g. meshes/actors/character/animations/) for subdirectories
// containing FNIS_*_List.txt files.  Each list file's animFile paths are relative to its
// containing subdirectory (the mod's animation folder).  The `actor` is derived from the
// parent path.  Skips files that parse to zero animations.
ScanResult ScanForLists(const std::filesystem::path& animationsDir,
                        const std::string&            actor,
                        std::vector<std::string>*     warnings = nullptr);

// ── Grouping ────────────────────────────────────────────────────────────────

static constexpr std::size_t kMaxClipsPerContainer = 30000;

struct AnimGroup {
    const AnimDecl*              head = nullptr;
    std::vector<const AnimDecl*> continuations;
    std::size_t ClipCount() const { return 1 + continuations.size(); }
};

// Group parsed animation declarations into AnimGroups (sequences coalesced with
// their continuations).  Used by both the HKX synthesizer and the YAML converter.
std::vector<AnimGroup> GroupAnimations(const ScanResult& scan);

}  // namespace CommunityBehaviors::fnis
