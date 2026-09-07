# Havok-Driven 3D Scene UI — Research Notes

> **Status:** research / not yet designed. **Not part of the progress-bar branch** — relocate to the
> framework's own repo/branch when it gets one. Addresses are for **Skyrim SE AE 1.6.1170**
> (image base `0x140000000`), from the unpacked runtime + the master `Skyrim.hky`.

## Thesis

Skyrim renders parts of its UI as **Havok-animated 3D scenes** — most visibly the skill/perk
constellation. The scene-in-UI system is **general, shipping, and exposed to plugins** via
`UI3DSceneManager`. The *only* piece that was ever locked behind the Havok license / missing tooling
is **authoring the behavior graphs** that animate the scene elements — and that is exactly what
Community Behaviors (CB) unlocks. Therefore custom Havok-driven 3D UI — custom perk trees, diegetic
3D HUDs, spatial menus — is a **content + tooling** problem, not a locked-format one.

## How this was found (the thread)

Reviving the compiler progress bar → drawing it via a passive `IDXGISwapChain::Present` hook (the
CS/OAR pattern) → which needed CB's graphs → which led to the master's `interfacebehaviors/` → which
led to `StatsMenu` → which led to `UI3DSceneManager`. The progress-bar work confirmed the renderer
struct (`BSGraphics::Renderer`) and the present pipeline used here.

## The perk constellation, dissected

`StatsMenu` (ctor `FUN_14095ed20`) is a **hybrid** of three layers:

1. **Scaleform chrome** — `StatsMenu.swf`, loaded via `BSScaleformManager`. 2D: perk names, skill
   list, cursor, text. Driven by GFx invokes (`gotoAndPlay`/`gotoAndStop`/`SetStatsMode`/`UpdatePerkText`).
2. **3D skydome NIF** — `Interface/INTPerkSkydome.nif` (`DLC01/Interface/INTVampirePerkSkydome.nif`
   in beast mode), loaded via `RequestModel`, registered with **`UI3DSceneManager`** (slot 6). The
   constellation stars and connecting lines are **3D nodes** in this NIF.
3. **Havok interface behaviors** — animate the star/line nodes.

### The interface behaviors (from the master `Skyrim.hky`)

Path: `meshes/interface/interfacebehaviors/`. Two characters, both **single-bone
(`SingleBoneSkeleton.hkx`), empty `animations.txt`**:

- **`StarCharacter`** → `StarBehavior.hkb` — each perk **star**.
- **`Line`** → `Line.hkb` — each connecting **line** (states: Locked/Unlock/Unlocked +
  FadeIn/FadeOut variants).

`StarBehavior` is a small state machine (`OwnState`/`LockedState`, generators `Own/Locked/Unlocked/
Owned/Deselect`). Its event vocabulary:

```
Darken · Lighten · Brighten          (glow / hover feedback)
Unlock · Own · Select · Deselect     (perk state changes)
UnlockedWild · OwnedWild · SelectedWild   (idle twinkle/shimmer loops)
SoundPlay · reset
```

Each star/line is a **one-boned mini-actor**: the behavior graph outputs its single bone's transform
each frame; that transform drives the 3D star/line node in the skydome NIF.

## The controller blueprint — `UpdateConstellationAnims`

This is the reusable pattern, the thing a framework generalizes.

- **Element collection:** `StatsMenu+0x2c0` is an array of graph-bearing node objects, count at
  `+0x2d0`. Each element is polymorphic (vtable) and carries both a Havok animation graph **and** app
  metadata (perk/ActorValue association, child links).
- **Binding controller:** `UpdateConstellationAnims` (RTTI `.?AVUpdateConstellationAnims@…@@`,
  visitor `Func0` at `0x14096bcf0`, `VTABLE___UpdateConstellationAnims`) is a stack-instantiated
  **visitor functor swept over the element array**.
- **Per element** it reads game state and pushes an event token into the element's graph via a
  **virtual at vtable+8 taking a `BSFixedString`** — i.e. `NotifyAnimationGraph(event)`:

  | Condition | Token (runtime-interned `BSFixedString`) |
  |---|---|
  | `Actor::HasPerk(player, perk)` | `"Own"` (`aOwn`, global `0x1431af688`) |
  | level ≥ req **and** `TESCondition::IsTrue` | `"Unlock"` (`aUnlock`, `0x1431af6a0`) |
  | this skill's tree is the one being viewed | `"reset"` (`aReset`, `0x1431af690`) |

- **Trigger sites** (all re-run the sweep): buy-perk `FUN_140961d60`, level-up `FUN_14096ade0`,
  skill-select `FUN_140962380`. Cursor `Select`/`Deselect` come from `StatsMenu::ProcessMessage`
  (`0x14095f710`) on input.

The star's `StarBehavior` state machine receives the event → transitions → plays the matching
animation on the 3D node. **"The perk menu is an actor's animation graph where the actor is a star and
the AI is the perk logic."**

## Generalized into a framework

The pattern maps 1:1 onto framework layers (Bethesda hand-wrote one instance of it):

