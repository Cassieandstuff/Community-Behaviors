# CB-14: the fall animation sometimes lingers after landing

**Status:** OPEN (deferred)
**First seen:** 2026-10-19   **Area:** runtime (character controller ground states / behavior graph)

## Symptom
With `bGroundStates` on, the player occasionally lands and stays in the falling animation for up to a second or
two before the graph returns to locomotion. Movement and physics are normal during it; only the animation is late.

## Root cause
Unknown. The controller lands correctly (the log shows `Airborne -> Grounded (landed)` at the touchdown), so the
suspicion is on the graph side: what the landing hands the behavior graph, or how the graph leaves its fall state.

## Ruled out
- **The ground/air bounce** (fixed in a545ecf). Walking on uneven ground used to drop the character into the air for
  a few hundredths of a second and land it again, over and over (31 relandings within 0.1 s in one session, a 2 s
  bounce at one lip). The snap-back now holds the ground unless the body is launching; the next session had 1 such
  relanding in 149 landings, and the lingering fall was still seen.

## Disposition
Deferred: treated as an animation-side glitch while the controller work continues. Next lead: watch the graph's
jump/fall variables across a landing with `bRuntimeTrace` (a debug/*.yaml probe naming them), and compare which
landing path ran (`kHitFlags` set by `EnterGrounded`, `src/features/Advanced Controller/CharStateWrapper.cpp`) with
what vanilla OnGround does on a landing.
