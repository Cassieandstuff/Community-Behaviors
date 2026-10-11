#pragma once
/// @file
/// @brief The converter core: turns a Nemesis/Pandora behavior load order into per-mod Community Behaviors .hky
///        bundles plus a loadorder.txt, each graph patch derived against the shipped Skyrim.hky base
///        (CB::engine::decompile::ConvertModDelta); also the derived-bundle regen and the offline gates.
#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace CB::converter {

using LogFn = std::function<void(std::string)>;

struct Options {
    std::string dataDir;       // VFS Data folder (contains Nemesis_Engine/mod/<code>/)
    std::string templatesDir;  ///< vanilla tagfile templates <g>.xml (+ _1stperson/<g>.xml);
                               ///< characters/ subdir = vanilla character files at their
                               ///< meshes-relative paths (e.g. characters/actors/character/
                               ///< characters/defaultmale.hkx)
    std::string outputDir;     ///< writes <out>/community_behaviors/plugins/... + loadorder.txt

    // MO2 instance root (contains mods/ + profiles/<profile>/modlist.txt). When resolved, the
    // converter ATTRIBUTES each contribution to its owning installed mod: per-mod bundles named
    // <modName>.hky (both legs — Nemesis codes AND precompiled loose graphs — grouped under the
    // one mod), and the delta load order follows modlist.txt priority (top = winner). Empty =
    // best-effort auto-derive from dataDir / %LOCALAPPDATA%\ModOrganizer; if nothing resolves the
    // converter falls back to the un-attributed VFS path (flat <code>.hky + anonymous
    // BehaviorFiles.hky), byte-identical to the pre-discovery behavior.
    std::string mo2Instance;
};

// ── Pre-conversion load-order plan ────────────────────────────────────────────────────────────────
// The framework the GUI arranger + the converter both read. Built once from an MO2 profile BEFORE any
// conversion, so master tables are decided up-front and stable (append-only). One entry per installed mod
// that carries behavior content (Nemesis codes and/or precompiled graphs), in load order.
struct PlanMod {
    std::string              modName;                 // installed mod folder name (bundle identity)
    int                      priority = 0;            // 1 = winner (top of the load order)
    std::vector<std::string> codes;                   // its Nemesis mod-codes (original case)
    bool                     hasPrecompiled = false;  // ships precompiled loose behavior graph(s)
    // Ordered master BUNDLE names: index 0 is IMPLICIT self; [0]="Skyrim" (the base, present for all),
    // [1..] = cross-bundle masters resolved from shared mod-codes (a mod referencing another's code).
    // FormId master table = [self(implicit 0), Skyrim(1), ...cross-bundle(2+)] — append-only stable.
    std::vector<std::string> masters;
};

// The whole plan: mods in load order + the code->owning-mod map the cross-bundle master resolution used.
struct LoadOrderPlan {
    bool                                         ok = false;
    std::string                                  error;
    std::string                                  profileDir;   // resolved profiles/<profile>
    std::string                                  modsDir;      // resolved mods/
    std::string                                  gameDataDir;  // from the profile (may be empty)
    std::vector<PlanMod>                         mods;         // priority 1 (winner) FIRST
    std::unordered_map<std::string, std::string> codeOwner;    // PathKey(code) -> owning mod (top wins)
};

// Build the plan from an MO2 profile folder OR instance root (auto-resolves the active profile). Read-only
// (no conversion, no disk writes). Catalogues each mod's codes, resolves cross-bundle masters by scanning
// each mod's patches for foreign #code$N references, and orders masters by load priority.
LoadOrderPlan BuildLoadOrderPlan(const std::string& profileOrInstance, const LogFn& log);


/// @brief One root .bsa of an enabled mod, with the engine's verdict on whether it is ever loaded.
struct ModArchiveInfo {
    std::string modName;
    std::string archive;         ///< full path on disk
    std::string plugin;          ///< the active plugin that loads it (counted only)
    std::string reason;          ///< human-readable verdict (why counted / why skipped)
    int         loadIndex = -1;  ///< that plugin's load index (counted only; later = higher archive precedence)
    bool        counted = false;
};

