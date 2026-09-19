#pragma once
// Converter — turns a Nemesis/Pandora behavior load order into per-mod Community Behaviors
// .hky bundles, calling havok::sct::ConvertPatch (the same core as havok-core-cli's
// vanbase / patchdelta). This is the C++ port of tools/br_stage_delta.sh, per Nemesis
// code: one vanilla BASE bundle + one .hky per mod code, plus a loadorder.txt.
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace bconv {

struct Options {
    std::string dataDir;       // VFS Data folder (contains Nemesis_Engine/mod/<code>/)
    std::string templatesDir;  // vanilla templates: <g>.hkx (binary) + <g>.xml (tagfile);
                               // characters/ subdir = vanilla character files at their
                               // meshes-relative paths (e.g. characters/actors/character/
                               // characters/defaultmale.hkx)
    std::string baseDir;       // pristine vanilla singlefiles (animation{set,}datasinglefile.txt)
    std::string outputDir;     // writes <out>/community_behaviors/plugins/... + base/ + loadorder.txt

    // MO2 instance root (contains mods/ + profiles/<profile>/modlist.txt). When resolved, the
    // converter ATTRIBUTES each contribution to its owning installed mod: per-mod bundles named
    // <modName>.hky (both legs — Nemesis codes AND precompiled loose graphs — grouped under the
    // one mod), and the delta load order follows modlist.txt priority (top = winner). Empty =
    // best-effort auto-derive from dataDir / %LOCALAPPDATA%\ModOrganizer; if nothing resolves the
    // converter falls back to the un-attributed VFS path (flat <code>.hky + anonymous
    // BehaviorFiles.hky), byte-identical to the pre-discovery behavior.
    std::string mo2Instance;

    // MO2-PROFILE mode: merge the WHOLE load order into ONE Pandora.hky (unified delta per graph, in
    // MO2 order — later/higher-priority mod wins, matching ConvertModDelta's later-overrides-earlier).
    // The user ships one bundle and never manages a CB load order; the runtime merge collapses to
    // base + Pandora.hky (+ CB-native on top). Default false = per-mod bundles (single-mod / author path).
    // Kept before pandoraModOrder so the positional aggregate-init in ConverterUI (…, mo2Instance,
    // singleBundle) stays valid.
    bool singleBundle = false;

    // MO2-PROFILE mode: an OPTIONAL user-tuned MOD order (owning-mod names, TOP = winner = Pandora
    // priority 1). Empty = use Pandora's ActiveMods.json order (or scan order). When set, the single-
    // bundle merge groups each code under its owning mod and orders the mods per this list (winner last
    // in the merge). Populated by the Pandora Order tab from its persisted pandora_order.txt; the --cli
    // path leaves it empty (pure Pandora order). Kept LAST.
    std::vector<std::string> pandoraModOrder;
};

// One code's place in the Pandora load order, for the Pandora Order UI + analysis. `owningMod` is the
// installed mod that ships the code (empty if no MO2 instance / unresolved — display falls back to code).
struct PandoraCodeInfo {
    std::string code;
    std::string owningMod;
    int         priority = 0;     // Pandora ActiveMods priority (1 = winner); 0 if unranked/scan-order
    bool        active   = true;  // Pandora active flag (inactive = excluded from the merge)
};

// One graph touched by 2+ codes — where merge ORDER matters. `winner` is the code latest in the merge
// order (later-overrides-earlier). `nodeCollisions` = base nodes (#NNNN) that 2+ codes override.
struct PandoraGraphConflict {
    std::string              graph;           // "1hm_behavior", "_1stperson/1hm_behavior"
    std::vector<std::string> codes;           // codes touching this graph, in merge order
    int                      nodeCollisions = 0;
    std::string              winner;
};

// Read-only analysis of the active Pandora load order for the UI (display + conflict flags), computed
// the SAME way the conversion orders (shared helpers), so the tab and the convert never drift.
struct PandoraAnalysis {
    bool                             fromPandora = false;  // true = order came from ActiveMods.json
    std::vector<PandoraCodeInfo>     codes;                // DISPLAY order: winner (priority 1) FIRST
    std::vector<PandoraGraphConflict> conflicts;           // graphs with 2+ codes, order-sensitive
};

// Analyze the active load order in `dataDir` (Nemesis_Engine/mod/* codes) against Pandora's
// ActiveMods.json + (optionally) the MO2 instance for owning-mod grouping. Read-only; no conversion.
PandoraAnalysis AnalyzePandoraOrder(const std::string& dataDir, const std::string& mo2Instance);

struct Result {
    bool        ok = false;
    std::string error;
    int graphs     = 0;   // vanilla base graphs emitted (-> Skyrim.hky)
    int charUnits  = 0;   // vanilla character units emitted (-> Skyrim.hky)
    int mods       = 0;   // Nemesis codes that produced any bundle (behavior/setdata/animdata)
    int deltas     = 0;   // per-mod graph deltas emitted
    int charDeltas = 0;   // per-mod character roster deltas emitted
    int setMods    = 0;   // codes that contributed set-data
    int animMods   = 0;   // codes that contributed anim-data
    int baseFiles  = 0;   // pristine vanilla singlefiles copied to Skyrim.hky/meshes/ (0..2)
    int fnisAnims  = 0;   // FNIS animations merged into FNIS.hky
    int fnisEvents = 0;   // FNIS events merged into FNIS.hky
    int skipped    = 0;   // graph/mod conversions that failed (see log)
};

using LogFn = std::function<void(std::string)>;

// Convert the whole load order. BLOCKING — run on a worker thread. `log` receives
// progress lines; `cancel`, when set, aborts at the next graph boundary.
Result ConvertLoadOrder(const Options& opt, const LogFn& log, const std::atomic<bool>& cancel);

struct BaseBuildResult {
    bool        ok = false;
    std::string error;
    int behaviors = 0, projects = 0, characters = 0, skeletons = 0, failed = 0;
    // Animation BAKE (decompile hkx -> animation.yaml into the master, self-gated on pose fidelity):
    // animOk = baked (round-trip faithful within tolerance, unit kept in the master); animSkip = not
    // baked (non-spline, decode/recompile failure, or fidelity/shape miss) — falls through to the loose
    // vanilla .hkx at runtime; animFail = the source .hkx couldn't even be read.
    int animOk = 0, animFail = 0, animSkip = 0;
};

// Build the shippable Skyrim.hky MASTER: decompile EVERY vanilla behavior-system file
// (behaviors / projects / characters) under `vanillaMeshesDir` into a staging tree and
// pack it into the single-file `outHky`. This master is the base every BR mod deltas
// against — the "Skyrim.esm" of behaviors; without it there is no editable surface.
// Staging goes to a SHORT temp dir so the deep unit tree never trips MAX_PATH, and only
// the packed archive is kept. BLOCKING — run on a worker thread.
//
// `templatesDir` (optional): a folder of `<behavior-stem>.xml` tagfile templates. Any
// behavior with a matching template is decompiled through the tagfile-id ORACLE so its
// node ids are the #NNNN the per-mod deltas reference; without this the base uses the
// decompiler's encounter-order ids, which drift from the deltas and mis-merge overrides
// onto the wrong node (the horse-behavior null-child crash). Behaviors with no template
// have no delta either, so their (arbitrary) numbering is harmless. Empty = the old
// all-encounter-order behaviour.
// `keepStagingDir` (optional): when non-empty, the unpacked staging tree (Skyrim.hky/<meshes…>) is
// built THERE and KEPT instead of a temp dir that's deleted after packing — a short, observable layout
// for debugging with no separate unpack step. Pick a SHORT path (e.g. a D:\ root) to stay under MAX_PATH.
BaseBuildResult BuildBaseBundle(const std::string& vanillaMeshesDir, const std::string& outHky,
                                const LogFn& log, const std::atomic<bool>& cancel,
                                const std::string& templatesDir = "", const std::string& keepStagingDir = "");

struct RegenResult {
    bool            ok = false;
    std::string     error;
    BaseBuildResult build;                 // counts from the underlying BuildBaseBundle
    int             checked  = 0;          // templated graphs validated against vanilla
    int             faithful = 0;          // of those, byte-identical graphdata to vanilla
    std::vector<std::string> drift;        // graphs whose regenerated graphdata != vanilla
};

// Regenerate the shipped Skyrim.hky MASTER **and validate it** in one call — the reproducible,
// self-checking entry point for rebuilding the base (use this instead of a bare BuildBaseBundle
// so a lossy-flag / dropped-symbol regression can never silently ship again). It:
//   1. builds the whole master to a TEMP file via BuildBaseBundle (canonical corpus + templates),
//   2. GATE: for every templated graph, compiles its base unit out of the fresh master, decompiles
//      it, and byte-diffs data/graphdata.yaml against the SAME graph's vanilla binary decompile
//      (graphdata is where the symbol tables + role/flags live — exactly what the XML-oracle path
//      used to corrupt), collecting any drift, then
//   3. promotes TEMP -> outHky. In `strict` mode any drift is fatal and outHky is left untouched;
//      otherwise it writes and returns the drift list for the caller/operator to eyeball (some
//      drift is a known-benign pre-existing quirk, e.g. the ' iState_NPCSneaking' leading-space
//      duplicate magicbehavior/magicmountedbehavior carry). BLOCKING — run on a worker thread.
RegenResult RegenerateMaster(const std::string& vanillaMeshesDir, const std::string& templatesDir,
                             const std::string& outHky, bool strict,
                             const LogFn& log, const std::atomic<bool>& cancel,
                             const std::string& keepStagingDir = "");

}  // namespace bconv
