# Community Behaviors

A runtime **Havok behavior-graph compiler** for Skyrim SE/AE, delivered as a CommonLibSSE-NG
SKSE plugin. It compiles behavior graphs over a mod load order **at runtime** and serves them to
the live engine — replacing the offline Nemesis/Pandora patch step — plus an offline **Behavior
Converter** that ingests existing Nemesis/Pandora load orders into Community Behaviors `.hky` bundles.

## License

[GPL-3.0-or-later](LICENSE) WITH [Modding Exception AND GPL-3.0 Linking Exception (with Corresponding
Source)](EXCEPTIONS).

The exceptions are the ones the SKSE / CommonLib plugin ecosystem uses:

- **Modding Exception.** Community Behaviors links with the game it modifies without placing that
  game under the GPL. The *Modded Code* is Skyrim (Special Edition, Anniversary Edition and VR).
- **GPL-3.0 Linking Exception (with Corresponding Source).** Community Behaviors may be combined with
  *Modding Libraries* whose licenses aren't GPL-compatible, as long as the source of the parts used
  ships with the combination. The *Modding Libraries* are SKSE and CommonLibSSE-NG.

Third-party code keeps its own license: vendored sources carry theirs beside them (e.g.
`src/engine/common/miniz.LICENSE`), and dependencies pulled through vcpkg / FetchContent ship under
their upstream terms. The `schema/templates/` XML is derived from Skyrim's own behavior files, for use
with the game.

## Layout

```
src/core/          the runtime SKSE plugin — discover → resolve → serve, plus bootstrap, debug, hooks
src/engine/        the shared reflection engine (schema registry, codecs, compile/decompile, merge) —
                   CommonLib-free, linked by the plugin, the converter, and the tools
src/features/      self-registered compile-time graph features
src/converter/     the offline GLFW+ImGui Behavior Converter (BehaviorConverter.exe)
common/            what features build against: feature-sdk/ (IGraphFeature/FeatureRegistry), config/
schema/            the Havok class schema, described once as data (+ the converter's templates/)
tools/             dev-only tools (hky-tool, cbdiff, x64dbg auto-attach, Blender control rig)
cmake/             helpers (CommonLibSSE, Deploy, SctSources, codegen, glad)
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
    GIT_TAG        v0.5.1)
FetchContent_MakeAvailable(community_behaviors)
target_link_libraries(your-tool PRIVATE cb::CB-API)
```

## How it works (the short version)

A Havok class is described **once as data** (`schema/`), which the engine's schema registry loads and
its codecs use to read/write `.hkx` byte-exactly. The engine holds the name-keyed `.hky` document,
merges deltas across a load order, and emits compiled graphs. The runtime plugin serves those compiled
bytes to the engine; the converter runs the same core offline.

## Notes for contributors

- **Havok is case-sensitive end-to-end** — never lowercase a path/name/CRC the engine or vanilla data
  will match; a miscased value binds to nothing (A-pose, dead root motion) with no error.
- Deeper architectural SOP lives in [CLAUDE.md](CLAUDE.md).
