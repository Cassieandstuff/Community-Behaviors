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
src/                         CB's source. src/ is the include root; the runtime plugin is organized by
                             PIPELINE STAGE under core/, the engine libraries by CONCERN (each a module).
  Plugin.cpp                 the SKSE entry point (defines SKSEPluginLoad) — stays at src/ root
  include/                   force-include-tier shared headers: PCH.h (force-included) + PluginLogger.h
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
  havok-framing|schema|io|model|anim|pipeline/   the data-driven Havok stack (each an add_library module,
                             include/<lib>/ for its public header, no include/internal wrapper)
  sct-config/                config scanner (JSON/YAML/INI); sct-utilities/ folder-picker+zip (converter)
Havok/core/Schema/           the vanilla class-schema tree (schema-as-core; room for Havok/features/ later)
Retirement Home/havok-core/  QUARANTINED legacy typed backbone — see its README; do NOT build new things
                             here, put new components in src/. It's the offline oracle + converter core.
APP/Behavior Converter/      the GLFW+ImGui converter (BehaviorConverter.exe); src/ flat, ships templates/+Havok/
```

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
- `COMMONLIB_SSE_FOLDER` — a local CommonLibSSE-NG clone; if unset/not a dir, the pinned powerof3 fork is FetchContent'd.
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

## Correctness gates

- **Schema vs typed** — `havok-core-cli` byte-diffs the schema-driven emit against the typed oracle.
- **Master fidelity** — `generate-base-master` validates every templated graph's graphdata byte-vs-vanilla
  (17/17 on the base chain).
- Both halves are proven: runtime (ragdolls + behaviors in-game) and offline (converter fidelity).

---

## Knowledge tiers

- **CLAUDE.md** — always-on architectural SOP (this file). Keep it LEAN; it's injected every turn.
- **Auto-memory** — preferences, decisions + context, project state (soft, per-user). NOT bug status.
- **`docs/bugs/`** — the checked-in bug log: one file per OPEN bug + a thin index, and a fixed bug
  **leaves** (deleted with its index line in the same commit as the fix; git history is the archive —
  nothing accumulates). Bugs are shared/authoritative, so they live here, not in memory. Find a bug →
  add its file; fix it → remove it; sync in the same change as the code. See `docs/bugs/README.md`.
- **Code is the source of truth**, above docs and comments. Verify code-shaped claims against the code.

When about to record something SOP-shaped into a checked-in file, flag it for the user's approval —
don't add it silently.