/// @brief Resolve which enabled mods' archives the engine loads (plugin rule + MO2 VFS precedence). Read-only.
std::vector<ModArchiveInfo> ResolveModArchives(const std::string& mo2Instance, const LogFn& log);

/// @brief Run ONLY the per-mod animation packaging pass (loose + engine-loaded archives, every decompilable
///   animation) into <pluginsDir>/<mod>.hky/ unit trees — no behavior conversion. The stage gate / profiling harness.
bool PackageAllModAnimations(const std::string& mo2Instance, const std::string& pluginsDir, const LogFn& log);

struct Result {
    bool        ok = false;
    std::string error;
    int mods       = 0;   // Nemesis codes that produced any bundle (behavior/setdata/animdata)
    int deltas     = 0;   // per-mod graph deltas emitted
    int charDeltas = 0;   // per-mod character roster deltas emitted
    int setMods    = 0;   // codes that contributed set-data
    int animMods   = 0;   // codes that contributed anim-data
    int fnisAnims  = 0;   // FNIS animations merged into FNIS.hky
    int fnisEvents = 0;   // FNIS events merged into FNIS.hky
    int skipped    = 0;   // graph/mod conversions that failed (see log)
};

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

/// @brief Build a shippable base bundle (Skyrim.hky, XPMSSE.hky, ...): decompile EVERY behavior-system file
///        (behaviors / projects / characters / skeletons / animations) under `vanillaMeshesDir` into a staging tree
///        and pack it into the single-file `outHky`. Skyrim.hky is the base every mod bundle layers over — the
///        "Skyrim.esm" of behaviors. BLOCKING — run on a worker thread.
/// @param outHky its filename names the bundle; any regen source runs this same build over whatever its meshes tree holds.
/// @param keepStagingDir when non-empty, the unpacked <Name>.hky/ tree is KEPT under it (only <Name>.hky/ is cleared;
///        the root is shared across bundles) instead of a temp dir deleted after packing. Pick a SHORT path (MAX_PATH).
/// @pre The shared schema registry is armed (SetSharedSchemaDir) — the build fails without it.
BaseBuildResult BuildBaseBundle(const std::string& vanillaMeshesDir, const std::string& outHky,
                                const LogFn& log, const std::atomic<bool>& cancel,
                                const std::string& keepStagingDir = "");


/// @brief The multi-layer gate over every templated graph patched by 2+ Nemesis mods under `modsDir` (MO2 mods):
///        each mod's derived field patch merged onto the base in order vs today's all-mods MergeTagfiles.
bool MultiLayerRoundTripAll(const std::string& modsDir, const std::string& templatesDir, const std::string& workRoot,
                            const LogFn& log, const std::string& baseHky = "");
/// @brief Apply hand-written .hky patch units (in order) onto `meshesDir/rel` (a vanilla behavior / character .hkx); the
///        base unit, merged unit and merged.hkx land in `outDir`. Bones resolve through `baseHky`'s rigs.
bool HkyMerge(const std::string& baseHky, const std::string& meshesDir, const std::string& rel, const std::string& outDir,
              const std::vector<std::string>& patchDirs, const LogFn& log);
/// @brief The field-patch gate: for every behavior/character .hkx under `moddedMeshes` with a vanilla counterpart,
///        merge(vanilla, derive(vanilla, mod)) must serialize byte-identical to the mod. Work trees under `workRoot`.
bool PatchRoundTripAll(const std::string& vanillaMeshes, const std::string& moddedMeshes, const std::string& workRoot,
                       const LogFn& log, const std::string& baseHky = "");
