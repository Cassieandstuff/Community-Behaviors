# TODO: CB animation pipeline rework (root-only motion, compile everything)

Status 2026-10-28: designed partway, **nothing built**. Branch `claude/elegant-bouman-098ab7`. This file is a parking
note for unfinished work; delete it when the work lands (repo policy: no design docs survive their work).

## Settled model (user, 2026-10-28)

- **Compile everything.** Every animation unit in every bundle compiles in game into the behavior cache
  (`Data/community_behaviors_cache/...`), never into the real `meshes/` path. The per-bundle `compile_animations`
  opt-in goes. A full compile measured 15-20 s.
- **Serve with vanilla identity, swap the bytes** (user picked "option C" over rewriting the roster to cache paths).
  The engine keeps loading each animation under its vanilla path / CRC ID; CB makes that ID read CB's compiled bytes.
  The seam is still open, see below.
- **OAR is not supported.** CB fails at startup when OAR is detected, with a message that OAR mods must be converted.
  `AnimationDataServer`'s DAR/OAR override-copy motion path goes.
- **Root bone only.** `NPC Root [Root]` (track 0) is the only carrier of actor motion in an animation unit. Regen
  composes each clip's vanilla adsf motion onto the root: root(t) <- M(t) * root(t), no gate, identity when the clip has
  no adsf record. At compile, `ResolveRootMotion` becomes a plain root extraction (strip the root, emit the adsf
  record). No cascade into child bones (rejected).
- **The `motion:` field goes**, both blocks and `motion: none`. Clips that were visual-only gaining actor motion is a
  FIX (Bethesda lacked the convention early on).
- **AMR is stripped.** Decompile folds `animmotion` / `animrotation` annotations onto the root and removes them (today
  it keeps them verbatim and also folds them, so converted clips carry their motion twice).
- **Unit override is wholesale**: an animation unit replaces whole, config included.

## Engine findings (Ghidra, AE 1.6.1170, read-only)

- LoadClips (`0x140bc9310`) only binds clips to roster slots, by FILE NAME (the folder is ignored). Right after it
  returns, `FUN_140bc26d0` joins the project dir with each `animationNames[i]` and builds the slot's ID
  (ProjectDBData `+0x40`, stride `0x20`, count `+0x50`). Every clip load goes through that table.
- An animation's bytes come from a **global ID -> stream registry** (`DAT_143271d00`), filled ONCE at startup
  (`FUN_14064b450`: every BSA file, then every loose file under the resource prefixes when `bInvalidateOlderFiles=1`).
  No path is resolved at load. The so-called EntryDB request `0x140d08da0` only asks "is this ID registered";
  unregistered = pending forever. **A file written after launch is never found unless CB registers it.**
- Anim DB `DAT_14315c950` = `EntryDB<BShkbHkxDB::DBTraits>`. Find-or-create `FUN_140bab670` copies the registered
  stream into `entry+0x20`; IO thread `FUN_140bab7f0` -> `BShkbHkxDB_CreateEntry_LoadProjectData` ->
  `DBData::Func13` `0x140ba7b60` reads it. Roster clips and the preload lists all go through this DB, so one seam
  covers them.
- In-memory ID layout `{file crc, ext, dir crc}` (`BSResource::ID::GenerateFromPath` `0x140d0f780`).
- Entry state word `entry+0xc`: low 27 bits refcount; `1` new; `0x10000001` / `0x20000001` queued; `|0x80000000`
  loading; `0x3xxxxxxx` loaded; `0x40000000` unreferenced (free list); `0x5xxxxxxx` failed/stale; `0x7xxxxxxx` busy.
  Release: `0x30000001` -> CAS `0x40000000` + free list, else CAS decrement. Stream refcount `stream+0x10`
  (+0x1000 per ref).
- Side finding: `FUN_140bc1d70` loads the project `.hkt`/`.hkx` by ID through this same DB, not through
  `BSResourceAssetLoader_ResolveTypedHkx` (ByteServe's seam). Check whether that bypasses ByteServe.

## Open decision: the byte-swap seam

1. **Registry re-point (recommended).** After compiling a clip, register its VANILLA ID (from the engine's own
   `0x140d0f780`, so the path fold stays inside the engine) to a stream over its cache file, through the engine's
   register call `0x140d08d10(_, ID*, BSTSmartPointer<Stream>*)`; the stream from the root `LocationTree`
   (`DAT_143271d90`) `DoCreateStream(<cache path>)`. An existing unreferenced entry keeps its old stream -> evict via
   the DB's vtable slot 5 `0x140babc60(db, ID*, stream*)`; a referenced one keeps vanilla bytes until released (log
   it). Ordering already fits: `WriteNativeAnimations` runs before `RedirectReady` flips (`Resolver.cpp:1206` <
   `:1299`) and every CB graph waits on `RedirectReady`. Mod-only animations (no vanilla file) become loadable too. No
   per-load detour.
