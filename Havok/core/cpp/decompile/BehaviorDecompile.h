// BehaviorDecompile.h — the decompile/emit + tagfile + convert half of the former havok-model facade.
// Split out of <havok-model/HavokModel.h> in org-pass firesale phase 3f. Impl: decompile/HavokModel.cpp.
#pragma once
#include <codec/serialization/HavokIo.h>
#include <interface/reflection/HavokSchema.h>
#include <codec/serialization/packfile/IHavokObject.h>

#include <memory>
#include <string>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

namespace havok::model {
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
//     are the stable #NNNN recovered by the structural oracle (havok::sct::AlignTagfile), the identity
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
bool DecompileBehaviorSchema(const std::vector<std::uint8_t>& bytes, const std::string& xmlText,
                             const schema::SchemaRegistry& reg, const std::string& outDir, std::string& err);

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
ModDeltaResult ConvertModDelta(const std::string& baseTagfileXml, const std::vector<std::string>& patchDirs,
                               const schema::SchemaRegistry& reg, const std::string& outDeltaDir);
} // namespace havok::model