/// @brief Root-motion fold gate over an unpacked tree's animation units that carry `motion:`: fold the motion onto the
///        root bone, extract it back (XYZ + yaw, HCT-style), and compare through the engine's own motion evaluator.
///        Clean-root clips must round-trip; root-residual clips report how much physical motion extraction adds.
///        Writes a per-unit CSV to `outCsv`.
bool MotionFoldGate(const std::string& treeDir, const std::string& outCsv, const LogFn& log);
/// @brief The adsf projection gate: project the AnimationDataSingleFile from `hkyPath` alone (its project index,
///        graphs, rosters and animations — ProjectAnimData, the runtime's code) and compare it against `vanillaTxt`:
///        clips (presence, animIndex, speed, triggers) and each clip's motion through the engine's evaluator.
bool AdsfProjectionCheck(const std::string& hkyPath, const std::string& vanillaTxt, const LogFn& log);
/// @brief The schema-driven text codec gate: read each file through its `kind: metadata` descriptors (MetaText), write
///        it back, and require byte-identical output; record counts are cross-checked against the hand-written parsers.
///        A file whose name contains "animationset" reads as AnimationSetDataSingleFile, else AnimationDataSingleFile.
bool MetaTextRoundTrip(const std::vector<std::string>& files, const LogFn& log);
/// @brief The CB-8 gate: every vanilla spline animation under `meshesDir` that carries float tracks is decompiled,
///        recompiled, and its floatBlockOffsets compared against vanilla's. Also reports how vanilla fills the array
///        for clips without float tracks, and whether its offsets are block-relative.
bool AnimFloatOffsetsGate(const std::string& meshesDir, const std::string& workRoot, const LogFn& log);
/// @brief End-to-end check of the folded storage: every unit that carried a `motion:` record in `oldTree` (a pre-fold
///        regen) must resolve — as the runtime resolves it (ResolveRootMotion) — from the unit of the same path in
///        `newTree` to the same engine-sampled motion.
bool MotionStorageCheck(const std::string& newTree, const std::string& oldTree, const LogFn& log);
/// @brief The reflective-format gate over every behavior .hkx under `meshesDir`: vanilla -> reflective .hky ->
///        ParseHky -> binary, byte-compared (RoundTripReflective). Differing graphs' trees are kept under
///        `<workRoot>/<n>` (SHORT path). `baseHky` supplies the rig binding for bone names.
/// @param allKinds also gate characters, projects and skeletons (the reflective codec is schema-generic).
bool HkyRoundTrip(const std::string& meshesDir, const std::string& workRoot, const LogFn& log, const std::string& baseHky = "",
                  bool allKinds = false);
/// @brief The serializer's own round-trip over every behavior .hkx: deserialize -> re-serialize vs the source bytes.
///        Differing pairs are kept as `<outRoot>/<n>/{src,canon}.bin` for inspection.
bool SerializerRoundTrip(const std::string& meshesDir, const std::string& outRoot, const LogFn& log);

struct RegenResult {
    bool            ok = false;
    std::string     error;
    BaseBuildResult build;                 // counts from the underlying BuildBaseBundle
    int             checked  = 0;              ///< units with a source binary, validated
    int             faithful = 0;              ///< of those, serving byte-identical to the source's re-serialization
    std::vector<std::string> drift;            ///< units that don't (or fail to parse)
};

// Regenerate the shipped Skyrim.hky MASTER **and validate it** in one call — the reproducible,
// self-checking entry point for rebuilding the base (use this instead of a bare BuildBaseBundle
// so a lossy-flag / dropped-symbol regression can never silently ship again). It:
//   1. builds the whole master to a TEMP file via BuildBaseBundle (every unit, reflective + skeletons),
//   2. GATE: parses every behavior / character / project unit back through the runtime's reader (rig-bound
//      bones) and serializes it, byte-comparing against its source binary's canonical re-serialization —
//      exactly what the runtime serves for an untouched graph — collecting any drift, then
//   3. promotes TEMP -> outHky. In `strict` mode any drift is fatal and outHky is left untouched;
//      otherwise it writes and returns the drift list. BLOCKING — run on a worker thread.
RegenResult RegenerateMaster(const std::string& vanillaMeshesDir, const std::string& outHky, bool strict,
                             const LogFn& log, const std::atomic<bool>& cancel,
                             const std::string& keepStagingDir = "");

}  // namespace CB::converter
