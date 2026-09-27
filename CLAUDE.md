# CLAUDE.md

Guidance for Claude Code working in the **Community Behaviors** standalone repo.

---

## Havok is CASE-SENSITIVE — assume it always (directive)

**Assume at all times that Havok's internals — file/path evaluation, resource resolution, CRC
computation, name and binding lookups, and ANY data manipulation that touches Havok — are
CASE-SENSITIVE.** The engine and toolchain silently mismatch on case where the OS/USVFS would not,
and the failure is invisible: a miscased path/name/CRC binds to *nothing* (A-pose, dead root motion)
or the *wrong* thing — never an error. **Preserve the ORIGINAL case end-to-end** for every Havok
path, filename, clip/animation/event/variable name, and anything a CRC is computed over. Lowercasing
is safe ONLY for a purely INTERNAL key whose every side is controlled and lowercased identically —
the moment a value crosses back to the engine or vanilla data it must carry vanilla case. Treat any
new lowercase-normalization on Havok data as a bug until proven internal-only.

---

## What this repo is

**Community Behaviors (CB)** — a runtime SKSE plugin (CommonLibSSE-NG, GPL3) that compiles Havok
behavior graphs over a mod load order and serves them to the live game, plus the offline **Behavior
Converter** that ingests Nemesis/Pandora load orders into CB `.hky` bundles. It was extracted from a
monorepo into this single-purpose repo; the data-driven Havok stack IS CB's own source, not vendored
libs.

## Layout

```
include/                     PUBLIC API ONLY (repo root): CB-API.h — the single facade an external tool
                             #includes (<CB-API.h>). Nothing private goes here. Backed by the API/ target.
API/                         the cb::CB-API facade target (INTERFACE lib) + its dogfood drift-gate
                             (cb-api-check). CB is a first-party consumer of <CB-API.h>. Peer of APP/.
src/                         CB's source. src/ is the private include root; the runtime plugin is organized by
                             PIPELINE STAGE under core/, the engine libraries by CONCERN (each a module).
  Plugin.cpp                 the SKSE entry point (defines SKSEPluginLoad) — stays at src/ root
  pch/                       force-include-tier shared headers: PCH.h (force-included) + PluginLogger.h
                             (LOG_* macros). On the include path, so bare "PCH.h"/"PluginLogger.h" resolve.
  core/                      THE COMPILER PIPELINE, foldered by stage (include stage-prefixed, e.g.
                             "core/resolve/Resolver.h"):
    core/discover/           find + read the .hky bundles (BundleReader, BundleManifest, ServeKey)
    core/resolve/            load order → merged/compiled graphs + membranes (Resolver, SymbolInjector,
                             GraphClipSink, Watermark). Compile itself is delegated to the havok-* libs.
    core/serve/              deliver binaries to the live engine (ByteServe, Animation{,Set}DataServer,
                             CBMemoryStream, SkinnedMesh)
    core/bootstrap/          startup + warm-up + progress (CompileGate, ProgressOverlay, ProgressHud)
    core/debug/              opt-in diagnostics/probes (AnimDataProbe, SyncClipProbe, DebugOverlay, DebugFlags)
  features/<Owner>/          self-registered compile-time graph features (BR_REGISTER_FEATURE); ERGate.h
                             (the ER wildcard-gate hook, non-compiler) lives here too
  Hooks/                     header-only BSResource-ctor hook lib (<Hooks/hookslib.h>)
  havok-framing|schema|io|model|anim|pipeline/   the data-driven Havok stack (each an add_library module).
                             Headers + sources are CO-LOCATED (no include/ vs cpp/ split): a component's
                             Foo.h sits beside Foo.cpp under the module's namespaced path (e.g.
                             havok-model/havok/model/BehaviorData.{h,cpp}); the module root IS the include
                             root, so cross-module includes keep their <havok/model/…> paths. Exported
                             modules install only *.h. The public CB-API facade lives at the root include/.
  sct-config/                config scanner (JSON/YAML/INI); sct-utilities/ folder-picker+zip (converter)
Havok/core/Schema/           the vanilla class-schema tree (schema-as-core; room for Havok/features/ later)
Retirement Home/havok-core/  QUARANTINED legacy typed backbone — see its README; do NOT build new things
                             here, put new components in src/. It's the offline oracle + converter core.
APP/Behavior Converter/      the GLFW+ImGui converter (BehaviorConverter.exe); src/ flat, ships templates/+Havok/
```

## Namespacing (describes PURPOSE, not location) — directive

A namespace names WHAT code is for; a folder names where it happens to live. They are decoupled on
purpose — folders churn (the firesale moved everything), a purpose namespace must not. So the namespace
is NOT derived from the path, and a file moving folders never renames it.

- **One root. `CB::core::` for all core functionality; `CB::feature::<Name>` for features (later).**
  That is the entire reason `core` exists — there is no bare `havok::` / `sct::` / `linker` / `model` /
  `merge`, no second root. A stray top-level namespace is a bug.
