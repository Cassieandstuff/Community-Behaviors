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

Lead, not proven: this horse has the phantom because of CB. With `bHorseProxyController=true` (the deployed settings.ini),
`CharMountProxy.cpp` sets `kAlwaysUseProxyController` on every mountable race at kDataLoaded; the 02:37 run logged it for
`HorseRace` (`000131FD`). The engine picks the controller at 0x14065d590: the proxy for the player, for any actor while
`[HAVOK] bUseCharacterRB` is 0, or when the race has the flag; else the rigid body. That setting defaults to 1 (0x14202c938)
and no ini in the game's folder, the MO2 profile or the mods sets it, so without the flag this horse takes the rigid body. The
rigid-body controller builds an `hkpCharacterRigidBody` over an `hkpRigidBody` (0x140ed3bc0, 0x140b7bbc0) and no
`hkpCachingShapePhantom`: that constructor (0x140b49660) is reached only from `bhkCachingShapePhantom`'s create and clone and
from `hkbProxyModifier`, and both crashing paths reach the phantom through the proxy. The flag dates from 2026-10-26 (e61f1247, 1656654a) and the first
crash on file is 2026-11-03, so it may crash only together with something newer such as the SDK cut, or earlier runs may not
have reached this horse. When the flag was turned on in the deployed settings is not known.

## Ruled out
- b1c1fca8 (the search in the pose search node): three of the crashes predate it, the 02:25 one by minutes (committed 02:28,
  deployed 02:32). It touches no physics, and the horse's stack has no graph or plugin frame.
- The Jolt mirror: the 11-04 builds lack it.
- `bUseWorldLock` off: its default is on (0x14202c810) and no ini sets it.
- SDK bodies on the agent list: `Defined.yaml` has no `hkpCachingShapePhantom`, `hkpPhantom` or agent function.

Not ruled out: `bHorseProxyController`, which reaches every horse, NPC horses included (CB's per-frame controller features
act only on the player and a ridden horse, but this flag changes which controller the rest are built on; the player's phantom
can also add and remove pairs with this horse, under the write lock); the 513 SDK bodies in general (`SetWorld`,
`SetTransformImpl`, `CheckSupportImpl` on the proxy controller read the same as the exe, but have not been run off); other
physics plugins (Precision.dll appears in the stack residue of the three earlier crashes); and vanilla's proxy code on an NPC.
No log from before the SDK cut shows this crash, which proves little: those runs may not have reached this horse.

## Disposition
Cheapest first: replay the save with `bHorseProxyController=false`. It needs a game restart, since the flag is read once at
startup and nothing rebuilds a live controller. Then replay with Precision off, then with `[HavokRuntime] sFunctionsOff`
listing the proxy controller's bodies. If those do not settle it, log add and remove on `hkpCachingShapePhantom` behind a debug
flag: the thread, whether it holds the world's write lock, and any overlap on one phantom. That names the path at its first
occurrence.
