# Consuming `cb-resolve` from another project

`cb-resolve` is Community Behaviors' load-order resolution + `.hky` document API, exported so any tool
(e.g. a behavior editor) resolves a load order **identically to the CB compiler** — no drift. Consuming
the repo as a subproject builds **only** that surface: it skips the SKSE plugin, the converter,
CommonLibSSE, and havok-core.

## FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(community_behaviors
    GIT_REPOSITORY https://github.com/Cassieandstuff/Community-Behaviors.git
    GIT_TAG        <pin-a-commit-or-tag>)   # pin it — don't float on main
FetchContent_MakeAvailable(community_behaviors)

target_link_libraries(your-tool PRIVATE cb-resolve)   # the ONLY target you link
```

That's it — `cb-resolve` pulls `havok-model` + `havok-schema` (→ `havok-io`, `havok-framing`)
transitively. When CB is consumed this way it prints:

```
Community Behaviors: consumed as a subproject — exposing cb-resolve + engine modules only ...
```

## Use it

```cpp
#include <cb-resolve/CbResolve.h>

std::string err;
auto arc = cb::resolve::Archive::LoadFromFile("SomeMod.hky", err);   // read a packed .hky
cb::resolve::ResolvedGraph g = cb::resolve::LoadOrder::LoadMerged({ /* dirs or unit sources, load order */ });
cb::resolve::SchemaRegistry reg;   // load the Havok/ class schema; feed to the merge for field `merge:` policy

// Write side: pack an authored YAML tree back into a single-file .hky (round-trips with LoadFromFile),
// so a tool exports a bundle identically to CB's build-time packer.
cb::resolve::Archive::PackDirectory("MyBundle.hky/", "MyBundle.hky", err);
```

The stable public contract (`cb::resolve::`): `Archive` (read `.hky` via `LoadFromFile`, write via
`PackDirectory`), `LoadOrder::LoadMerged → ResolvedGraph` (the load-order merge) + `NodeContributions`
(provenance), `SchemaRegistry` (the merge classifier), `ProjectSpec` / `CharacterData` +
`CharacterLoader` (the project/character units), `SchemaVersion` / `CheckSchemaCompat` (the editor↔
compiler stamp), `DeriveClipInputsFromBehavior` (animationdata from the graph), `UnitSource`. The
fuller model is reachable through the namespace aliases `cb::resolve::model` / `animdata` / `merge` /
`schema` (broader, less frozen). Link `cb-resolve` and include only this header. See
[cb-resolve-surface.md](cb-resolve-surface.md) for the full tiered map.

### The base master (vanilla `Skyrim.hky`) — a runtime input, not shipped

Resolution is **deltas over a base**: `LoadMerged` takes layers *base-first*, and layer 0 is the vanilla
master `Skyrim.hky`. That master is **not** in this package — it's a decompiled representation of the
user's own vanilla game data (produced only by the full CB build from `$SKYRIM_DATASOURCE`), so
redistributing it would ship Bethesda's assets. A consumer instead **opens it from the user's install at
runtime**, the same way the Scene Editor does: resolve the game `Data/` root (MO2's VFS / registry)
yourself, then load the CB-mod-relative path under it:

```cpp
// gameDataRoot = the Data/ dir you resolved from MO2 / the registry.
std::string base = gameDataRoot + "/" + cb::resolve::kBaseMasterDataPath;  // .../Skyrim.hky
auto g = cb::resolve::LoadOrder::LoadMerged({ base, modDeltaA, modDeltaB /* ascending priority */ });
```

Only the base layer must carry `behavior.yaml`; every later layer is a mod delta. For dev/testing you
supply your own authorized copy of the master out-of-band — never through this registry.

Feed the shipped schema tree to the merge classifier so it resolves identically to the compiler:
```cmake
find_package(cb-api CONFIG REQUIRED)
target_compile_definitions(your-tool PRIVATE CB_SCHEMA_DIR="${CB_API_SCHEMA_DIR}")
```
```cpp
cb::resolve::SchemaRegistry reg; std::string err;
reg.LoadDir(CB_SCHEMA_DIR, err);                 // the Havok/ tree shipped with this package version
cb::resolve::LoadOrder::SetSchemaRegistry(&reg); // now per-field merge: policy drives the compose
```

## Consumer requirements

- **MSVC, C++23** — the engine modules are C++23; build with a matching toolset (the modules also set
  their own `CXX_STANDARD 23`).
- **`ryml`** — the modules' YAML front-end. Provide it via your toolchain (vcpkg `ryml`,
  `find_package(ryml CONFIG)` resolvable). CB does not vendor it for consumers.
- **Matching triplet / runtime** — the modules build static (CB ships `x64-windows-static`,
  `/MT`). Match your triplet + `CMAKE_MSVC_RUNTIME_LIBRARY` or the static libs won't link.
- Windows short build path — the engine trees nest deep; a very long build directory can trip MAX_PATH
  during the compiler probe.

## Not exported

havok-core (the quarantined typed backbone: HKX decode, spline codec, decompilers), the SKSE plugin,
and the converter are top-level-build-only. Load-order resolution is deliberately havok-core-free.
