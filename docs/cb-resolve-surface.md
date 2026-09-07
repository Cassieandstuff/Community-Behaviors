# The `cb-resolve` surface — what an external tool can hoist from CB

`cb-resolve` exists so any tool (behavior editor, converter, batch pipeline) does the CB compiler's
work **identically to the compiler** — one shared library, many consumers, no drift. This doc is the
reference for *what* is exposable and *what it costs*, so we grow the public contract deliberately
instead of one symbol at a time.

The organizing idea: **"resolve a load order" is one verb; a tool needs the whole noun surface** —
import/merge, the typed model, project/character/animation data, provenance, the schema (code **and**
data), and — separately — skeleton/HKX. Those don't all sit at the same cost, so they're tiered.

The single public header is `<cb-resolve/CbResolve.h>`; consumers link the `cb-resolve` target and
include only that header. Everything below is reached (or will be) through the `cb::resolve::` names.

---

## Tier 0 — the resolve core (shipped + curated)

Stable today, blessed in `CbResolve.h`.

| Capability | `cb::resolve::` name | Backed by |
| --- | --- | --- |
| Read a packed `.hky` | `Archive::LoadFromFile` | `havok::model::HkyArchive` |
| **Write / pack a `.hky`** | `Archive::PackDirectory` | same class (round-trips with load) |
| A unit's file view | `UnitSource` | `havok::model::IUnitSource` |
| Import + merge a load order | `LoadOrder::LoadMerged` | `havok::model::YamlBehaviorLoader` |
| The resolved graph model | `ResolvedGraph` | `havok::model::BehaviorData` |
| The merge classifier (schema *class*) | `SchemaRegistry` | `havok::schema::SchemaRegistry` |

`LoadMerged` takes either directory layers or `IUnitSource` layers in ascending load-order priority;
point it at a `SchemaRegistry` (`SetSchemaRegistry`) so per-field `merge:` policy drives the compose —
the same classifier the compiler uses.

## Tier 0, the data half — the Havok/ schema tree (now shipped)

`SchemaRegistry` is only half the schema. `SchemaRegistry::LoadDir(root)` loads the **`Havok/` YAML
tree** as *runtime data* — the ground truth for class layout, per-field `merge:` policy, and the
`schema_version:` stamp that gates editor↔compiler compatibility. **The code without the tree yields an
empty classifier — a merge that is silently NOT identical to the compiler.**

The package now installs the tree beside its config and exposes its location. In consumer CMake:

```cmake
find_package(cb-resolve CONFIG REQUIRED)
# CB_RESOLVE_SCHEMA_DIR      -> the shipped Havok/ tree (relocatable; don't hardcode)
# CB_RESOLVE_SCHEMA_VERSION  -> e.g. "1.0.0-rc.1", read from the tree's SCHEMA.yaml
target_compile_definitions(your-tool PRIVATE CB_SCHEMA_DIR="${CB_RESOLVE_SCHEMA_DIR}")
```
```cpp
havok::schema::SchemaRegistry reg;
std::string err;
reg.LoadDir(CB_SCHEMA_DIR, err);          // now lockstep with the compiler that shipped this package
cb::resolve::LoadOrder::SetSchemaRegistry(&reg);
```

The version stamp is the editor↔compiler contract (`SchemaVersion` / `CheckSchemaCompat` in the schema
header): a produced `.hky` echoes it, the compiler gates ingest on it.

---

## Tier 1 — havok-core-free (now curated)

All of this is compiled into the `havok-model` / `havok-schema` libs the package ships, its headers are
installed, and it is now **blessed** in `CbResolve.h`. The top-level `cb::resolve::` names are the
recommended, stable contract; the broader engine namespaces (`cb::resolve::model` / `animdata` / `merge`
/ `schema`) hoist the fuller model wholesale (e.g. the ~30 typed `*Def` node types) and track the engine.

| Capability | Header / type | Why a tool wants it |
| --- | --- | --- |
| Typed node model | `BehaviorData` holds ~30 typed `*Def` maps (clips, blenders, state machines, modifiers, …) | The real "represent/edit a project" surface — read and edit nodes by type, not raw YAML |
| Project unit | `havok/model/ProjectData.h` (`ProjectSpec`) | The top-level project a tool imports/exports |
| Character unit | `havok/model/yaml/CharacterYamlLoader.h` | Character-level import |
| Animation data | `havok/anim/AnimationData.h`, `AnimDataDeriver.h`, `havok/sct/AnimDataFromBehavior.h` | Derive animationdata **from** behavior — havok-core-free (distinct from the retired `havok::anim::AnimationDef`) |
| Provenance | `LoadMerged`'s `NodeContribution` scan | "Which load-order layer set this node/value" — gold for an editor's diff/inspector |
| Version gating | `SchemaVersion`, `CheckSchemaCompat` (schema header) | Refuse/accept an authored `.hky` against the current tree, exactly as the compiler does |
| Merge primitive | `cb::resolve::merge` (`BashMerge.h`) | The lower-level compose used by the merge |

**Curation policy:** the top-level `cb::resolve::` names are frozen contracts — add one when a consumer
needs it and keep it stable. The namespace aliases are the escape hatch for everything else: reachable,
but "may move," so prefer a curated name where one exists.

---

## Tier 2 — needs havok-core (opt-in feature, not default)

The "represent a decompiled skeleton correctly" ask, plus HKX decode/compile. These live in
`Retirement Home/havok-core` (`SkeletonImport.h`, `SkeletonCompiler.h`, `SkeletonYaml.h`,
`AnimationImport.h`) — the quarantined, proprietary, license-encumbered backbone. This is exactly where
the havok-core-free premise ends, so it must **not** poison the default surface.

Plan: a vcpkg feature — `cb-resolve[skeleton]` (and/or `[hkx]`) — that is opt-in and only resolvable by
consumers who already have havok-core. Default `cb-resolve` stays clean; the feature adds the havok-core
targets + a `cb::resolve::skeleton::` sub-surface. Design this only when a consumer actually needs it.

| Capability | Where | Tier-2 gate |
| --- | --- | --- |
| Skeleton import / resolve → `SkeletonData` | `havok/sct/SkeletonImport.h`, `SkeletonCompiler.h` | `[skeleton]` |
| Skeleton YAML round-trip | `havok/sct/SkeletonYaml.h` | `[skeleton]` |
| HKX asset decode | havok-core | `[hkx]` |

---

## Tier 3 — doesn't exist in CB yet

Upstream authoring first; no port/curation change resolves these.

- `EmitAnimationYaml` / `EmitBehaviorTree` — emit paths that are absent from CB entirely.
- Anything depending on the **retired** `havok::anim::AnimationDef` (removed havok-core).

---

## How to grow the surface (checklist)

1. Confirm the symbol's tier (is it havok-core-free? already installed?).
2. Tier 1: add the alias/include to `CbResolve.h` + a line in `docs/consuming-cb-resolve.md`. Done.
3. Tier 0 data (like the schema tree): add an `install(DIRECTORY …)` rule + expose its path in
   `cb-resolve-config.cmake.in` via `PATH_VARS` / `set_and_check`.
4. Tier 2: add a vcpkg feature; never link havok-core in the default build.
5. Cut a CB tag, then bump the `cb-resolve` port (`REF` + `SHA512` + `vcpkg.json` version) in CB-loader
   and re-run `vcpkg x-add-version`.

## License note

Shipping the `Havok/` tree redistributes CB *data*, under the same **All Rights Reserved** posture as
the rest — fine for the copyright holder's own / authorized use; not for third-party publication until
CB is released under an open license.
