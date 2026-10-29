# CB-1: converter can't read Havok tagfile-format .hkx

**Status:** INVESTIGATING (fix landed; the master build needs a re-run to confirm the 6 failures are gone)
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
`src/engine/codec/serialization/packfile/PackFileTypes.h` and correctly rejects them. The payload is
still ordinary Havok (`hkRootLevelContainer` / `namedVariants` follow the header) — same object model,
different binary container. CB's `PackFileDeserializer` only implements the packfile container.

## Ruled out

- **Not corruption / not a regression:** the bytes are a valid, well-known Havok container; the same
  converter code runs in the monorepo, so it skipped the identical 6 files.
- **Not a fidelity issue:** these are outside the 17 templated graphs; the master's graphdata gate is
  17/17 vs vanilla with them skipped.

## Disposition

**Fix landed:** `codec/serialization/tagfile/BinaryTagfile` reads binary tagfiles (versions 0 and 3) into schema
objects, by member name onto the schema's fields, and `PackFileDeserializer::DeserializePartially` transcodes one to a
packfile before reading it, so every packfile reader (sections, fixups, read order, `AssignIdentity`) takes it unchanged.

Measured on the 18 tagfiles in `ccBGSSSE001-Fish.bsa` (the 6 `.hkx` above plus 12 `.hkt`), against hkxcmd's `-v:AMD64`
packfile of each: 12 byte-identical, 6 equal content. Those 6 (2 behaviors, 4 characters) are byte-identical to CB's own
round trip of hkxcmd's file, so the remaining difference is the writer's, not the reader's.

Not exercised: the master build itself (`--build-base` / `generate-base-master`), tuple members, vec4 arrays, and
tagfile versions other than 0 and 3 (refused with the version named).
