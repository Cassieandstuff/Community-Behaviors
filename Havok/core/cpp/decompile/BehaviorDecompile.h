// BehaviorDecompile.h — the decompile/emit + tagfile + convert half of the former havok-model facade.
// Split out of <havok-model/HavokModel.h> in org-pass firesale phase 3f. Impl: decompile/HavokModel.cpp.
#pragma once
#include <codec/serialization/HavokIo.h>
#include <interface/UnitSource.h>
#include <interface/reflection/HavokSchema.h>
#include <codec/serialization/packfile/IHavokObject.h>

#include <cstdint>
#include <memory>
#include <string>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

namespace CB::core::decompile {
using namespace CB::core::codec;
using namespace CB::core::common;
// ── identity/index pass ───────────────────────────────────────────────────────
// Walk the SchemaObject graph in ENCOUNTER ORDER (matching the reference BehaviorDecompiler's DFS, so
// the yaml filenames + id refs are byte-stable) and assign each object a canonical id + a category
// folder (clips / states / transitions / generators / selectors / references / tagging / modifiers /
// data). Ids live here; the emit consumes this map.
struct Identity {
    // obj -> canonical id (yaml filename + ref). A CANONICAL STRING: base nodes are the unpadded decimal
    // tagfile number ("184"); a Nemesis new node is its string token ("CASSIE$0"). Numeric ids render
    // byte-identically to a plain integer everywhere, so string ids cost the base path nothing while
    // letting patch-added nodes (and refs to them) live in the same graph.
    std::unordered_map<const IHavokObject*, std::string> ids;
    std::unordered_map<const IHavokObject*, std::string> category;  // obj -> folder
    // DIFF-ONLY: when set, refIdOf emits a node ref as the target's `Class:name` instead of its numeric
    // #NNNN, so a cross-compile tree-diff aligns refs by stable identity (two compiles renumber the same
    // node differently). Never set on a production emit — it changes ref rendering and is not a valid .hky.
    bool refsAsNames = false;
};

// The .hky folder a class's node lives in ("clips", "states", …), or "" if the class is inlined into
// its owner's YAML rather than getting its own file. A pure function of the class name (from the
// reference decompiler's per-class emit sites).
std::string CategoryForClass(const std::string& className);

// Assign canonical ids + categories over a fully-deserialized graph. `des` must have completed a full
// Deserialize (root walk) so ListObjects/RefsInReadOrder/DeserializedObjects are populated.
//
// Ids come from the IDENTITY/INDEX step, which has two providers:
//   • tagfile-aligned (vanilla, matches Skyrim.hky): pass the matching vanilla tagfile `xmlText` — ids
//     are the stable #NNNN recovered by the structural oracle (CB::core::decompile::AlignTagfile), the identity
//     the runtime merges by. This is NOT computable from the packfile alone.
//   • encounter-order fallback (no XML): post-order (ReadCompletionOrder) numbering for a self-
//     consistent hkx→hky→hkx round-trip when Skyrim.hky-exact filenames aren't required.
// Category is a pure function of class either way. (Stage 3, increment 2.)
Identity AssignIdentity(PackFileDeserializer& des, const schema::SchemaRegistry& reg,
                        const std::string& xmlText = {});

// ── schema-driven emit ────────────────────────────────────────────────────────
// Emit the per-node `.hky` files for a behavior graph into `outDir` — `<category>/<id>.yaml` for every
// node with a non-empty category, a pure projection of the SchemaObject graph driven by the schema +
// the Identity map (ids/refs) + name resolution from the graph's hkbBehaviorGraphStringData. Gated
// file-vs-live-`vanbase`. (Stage 3, increment 3 — built out category by category.)
// `deltaIds` (optional): emit ONLY the file nodes a patch touched — the per-mod .hky DELTA LoadMerged
// serves. Each category-"" delta id (an inline sub-object a Nemesis patch edits by its own #NNNN) is
// folded up to the file node that inlines it; an id with no file-node owner is reported via `warnings`
// as moot (not referenced by the merged graph). nullptr = emit the whole graph (the base bundle).
bool EmitHky(const Identity& identity, const schema::SchemaRegistry& reg,
             const std::string& outDir, std::string& err, const std::set<std::string>* deltaIds = nullptr,
             std::vector<std::string>* warnings = nullptr);

// Emit `<outDir>/data/additive.yaml` — the vocabulary a merge ADDED over the base (events/variables/
// character-properties present in the merged graph's string-data but not the base's), each with the
// type/flags/initial-value read from the merged graph-data. The runtime unions this in. No file is
// written when nothing was added. Pairs with EmitHky's delta mode to complete a per-mod .hky delta.
bool EmitAdditiveVocab(const Identity& baseId, const Identity& mergedId,
                       const std::string& outDir, std::string& err);

// Emit the whole-graph scaffolding a FULL decompile needs but EmitHky (node-only) omits:
// `<outDir>/behavior.yaml` (packfile + behavior header: name/variableMode/rootGenerator/data) and
// `<outDir>/data/graphdata.yaml` (the full variable/event/characterProperty roster). Together with a
// full EmitHky node emit (no deltaIds), this reproduces the typed DecompileBehaviorTree tree — the schema
// decompiler's base-master output. Not used for per-mod deltas.
bool EmitFullBaseScaffolding(const Identity& identity, const std::string& outDir, std::string& err);

// Schema decompile of a whole BEHAVIOR binary to a full `.hky` unit tree — the schema peer of the typed
// DecompileBehaviorTree / ConvertPatch base path. Deserialize (MakeSchemaFactory) → AssignIdentity (oracle
// #NNNN when `xmlText` is the matching tagfile, else encounter-order) → EmitHky (nodes) +
// EmitFullBaseScaffolding (behavior.yaml + graphdata). Byte-identical to the typed base (emit-check gate),
// so base + per-mod deltas are produced by ONE emitter. Behaviors only (character/project/anim stay typed).
// `selfIndex` (default -1 = legacy bare ids): when >= 0, every emitted node id becomes a FormId
// "selfIndex:local" (ids AND refs, both from identity.ids). The base master passes 0 — Skyrim's own nodes
// are self (index 0, since for Skyrim.hky self == base) — so its nodes read "0000x<local>" (self-teaching:
// copy the id to author an override). Byte-neutral to compiled graphdata (the id text isn't compiled).
bool DecompileBehaviorSchema(const std::vector<std::uint8_t>& bytes, const std::string& xmlText,
                             const schema::SchemaRegistry& reg, const std::string& outDir, std::string& err,
                             int selfIndex = -1);

// Schema-native loose-graph delta derive — the peer of havok-core's typed DeriveLooseBehaviorDelta, for a
// mod shipping a precompiled FULL behavior graph (no Nemesis patch). Deserialize vanilla + mod to
// SchemaObject graphs, match nodes by owner-qualified name identity, number the vanilla side by the SAME
// schema READ-ORDER the base master now uses (so matched overrides land on the base's ids), mint new nodes
// as "<modCode>$N", emit both trees, and copy each new/changed mod node into `outDeltaDir`. Pairs with
// BuildBaseBundle's schema base decompile (both read-order) — the coordinated no-template flip.
struct LooseDeriveResult {
    bool ok = false; std::string error;
    int matched = 0, added = 0, changedNodes = 0, newNodes = 0, removedFromBase = 0;
};
LooseDeriveResult DeriveLooseBehaviorDeltaSchema(const std::vector<std::uint8_t>& vanBytes,
                                                 const std::vector<std::uint8_t>& modBytes,
                                                 const std::string& modCode,
                                                 const schema::SchemaRegistry& reg,
                                                 const std::string& outDeltaDir);

/// @brief Reflective .hky emit — every schema field of every file node, in schema order, rendered by FieldKind
///        alone (inline objects nest as maps with their `class:`; the root container's inline subtree lands in
///        graph.yaml). MEASUREMENT stage of the reflective-format work: diffed against EmitHky to find what the
///        bespoke renderers drop or reshape. No loader consumes it yet.
/// @param bones the graph's rig in rig order (RigIndex), so `boneref`/`bonelist`/`boneweights` fields render by bone
///              NAME; null = rig unknown (indices stay numeric — still lossless).
bool EmitHkyReflective(const Identity& identity, const schema::SchemaRegistry& reg,
                       const std::string& outDir, std::string& err, const std::vector<std::string>* bones = nullptr);
/// @brief Deserialize + encounter-order AssignIdentity + EmitHkyReflective (ids align with an XML-less
///        DecompileBehaviorSchema of the same bytes, so the two trees diff file-for-file).
bool DecompileBehaviorReflective(const std::vector<std::uint8_t>& bytes, const schema::SchemaRegistry& reg,
                                 const std::string& outDir, std::string& err,
                                 const std::vector<std::string>* bones = nullptr);

/// @brief The parsed reflective .hky — every object (owned), the root container, and the identity (file-node ids +
///        categories; inline objects join with category "").
struct HkyListHistory;
struct ParsedHky {
    std::vector<std::shared_ptr<io::SchemaObject>> objects;
    std::shared_ptr<io::SchemaObject>              root;
    Identity                                       identity;
    std::shared_ptr<HkyListHistory>                listHistory;   ///< @brief MergeHkyPatch's per-list record (see UNION WINS)
};
/// @brief ParseHky — the exact inverse of EmitHkyReflective: `dir` (graph.yaml + `<category>/<id>.yaml`) -> the
///        SchemaObject graph, serializable byte-for-byte. Event/variable name companions WIN over their indices (the
///        name is the stable identity across mods); bone names resolve through `bones` (the graph's rig). Fail-loud:
///        an unknown enum name, bone, symbol, class or id is an error, never a silent 0.
bool ParseHkyReflective(const CB::core::common::IUnitSource& src, const schema::SchemaRegistry& reg, ParsedHky& out,
                        std::string& err, const std::vector<std::string>* bones = nullptr);
/// @brief Convenience: an on-disk unit directory.
bool ParseHkyReflective(const std::string& dir, const schema::SchemaRegistry& reg, ParsedHky& out, std::string& err,
                        const std::vector<std::string>* bones = nullptr);

/// @brief The reflective-format GATE for one behavior binary: deserialize -> re-serialize (`canon`), EmitHkyReflective
///        into `workDir` -> ParseHkyReflective -> serialize with the same header, compared byte-for-byte to canon.
struct HkyRoundTripResult {
    bool        ok = false;             ///< the pipeline ran (else `error`)
    bool        identical = false;      ///< parse(emit(x)) serializes byte-identical to canon
    bool        canonIsSource = false;  ///< canon == the input bytes (the serializer's own round-trip)
    std::size_t firstDiff = 0;          ///< first differing byte offset when !identical
    long long   sizeDelta = 0;          ///< re-serialized size minus canon size
    std::string detail;                 ///< on a mismatch: per-class object-count differences (class:canon->back)
    std::string error;
};
HkyRoundTripResult RoundTripReflective(const std::vector<std::uint8_t>& bytes, const schema::SchemaRegistry& reg,
                                       const std::string& workDir, const std::vector<std::string>* bones = nullptr);

// ── Field patches: reflective .hky DELTAS ──────────────────────────────────────────────────────────────────────────
struct HkyPatchStats { int patched = 0; int added = 0; };
/// @brief Derive the FIELD PATCH turning `baseBytes` into `modBytes`: mod nodes match base nodes by (class, name)
///        (unnamed shared leaves by content) and take the base's FormIds as master `baseIndex`; unmatched nodes are
///        NEW (self index 0, full body). A patched node file carries only its changed fields (nested inline objects
///        deep-diffed; compose/union arrays as the appended tail).
bool DeriveHkyPatch(const std::vector<std::uint8_t>& baseBytes, const std::vector<std::uint8_t>& modBytes,
                    const schema::SchemaRegistry& reg, const std::string& patchDir, std::string& err,
                    const std::vector<std::string>* bones = nullptr, std::uint16_t baseIndex = 1, HkyPatchStats* stats = nullptr);
/// @brief Apply a patch unit onto a parsed graph. FormIds resolve through each unit's master table (`baseMasters` /
///        `patchMasters`, index -> bundle), so a patch's `0001x…` names the base's `0000x…`. A patched node changes
///        only the fields it names; compose/union arrays append; `{replace: [...]}` replaces.
/// @note UNION WINS (user decision, 2026-09-29): a list whose membership ONE mod changes takes that mod's version
///       verbatim (removals, order); once a SECOND mod changes it, it becomes the pre-patch list + every mod's
///       additions in load order, so both mods' additions survive and contested removals are dropped. The record
///       lives on `base.listHistory` across calls.
bool MergeHkyPatch(ParsedHky& base, const CB::core::common::IUnitSource& patch, const schema::SchemaRegistry& reg,
                   const std::vector<std::string>& baseMasters, const std::vector<std::string>& patchMasters,
                   std::string& err, const std::vector<std::string>* bones = nullptr, HkyPatchStats* stats = nullptr);
/// @brief The field-patch GATE: merge(base, derive(base, mod)) must serialize byte-identical to the mod.
struct HkyPatchRoundTripResult {
    bool ok = false, identical = false;
    HkyPatchStats derived, merged;
    std::string detail, error;
};
HkyPatchRoundTripResult PatchRoundTrip(const std::vector<std::uint8_t>& baseBytes, const std::vector<std::uint8_t>& modBytes,
                                       const schema::SchemaRegistry& reg, const std::string& workDir,
                                       const std::vector<std::string>* bones = nullptr);

/// @brief The MULTI-LAYER gate: every mod's field patch (derived from base + that mod's Nemesis patches alone),
///        merged onto the base in order, must equal today's merge of all the mods' patches (MergeTagfiles).
struct MultiLayerMod { std::string name; std::vector<std::string> patches; };   ///< raw Nemesis #*.txt texts
struct MultiLayerResult {
    bool ok = false, identical = false;
    std::vector<std::pair<std::string, HkyPatchStats>> perMod;
    std::string detail, error;
};
MultiLayerResult MultiLayerRoundTrip(const std::string& baseXml, const std::vector<MultiLayerMod>& mods,
                                     const schema::SchemaRegistry& reg, const std::string& workDir,
                                     const std::vector<std::string>* bones = nullptr);

// ── schema-driven tagfile emit (the tagfile codec — base-self-align) ──────────────────────────────
// Emit the SchemaObject graph as the Havok TAGFILE __data__ section (the `<hkobject name="#NNNN" …>`
// form the converter's structural oracle + bashed merge consume as `vanById`). A pure, per-field
// projection driven by the schema — the data-driven replacement for the converter's per-class
// tagfile populators. Objects are emitted in Identity id order; refs render as `#NNNN`, inline structs
// nest an unnamed `<hkobject>`, SERIALIZE_IGNORED fields render as comments. With encounter-order ids
// (AssignIdentity without a tagfile XML), this reproduces Skyrim.hky's canonical numbering — so the
// base binary IS its own tagfile, and ConvertPatch needs no external template. Gated by re-emitting
// the vanilla templates/*.xml corpus. Returns the __data__ objects (no packfile/section wrapper).
std::string EmitTagfile(const Identity& identity, const schema::SchemaRegistry& reg, std::string& err);

// ── schema-driven tagfile PARSE (the inverse codec — model-merge foundation) ───────────────────────
// Parse a Havok TAGFILE __data__ fragment (a run of `<hkobject name="#NNNN" class="C">` blocks, with or
// without the packfile/section wrapper) into a SchemaObject graph + its Identity — the exact inverse of
// EmitTagfile, driven by the SAME schema field walk. Refs (`#NNNN`) wire by id in a second pass; inline
// structs recurse; enums/flags decode via ResolveEnum; SERIALIZE_IGNORED fields (emitted as comments,
// never hkparams) stay at their Init defaults. This is the reusable primitive under (b) — apply Nemesis
// patches to the Skyrim.hky model BY #NNNN with no binary deserialize/AlignTagfile — and under runtime
// tagfile conversion. Gated by round-trip: ParseTagfile(EmitTagfile(g)) re-emits byte-identical.
//
// NOTE (first cut): the field kinds EmitTagfile does not emit (QsTransform(Array), BoolArray, EmptyArray,
// EmptyPtr) are likewise not parsed — they keep Init defaults, which round-trips against EmitTagfile but
// is a known gap for any consumer that needs those fields verbatim.
struct ParsedTagfile {
    std::vector<std::shared_ptr<io::SchemaObject>> objects;   // owns the graph's lifetime
    Identity                                       identity;  // ids + category over `objects`
};
bool ParseTagfile(const std::string& xmlText, const schema::SchemaRegistry& reg,
                  ParsedTagfile& out, std::string& err);

// ── id-keyed model merge (the core of (b)) ─────────────────────────────────────────────────────────
// Overlay Nemesis patch tagfiles onto a vanilla base tagfile BY #NNNN — the data-driven replacement for
// ConvertPatch's typed deserialize + AlignTagfile + typed populate. The base's ids come straight from its
// tagfile (no binary, no oracle, no off2id); patch ids match by construction. Merge is a field-wise
// overlay in load order (base, then each patch — a later layer wins on the fields it names, keeps the
// rest), and refs resolve across the MERGED id space (a patch node may point at a base-only node). The
// result is a single SchemaObject graph ready for EmitHky (the .hky delta LoadMerged serves) or EmitTagfile.
// `deltaIds` receives every id a patch overrode or added — the set a per-mod .hky delta emits.
//
// Base + patches are already the Nemesis flow's inputs: pass each patch through xml::StripPatchOriginals
// (apply MOD_CODE OPEN edits) before handing its text here.
bool MergeTagfiles(const std::string& baseXml, const std::vector<std::string>& patchXmls,
                   const schema::SchemaRegistry& reg, ParsedTagfile& out,
                   std::vector<std::string>& deltaIds, std::string& err);

// One mod's full behavior delta, data-driven end to end — the schema-path replacement for the converter's
// typed ConvertPatch+DecompileNativeDelta. Reads every `#*.txt` in `patchDirs` (load order), applies each
// Nemesis MOD_CODE edit (StripPatchOriginals), overlays them onto `baseTagfileXml` by #NNNN (MergeTagfiles,
// new #code$N nodes included), and writes the per-mod `.hky` delta into `outDeltaDir` — reachable delta
// file nodes + data/additive.yaml (added vocab). `baseTagfileXml` is the vanilla graph's tagfile (its
// #NNNN are the canonical ids the runtime base is keyed by). Faithful to the patch by construction (same
// parse+emit machinery gated byte-identical to the typed path on overrides, and strictly more complete on
// new nodes, which the typed populators drop).
struct ModDeltaResult {
    bool                     ok = false;
    std::string              error;
    int                      patchNodes = 0;   // #*.txt patch objects read (overrides + new)
    int                      deltaIds   = 0;   // ids a patch overrode or added
    std::vector<std::string> warnings;
};

// FormId minting context for ConvertModDelta. nullptr => legacy bare ids ("#NNNN" overrides, "code$N" new
// nodes). When supplied, each node id becomes a FormId "IIIIxLLLL" (index convention: self=0, skyrim=1,
// further masters 2+ — see FormId.h):
//   bare "#NNNN"        -> "0001x<local>"        (edit of a vanilla/Skyrim node; origin = Skyrim = index 1)
//   "<code>$N"          -> "<codeIndex[code]>x<N>"  (own code -> selfIndex=0; foreign code -> its master index 2+)
// The converter builds `codeIndex` ONCE per bundle (pre-scanning all the bundle's graphs, so a master's
// index is the SAME in every graph) as its position in the ordered master table [self(0), skyrim(1),
// masters…(2+)] — matching the loader's BuildMasterTable so emit + load-time resolution agree. A "code$N"
// whose code is absent from codeIndex is unknown-foreign: bound to selfIndex + a warning.
struct NemesisFormIdCtx {
    std::uint16_t                                selfIndex = 0;   // this bundle's own index (self = 0)
    std::unordered_map<std::string, std::uint16_t> codeIndex;     // Nemesis code (lowercase) -> master index
};

ModDeltaResult ConvertModDelta(const std::string& baseTagfileXml, const std::vector<std::string>& patchDirs,
                               const schema::SchemaRegistry& reg, const std::string& outDeltaDir,
                               const NemesisFormIdCtx* fid = nullptr);
} // namespace CB::core::decompile
