# CB-1: converter can't read Havok tagfile-format .hkx

**Status:** INVESTIGATING (deferred — known limitation, low priority)
**First seen:** 2026-09-06   **Area:** converter / havok-framing

## Symptom

`generate-base-master` over the full vanilla corpus reports 6 decompile failures (of 434), e.g.:

```
skeleton read/parse FAILED: creationclub/bgssse001/effects/fishing/fishingbehavior/animations/cast.hkx
  — BinaryReaderEx: read UInt32: 0xcab00d1e | expected one of: 0x57e0e057 | ending position: 0x4
```

The affected files (all CreationClub `bgssse001`):
- `.../effects/fishing/fishingbehavior/animations/cast.hkx`
- `.../effects/fishing/fishingbehavior/animations/ready.hkx`
- `.../effects/fishing/fishingbehavior/characterassets/skeleton.hkx`
- `.../giantcrab/giantcrabbehavior/characterassets/skeleton.hkx`

(The `horsebehavior.xml` obj-count *note* in the same run is informational, not a failure.)

## Root cause

These are Havok **tagfiles**, not **packfiles**. Their first 8 bytes are `1e0d b0ca cefa 11d0` =
`0xCAB00D1E 0xD011FACE` (the tagfile magic); the reader asserts the packfile magic `0x57E0E057` at
`src/havok-framing/include/havok/core/PackFileTypes.h:69` and correctly rejects them. The payload is
still ordinary Havok (`hkRootLevelContainer` / `namedVariants` follow the header) — same object model,
different binary container. CB's `PackFileDeserializer` only implements the packfile container.

## Ruled out

- **Not corruption / not a regression:** the bytes are a valid, well-known Havok container; the same
  converter code runs in the monorepo, so it skipped the identical 6 files.
- **Not a fidelity issue:** these are outside the 17 templated graphs; the master's graphdata gate is
  17/17 vs vanilla with them skipped.

## Disposition

**Deferred.** A tagfile reader is a separate parser (distinct section/fixup layout from the packfile
path), and the only affected content is niche CC fishing + giantcrab assets that no current load order
compiles. Revisit only if those CC creatures need to enter a compiled load order — at which point the
fix is a tagfile deserializer feeding the same object model the packfile path builds.
