# Actor↔Object Positioning Sync — Research Notes

> **Status:** research / scoped, not built. **Not part of the progress-bar branch** — relocate to its
> own repo/branch. Evidence from the unpacked master `Skyrim.hky` and Skyrim SE AE 1.6.1170.

## Goal

Paired/synchronized **actor↔object** interactions — first target: an actor walks to a door, is placed,
and plays a **push-open synchronized with the door's swing**. Reusable beyond doors (levers, chests,
coffins, any "actor manipulates a thing" moment).

## What the master tells us (the precedent)

- **True Havok paired-sync (`BSSynchronizedClipGenerator`) is actor-exclusive.** 537 files use it, **all**
  under `actors/`. Zero in `animbehaviors` / `clutter` / `architecture`. Objects never join the tight
  motion-matched pairing system.
- **Actor↔object coordination is done by events + authoring, not sync anchors.** Canonical example: the
  **draugr sarcophagus** (`clutter/ruins/sarcophaguslid/…/sarcophagusbehavior.hkx`) — a standalone object
  behavior (own 1-bone skeleton) with `SarcophagusOpenA/B` clips, triggered by events `OpenStartA`/
  `OpenStartB`, matched to the draugr's two getup animations, **authored to line up**. That *is* a paired
  actor↔object animation — just via events, not a shared Havok sync point.
- **The door is already an object behavior.** `animbehaviors/doors/doortemplate01.hkx`: events
  `Open`/`Opening`/`Close`/`Closing`, clips `Open.hkx`/`Close.hkx`, 1-bone door skeleton. Fire `Open` and
  it swings. The object side needs nothing new.

**Conclusion:** the prototype follows the sarcophagus model, generalized — the "sync" is an SKSE event
relay across two independent graphs; positioning is SKSE placing the reference.

## Architecture — three components

### 1. Door graph (vanilla, ~free)
`doortemplate01` already responds to `Open`. Just fire it on cue.

### 2. Actor interaction subgraph (CB-authored, FNIS-idiom)
Delta onto `defaultmale`/`defaultfemale`:
- A branch reachable from a **wildcard transition** on `CB_DoorPush`.
- One **push-open clip** with a **clip-trigger annotation** at hand-contact: `CB_DoorContact`.
- *(v2)* a `BSTweenerModifier` for smooth approach, targets bound to graph variables.

### 3. SKSE plugin (placement + event relay = the sync)
- **Trigger:** hook door activation (or player-targets-door + input).
- **Place:** move the actor to the door use-marker/offset (world-space `SetPosition`/interp — the
  furniture model; trivial for SKSE).
- **Fire:** `NotifyAnimationGraph(actor, "CB_DoorPush")`.
- **Relay (the sync):** sink the actor's animation events; on `CB_DoorContact`, call
  `NotifyAnimationGraph(door, "Open")`. Two graphs, coordinated by relaying one contact event — no
  shared Havok sync point required.

## Positioning: two tiers

- **v1 — SKSE places the reference**, then fires. Furniture/sarcophagus model. **No graph nodes, no
  vector4 variables, no space/motion-extraction unknowns.** Start here.
- **v2 — in-graph tween.** `BSTweenerModifier` (sig `0x0d2d9a04`): fields `tweenPosition`/`tweenRotation`/
  `tweenDuration`, **`targetPosition: vector4`**, **`targetRotation: quaternion`**. Every node inherits
  `hkbBindable.variableBindingSet`, so those targets can bind to graph variables; SKSE writes the door
  transform and the vanilla tweener warps the actor. Smoother, animation-integrated. Deferred.
- Also available in the schema: **`hkbKeyframeBonesModifier`** (sig `0x95f66629`) — keyframe arbitrary
  bones **including the root** to a transform (feeds motion extraction = controlled placement);
  **`hkbRotateCharacterModifier`** (face the target); **`hkbGetUpModifier`** (rising — the sarcophagus
  class of interaction).

## Milestones (risk-ordered)

| M | Proves | Notes |
|---|---|---|
| **M0** | SKSE fires `Open` on a door ref via `NotifyAnimationGraph`; it swings | ~30 min; de-risks the entire object side |
| **M1** | Actor plays push-open off a wildcard `CB_DoorPush` | CB delta + one clip; the FNIS path |
| **M2** | `CB_DoorContact` annotation reaches SKSE, which relays `Open` to the door | the sync |
| **M3** | SKSE places the actor at a real door marker, then M2 | v1 complete |
| **M4 (v2)** | Tweener smooth-approach via bound variables | procedural-alignment upgrade |

## Real unknowns (tests, not walls)

1. **Cross-object `NotifyAnimationGraph` on a door ref** (M0) — doors have graph managers; confirm first,
   everything depends on it.
2. **Sinking actor animation events from SKSE** (M2) — `BSTESAnimationGraphEvent` sink / animation-event
   hook; confirm `CB_DoorContact` is observable (CommonLib exposes animation-event sinks).
3. **Timing feel** — whether the relayed `Open` lands tight, or needs a small lead/lag. Authoring, not
   a blocker.
4. **(v2 only)** tweener target space + motion-extraction interaction; the vector4/quaternion graph
   variable setter API. Fully deferred out of v1.

## Assets

- **One** "push door open" actor animation (repurpose an existing activate/shove idle for the prototype).
- Door uses vanilla `Open.hkx`/`Close.hkx`. Nothing new on the object side.

## Success criterion

Walk to a flagged door → actor snaps to the use spot → plays a push → door swings open in time with the
hand. Vanilla nodes + CB deltas + a small SKSE plugin doing place + fire + relay.

## Relationship to the custom-node frontier

This prototype deliberately uses **no custom executable Havok node** — it's the pragmatic base. The
custom-node capability (a registered `hkbModifier` subclass with a vtable in the plugin, executing in the
pose pipeline — feasible because CB cracked `getSignature`) is the **v3 upgrade**: continuous procedural
alignment / contact-driven IK that corrects every frame instead of tweening to a fixed target. Held in
reserve; not needed to ship a working paired actor↔object interaction.

## Key facts / addresses

- `BSSynchronizedClipGenerator` usage: 537 files, all `actors/` (no objects).
- Sarcophagus: `clutter/ruins/sarcophaguslid/behaviors/sarcophagusbehavior.hkx` — clips
  `SarcophagusOpenA/B`, events `OpenStartA/B`, `SnapClosed`, `Done`, `SoundPlay.NPCDragonPriestCoffinOpen`.
- Door template: `animbehaviors/doors/doortemplate01.hkx` — events `Open/Opening/Close/Closing`.
- Node schemas (CB `Havok/core/Schema/gen/`): `BSTweenerModifier` (0x0d2d9a04),
  `hkbKeyframeBonesModifier` (0x95f66629), `BSLimbIKModifier` (0x8ea971e5),
  `hkbRotateCharacterModifier`, `hkbGetUpModifier`.
- Binding: `hkbNode → hkbBindable.variableBindingSet` (ptr `hkbVariableBindingSet`) — every node's
  fields are variable-bindable.
