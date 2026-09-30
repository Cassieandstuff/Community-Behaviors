// BehaviorDecompile.h — the decompile/emit + tagfile + convert half of the former havok-model facade.
// Split out of <havok-model/HavokModel.h> in org-pass firesale phase 3f. Impl: decompile/HavokModel.cpp.
#pragma once
#include <codec/serialization/HavokIo.h>
#include <interface/UnitSource.h>
#include <interface/reflection/HavokSchema.h>
#include <codec/serialization/packfile/IHavokObject.h>
#include <codec/serialization/packfile/PackFileTypes.h>
#include <codec/format/AnimationScan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string_view>
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
    /// @brief Minted roster keys (`roster: id|slot`) per (owner object, field index), parallel to that list. Absent
    ///        (or out of step with the list) = keyed by position, the base unit's own sub-record FormIDs.
    std::map<std::pair<const IHavokObject*, std::size_t>, std::vector<std::string>> subKeys;
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
// Deserialize (root walk) so ListObjects/RefsInReadOrder/DeserializedObjects are populated. Ids are
// encounter order; category is a pure function of class.
Identity AssignIdentity(PackFileDeserializer& des, const schema::SchemaRegistry& reg);

/// @brief Reflective .hky emit — every schema field of every file node, in schema order, rendered by FieldKind
///        alone (inline objects nest as maps with their `class:`; the root container's inline subtree lands in
///        graph.yaml). ParseHkyReflective is its exact inverse.
/// @param bones the graph's rig in rig order (RigIndex), so `boneref`/`bonelist`/`boneweights` fields render by bone
///              NAME; null = rig unknown (indices stay numeric — still lossless).
/// @brief A `scan:` list's RECIPE, made from the list it replaces (the base-master regen derives a character's
///        animations.yaml from its vanilla roster + the baked animation tree). nullopt = keep the literal list.
using AnimationScanFn = std::function<std::optional<CB::core::anim::AnimationScan>(const std::vector<std::string>& list)>;
bool EmitHkyReflective(const Identity& identity, const schema::SchemaRegistry& reg,
                       const std::string& outDir, std::string& err, const std::vector<std::string>* bones = nullptr,
                       const AnimationScanFn* scan = nullptr);
/// @brief Deserialize + encounter-order AssignIdentity + EmitHkyReflective: a complete unit, its nodes self-owned
///        FormIDs (`0000x…`) — the form a base unit (and any full graph a bundle ships) takes.
bool DecompileBehaviorReflective(const std::vector<std::uint8_t>& bytes, const schema::SchemaRegistry& reg,
                                 const std::string& outDir, std::string& err,
                                 const std::vector<std::string>* bones = nullptr, const AnimationScanFn* scan = nullptr);

/// @brief The parsed reflective .hky — every object (owned), the root container, and the identity (file-node ids +
///        categories; inline objects join with category "").
struct ParsedHky {
    std::vector<std::shared_ptr<io::SchemaObject>> objects;
    std::shared_ptr<io::SchemaObject>              root;
    Identity                                       identity;
    /// @brief A `scan:` field read as its recipe: the list stays EMPTY until ExpandAnimationScans builds it.
    struct ScanField { io::SchemaObject* obj; std::size_t field; CB::core::anim::AnimationScan recipe; };
    std::vector<ScanField>                         scans;
};
/// @brief The compile-time EDITING view over a merged graph — the handle every graph feature receives.
/// @note  Vocabulary edits keep Havok's parallel arrays in LOCKSTEP (names / infos / word values), so a feature can't
///        push a name without its info and value. New names append, so every index a mod already relies on stays
///        valid. Structural edits go through the SchemaObject graph; an object shared by several owners (a condition
///        used by two transitions) must be REPLACED per owner, never mutated in place.
class GraphEdit {
public:
    enum class Add { Added, Exists, Failed };
    GraphEdit(ParsedHky& graph, const schema::SchemaRegistry& reg) : m_graph(graph), m_reg(reg) {}

    /// @brief The behavior graph's data object (variables / events), or null (a character / project graph).
    io::SchemaObject* GraphData() const;
    std::optional<std::size_t> VariableIndex(std::string_view name) const;
    std::optional<std::size_t> EventIndex(std::string_view name) const;
    /// @brief Append a WORD-typed variable (`type` = a VariableType name, e.g. "VARIABLE_TYPE_BOOL"; `word` = its raw
    ///        4-byte value — a REAL's float bits). Exists = the name is already declared (left untouched: the graph's
    ///        own declaration is authoritative). Failed (+ `err`) for a vector / pointer type or no graph data.
    Add AddVariable(std::string_view name, std::string_view type, std::int32_t word, std::string& err);
    Add AddEvent(std::string_view name, std::string& err);