- **Depth: `CB::core::<purpose>` (3) is the backbone.** A 4th level ONLY when a purpose holds 2+ sibling
  sub-namespaces that genuinely need distinguishing — a real family, not a category invented to feel
  tidy. 5 levels is a smell: the deep levels stop disambiguating anything 3 wouldn't. When tempted, the
  4th-level thing is usually its own level-3 purpose, or it's a type, not a namespace.
- **`<purpose>` is a role word**, the concern the code serves: `discover resolve serve bootstrap debug`
  (pipeline) · `compile decompile codec interface merge schema` (engine) · `skeleton anim animdata linker`
  (domain) · `hooks config` (infra). The one sanctioned 4th-level family: `CB::core::codec::{spline,crc,
  formid,vec4,io}` (all genuinely codecs).
- **Depth is free — use the language.** Define with nested syntax `namespace CB::core::resolve { … }`
  (C++17, no brace pyramid); alias at heavy call sites `namespace ccr = CB::core::resolve;`. Verbosity at
  use sites is a `using`-alias problem, never a reason to flatten a real purpose.
- **`detail` / `en` / anonymous namespaces** stay as the innermost leaf under a purpose
  (`CB::core::codec::detail`), never top-level.

## Build

CLI cmake works (generator **Visual Studio 18 2026**; VS's bundled cmake at
`C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`).
Single root, presets `release` / `debug` (triplet `x64-windows-static`, config **RelWithDebInfo**):

```
cmake --preset release
cmake --build build\release --config RelWithDebInfo --target CommunityBehaviors
```

- `-DCB_BUILD_CONVERTER=OFF` → lean plugin-only configure (skips the glfw/imgui FetchContent + converter).
- Build one target (`CommunityBehaviors`, `behavior-converter`, `havok-core-cli`) rather than `ALL` to
  avoid triggering the multi-minute `generate-base-master` regen.
- **No builds without a green light** — the deploy writes over the live MO2 mod + tools folder that the
  user (and parallel sessions) may be testing. Propose + wait for an explicit yes before building.

### Environment variables

- `VCPKG_ROOT` — vcpkg clone (toolchain, via the preset). Required.
- CommonLibSSE-NG is a **git submodule** at `lib/commonlibsse-ng` (alandtse fork, `ng` branch, full SE+AE+VR). No env var — `git submodule update --init --recursive` after clone.
- `SKYRIM_MODS_FOLDER` — MO2 mods folder; `cb_deploy` ships the mod there (`<mod>/SKSE/Plugins`,
  `<mod>/Community Behaviors/Havok` schema, `<mod>/community_behaviors/plugins/*.hky` bundles).
- `SKYRIM_TOOLS_FOLDER` — the converter deploys `BehaviorConverter.exe` (+ templates/ + Havok/) here.
- `SKYRIM_DATASOURCE` — unpacked vanilla game data root (contains `meshes\`); when set, arms the
  `generate-base-master` regen (derives + byte-validates `Skyrim.hky` vs vanilla). Unset → regen skipped,
  the already-deployed master stays.
- `SCT_BASE_MASTER_DIR` — a **SHORT** D-drive path (e.g. `D:\sct-base-master`) for the unpacked master.
  See the diff strategy below. Must be short: these trees blow past Windows MAX_PATH under a deep root.

## Base-master diff strategy (packed .hky is NOT bit-reproducible)

A packed `.hky` (Skyrim.hky, the bundles) is a miniz/deflate zip — its compressed bytes differ across
toolchain versions **even when the content is identical**, so a byte-diff of packed files is meaningless.
**Always diff the UNPACKED trees.** Two supports for this:

- `havok-core-cli hky-unpack <in.hky> -o <shortDir>` — decompress any `.hky` to its YAML tree,
  original path case preserved. Unpack to a SHORT path (MAX_PATH).
- When `SCT_BASE_MASTER_DIR` is set, `cb_deploy` auto-unpacks the deployed master to
  `<SCT_BASE_MASTER_DIR>/current`, rotating the prior tree to `<SCT_BASE_MASTER_DIR>/previous` — so a
  two-point cross-build diff (`diff -rq previous current`) is always ready with no manual bookkeeping.
  The regen's kept-unpacked tree honors the same env var.

## Scratch outputs go under `D:\CB-tmp` (directive)

Any offline scratch output that must live on a **SHORT** path to dodge Windows MAX_PATH — hky unpacks,
diff trees, merge/canary outputs, temp conversion targets, analysis dumps — goes in a **new named
subfolder** under `D:\CB-tmp\` (e.g. `D:\CB-tmp\1hm-scan\`), never straight onto the `D:\` root. Keep
each run's output in its own subfolder so it's self-identifying and easy to sweep. Do NOT scatter
`D:\rc`, `D:\co`, `D:\u`-style roots at the drive top level. (Session-private junk with no MAX_PATH need
still belongs in the session scratchpad dir.)

## Correctness gates

- **Schema vs typed** — `havok-core-cli` byte-diffs the schema-driven emit against the typed oracle.
- **Master fidelity** — `generate-base-master` validates every templated graph's graphdata byte-vs-vanilla
  (17/17 on the base chain).
- Both halves are proven: runtime (ragdolls + behaviors in-game) and offline (converter fidelity).

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
