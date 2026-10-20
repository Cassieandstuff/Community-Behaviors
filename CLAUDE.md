# CLAUDE.md

Guidance for Claude Code working in the **Community Behaviors** standalone repo.

---

## Case: CB never changes the case of anything (directive)

**Names are copied byte-exact and compared EXACTLY** — event / variable / character-property / attribute
names, clip and annotation text, setdata project and set names, every string inside a unit. The engine binds
them case-sensitively: a miscased name binds to *nothing* (A-pose, dead root motion) or the *wrong* thing —
never an error. Reflection carries them verbatim; nothing may fold them.

**Folding exists only INSIDE a codec whose external definition folds** — the setdata path CRC (`codec/crc`),
the adsf's lowercase animation file names, and PATH IDENTITY (does an engine request name this unit/file?),
which the engine's own resource lookup resolves case-insensitively (BSA lookup hashes the lowercased path —
vanilla BSAs store every name lowercase, while references spell paths in mixed case). The fold never escapes
its codec: no ad-hoc ToLower helpers.

**A file CB writes is opened by the exact string CB wrote it under** (cache / swap paths), so it is precise on
any filesystem, Linux included. Unit names keep their source's spelling (vanilla's is lowercase). Treat any
new ToLower / case-insensitive compare outside those codecs as a bug.

---

## What this repo is

**Community Behaviors (CB)** — a runtime SKSE plugin (CommonLibSSE-NG, GPL3) that compiles Havok
behavior graphs over a mod load order and serves them to the live game, plus the offline **Behavior
Converter** that ingests Nemesis/Pandora load orders into CB `.hky` bundles. It was extracted from a
monorepo into this single-purpose repo; the data-driven Havok stack IS CB's own source, not vendored
libs.

## Layout

