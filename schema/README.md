# `schema/` — the class schema (the source of truth for what a Havok class IS)

**The descriptors are DATA.** One descriptor file per Havok class, describing its binary layout in
the SSE/AE packfile. (`Havok/` is the exception: the reimplemented runtime's C++ — see Layout. It never
ships; staging copies data only.) It is the single source of truth every consumer reads to know a class — the
`havok-io` serde engine (read/write), the merge engine, the compiler, a future CommonLib-for-Havok
codegen, and modules. The core knows *nothing* about read/write/transpile/convert/compile — those are
consumers of this schema. (See the rewrite plan; `havok-core` v2, Stage 1.)

Because the schema is decoupled from how it's consumed, both **build-time codegen** (the fast built-in
path) and **runtime loading** (module-added classes, no recompile) read the same file.

## Descriptor format

A class descriptor is YAML:

```yaml
name:      hkReferencedObject     # the class name (packfile class-name table + registry key)
parent:    hkBaseObject           # base class (its fields are laid down FIRST; null for a root)
signature: 0x3b1c1113             # the class CRC (HK_CLASS_ID sig)
size:      16                     # total struct size (bytes), 64-bit/SSE — a cross-check on the field walk
fields:    [ … ]                  # this class's OWN fields, in serialized order (parent's come first, from its descriptor)
```

`fields` is an ordered list; the serde engine lays down the parent's fields (recursively) then these.
Each entry is one of:

| Entry | Meaning | Serde op (old, being replaced) |
|---|---|---|
| `{ vtable: true }` | the runtime vtable slot — a pointer-sized value written as 0, fixed up on load | `ReadUSize`/`WriteUSize` |
| `{ name: X, type: <scalar> }` | a scalar field | `ReadUInt16`/`ReadInt32`/… |
| `{ name: X, type: string }` | an `hkStringPtr` → std::string | `ReadStringPointer` |
| `{ name: X, type: cstring }` | a `char*` CString (8 B slot) | `ReadCString` |
| `{ name: X, type: quaternion }` | inline `hkQuaternion` (16 B) | `ReadQuaternion` |
| `{ name: X, type: ptr, ref: <Class> }` | a class pointer → `shared_ptr<Class>` | `ReadClassPointer<Class>` |
| `{ name: X, type: structarray, ref: <Class> }` | `hkArray<Class>` (by value) | `ReadClassArray<Class>` |
| `{ name: X, type: ptrarray, ref: <Class> }` | `hkArray<Class*>` | `ReadClassPointerArray<Class>` |
| `{ name: X, type: vec4array }` | `hkArray<Vector4>` | `ReadVector4Array` |
| `{ name: X, type: qstransformarray }` | `hkArray<hkQsTransform>` | `ReadQSTransformArray` |
| `{ name: X, type: stringarray }` | `hkArray<hkStringPtr>` | `ReadStringPointerArray` |
| `{ name: X, type: scalararray, scalar: <scalar> }` | `hkArray<scalar>` (float/int16/byte/…) | `ReadSingleArray`/`ReadInt16Array`/… |
| `{ name: X, type: struct, ref: <Class> }` | an inline value member (its fields inlined) | `X.Read(…)` |
| `{ name: X, type: vector4 }` | inline `hkVector4` (16 B) | `ReadVector4` |
| `{ name: X, type: qstransform }` | inline `hkQsTransform` (48 B) | `ReadQSTransform` |
| `{ name: X, type: boolarray, count: N }` | a C-style inline `bool[N]` | `ReadBooleanCStyleArray<N>` |
| `{ empty: array }` | a void-typed SERIALIZE_IGNORED `hkArray` slot (16 B, no value) | `ReadEmptyArray` |
| `{ empty: ptr }` | a void-typed SERIALIZE_IGNORED pointer slot (no value) | `ReadEmptyPointer` |
| `{ pad: N }` | align the cursor to an N-byte boundary | `Pad(N)` |
| `{ skip: N }` | advance the cursor N bytes (explicit trailing pad) | `Skip(N)` |

