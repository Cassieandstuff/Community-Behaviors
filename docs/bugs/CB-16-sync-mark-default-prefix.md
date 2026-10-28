# CB-16: Animation64's sync-mark reader reads an unset role prefix as "" (paired idles never start)

**Status:** INVESTIGATING (fix landed, needs an in-game check)
**First seen:** 2026-10-25   **Area:** runtime (Animation 64 readers, `[Animation64] bReaders`)

## Symptom
Mounting a horse: the rider jitters in place and never mounts. Expected for every paired idle whose
unprefixed role has a null `SyncAnimPrefix` (the intro's bonds cut, `pa_OffsetBoundStandingCut`, has one).

## Root cause
The game starts a paired idle in 0x140547cf0: it collects each actor's `BSSynchronizedClipGenerator` by class
and name, then validates the scene (0x1405463f0), which looks up every participant's start mark through
0x140b948e0 (`CB::havok::BSSynchronizedClipGenerator::sampleSyncMark`). A failed lookup aborts the scene
(onClipDeactivated clears the participants and sends `PairEnd` / `pairedStop`), and the package retries every
frame: the jitter.

The lookup finds the annotation track named exactly the role prefix. A null prefix stands for the exe's
default, the global at 0x142022418, which points to `"NPC"`. `Paired_Mount.hkx` names the rider's track `NPC`
(track 55) and has none named `""`. With the takeover on, 0x140b948e0 jumps into CB, and Animation64's
replacement (`OnSyncMark`, `src/features/Animation 64/Readers.cpp`) used a literal `""` for a null prefix.
So the rider's mark was never found and the mount was rejected every time. The havok/ copy reads the exe
global and was right; only its name (`g_bssyncEmptyString`) and comments said `""`.

Same failure mode as [CB-15](CB-15-sync-prefix-null-marker.md) (a prefix of `&#9216;` matches no track either).
CB-15 explains the jitter before Animation64 existed, this one explains why it stayed after CB-15 was fixed.

## Ruled out
Verified live in x64dbg on 2026-10-26, paused mid-jitter:
- Scene 0x15D783B56C0 (`BGSSynchronizedAnimationInstance`) held the three right nodes: the player's
  3rd-person and 1st-person `HorseEnter`, the horse's `pa_HorseEnter`. Its participant count was 0, and no
  node had a scene or had been activated, so validation had failed.
- `Paired_Mount` was resident in the animation EntryDB (loaded, 3 references). The CRC table entries at the
  clips' indices (player 1294, horse 29) equal vanilla's set-data triple. Rosters, `bIsSynced` bindings and the
  adsf match vanilla (see CB-15).
- Pointing both player `HorseEnter` clones' `SyncAnimPrefix` at the exe's `"NPC"` and resuming: the mount
  went through.

## Disposition
Fixed: `OnSyncMark` resolves a null prefix through the exe global (`DefaultSyncPrefix`, falling back to
`"NPC"` on a runtime without a pin), and the comments that said `""` are corrected. Close once a build with
the fix mounts a horse and plays the intro's bonds cut in game with `[Animation64] bReaders=true`.
`g_bssyncEmptyString` keeps its misleading name until CB regenerates its registry from OPEN-HAVOK (cbmatch-emitted rows are
frozen until then).