```
src/                         CB's code. src/ is the plugin's private include root.
  core/                      THE SKSE PLUGIN (CB::core::), foldered by pipeline stage (include
                             stage-prefixed, e.g. "core/resolve/Resolver.h"):
    Plugin.cpp               the SKSE entry point (defines SKSEPluginLoad)
    pch/                     PCH.h (force-included) + PluginLogger.h (LOG_* macros); on the include path,
                             so bare "PCH.h"/"PluginLogger.h" resolve
    discover/                find + read the .hky bundles (BundleReader, BundleManifest, ServeKey)
    resolve/                 load order → merged/compiled graphs + membranes (Resolver, SymbolInjector,
                             Watermark). Compile itself is delegated to the engine.
    serve/                   deliver binaries to the live engine (ByteServe, Animation{,Set}DataServer);
                             conditions/ = the opt-in condition sensors on the setdata matcher
    bootstrap/               startup + warm-up + progress (CompileGate, ProgressOverlay, ProgressHud);
                             sequencer/ = the warm-up ThreadPool
    debug/                   opt-in diagnostics (DebugFlags — the flag registry the converter's Debug tab
                             renders; Settings — settings.ini loaded once, read via FlagOn(id); DebugOverlay;
                             RuntimeTrace — the per-frame graph-variable watch)
  platform/                  THE RUNTIME SDK (CB::platform::), plugin-only: what makes havok/ and the game usable
                             from a feature. hooks/ = the hook library (<platform/hooks/hookslib.h>): Override<&fn>
                             on any declared function (the takeover's cell when the image runs it, else an entry
                             detour), CallSite<engine::X, Sig> on one call instruction, Live<&global>(); one
                             subscriber chain, attached once by InstallAll. hooks/registry = engine.yaml (per-runtime
                             Address Library pins + identity fields); hooks/takeover = the runtime image relocated at
                             load, every replaced function behind a cell, and the schema link shim. game/ = the game
                             functions CB calls or hooks, declared as free functions over CommonLib types, each with
                             an engine.yaml row (its decorated name comes from the first link error). Flags,
                             settings, the overlay host and startup phases move here next; until then it still
                             reads core/debug.
  engine/                    THE SHARED REFLECTION ENGINE (CB::engine::) — one `engine` static lib the plugin,
                             converter, and tools all link. CommonLib-FREE: configure fails on any RE/, SKSE/
                             or REL/ include. The folder IS the include root (<common/…> <interface/…>
                             <interface/reflection/…> <codec/…> <compile/…> <decompile/…> <text/…> <disk/…> <exec/…>); headers sit beside
                             their sources.
  features/<Owner>/          self-registered graph features (CB_REGISTER_FEATURE, CB::feature::), compiled
                             straight into the plugin; ERGate.h (the ER wildcard-gate hook) lives here too
  converter/                 the GLFW+ImGui Behavior Converter (BehaviorConverter.exe, CB::converter::);
                             platform/ = native dialogs, known folders, zip
common/                      what features build against; common/ is their include root:
  feature-sdk/               <feature-sdk/FeatureRegistry.h> — IGraphFeature + CB_REGISTER_FEATURE (CB::feature::).
                             Havok needs no SDK copy: features include havok/'s headers and call them as declared;
                             Override<&fn> and Live<&global>() (platform/hooks) reach the takeover's cells and the
                             exe's mutable globals. havok/ stays pristine; changes are overrides.
  config/                    <config/Config.h> — the config scanner (discover + read + JSON, CB::config::)
schema/                      the class schema as DATA — one YAML per class in family folders (generators/,
                             modifiers/, …; vanilla hk*, Bethesda BS*, CB* custom classes told apart by prefix);
                             metadata/ holds enums, merge semantics, debug probes and the adsf/asdsf descriptors.
                             templates/ = the converter's Nemesis-ingest XML oracles. Deployed as data: plugin →
                             Data/community_behaviors/schema (no templates/), converter → <exe>/schema.
havok/                       the reimplemented Havok runtime (CB::havok::), laid out like Havok's source tree
                             (Common/Base, Animation, Behavior, Physics; below them the exe's __FILE__ paths), compiled
                             by tools/cbmatch (VS2015) into the runtime image. Its headers also compile into the
                             plugin, where the schema link shim (engine.yaml `mangled` rows) binds every out-of-line
                             function and const global to the exe. Never deployed.
tools/                       dev-only, never shipped: hky-tool, cbdiff, cbmatch (byte-matches havok/ against
                             the exe), x64dbg-autoattach, blender-control-rig. cbmatch/workbench/ = functions still
                             being matched (manifests/workbench.yaml, failures expected); cbmatch/devices/ = matching
                             devices, not Havok files
features/                    authored FEATURE BUNDLES as data — not code (that is src/features/): one <Name>.hky/
                             folder per feature, assembled at build time into the shipped <Name>.hky
                             (cmake/FeatureBundles.cmake: an LFS-pointer guard, then `hky-tool assemble`; the plugin
                             target depends on it). Inside: PARTITIONS (*.hky at the top: packed YAML trees laid out
                             like a Data folder, e.g. Animations.hky holding meshes/actors/character/animations/...;
                             Git LFS, `git lfs pull` before building; repacked only deliberately with `hky-tool pack`,
                             never by the build), loose files shipped at their path (manifest.json, hand-authored
                             patch units), generate/ — JSON configs naming a generator hky-tool knows (today:
                             clipSelectorBehavior, rosterPatch) that writes files derived from a partition, so they can
                             never drift from it — and inject/ —
                             JSON configs {"targets": [{"path", "field", "values"}]} keyed by a file's path inside the
                             bundle, writing the fields tools/hky/injectables.json allows per file kind (today:
                             animation -> tags; anything else fails the build). `hky-tool autotag` seeds tag configs
                             and never overwrites one.
include/ + API/              the public CB-API facade (<CB-API.h>) + its INTERFACE target and drift-gate
                             (cb-api-check). Consumed by Skyrim Content Tools (FetchContent, API-only, pinned by
                             commit) — a change there bumps its pin. Nothing private goes in include/.
```

## Namespacing (follows the folder) — directive

The top-level folder picks the ROOT; the purpose word names what the code is for. A file that moves to
another root renames its namespace; a stray top-level namespace is a bug.

- **Roots.** `CB::core::` = the plugin (src/core) · `CB::platform::` = the runtime SDK (src/platform) ·
  `CB::engine::` = the shared engine (src/engine) ·
  `CB::feature::` = features + the feature SDK · `CB::converter::` = the converter · `CB::config::` = the
  config scanner · `CB::havok::` = the reimplemented Havok runtime (havok/; Havok's class names directly under
  the root, opened as `namespace CB { namespace havok {` since VS2015 builds it). Nothing else at the top — no
  bare `havok::` outside CB, no `sct::` / `bconv::`.
