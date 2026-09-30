# Community Behaviors

A runtime **Havok behavior-graph compiler** for Skyrim SE/AE, delivered as a CommonLibSSE-NG
SKSE plugin. It compiles behavior graphs over a mod load order **at runtime** and serves them to
the live engine — replacing the offline Nemesis/Pandora patch step — plus an offline **Behavior
Converter** that ingests existing Nemesis/Pandora load orders into Community Behaviors `.hky` bundles.

> **License: All Rights Reserved (temporary).** This repository is source-visible but **not** open
> source — no usage, copy, modification, or redistribution rights are granted. See [LICENSE](LICENSE).
> It is intended to be released under GPL-3.0 (+ a linking exception) in the future; until then the
> LICENSE terms govern.

## Layout

```
src/                        CB source (flat: each Foo.cpp beside its Foo.h)
  *.cpp                      the runtime plugin (Plugin.cpp entry, Resolver, ByteServe, …)
  features/<Owner>/          self-registered compile-time graph features
  havok-framing/             packfile framing (byte primitives, (de)serializer)   ┐
  havok-schema/              the class-schema loader (Havok/ is the source of truth)│ the data-driven
  havok-io/                  generic schema-driven .hkx read/write (byte-exact)     │ Havok stack
  havok-model/               name-keyed .hky model + load-order merge + emit        │
  havok-pipeline/            the graph-feature SDK (IGraphFeature/FeatureRegistry)  ┘
  sct-config/ sct-utilities/ config scanner / desktop plumbing
Havok/core/Schema/          the vanilla Havok class schema, described once as data
Retirement Home/havok-core/ QUARANTINED legacy typed backbone (being dissolved; don't build on it)
APP/Behavior Converter/     the offline GLFW+ImGui converter (BehaviorConverter.exe)
cmake/                      helpers (CommonLibSSE, Deploy, SctSources, ports, glad)
```

## Build

Requires the **Visual Studio 18 2026** toolchain and env `VCPKG_ROOT`. CommonLibSSE-NG is vendored
as a git submodule ([alandtse fork](https://github.com/alandtse/CommonLibSSE-NG), `ng` branch, full
SE+AE+VR) at `lib/commonlibsse-ng` — clone with submodules, or initialise them after cloning:

```bash
git submodule update --init --recursive

cmake --preset release          # full build: plugin + converter + build-time gates
cmake --build build/release --config RelWithDebInfo --target CommunityBehaviors
```

Presets: `release` / `debug` (full), `api` (the `cb::CB-API` library surface only).
Options: `-DCB_BUILD_CONVERTER=OFF` (skip the GUI converter for a lean plugin build),
`-DCB_API_ONLY=ON` (build only `cb::CB-API`).

Relevant env vars: `SKYRIM_MODS_FOLDER` (MO2 deploy target for the plugin + its data),
`SKYRIM_TOOLS_FOLDER` (converter deploy), `CB_REGEN_SOURCE` (per-bundle `<Name>.hky` source folders; arms the derived-bundle regen),
`SCT_BASE_MASTER_DIR` (short path for the cross-build master-diff tree).

## Using the API (`cb::CB-API`)

Any tool can merge a load order **identically to the CB compiler** by linking `cb::CB-API` and
including `<CB-API.h>` — the `.hky` reader, the FormID merge, patch derivation, and the animation /
skeleton codecs, with no SKSE/plugin dependency. The header documents the surface.

```cmake
FetchContent_Declare(community_behaviors
    GIT_REPOSITORY https://github.com/Cassieandstuff/Community-Behaviors.git
    GIT_TAG        v0.3.2)
FetchContent_MakeAvailable(community_behaviors)
target_link_libraries(your-tool PRIVATE cb::CB-API)
```

## How it works (the short version)

A Havok class is described **once as data** (`Havok/core/Schema`), which `havok-schema` loads and
`havok-io` uses to read/write `.hkx` byte-exactly. `havok-model` holds the name-keyed `.hky` document,
merges deltas across a load order, and emits compiled graphs. The runtime plugin serves those compiled
bytes to the engine; the converter runs the same core offline. The legacy typed `hkb*`/`hka*` backbone
in `Retirement Home/havok-core` is being dissolved into this stack and should not be extended.

## Notes for contributors

- **Havok is case-sensitive end-to-end** — never lowercase a path/name/CRC the engine or vanilla data
  will match; a miscased value binds to nothing (A-pose, dead root motion) with no error.
- Deeper architectural SOP lives in [CLAUDE.md](CLAUDE.md).