2. **IO-loader detour.** Detour `FUN_140bab7f0` and CAS the cache stream into `entry+0x20` before the read. Catches
   already-created entries, but runs on every animation load on the IO thread under the entry protocol.

Unverified, for the in-game test: the exact path form `LocationTree::DoCreateStream` wants for a file under
`Data/community_behaviors_cache/` (ByteServe's swap uses `meshes\..\community_behaviors_cache\<rest>`).

## Build map

- **Compile all** — `src/core/discover/BundleManifest.{h,cpp}` (`compileAnimations`), `src/core/resolve/Resolver.cpp`
  (`bundleCompileAnims` at ~266/289/369/412; `WriteNativeAnimations` ~902-1065 writes to `dataRoot / outKey`, i.e.
  the real meshes path, with a ledger + orphan removal), `features/Motion Matching.hky/manifest.json`, the converter's
  manifest writer (`Converter.cpp:895`). Write to `CacheDiskRel(outKey)` instead, keep the ledger (its orphan pass
  then cleans the old meshes-path outputs). **Trap:** the cache sweep in `MaterializeCacheToDisk` (`Resolver.cpp`
  ~1095) deletes every cache file not in `m_reuse` except four root files; compiled animations must join its keep set.
- **Seam** — new code beside ByteServe (`src/core/serve/`), engine functions declared in `src/platform/game/` with
  `engine.yaml` rows.
- **OAR** — detect at startup (open question: what counts — `OpenAnimationReplacer.dll` loaded, OAR/DAR folders in
  the load order, or both — and the fail form). Remove `OverrideCopy` / `OverrideBase` and the `overrides` map in
  `src/core/serve/AnimationDataServer.cpp` (~45-60, ~205-260, ~308).
- **Regen / decompile** — `src/engine/codec/motion/RootMotion.{h,cpp}`: `FinalizeUnitMotion` -> always fold (drop
  `CheckFoldRoundTrip` / `FoldRoundTrips` gate use, `IsStill` skip, explicit block, `motion: none`, `UnitMotionForm`);
  `ResolveRootMotion` -> `ExtractRootMotion` only. Converter `--regen-master` motion reunion `Converter.cpp` ~2405-2560
  (calls `FinalizeUnitMotion` per job). `src/engine/decompile/AnimationDecompiler.cpp` ~236-265: fold AMR and remove
  the `animmotion` / `animrotation` annotations from the emitted tracks. `AnimationYamlLoader.cpp` ~178-201 and
  `AnimationDef.h` (`motion`, `rootMotionVisual`): stop reading `motion:` (decide: reject with "reconvert" or ignore).
  Retire the converter verbs built on the field: `MotionFoldGate`, `MotionStorageCheck` (`Converter.h` ~146, ~163;
  `Converter.cpp` ~3015, ~3121, ~3272). `AnimationDataServer.cpp:199` keeps extracting from the root.
- **Coordination:** branch `claude/regen-rules-870c8a` (session cb-regen-rules) also edits `Converter.cpp`; read it
  before touching the regen, don't edit its worktree.

## Gates (deploy off: `-DBUILDER_DEPLOY=OFF`)

Served-bytes regen gate stays green; `--anim-float-offsets` stays green; a scratch regen (under `D:\CB-tmp\<name>\`)
shows zero `motion:` keys. In-game testing is the user's. This worktree's `lib/commonlibsse-ng` submodule is not
initialised yet (`git submodule update --init --recursive` before building).

## Later steps (out of scope here; don't block them)

Animations as reflective units under the whole-unit override rule (config included); the roster built from the
animations folder itself; per-animation `variant` config (male/female, later first person), reflected by BK's clip
generators; moving the regen out of the converter (which retires in favour of Behavior Kit).

## Resume

1. Get the user's pick on the seam (above), then the OAR detection question.
2. Build compile-all into the cache + the seam; then OAR fail + override-copy removal; then the regen fold/strip and
   the `motion:` removal.
3. Run the gates with deploy off; report. Memory: `motion-reference-frame`, `runtime-serve-and-setdata-re` (has the
   registry findings).
