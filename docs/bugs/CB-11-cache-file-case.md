# CB-11: compiled cache files are written lowercase but opened with original case

**Status:** INVESTIGATING
**First seen:** 2026-10-11   **Area:** runtime (byte serve)

## Symptom
None observed — behaviors/characters/skeletons serve fine in testing, which suggests the VFS lookup is
case-insensitive here. Found by code audit because the code's own comments contradict each other.

## Root cause
- Compiled behavior/character/skeleton bytes are written at `CacheDiskRel(<lowercased serve key>)`
  (`src/core/resolve/Resolver.cpp:837`, `:1186`) and `HasCacheFile` checks that lowercased path (`:1613`).
- ByteServe swaps the engine's open to `CacheSwapPath(raw)`, which keeps the ORIGINAL case
  (`src/core/serve/ByteServe.cpp:95`).
- ServeKey.h's comments (and the project path at `Resolver.cpp:1572`) say MO2's USVFS matches
  case-sensitively — which is why the synthesized PROJECT is written at its original-case path. If that is
  true, every behavior/character/skeleton swap is latently broken; if it is false, the project special case
  (`CacheDiskRelRaw`) is unnecessary. `HasCacheFile`'s comment assumes a case-insensitive filesystem.

## Ruled out
—

## Disposition
One in-game check under MO2: open a cached behavior whose vanilla path is mixed-case and confirm the swap
reads CB's bytes (log the resolver's result). Then make the write and open agree (preferably both original
case, per the CLAUDE.md case directive) and delete whichever comment is wrong. Same class: the OAR
`config.json` open through a lowercased key (`src/core/serve/AnimationDataServer.cpp` ~114-118).
