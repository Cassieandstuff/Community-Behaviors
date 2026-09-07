# Reflection-Enrichment Plan — schema projection → full reflection

Status: roadmap (not started). Owner-approved direction. See also `memory/havok-signature-re.md`
(the `getSignature` reconstruction) and `memory/enums-in-schema.md`.

## Why

`Havok/core/Schema` is a **serialization projection**: it captures the byte layout exactly (so object
`size` derives and bytes round-trip perfectly), but it discards the *reflection* of `SERIALIZE_IGNORED`
members — collapsing them to `skip` / `empty` / `pad`, or dropping them entirely. `hkClass::getSignature`
computes its CRC over the **full reflection** (every real member, ignored ones included), so the
projection cannot reproduce most signatures.

Measured (this repo, offline):
- **`size` is fully derivable** — `havok-core-cli schema-parity` reports **0 size mismatches**:
  `SchemaRegistry::ComputeSize` (from the field list) equals every authored `size:`.
- **`signature` is NOT derivable from the projection** — running the validated `hksig` algorithm from
  the yaml reproduces only ~7 of 59 skip/empty-free classes. Diagnostic: `hkbCondition` and
  `hkbEventPayload` both miss by the *same constant* → they are each missing the *same* dropped member.
  Even "clean-looking" classes silently drop ignored members the CRC needs.

Enriching to full reflection unlocks, at once:
1. **Retire the last magic number** — derive-and-assert `signature:` (as `size:` already can be).
2. **Mint signatures for new custom classes** — the north star; you cannot stamp a class you cannot reflect.
3. **Validate the schema against the engine's own truth**, not just against byte round-trips.

## The concrete gap (what to recover, per class)

Each hidden member needs its reflection record: `name`, `type` ordinal, `subtype`, `cArraySize`,
`flags` (incl. `SERIALIZE_IGNORED`), plus the referenced class (struct / ptr / array-of-struct) or enum.

| Projection today            | Full reflection it hides                                            |
| --------------------------- | ------------------------------------------------------------------ |
| `skip: N`                   | one-or-more ignored members totalling N bytes (name/type/flags)    |
| `empty: ptr`                | an ignored `POINTER` member                                        |
| `empty: array`              | an ignored `ARRAY` member (with its subtype)                       |
| `pad: N`                    | **nothing** — true alignment padding is *not* an `hkClassMember`; it only bumps objectSize. Load-bearing distinction. |
| `{ name, ignored: true }`   | already named+typed; needs the `SERIALIZE_IGNORED` flag surfaced to the sig pass |
| *dropped entirely*          | zero-serialization-footprint members still present in reflection (rare) |

## Source of truth

Serialization cannot see ignored members, so the source must be **reflection**:

1. **Engine reflection dump — definitive, recommended.** The game's per-class `hkClass` member tables
   are exactly what `getSignature` reads. `writeSignature` and the member-array layout are already
   located live in x64dbg (`havok-signature-re.md`). A one-time script walks the class registry and
   dumps every class's members (name/type/subtype/flags/offset) → a reflection manifest. Highest
   fidelity; reuses the rig that already validated `hksig`.
2. **hkxcmd `Report` / community class DBs (hkxpack, HKLib) — fast, verify only.** LE-era (2010.1) and
   often serialization-derived (so may lack ignored members / drift on layout). Cross-check a sample.
3. **2013 Havok SDK reflection — partial.** Has base/common classes but *not* the Skyrim `hkb*` classes.

→ Dump from the game (1), sanity-check a sample against (2).

## Schema representation

Keep serialization byte-exact; add reflection to the *same* list:
- Replace `skip` / `empty` with the real members carrying full reflection + `ignored: true`. The
  serializer treats an `ignored` member as its byte-equivalent (skip/empty → no serde change); the
  signature pass reads the reflection. One authoritative member list, `ignored` separating
  serde-visible from reflection-only.
- This is exactly the shape a custom-class author writes — so the enrichment format *is* the
  custom-class authoring format.

## Validation gate (offline; all must hold or build fails)

1. `ComputeSignature(enriched) == authored signature` — new CRC gate (target 176/176).
2. `ComputeSize(enriched) == authored size` — enrichment must not drift the byte layout.
3. Serializer output **byte-identical** to today — ignored members serialize as their skip/empty
   equivalent, so schema-vs-typed and `generate-base-master` (17/17) stay green.

Wire as `havok-core-cli schema-sign-check <Havok-dir>`, mirroring `schema-parity`.

## Phasing

1. **Dumper** — reuse the x64dbg `writeSignature` rig; dump all ~176 classes' member tables to a manifest.
2. **Port `hksig` to C++** — `SchemaRegistry::ComputeSignature`, beside `ComputeSize`; and **pin the
   `TYPE_FLAGS` branch** live (the one `NotImplemented` case in `hksig`, needed for `flags:` members).
3. **Enrich class-by-class** from the manifest (auto-generate the enriched yaml, human-review), gating
   each with `schema-sign-check` until 176/176.
4. **Retire** authored `signature:` / `size:` → derive-and-assert.
5. **Custom-class minting falls out for free** — author full reflection, `ComputeSignature` stamps it.

## Effort / risk

- Dumper + C++ `ComputeSignature` + `TYPE_FLAGS` pin: moderate, one-time — the algorithm is already
  reconstructed and validated bit-exact.
- Enrichment: mechanical per-class (~176), automatable from the manifest.
- Risk to serialization: **low** — the byte gates catch any drift. This is data recovery + one
  algorithm port, not a serializer change.
