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
```

The public contract (`cb::resolve::`): `Archive` (read `.hky`), `LoadOrder::LoadMerged → ResolvedGraph`
(the load-order merge), `SchemaRegistry` (the merge classifier), `UnitSource`. These are stable aliases
over internal types — link `cb-resolve` and include only this header.

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