| Framework concept | StatsMenu implementation |
|---|---|
| **Element** | `{ Havok graph + app metadata }` node object; `NotifyAnimationGraph` at vtable+8 |
| **Collection** | array at `StatsMenu+0x2c0` (count `+0x2d0`) |
| **Binding controller** | `UpdateConstellationAnims` visitor; state → event token |
| **Builder** | instantiate element + attach graph to scene node *(the one un-cracked brick)* |
| **Render** | skydome NIF via `UI3DSceneManager` (or world-anchor) |

**Two element drive modes:**

- **Event-driven** (perk-class): logical state → discrete events (`Own`/`Unlock`/`Select`). Interactive.
- **Variable-driven** (QTE / diegetic-HUD class): a shared **parametric blend float** — the same
  graph variable that drives the actor's blend, mirrored into the UI element's graph via
  `SetGraphVariableFloat`. One source of truth; the HUD literally *is* the animation, on a different
  mesh. Non-interactive, perfectly synced.

Declarative target: replace hardcoded `HasPerk → "Own"` with `bind: { event: "Own", when: <cond> }`
and `bind: { var: "Progress", from: <source> }`.

## `UI3DSceneManager` — the render surface (plugin-exposed)

CommonLibSSE-NG exposes it directly (`RE/U/UI3DSceneManager.h`) — **no hooking needed**:

```cpp
class UI3DSceneManager : public BSTSingletonSDM<UI3DSceneManager> {
    static UI3DSceneManager* GetSingleton();
    void AttachChild(NiAVObject*);                          // inject geometry into the current scene
    void AttachChild(NiAVObject*, INTERFACE_LIGHT_SCHEME);
    void DetachChild(NiAVObject*);
    void SetCameraFOV(float);
    void SetCameraRotate(const NiMatrix3&);
    void SetCameraPosition(const NiPoint3&);
    // ...
    NiPointer<NiNode> menuObjects[8];   // 0x38 — the scene roots
};  // sizeof == 0x118
```

- **8 scene slots** (`menuObjects[8]`); **7 attach-valid** (`AttachChild` gate `sceneIdx-1 < 7`;
  slot 0 reserved). Used by map, inventory, RaceMenu, StatsMenu (slot 6), main-menu backdrop, ….
- Each slot is a real `NiNode` scene root with **lighting** (`MenuLight`), **shadows**
  (`ShadowSceneNode`), a **camera**, and an **image-space modifier** (post-processing). Proper little
  3D worlds, not flat viewports.
- **A plugin can inject its own geometry and drive the camera with zero RE.** `AttachChild` calls a
  virtual at vtable+0x1a8 on the slot's scene object; it is mutex-guarded.

**Ceiling:**
- 8 fixed slots — no 9th without hooking the manager. (One slot can hold an entire attached `NiNode`
  tree, so a whole spatial UI can live in one slot.)
- To *display* a slot you still need an `IMenu` context that renders it (like `StatsMenu`). Registering
  a custom menu is standard SKSE work, not RE.

Relevant runtime addresses: `AttachChild` `0x1409736c0`, `DetachChild` `0x140973790`,
`SetCameraPosition/Rotate/FOV` `0x140973cb0/0x140973d60/0x140973db0`, `GetSingleton` global
`0x143187780`, slot-select `FUN_1409738f0`.

## Applications

- **Custom perk trees** — the decade-old "impossible," blocked purely on authoring `StarBehavior`-class
  graphs. CB unblocks it. Perk trees become a *content* problem.
- **Diegetic 3D HUD** (e.g. True Cinematics QTE) — a world-anchored NIF whose graph shares the actor's
  parametric blend variable. Prisma-free, perfectly synced.
- **Spatial / 3D menus** — "wrap the UI in a 3D scene" = one slot + `AttachChild` everything + one camera.
- **Windows / portals** — *distinct capability*: render-to-texture onto a **world surface** (mirror/
  portal), not `UI3DSceneManager` (which is screen-composited). Precedents exist (water reflection,
  local map). Separate research thread.

## Open probes (next RE)

1. **The Builder brick** — the writer of `StatsMenu+0x2c0`: how a node object is created, its
   `StarBehavior` graph loaded/attached, bound to a skydome NIF node, and positioned. This is the one
   engine-interaction the framework's Builder must replicate. Lives in the per-skill constellation-load
   / skill-select handler.
2. **Auto-instantiation from NIF** — whether `AttachChild`'d geometry with `BSBehaviorGraphExtraData`
   on its nodes auto-creates the graphs via the animation system. If so, the Builder is trivial
   (author the NIF with graph extra-data pointing at CB-compiled behaviors, attach, done).
3. **World-anchored graph objects** — the attach path for a diegetic HUD *outside* `UI3DSceneManager`.
4. **RTT-to-world-surface** — how water / local-map bind their render target; is it generalizable to
   "any mesh, any scene" for windows/portals?
5. **Custom menu → slot** — how an `IMenu` registers to drive a `UI3DSceneManager` slot.

## Not-a-locked-door thesis (restated)

Every link in the chain is either **public API** (`UI3DSceneManager::AttachChild`/`SetCamera*`,
`NotifyAnimationGraph`, `SetGraphVariableFloat`, `RequestModel`) or **the thing CB already unlocked**
(authoring the Havok graphs). The 3D-scene-in-UI system is general and shipping; the graph-authoring
half was the only wall. Community Behaviors is the key that turns "you need a Havok license and lost
proprietary tools" into "you read the tree."
