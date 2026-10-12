# CB-11: compiled cache files are written lowercase but opened with original case

**Status:** INVESTIGATING (fix landed, awaiting in-game confirmation)
**First seen:** 2026-10-11   **Area:** runtime (byte serve)

## Symptom
None observed — behaviors/characters/skeletons serve fine in testing, which suggests the VFS lookup is
case-insensitive here. Found by code audit because the code's own comments contradict each other.

## Root cause
- Compiled behavior/character/skeleton bytes were written at `CacheDiskRel(<lowercased serve key>)`, but
  ByteServe swapped the engine's open to the request's RAW spelling (original case). Only a case-folding
  lookup made the two meet.
- The synthesized PROJECT was the special case: written at its raw spelling (`CacheDiskRelRaw`) to match its
  raw swap.

## Ruled out
—

## Disposition
Fixed by construction (CLAUDE.md case directive: a file CB writes is opened by the exact string CB wrote it
under): every swap — behavior, character, skeleton and project — is now `CacheSwapPath(key)`, naming exactly
the `CacheDiskRel(key)` file the cache pass wrote; `CacheDiskRelRaw` and the raw-spelling swap are gone
(`src/core/discover/ServeKey.h`). Remove this bug once an in-game run under MO2 confirms owned projects,
characters, behaviors and skeletons still serve (log lines `[byteserve] serving …` / `byte-served project …`
with a swap path under `community_behaviors_cache\`). Same class, not yet changed: the OAR `config.json` open
through a lowercased key (`src/core/serve/AnimationDataServer.cpp` ~114-118).
