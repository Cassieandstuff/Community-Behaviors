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

Third-party code keeps its own license: vendored sources carry theirs beside them, and dependencies
pulled through vcpkg / FetchContent ship under their upstream terms. The `src/converter/templates/` XML is
derived from Skyrim's own behavior files, for use with the game.

### HKY-SDK

The engine (the codecs, the class schema, the `.hky` unit, compile, decompile and merge, and the locomotion
model) lives in HKY-SDK, a separate repository fetched at configure time. It is private for now, so building
CB needs access to it, as with the CB SDK below.

### The CB SDK

The plugin compiles against the CB SDK, a private repository at `lib/cb-sdk` holding CB's reconstruction
of the Havok and Bethesda code inside Skyrim. It is not covered by this repository's license and its
source is not published, so only people with access to it can build the plugin. Contributors work by
invitation for now.

As an additional permission under GPL-3.0 section 7, Community Behaviors may be combined with the CB SDK
as a *Modding Library* without the SDK's source shipping with the combination. Every other condition of
the license, including the source of Community Behaviors itself, still applies.

The plan is to release the SDK's headers publicly at a later time, with the compiled code as a prebuilt
library, so that anyone can build the plugin. That work is not finished and has no date.

## Layout

```
src/core/          the runtime SKSE plugin — discover → resolve → serve, plus bootstrap, debug, hooks
src/features/      self-registered compile-time graph features
src/converter/     the offline GLFW+ImGui Behavior Converter (BehaviorConverter.exe)
common/            what features build against: feature-sdk/ (IGraphFeature/FeatureRegistry), config/
                   (+ config/compat, the registry of mods CB does not coexist with)
tools/             dev-only tools (hky-tool, cbdiff, x64dbg auto-attach, Blender control rig)
cmake/             helpers (CommonLibSSE, SctSources, codegen, glad)
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

Deploy and packaging go through [BUILDER](https://github.com/Cassieandstuff/libs-BUILDER): a build stages each product
in `build/<preset>/stage/<config>/<Name>` and deploys it to `SKYRIM_MODS_FOLDER` (the plugin + its data) /
`SKYRIM_TOOLS_FOLDER` (the converter); `-DBUILDER_DEPLOY=OFF` stages only, `--target package` zips (BUILDER's README
has the env-var table). Other env vars:
`CB_REGEN_SOURCE` (per-bundle `<Name>.hky` source folders; arms the derived-bundle regen),
`SCT_BASE_MASTER_DIR` (short path for the cross-build master-diff tree).

## Using the API (`cb::CB-API`)

Any tool can merge a load order **identically to the CB compiler** by linking `cb::CB-API` and
including `<CB-API.h>` — the `.hky` reader, the FormID merge, patch derivation, and the animation /
skeleton codecs, with no SKSE/plugin dependency. The header documents the surface.

```cmake
FetchContent_Declare(community_behaviors
    GIT_REPOSITORY https://github.com/Cassieandstuff/Community-Behaviors.git
    GIT_TAG        v0.5.2)
FetchContent_MakeAvailable(community_behaviors)
target_link_libraries(your-tool PRIVATE cb::CB-API)
```

## How it works (the short version)

A Havok class is described **once as data** (HKY-SDK's `schema/`), compiled into the engine at build time; its
schema registry and codecs use it to read/write `.hkx` byte-exactly. The engine holds the name-keyed `.hky` document,
merges deltas across a load order, and emits compiled graphs. The runtime plugin serves those compiled
bytes to the engine; the converter runs the same core offline.

## Notes for contributors

- **Havok is case-sensitive end-to-end** — never lowercase a path/name/CRC the engine or vanilla data
  will match; a miscased value binds to nothing (A-pose, dead root motion) with no error.
- Deeper architectural SOP lives in [CLAUDE.md](CLAUDE.md).
