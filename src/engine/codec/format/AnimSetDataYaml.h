#pragma once
// AnimSetDataYaml — animationsetdatasinglefile.txt as a decomposed FOLDER (the ".txt/" tree).
//
// The monolithic set-data cache decomposes into a folder carrying its own name+extension, the
// same convention a "<name>.hkx" behavior decomposes into a "<name>.hkx/" folder:
//
//     meshes/animationsetdatasinglefile.txt/
//       index.yaml            project ORDER + exact header strings (the file's line-1 manifest)
//       setdata/<stem>.yaml   the base: one project's sets (gate / equip / attacks + `animations`),
//                             self-identifying via `project:`
//       movesets/<stem>.yaml  a mod bundle's authored sets (gate / equip / attacks), merged at serve
//
// The per-set CRC membership (a path hash the engine matches loaded animations against) is not
// shipped as data: the regen reverses it to animation PATHS (ResolveSetdataPaths; a triple that
// doesn't reverse stays a verbatim "@crc" residue token) and the serve re-hashes them
// (CompileSetdataCrcs / TripleForAnimation). `<stem>` is the lowercased basename of the project
// header ("ChickenProjectData\ChickenProject.txt" -> "chickenproject").
//
// Shared by the converter (decompose) and the runtime (ServeSetData composes the base from this
// folder) — ryml-gated.

#include "codec/format/AnimationSetData.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace CB::engine::animsetdata {

    // One project's sets -> movesets.yaml text (authoring intent only; CRCs live in crcs.yaml).
    std::string EmitMovesetsYaml(const Project& project);

    // movesets.yaml -> a SingleFile carrying ONE project (header derived from `projectName`,
    // e.g. "DefaultMale" -> "DefaultMaleData\DefaultMale.txt"). Sets carry gate/equip/attacks
    // with EMPTY crcs (Assemble fills them from crcs.yaml; mod deltas derive them). Never throws —
    // malformed input leaves `err` set and returns whatever parsed.
    SingleFile ParseMovesetsYaml(const std::string& text, const std::string& projectName,
                                 std::string& err);

    // ── The ".txt/" folder pieces ────────────────────────────────────────────────
    // The lowercased basename-stem of a project header ("ChickenProjectData\ChickenProject.txt"
    // -> "chickenproject") — the key the setdata/ and movesets/ files are named by.
    std::string StemForHeader(const std::string& header);

    // index.yaml: the project ORDER + exact header strings (the .txt's leading count + list).
    // This is the one piece neither authored nor derivable — the engine indexes projects by it.
    std::string              EmitSetdataIndexYaml(const SingleFile& sf);
    std::vector<std::string> ParseSetdataIndexYaml(const std::string& text, std::string& err);

    // Stitch the decomposed folder back into a SingleFile, in index order. `headers` gives the
    // project order + exact header strings; `movesetsByStem` supplies each project's sets (order
    // authoritative); `crcsByStem` supplies the baked registrations, matched to sets by name. A
    // header absent from movesetsByStem yields a zero-set project (still emitted, from the index);
    // a set absent from crcsByStem keeps empty CRCs. EmitSingleFile on the result reproduces the
    // vanilla .txt byte-for-byte when the three pieces are the vanilla decompose.
    SingleFile AssembleSetdata(
        const std::vector<std::string>& headers,
        const std::map<std::string, SingleFile>& movesetsByStem,
        const std::map<std::string, std::map<std::string, std::vector<CrcTriple>>>& crcsByStem);

    // ── Per-project setdata file (animationsetdatasinglefile.txt/setdata/<stem>.yaml) ──────
    // Self-identifying: it carries `project:` (the exact asdsf header) so the serve can key it back to
    // the index order without a char->project reverse map.
    // Body is the same `sets:` shape as EmitMovesetsYaml (gate/equip/attacks + animations).
    std::string EmitSetdataUnitYaml(const Project& project);

    // Parse a data/setdata.yaml -> one Project (header from `project:`, sets from `sets:`). Never
    // throws — malformed input leaves `err` set and returns whatever parsed.
    Project ParseSetdataUnitYaml(const std::string& text, std::string& err);

    // ── CRC <-> path resolution (the searchable codec, authorable side) ──────────────
    // Build a (folderCrc<<32 | fileCrc) -> data-relative-path index from candidate animation paths
    // (each in the form the engine CRCs, e.g. "meshes\actors\character\animations\x.hkx"). The caller
    // supplies candidates (walk the datasource meshes at regen, the roster at serve). O(candidates),
    // built ONCE — then reversal is O(1) per triple.
    std::unordered_map<std::uint64_t, std::string> BuildCrcIndex(const std::vector<std::string>& candidatePaths);

    // REVERSE (regen extract): fill each set's `animations` (authorable source) from its `crcs`, in
    // order 1:1. A triple whose (folder,file) is in `crcIndex` becomes the animation PATH; one that
    // isn't becomes a residue token "@crc <folder> <file> <ext>" (kept verbatim, folder still known).
    void ResolveSetdataPaths(Project& project, const std::unordered_map<std::uint64_t, std::string>& crcIndex);

    // COMPILE (serve/compose): re-derive `crcs` from `animations`, in order 1:1 — a path re-hashes via
    // TripleForAnimation, a "@crc ..." residue token parses back to its verbatim triple. No-op (keeps
    // existing `crcs`) when `animations` is empty (legacy path). Byte-exact against the vanilla CRCs.
    void CompileSetdataCrcs(SetFile& set);

}  // namespace CB::engine::animsetdata