**Scalar types:** `int8 byte uint16 int16 uint32 int32 uint64 int64 float half bool` (`half` = hkHalf, 2 B).

**`count: N`** on a `scalar`/`ptr`/`vector4`/`qstransform` field = a fixed inline `std::array<T,N>` (N elements, no `hkArray` header) — e.g. `{ name: sweptTransform, type: vector4, count: 5 }`. Distinct from `boolarray`'s `count` (element count of a C-style `bool[N]`).

**`enum: <Type>`** on a scalar field names its enum type (e.g. `{ name: mode, type: int8, enum: PlaybackMode }`) so the `.hky` YAML renders the value by name (`MODE_SINGLE_PLAY`) rather than as an integer. No serde/size effect — a rendering hint the model layer consumes (tables in `HavokEnums.h`).

**Signature reflection.** A class's `signature:` is the CRC the engine derives from its reflection (member names,
types and flags, then its parent chain). Where the serialized layout says less than the engine's reflection, these
keys say the rest (no serde effect):
- class-level `interfaces: N` (hkBaseObject's 1) and `enums: [A, B]` (the class's declared enums, in order, each a
  `kind: enum` descriptor);
- `hktype: ulong | enum | flags` on a field whose reflected type differs from its `type:` (`enum`/`flags` here
  carries no enum reference; `enum: X` on a scalar means an ENUM member over that scalar, referencing X);