- **Depth: `CB::<root>::<purpose>` (3) is the backbone.** A 4th level ONLY when a purpose holds 2+ sibling
  sub-namespaces that genuinely need distinguishing — a real family, not a category invented to feel
  tidy. 5 levels is a smell. When tempted, the 4th-level thing is usually its own purpose, or a type.
- **`<purpose>` is a role word**, the concern the code serves: `discover resolve serve bootstrap debug
  conditions` (core) · `hooks` (platform) · `compile decompile codec merge schema` + `skeleton anim animdata animsetdata
  linker` + `text disk exec` (engine). `text` = the shared string operations (trim, line split, BOM, float
  text form, YAML quoting); `disk` = whole-file read/write, checked and optionally atomic; `exec` = ParallelFor
  (offline tools — in-game work runs on the plugin's injected BELOW_NORMAL pool). Use them instead of
  re-rolling a per-file helper. The one sanctioned 4th-level family: `CB::engine::codec::{spline,crc,formid,io,resource}` + the
  packfile serialization spine (all genuinely codecs).
- **`CB::engine::common` is the shared VOCABULARY — the base tier everything may depend on.** It holds the
  cross-cutting *types* every layer passes around: the unit/layer vocabulary (`IUnitSource`,
  `LayerSource`) and the primitives (`Vector4`, `xml`, `vec4`). Not a junk drawer for *operations* — those
  stay in their purpose namespaces. Because the engine references these types UNqualified throughout, a
  force-included prelude (`src/engine/common/CorePrelude.h`, the engine's PCH, also pulled by the plugin
  PCH and the converter) forward-declares and makes `CB::engine::common` + `CB::engine::codec` ambient in
  every TU. So a file never needs a per-file `using namespace` for the shared vocabulary.
- **Depth is free — use the language.** Define with nested syntax `namespace CB::core::resolve { … }`
  (C++17, no brace pyramid); alias at heavy call sites `namespace ccr = CB::core::resolve;`. Verbosity at
  use sites is a `using`-alias problem, never a reason to flatten a real purpose.
- **Forward-declare a shared type in its OWN namespace.** A fwd-decl inside `CB::engine::compile` of a
  type that lives in `common` creates a phantom `compile::X` that SHADOWS the real one (a fwd-declared name
  in the current namespace beats a `using`-directive). Always `namespace CB::engine::common { struct X; }`.
- **`detail` / `en` / anonymous namespaces** stay as the innermost leaf under a purpose
  (`CB::engine::codec::detail`), never top-level.

## Build

CLI cmake works (generator **Visual Studio 18 2026**; VS's bundled cmake at
`C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`).
Single root, presets `release` / `debug` (triplet `x64-windows-static`, config **RelWithDebInfo**):

```
cmake --preset release
cmake --build build\release --config RelWithDebInfo --target CommunityBehaviors
```

- `-DCB_BUILD_CONVERTER=OFF` → lean plugin-only configure (skips the glfw/imgui FetchContent + converter).
- Build one target (`CommunityBehaviors`, `behavior-converter`, `hky-tool`) rather than `ALL` to
  avoid triggering the multi-minute `generate-base-master` regen.
- **No builds without a green light** — the deploy writes over the live MO2 mod + tools folder that the
  user (and parallel sessions) may be testing. Propose + wait for an explicit yes before building.

### Environment variables

- `VCPKG_ROOT` — vcpkg clone (toolchain, via the preset). Required.
- CommonLibSSE-NG is a **git submodule** at `lib/commonlibsse-ng` (alandtse fork, `ng` branch, full SE+AE+VR). No env var — `git submodule update --init --recursive` after clone.
- `SKYRIM_MODS_FOLDER` — MO2 mods folder; `cb_deploy` ships the mod there (`<mod>/SKSE/Plugins`,
  `<mod>/community_behaviors/schema`, `<mod>/community_behaviors/plugins/*.hky` bundles).
- `SKYRIM_TOOLS_FOLDER` — the converter deploys `BehaviorConverter.exe` (+ schema/, templates inside) here.
- `CB_REGEN_SOURCE` — root of the derived-bundle regen sources (e.g. `D:\CB Regen Source`): one folder per
  bundle, NAMED for it (`Skyrim.hky\`, `XPMSSE.hky\`), laid out like a Data folder (`meshes\...`). Each one
  runs the SAME `generate-base-master` regen (decompile everything it holds to reflective units, gate
  every unit's served bytes vs its own binaries, pack) → `<Name>.hky`. A new derived bundle = a new source folder.
  Unset → regen skipped, the already-deployed bundles stay.
- `CB_SKYRIM_AE_EXE` / `CB_ADDRESSLIB_AE` — the unpacked AE 1.6.1170 SkyrimSE.exe and its Address Library
  database, for `tools/cbmatch/manifests/*.yaml` (which also read `VS140COMNTOOLS`, set by the VS2015 toolset).
- `SCT_BASE_MASTER_DIR` — a **SHORT** D-drive path (e.g. `D:\sct-base-master`) for the unpacked trees (each
  regen keeps `<Name>.hky\` there). See the diff strategy below. Must be short: these trees blow past
  Windows MAX_PATH under a deep root.

## Base-master diff strategy (packed .hky is NOT bit-reproducible)

A packed `.hky` (Skyrim.hky, the bundles) is a miniz/deflate zip — its compressed bytes differ across
toolchain versions **even when the content is identical**, so a byte-diff of packed files is meaningless.
**Always diff the UNPACKED trees.** Two supports for this:

- `hky-tool unpack <in.hky> -o <shortDir>` — decompress any `.hky` to its YAML tree,
  original path case preserved. Unpack to a SHORT path (MAX_PATH).
- When `SCT_BASE_MASTER_DIR` is set, `cb_deploy` auto-unpacks the deployed master to
  `<SCT_BASE_MASTER_DIR>/current`, rotating the prior tree to `<SCT_BASE_MASTER_DIR>/previous` — so a
  two-point cross-build diff (`diff -rq previous current`) is always ready with no manual bookkeeping.
  The regen's kept-unpacked `<Name>.hky` trees honor the same env var.

## Scratch outputs go under `D:\CB-tmp` (directive)

Any offline scratch output that must live on a **SHORT** path to dodge Windows MAX_PATH — hky unpacks,
diff trees, merge/canary outputs, temp conversion targets, analysis dumps — goes in a **new named
subfolder** under `D:\CB-tmp\` (e.g. `D:\CB-tmp\1hm-scan\`), never straight onto the `D:\` root. Keep
each run's output in its own subfolder so it's self-identifying and easy to sweep. Do NOT scatter
`D:\rc`, `D:\co`, `D:\u`-style roots at the drive top level. (Session-private junk with no MAX_PATH need
still belongs in the session scratchpad dir.)

## Correctness gates

- **Served bytes** — `generate-base-master` parses every behavior / character / project unit back through
  the runtime's reader (rig-bound bones) and byte-compares its serialization to the source binary's
  canonical re-serialization (1238/1238 on Skyrim.hky).
- **Reflective round-trips** (BehaviorConverter verbs) — `--hky-roundtrip [--all-kinds]` (emit→parse→serialize),
  `--patch-roundtrip` (merge(base, derive(base, mod)) == mod), `--multilayer-roundtrip` (every mod's derived
  patch merged in order vs today's merge of all).
- **Metadata text codec** — `--metatext-roundtrip <file>...`: the adsf/asdsf read through their `kind: metadata`
  descriptors (`schema/metadata/`, MetaText) and written back must be byte-identical (vanilla, Pandora output,
  CB's cache), with record counts matching the hand-written parsers.
- **Animation float offsets** — `--anim-float-offsets <meshesDir> <work>`: the spline decoder's walk must land
  on vanilla's stored `floatBlockOffsets` for every vanilla clip (5937/5937), and every clip recompiled by CB must
  store exactly where the decoder finds its float section. Run it after any spline compressor change. (Nothing to
  bump: compiled animations are keyed on the plugin DLL's hash, so every rebuild recompiles them once.)
- **Runtime byte matching** — `cbmatch tools/cbmatch/manifests/schema.yaml --allow-variant --diff 256 --strict`
  compiles havok/ per translation unit and diffs every listed function against the exe (EXACT / REGALLOC /
  VARIANT, references checked against engine.yaml). havok/ holds proven code only: `--strict` fails on any
  out-of-line definition that is neither listed (and passing) nor a unit's declared `drivers:` entry. A body
  graduates from the workbench into its havok/ unit once it passes; `--diff <seeds>` also runs every REGALLOC/VARIANT function and vanilla side by side
  under emulation on synthetic inputs, checking behavior where VARIANT can't see register dataflow. A VARIANT that
  needs rule A (local stack slots rearranged — the home area, and incoming stack-argument slots after the function's
  first write to them), E (SIMD rescheduled), I (a duplicated return tail), J (register setup rescheduled), N
  (commutative SIMD operands swapped), R (a SIMD value recomputed), O (a load folded into its use), U (unaligned vs
  aligned moves), M (an increment done in memory or in registers) or L (whole blocks laid out in another order — a
  second pass, accepted only when every edge and every test's direction pairs) runs `--diff` always and fails unless
  it agrees.
  `tools/cbmatch/tests/mutants.sh` proves the gate rejects broken copies (STRUCTURAL, or VARIANT plus a `--diff`
  divergence for the documented blind spots), with unmutated controls that must agree.
  Run both after any havok/ or cbmatch change. A full run ends with coverage against
  `tools/cbmatch/inventory.yaml` (regenerate with `--inventory`; `--todo <Class>` lists what's left).
  The same full run regenerates the takeover's outputs: `--emit-image <file>` (deployed to
  `Data/community_behaviors/runtime/havok-runtime.cbimg`; pinned to the exact exe build) and
  `--emit-registry src/platform/hooks/registry/engine.yaml` (the shim's identity fields: mangled, header, sig, this) —
  always together, so the registry names exactly what the image installs.
- Skeletons are NOT reflective units: they stay `bonelist.yaml` + `bones/` (SkeletonYaml + CompileSkeletonFull)
  until the schema codec reads ragdoll physics.

---

## Knowledge tiers

- **CLAUDE.md** — always-on architectural SOP (this file). Keep it LEAN; it's injected every turn.
- **Auto-memory** — preferences, project state, and **design rationale** (*why* the code is shaped this
  way — grounded in RE, a failed approach, or a measurement), soft + per-user. This is the home for design
  thinking. Verify against the code, flag discrepancies, prune with authoritative caution (the memory skill).
  NOT bug status.
- **`docs/bugs/`** — the checked-in bug log: one file per OPEN bug + a thin index, and a fixed bug
  **leaves** (deleted with its index line in the same commit as the fix; git history is the archive —
  nothing accumulates). Bugs are shared/authoritative, so they live here, not in memory. Find a bug →
  add its file; fix it → remove it; sync in the same change as the code. See `docs/bugs/README.md`.
- **Code is the source of truth**, above docs and comments. Verify code-shaped claims against the code.

**The repo stays PURE — no design docs.** The only prose the repo carries is: build + architecture (this
file), how-to-use (README + API comments), and `docs/bugs/`. Design *rationale* graduates to memory (above);
*vision*/speculative work lives at most in an ephemeral plan that dies when the work lands — never a
checked-in artifact. **No artifact — doc, memory, or comment — may present unbuilt or unverified work as
settled:** rationale records a decision already grounded; vision stays disposable. (A shared `docs/decisions/`
rationale tier, bugs-sheet style, is a possible future move — not adopted; revisit if collaborators appear.)

When about to record something SOP-shaped into a checked-in file, flag it for the user's approval —
don't add it silently.

## Comment hygiene (the Doxygen paradigm)

Doxygen generates the API reference + class/include/collaboration/call graphs from the code ITSELF
(`Doxyfile` at repo root — tooling config; output to `D:\CB-tmp\doxygen`, a build artifact, never
committed). So **the code is the `what`**; comments carry only what the generated mirror can't derive.

- **No comment for what Doxygen already shows** — signatures, types, structure, "this is class/function X",
  restating what the line does. Redundant narration is DELETED, not written. The mirror shows it.
- **Comments exist ONLY as Doxygen annotations** (`/// @brief`, `@param`, `@return`, `@note`, `@warning`,
  `@invariant`, `@pre`/`@post`) — never plain prose comments. One form, so every kept comment lands in the
  generated reference.
- **Annotate only the non-derivable:** the *why* (rationale, a failed approach, a case-sensitivity / ordering
  trap), the *contract* (ownership, pre/post-conditions, invariants, units), the *gotcha*. If it restates the
  code, cut it.
- **Verified or gone.** An annotation is checked against the code the moment it's written or touched; a stale
  or wrong annotation is a bug — fix or delete it, never leave it lying (same leave-on-obsolete spirit as
  `docs/bugs/`).
- **Deep rationale that outlives one symbol → memory** (the design-rationale tier), not a wall of comment.

Going forward this is the only comment style; a retroactive sweep converts/prunes legacy comments to it
(verify-first, then annotate). A whole-tree sweep is merge-hostile — run it against a quiesced tree.
