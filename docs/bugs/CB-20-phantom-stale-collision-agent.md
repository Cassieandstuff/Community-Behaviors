# CB-20: a character phantom keeps a freed collision agent, crashing the game

**Status:** INVESTIGATING
**First seen:** 2026-11-03   **Area:** runtime (character physics)

## Symptom
Access violation inside the exe's `hkpCachingShapePhantom`, on a call through a collision agent's vtable, reported as a
read of `0xFFFFFFFFFFFFFFFF`. Four crashes so far, all naming the Whiterun stables horse `0x00068D74`:

| Crash log (SKSE folder) | Faulting code | Slot |
|---|---|---|
| `crash-2026-11-03-03-53-32` | `+0xB49965`, `call [rax+0x28]`: the linear-cast loop of slot 15 (0x140b497b0), from the proxy's update | 5 |
| `crash-2026-11-03-09-07-08` | same | 6 |
| `crash-2026-11-04-02-25-05` | same | 6 |
| `crash-2026-11-04-02-37-20` | `+0xB49723`, `call [rax+0x38]`: the destructor (0x140b496c0) cleaning up agents, as the horse's cell unloads | 1 |

## Root cause
Unknown. What is established is the state at the crash: an entry of the phantom's agent list (`+0x1a0`, size `+0x1a8`, 16
bytes each) points at a freed block. In all four crashes the "agent" and the "vtable" read from it are slots of one 16 KB
block, each a multiple of 48 bytes from the block's end, so the vtable is a tbbmalloc free-list link (EngineFixes' allocator).

The destructor case shows the entry outlived the phantom's removal from the world: that removal clears every entry with a
live broad-phase pair, so this one no longer had a pair.

Lead, not proven: list mutation that is not exclusive. Add (0x140b49e30) raises the size, then builds the agent, then stores
it. Remove (0x140b4a030) cleans up the agent, lowers the size and copies the last slot down, leaving its old bytes past the
end. A remove during an add's window copies that old slot down, which is a freed agent, and loses the new one.

## Ruled out
- b1c1fca8 (the search in the pose search node): three of the crashes predate it, the 02:25 one by minutes (committed 02:28,
  deployed 02:32). It touches no physics, and the horse's stack has no graph or plugin frame.
- The Jolt mirror: the 11-04 builds lack it.
- `bUseWorldLock` off: its default is on (0x14202c810) and no ini sets it.
- The player's movement path as a direct cause: CB's controller features act only on the player and a ridden horse. Moving
  the player's phantom can still add and remove pairs with this horse, under the write lock.
- SDK bodies on the agent list: `Defined.yaml` has no `hkpCachingShapePhantom`, `hkpPhantom` or agent function.

Not ruled out: the 513 SDK bodies in general (`SetWorld`, `SetTransformImpl`, `CheckSupportImpl` on the proxy controller read
the same as the exe, but have not been run off), other physics plugins (Precision.dll appears in the stack residue of the
three earlier crashes), and vanilla. No log from before the SDK cut shows this crash, which proves little: those runs may not
have reached this horse.

## Disposition
Log add and remove on `hkpCachingShapePhantom` behind a debug flag: the thread, whether it holds the world's write lock, and
any overlap on one phantom. That names the path at its first occurrence. Cheaper first: replay the save with Precision off,
then with `[HavokRuntime] sFunctionsOff` listing the proxy controller's bodies.