- `name:` on an `{ empty: … }` slot, the reflected member's name; `hktype: stringptr` on an `{ empty: ptr }` slot
  when that member is an hkStringPtr rather than a pointer (hkbProjectStringData's `rootPath`).

**Class-level `factory: <name>`** names the engine factory name a class stands for, byte-exact (a typed movement
agent: `factory: "Player Controls"`), so a node's own name field can be checked against its class. At most one class
may claim a name; a second fails the load. No serde, size or signature effect.

A `CB*` class stores no `signature:`: the schema loader derives it, and fails the load unless every class it hashes
through reproduces its own stored signature. The behavior loader recomputes the signature and refuses a packfile
whose stored one differs.

**Optional per-field keys:**
- `ignored: true` — marks a *typed* SERIALIZE_IGNORED field (round-trips as a normal value; the tag is
  documentation only — it does NOT change serde). Void-typed ignored uses `empty:` instead.
- `merge: <strategy>` — the merge engine's per-field policy (`keep | lastwriter | replacearray |
  unionarray | guarderror`); consumed by the merge consumer, not serde. (Added as the merge layer lands.)
- `role: { role: <Role item>, flags: [<RoleFlags item>, …] }` — what the member is for, mirroring Havok's
  `hkbRoleAttribute` (both keys optional), e.g. `role: { flags: [FLAG_OUTPUT] }` = the node writes this member, so
  another node's binding or an output binding may read it. Items are the `Role` / `RoleFlags` enum items, spelled
  exactly; an unknown one fails the load. A member attribute: no serde, size or signature effect.
- `unitref: <kind>` — on a `string`, `cstring` or `stringarray` field: the string (each element of an array) names
  another file of that kind — `behavior`, `character`, `skeleton` or `animation`; another kind fails the load. Havok
  stores the path relative to the PROJECT folder, whichever file holds it: vanilla `0_master.hkx`'s
  `behaviorName` is `Behaviors\1HM_Behavior.hkx` (= `character\behaviors\1hm_behavior.hkx`), the horse
  project's `characterFilenames` is `Characters\Horse.hkx`, its character's `rigName` is
  `Character Assets\Skeleton.hkx`. The string is kept byte-exact; matching it to a unit is path identity (case-
  folded, as the engine's lookup is). A member attribute: no serde, size or signature effect.
- `user: <value>` — on a single integer scalar: the game fills the member at runtime, and `<value>` is the binary
  sentinel a file stores in its place (`hkbClipGenerator.animationBindingIndex`, `user: -1`). The `.hky` writes
  `user` exactly when the stored value equals the sentinel and reads `user` back to it, so the bytes round-trip; any
  other value renders as itself. A "none" index (an event id, a sync variable index) is authored data, not `user`:
  it stays `-1`. No serde, size or signature effect.

## Runtime binding (reserved — the uniform game/self invariant)

Every class MAY carry a `runtime:` block; today it's absent/`game` for all of them:

```yaml
runtime:
  source: game        # game = the engine's vtable (via Address Library); self = a reimplementation we provide
```

`source: game` means the engine runs the class (its vtable); `self` means our code does (custom classes,
or an eventual in-editor behavior runtime). The two are the SAME shape — making a class self-run is a
binding value, not a restructure. Nothing is `self` yet; the slot is reserved so custom classes and the
editor runtime are additive, not a second refactor.

## Kinds and identity — folders are decoration

Every `*.yaml` under `schema/` is loaded recursively and dispatched on its top-level `kind:`, never on its folder:

| `kind:` | What it is | Read by |
|---|---|---|
| (absent) | a class descriptor (above) | `SchemaRegistry::LoadDir` |
| `metadata` | a Bethesda text-cache record (adsf / asdsf / SpeedData) | `LoadDir` → MetaText |
| `enum` | a reflected enum: `name:`, `flags:`, `items:` | `LoadDir`, and baked into the binary by `cmake/EmbedEnums.cmake` |
| `semantics` | merge policy: strategy → `<Class>` / `<Class>.<field>` lists | `LoadDir` (applied after the walk) |
| `probe` | a compile-trace probe (see `metadata/debug/README.md`) | `CB::engine::compile::trace` |
| `rule` | a reversible transform over a whole unit set (see Rules) | `LoadDir` → `CB::engine::rules` |

A descriptor's identity is its `name:` within its kind (classes and metadata records share one namespace). A name
defined twice, or an unknown kind, fails the load and names the files. Moving a file between folders changes nothing.

## Rules

A `kind: rule` descriptor (`metadata/rules/<rule>.yaml`) declares a transform the regen (and the converter) runs over
a whole unit set — graphs, characters and projects together — through the C++ operation its `op:` names:

```yaml
name:   myrule                      # the rule's identity
kind:   rule
op:     MyOp                        # a registered CB::engine::rules op (Apply + Inverse), exact name
runs:   [regen, converter]          # where it applies
order:  { after: [other], before: [] }   # rules it runs after / before (each must exist)
select: [boneweights, bonelist, boneref] # the field kinds it touches: the schema row keys, or `user`
params: { … }                       # the op's own parameters
```

Every op ships its inverse. The regen applies the rules after the emit and re-emits the units they change; its
served-bytes gate reverses them in memory, from what Apply recorded in the same run, and compares every unit to its
source. Nothing of a rule is persisted in the bundle. The converter applies them forward only.

## Layout

`<family>/<ClassName>.yaml` — the families are for people only: they mirror the old `classes/*.h` grouping (`base/`,
`events/`, `variables/`, `generators/`, `modifiers/`, `statemachine/`, `graph/`, `animation/`, `physics/`,
`project/`, `arrays/`, `effects/`, `resource/`, `scene/`), and `metadata/` gathers the non-class kinds. A class with no
parent that only one class owns sits in its owner's family.

The reimplemented Havok runtime is not part of schema/: it lives in the top-level `havok/` (`CB::havok::`), one `.h`/`.cpp` per class, laid out like Havok's
own source tree: product roots `Common/Base`, `Animation`, `Behavior`, and below them the paths the exe's
`__FILE__` strings give (`Generator\Clip\hkbClipGenerator.cpp`, `Mapper\hkaSkeletonMapper.cpp`, …). Bethesda's
behavior classes sit in the same tree, as in their build. A translation unit is part of what gets matched (what a
caller may assume about a callee it can see), so code is compiled apart only where the bytes demand it, in a file
named for that (`hkbClipGeneratorOpaque.cpp`), never one posing as a Havok file. Built only by `tools/cbmatch` (VS2015, against
`tools/cbmatch/manifests/schema.yaml`); the include root is `Havok/` (`#include <Behavior/...>`).
