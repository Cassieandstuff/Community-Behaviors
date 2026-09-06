# `Havok/` — the class schema (the source of truth for what a Havok class IS)

**This tree is DATA, not code.** One descriptor file per Havok class, describing its binary layout in
the SSE/AE packfile. It is the single source of truth every consumer reads to know a class — the
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

**Optional per-field keys:**
- `ignored: true` — marks a *typed* SERIALIZE_IGNORED field (round-trips as a normal value; the tag is
  documentation only — it does NOT change serde). Void-typed ignored uses `empty:` instead.
- `merge: <strategy>` — the merge engine's per-field policy (`keep | lastwriter | replacearray |
  unionarray | guarderror`); consumed by the merge consumer, not serde. (Added as the merge layer lands.)

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

## Layout

`Havok/<family>/<ClassName>.yaml` — families mirror the old `classes/*.h` grouping (`base/`, `events/`,
`variables/`, `generators/`, `modifiers/`, `statemachine/`, `graph/`, `animation/`, `physics/`,
`project/`). `Havok/features/` is reserved for module-contributed classes.
