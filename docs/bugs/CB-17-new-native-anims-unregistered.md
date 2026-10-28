# CB-17: a native animation compiled for the first time A-poses until the next launch

**Status:** OPEN
**First seen:** 2026-10-26   **Area:** runtime (native animation compile, `Resolver::WriteNativeAnimations`)

## Symptom
After a deploy, the motion-matching clips A-posed in game; on the next launch they played correctly. Expected for
every compiled animation whose file did not exist when the game started: the first launch with a new opt-in
bundle, new or renamed clips, or a cleared MO2 overwrite folder.

## Root cause
The game finds an animation's file through an ID index built once at startup. Animation loads carry no path, only
the clip's CRC triple (`ActorAnim_BindingIndexToCrcTriple`, 0x140bb3c20). The clip loader's
resolver (0x140bcc220, ported by Animation64 in `src/features/Animation 64/Loader.cpp`) requests the triple through
0x140d08da0 -> 0x140d044f0 -> 0x140d06660, a sorted lookup in the global resource ID index at `DAT_143271d00`.

Loose files enter that index only when a prefix is traversed: the archive init (0x14064b450) registers the BSAs and
then calls `PrepareResourcePrefix("data\\", "data\\", 0)` (0x140d0ae10). Its traverser
(`BSResource::RegisterPrefix::2::Traverser::Func1`, 0x140d0e390) inserts each file's ID through 0x140d03a10. The
only other caller is the `RegisterPrefix` console command (0x140373200). Nothing registers a file created later.

CB compiles native animations in the compile gate's background thread (`WarmUpThread` ->
`MaterializeCacheToDisk` -> `WriteNativeAnimations`, `src/core/resolve/Resolver.cpp:902`). They are written as
loose files under the real `meshes\` tree (MO2 overwrite), long after the archive init. A file that already
existed at startup is in the index and loads the new bytes; a file created by this compile is not, so its clip
never loads and A-poses for the whole session. The next launch's startup scan finds it.

Evidence: the overwrite folder `meshes\actors\character\animations\MotionMatching\` was created at 2026-10-26
00:25:53 (first write of all 252 clips, after the 2026-10-25 deploy). The two later cold launches (10:01, 10:44)
rewrote the clips in place (files pre-existed) and loaded them.

## Ruled out
- The compile gate letting the engine request clips before they exist: in both cold logs the 252 clips were
  written before any graph opened (10:02:16 vs AutoplayCharacter 10:02:25, player 10:03:33; 10:44:32 vs 10:44:39).
  Every owned graph open waits in `WaitForCompile`.
- Animation64's loader: it runs the same code on every launch and only queries the index by triple.
- Different bytes on cold vs warm: the animation ledger skips all 252 on an unchanged DLL, so a warm launch plays the
  files the cold launch wrote.
- A file handle held open: `disk::WriteFile` closes before returning.

Separate, not this bug: both cold logs also show a non-finite pose from the playing MM child right after load
(`input pose non-finite (not the blend's)`, CharPoseSearch.cpp:419), during the first seconds while clips stream in.

## Disposition
Fix not chosen yet. Candidates: register the IDs of animations this compile created, through the engine's own
traversal (what `RegisterPrefix` does), on a thread where no lookup can race the unlocked insert; or create the files
before the archive init so the startup scan registers them.