    /// @brief Every object of class `cls` reachable from the root (nodes and inline objects), once each.
    void ForEach(std::string_view cls, const std::function<void(io::SchemaObject&)>& fn);
    /// @brief How many reachable objects are `base` or derive from it.
    std::size_t CountDerived(std::string_view base);
    /// @brief A new object of `cls`, schema-initialized and owned by the graph.
    std::shared_ptr<io::SchemaObject> Make(std::string_view cls);
    /// @brief A node's id (FormID / canonical key) for FeatureResult::touched; "" for an inline object.
    std::string IdOf(const IHavokObject* o) const;
    const schema::SchemaRegistry& Registry() const { return m_reg; }

private:
    ParsedHky&                     m_graph;
    const schema::SchemaRegistry&  m_reg;
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

/// @brief UNIT READERS — the facts load planning needs from a reflective unit, without compiling it.
std::vector<std::string> ReadProjectCharacters(const CB::core::common::IUnitSource& src, const schema::SchemaRegistry& reg);
/// @brief A character's string data (original case).
struct CharacterStrings {
    std::string              name, rigName, behaviorFilename;
    std::vector<std::string> animationNames;
};
/// @note  Read straight from graph.yaml, not parsed: a full parse needs the rig's bone names (the bone-weight presets
///        are bone-keyed) — the very thing the rig binding is for. A patch unit yields only what it states; its
///        animationNames (a union roster) are its additions.
std::optional<CharacterStrings> ReadCharacterStrings(const CB::core::common::IUnitSource& src);

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
///        deep-diffed; a roster as its changed / new / `{deleted: true}` entries by key, in the node's folder; a union
///        name roster as the names it adds; any other list whole).
bool DeriveHkyPatch(const std::vector<std::uint8_t>& baseBytes, const std::vector<std::uint8_t>& modBytes,
                    const schema::SchemaRegistry& reg, const std::string& patchDir, std::string& err,
                    const std::vector<std::string>* bones = nullptr, std::uint16_t baseIndex = 1, HkyPatchStats* stats = nullptr);
/// @brief Derive the patch against an already PARSED (and possibly merged) base — the base unit plus its masters'
///        patches — so every node a master added keeps its real FormID: `baseMasters` resolves the base unit's own
///        FormIDs (its owner first), merged-in ids are canonical, and all are re-expressed through `patchMasters`
///        (`[self, skyrim, masters…]`, by bundle stem).
bool DeriveHkyPatch(const ParsedHky& base, const std::vector<std::string>& baseMasters,
                    const std::vector<std::string>& patchMasters, const std::vector<std::uint8_t>& modBytes,
                    const schema::SchemaRegistry& reg, const std::string& patchDir, std::string& err,
                    const std::vector<std::string>* bones = nullptr, HkyPatchStats* stats = nullptr);
/// @brief Apply a patch unit onto a parsed graph. FormIds resolve through each unit's master table (`baseMasters` /
///        `patchMasters`, index -> bundle), so a patch's `0001x…` names the base's `0000x…`. A patched node changes
///        only the fields it names.
/// @note What mods co-edit has its OWN identity (user decision, 2026-09-29 — Bethesda's model): a roster's entries
///       are sub-records, overridden / added / deleted by key, so two mods' entries never collide; everything else is
///       a whole value, and the last mod to change it wins. There is no element-wise list merging.
bool MergeHkyPatch(ParsedHky& base, const CB::core::common::IUnitSource& patch, const schema::SchemaRegistry& reg,
                   const std::vector<std::string>& baseMasters, const std::vector<std::string>& patchMasters,
                   std::string& err, const std::vector<std::string>* bones = nullptr, HkyPatchStats* stats = nullptr);
/// @brief The RUNTIME compile through the reflective format: `layers[0]` is the graph's base unit (parsed whole), every
///        later layer a patch unit merged in load order — each layer's FormIds resolve through its own master table
///        (`[self, skyrim, declared…]` by bundle stem), so `0001x…` in a mod and `0000x…` in Skyrim.hky name the same
///        node. The merged graph serializes with `header` into `out`.
bool CompileLayersReflective(const std::vector<CB::core::common::LayerSource>& layers, const schema::SchemaRegistry& reg,
                             const std::vector<std::string>* bones, std::vector<std::uint8_t>& out, std::string& err,
                             const CB::core::codec::HKXHeader& header = CB::core::codec::HKXHeader::SkyrimSE());
/// @brief The merge half of CompileLayersReflective — the merged graph, for the compile stages that edit or read it
///        (features, reference qualification, adsf-derive) before SerializeGraph.
/// @brief A character unit's roster recipe (its animations.yaml), if it has one.
std::optional<CB::core::anim::AnimationScan> ReadAnimationScan(const CB::core::common::IUnitSource& src);
/// @brief A character unit's PROJECT folder as a lowercase '/' data path: the unit path minus `characters[ x]/<name>.hkx`.
std::string ProjectDirOf(const std::string& unitKey);
/// @brief Build every `scan:` list of `graph` (a unit at serve path `unitKey`) from `index`. Returns how many it built.
std::size_t ExpandAnimationScans(ParsedHky& graph, const std::string& unitKey, const CB::core::anim::AnimationIndex& index,
                                 std::vector<std::string>* collisions = nullptr);
/// @brief Empty every `scan:` list under `root` (a gate compares everything EXCEPT the scanned rosters).
void ClearScanFields(const std::shared_ptr<IHavokObject>& root);
bool MergeLayersReflective(const std::vector<CB::core::common::LayerSource>& layers, const schema::SchemaRegistry& reg,
                           const std::vector<std::string>* bones, ParsedHky& graph, std::string& err);
bool SerializeGraph(const ParsedHky& graph, std::vector<std::uint8_t>& out, std::string& err,
                    const CB::core::codec::HKXHeader& header = CB::core::codec::HKXHeader::SkyrimSE());
/// @brief Apply hand-written patch units onto a base binary, in load order (unit k's masters: `mod<k>`, then the base
///        as master 1 — so `0001x…` names a base node, `0000x…` the unit's own). Writes `<workDir>/base` (the base
///        unit), `<workDir>/merged.hkx` and `<workDir>/merged` (the result, re-decompiled).
bool MergeHkyUnits(const std::vector<std::uint8_t>& baseBytes, const std::vector<std::string>& patchDirs,
                   const schema::SchemaRegistry& reg, const std::string& workDir, std::string& err,
                   const std::vector<std::string>* bones = nullptr);
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
// result is a single SchemaObject graph — serialized, it is the mod side ConvertModDelta derives a patch from.
// `deltaIds` receives every id a patch overrode or added — the set a per-mod .hky delta emits.
//
// Base + patches are already the Nemesis flow's inputs: pass each patch through xml::StripPatchOriginals
// (apply MOD_CODE OPEN edits) before handing its text here.
bool MergeTagfiles(const std::string& baseXml, const std::vector<std::string>& patchXmls,
                   const schema::SchemaRegistry& reg, ParsedTagfile& out,
                   std::vector<std::string>& deltaIds, std::string& err);

/// @brief One bundle's PATCH for one graph, from its Nemesis patches (`patchDirs`' `#*.txt`, load order).
///        The mod side is the tagfile merge (MergeTagfiles) of its masters' patches (`masterPatchDirs`, load order)
///        and its own over the vanilla template; it is derived (DeriveHkyPatch) against `base` — the graph's base
///        unit with its masters' patch units merged in — so a master's node keeps its FormID and a `#<code>$N`
///        reference to it lands on that FormID. `patchMasters` is the bundle's table `[self, skyrim, masters…]`.
/// @note  Never the base unit's template-renumbered ids: the patch names base nodes by the base unit's own FormIDs.
struct ModDeltaResult {
    bool        ok = false;
    std::string error;
    int         patchNodes = 0;   ///< the bundle's own `#*.txt` patch objects read
    int         patched = 0;      ///< base nodes the patch changes
    int         added = 0;        ///< nodes the patch adds
};
ModDeltaResult ConvertModDelta(const std::string& baseTagfileXml, const std::vector<std::string>& masterPatchDirs,
                               const std::vector<std::string>& patchDirs, const ParsedHky& base,
                               const std::vector<std::string>& baseMasters, const std::vector<std::string>& patchMasters,
                               const schema::SchemaRegistry& reg, const std::string& outDeltaDir,
                               const std::vector<std::string>* bones = nullptr);
} // namespace CB::core::decompile
