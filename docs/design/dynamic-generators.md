# Dynamic generators

Nov 12, 2026. Two CB generator classes and the scoped conditions they pick by. CB's runtime, CB's compile and Behavior
Kit (BK) all work to this. True Cinematics (TC) is the first user; nothing in either class knows about cutscenes.

## Why

Today each TC cutscene ships its own `0_master` deltas: a reference generator and an entry transition per cutscene.
They pile up, and two mods that both patch `0_master` must be merged. Instead, `0_master` holds one generic slot, written
once, and a cutscene (or a weapon set, or a furniture set) is bound into that slot at run time. The dynamic clip
generator does the same for clips: one node picks from a folder by conditions the clips carry. That covers random
idles, equip variants, two guards in one cutscene graph playing different clips, and what OAR did. OAR is not supported
at run time; OAR content converts to this (see the last section).

## `CBDynamicBehaviorReferenceGenerator`

Schema: `schema/generators/CBDynamicBehaviorReferenceGenerator.yaml`. Parent `hkbGenerator`, 104 bytes.

| Field | Type | Meaning |
| --- | --- | --- |
| `key` | string | the slot's key, byte-exact |
| `fallback` | ptr hkbGenerator | optional; plays when nothing is bound |

On activation the node asks the binding registry what this character's actor is bound to under `key`, and runs that
behavior's root graph as its one child. Nothing bound: it runs `fallback`, or is inert without one. CB logs either case.
A bound sub-graph may hold slots of its own under other keys.

**Binding.** In the plugin: `CB::feature::dynamic::Bind(actor, key, behaviorPath)`, `Unbind(actor, key)` and
`BoundPath(actor, key)` (`src/features/Dynamic Generators/Bindings.h`). From another plugin: the POD message in
`include/CB-Bindings.h` (`cb::bindings::kBind` / `kUnbind`, a `BindMessage {actor FormID, key, behaviorPath}`) sent
through SKSE messaging to CommunityBehaviors. Strings are copied and compared exactly. A new Bind replaces the old
binding under the same key; an unbound behavior is released once no active generator holds it.

**Loading** happens at bind time, never at activation (a 4 s project load at activation would be a hitch). The
sub-graph's events and variables link into the character's by name, as a referenced behavior's do, so an event raised
on the actor (TC's `[TCEvent]-<uuid>`) reaches it without `0_master` listing it. **Not built yet**: CB has no seam for
the exe's behavior-reference load, so a binding resolves to nothing and the slot plays its fallback (CB-21).

**Entry and exit.** `0_master` gets one fixed entry transition into the slot's state and one fixed exit transition on an
event the sub-graph raises when it is done, identical for every cutscene. The exit event's name is still open.

One graph per cutscene per species, e.g. `meshes/actors/<species>/behaviors/<Mod>/<Cutscene>.hkx`. Roles of one species
share it and differ only in their clips, by alias.

## `CBDynamicClipGenerator`

Schema: `schema/generators/CBDynamicClipGenerator.yaml` (176 bytes) and `CBDynamicClipRow.yaml` (32). Parent
`hkbGenerator`, deliberately not `hkbClipGenerator`, so nothing that hooks clip generators acts on it.

| Field | Type | Meaning |
| --- | --- | --- |
| `triggers` .. `flags` | as hkbClipGenerator's | the clip settings every clip it plays gets: triggers, crops, start time, speed, enforced duration, user-controlled fraction, `mode`, `flags` |
| `folder` | string | relative to the project's animation folder (`meshes/actors/<project>/animations/`), byte-exact, either slash |
| `fallback` | string | one clip name, as `animationName` spells it; plays when no row holds |
| `blendDuration` | float | seconds a switch of clip dead-blends over (default 0.2) |
| `rows` | CBDynamicClipRow[] | compiled: `{clip, condition, priority, weight, flags}` |
| `fallbackClip` | ptr | compiled from `fallback` |

**The table is compiled.** When CB compiles at startup, `compile::FillDynamicClipTables` (`src/engine/compile/DynamicClips.h`)
fills `rows` with one row per clip directly in the folder, over the merged load order, sorted by name: each row a new
`hkbClipGenerator` carrying the generator's clip settings, plus the clip's selection data. `fallbackClip` is built from
`fallback`. There is no cache file and the runtime never scans folders; a clip a mod adds appears on the next compile. Two
generators on one folder each get their own copy. The project's characters roster every clip a dynamic generator may
play, since a clip binds only through the roster. BK calls the same function (`cb::dynamic::FillDynamicClipTables`)
with its own folder lister, so its preview and the game pick from the same table.

Today the folder lists the bundle-authored animation units of compile-opted-in bundles. Loose `.hkx` files and BSA
clips are not listed yet.

**Picking.** Rules in `src/engine/relay/DynamicClip.h`, shared with BK through `cb::dynamic`:
- At every activation: among the rows whose condition holds, the highest priority wins. Rows tied at that priority
  draw by weight, from a seed of (actor FormID, the clone's activation count, loops since). The same table and seed
  always pick the same row, so a preview, a test run or a cutscene replays. A weight <= 0 draws only when every
  candidate's does, then evenly. No row holds: `fallbackClip`. No fallback either: nothing plays, and CB logs it.
- `FLAG_INTERRUPTIBLE`: the row re-evaluates every update, and gives way when it stops holding or a higher priority
  now holds. A tie keeps the playing clip, so a draw never flickers. The cost is measured as a Tracy zone ("Dynamic clip
  evaluate").
- `FLAG_REDRAW_ON_LOOP`: re-pick when the clip loops (its local time runs backwards).
- `FLAG_KEEP_RANDOM_ON_LOOP`: with REDRAW_ON_LOOP, keep the drawn clip while it still holds at the top priority.
- The fallback plays out as a vanilla clip would, and gives way at its loop once a row holds.
- A switch dead-blends (`locomotion/DeadBlend.h`, as the pose search does) over `blendDuration`, so it never pops.

Only the playing clip is active, so a folder never streams all of its animations in. Each character runs its own clone
(`cloneNode`), so "self" in a condition is always that clone's actor. A clone copies the rows (taking a reference on
each clip and condition); only the pick, the seed inputs and the blend are per actor. Sharing the immutable table by a
reference-counted pointer is the fallback if memory ever matters.

Every clip in a folder must fire the events the graph waits for after the node (an equip clip's `weaponDraw`). BK
checks that statically.

Cutscenes are not a concept here. A cutscene's sub-graph holds its own dynamic clip generators with that scene's
folders, and never evaluates alongside the idle states' generators. Two roles of one species tell their clips apart by
alias: guard A's clip carries "self is alias A".

## Per-clip selection data

Authored in the clip's animation unit (the bare `.hkx` YAML), as a `selection:` map inside `animation:`
(`CBAnimation.selection`, class `CBClipSelection`, `schema/events/CBClipSelection.yaml`). A clip without one, or a
binary clip, gets priority 0, weight 1, no condition, no flags. A map that does not parse warns and keeps those
defaults.

```yaml
animation:
  name: GuardA_Talk
  # ... tracks, curves ...
  selection:
    priority: 10
    weight: 2
    flags: FLAG_INTERRUPTIBLE|FLAG_REDRAW_ON_LOOP
    condition:
      class: CBConditionAnd
      conditions:
        - class: CBScopedCondition            # self is the quest's alias 3
          test: IsSame
          other:
            - kind: alias
              arguments: ['MyScene.esp|0x000D62', '3']
        - class: CBConditionNot
          condition:
            class: CBScopedCondition          # nothing with WeapTypeSword in the right hand
            scope:
              - kind: equipped.right
            test: HasKeyword
            arguments: ['Skyrim.esm|0x0001E711']
```

`flags` is the `DynamicClipFlags` enum (`schema/metadata/enums/DynamicClipFlags.yaml`), written by name with `|`.

## Scoped conditions

Classes in `schema/events/`, all `hkbCondition`s, so any of them also works on a state machine transition:

| Class | Fields | Holds when |
| --- | --- | --- |
| `CBScopedCondition` | `scope` (CBConditionScope[]), `test`, `arguments` (string[]), `other` (CBConditionScope[]), `quantifier` | the test holds on the forms the scope produces |
| `CBConditionAnd` | `conditions` (ptr[]) | every one holds (none: holds) |
| `CBConditionOr` | `conditions` (ptr[]) | any one holds (none: does not) |
| `CBConditionNot` | `condition` | it does not (null: holds) |
| `CBConditionScope` | `kind`, `arguments` (string[]) | one step of a scope chain |

`CBGameRelayCondition` fits in the tree too, for game state no scope covers.

A **scope chain** starts from the character's actor (empty chain: the actor itself) and each step reads the forms the
step before produced, e.g. `[combatTarget, equipped.right]`, or `[alias(Q, 3)]`. Chains are capped at four steps.
`quantifier` (`ConditionQuantifier`) says whether a scope producing several forms (all worn armor) needs ANY form to
pass (default) or ALL of them. A scope that produces nothing does not hold.

**Types** are data: `schema/metadata/conditions.json` says what each scope accepts and produces, what each test
accepts, and their arguments, so BK type-checks while you edit (for example `HasKeyword` on a race's equipment).
`cb::dynamic::ParseConditionTypes` reads it and `ConditionTypes::Check` checks a chain and test. engine-tests holds the
plugin's evaluator to exactly the scopes and tests the file names.

| Scope | Reads | Produces | Arguments |
| --- | --- | --- | --- |
| `self` | (start) | Actor | |
| `alias` | (start) | Reference | quest FormKey, alias ID |
| `base` | Actor | NPC | |
| `race` | Actor | Race | |
| `equipped.right`, `equipped.left` | Actor | Equipped | |
| `ammo` | Actor | Ammo | |
| `worn` | Actor | Armor (several) | optional slot 30-61; none: every slot |
| `furniture` | Actor | Furniture (the base) | |
| `combatTarget`, `mount` | Actor | Actor | |

| Test | Accepts | Arguments |
| --- | --- | --- |
| `Exists` | any form | |
| `IsForm` | any form (a reference matches its base too) | FormKey |
| `IsSame` | any form | none; compares with `other`'s forms |
| `HasKeyword` | forms with keywords | keyword FormKey |
| `ActorValue` | Actor | name, `==` `!=` `<` `<=` `>` `>=`, value |
| `HasMagicEffect` | Actor | effect FormKey |
| `HasMagicEffectWithKeyword` | Actor | keyword FormKey |

A **FormKey** is `<plugin file>|<hex form id without the load-order byte>`, e.g. `Skyrim.esm|0x0001E711`, OAR's own form.
An alias is identified by its quest's FormKey and its alias ID, never its name (names repeat and get renamed). "Self is
alias A" is `IsSame` with an empty scope and `other: [alias(Q, A)]`. A scene's cast are its quest's ReferenceAliases,
which is how cutscene roles are told apart with no role concept in any node. `alias` produces a Reference: chaining
an actor scope after it (`alias(...).equipped.right`) needs an alias-to-actor step, not in the first set.

**Text form** (`relay/ConditionText.h`, settled by the user 2026-11-12): a method chain over the same tree, e.g.
`equipped.right.HasKeyword("Skyrim.esm|0x0001E711") && !IsSame(alias("MyScene.esp|0x000D62", 3))`. Dot-separated steps,
the last one the test; names byte-exact as conditions.json spells them (a dotted scope matches longest-first); `all.` in
front sets the quantifier; `!`, `&&`, `||`, parentheses. `ParseConditionText` type-checks against the table and
`FormatConditionText` writes it back. Nothing reads it yet: the unit format and BK still carry the classes.

**Cost.** Within one evaluation of a generator's rows, each distinct chain resolves once per actor and is shared
across the rows' conditions (`EvaluationScope`). FormKeys resolve once per process.

## Open

- **The exit event's name** - settled (user, 2026-11-12): per key, `<key>Done`. The cutscene slot's key is `Cutscene`, so
  its exit event is `CutsceneDone`.
- **CB-21**: the bind-time behavior load.
- Folder listing of loose and BSA clips.

## Follow-up: OAR conversion (a tool, not built)

OAR is never supported at run time; a converter turns OAR submods into this:

- **Per submod:** its conditions, priority and flags copy into each of its clips' `selection:` maps.
- **Clip generators:** each vanilla clip generator that plays a file an OAR submod replaces becomes a
  CBDynamicClipGenerator: its `fallback` is the vanilla clip, its folder holds the OAR variants.
- **Flags:** `interruptible` -> FLAG_INTERRUPTIBLE, `replaceOnLoop` -> FLAG_REDRAW_ON_LOOP, `keepRandomResultsOnLoop` ->
  FLAG_KEEP_RANDOM_ON_LOOP. OAR's `Random` condition becomes the weighted draw.
- **Conditions:** each OAR condition maps onto a (scope, test) pair, an AND/OR/NOT, or a Relay expression. The audit
  below is from OAR's documented condition list and must be checked against OAR's source before the converter is built.

| OAR condition | Maps to | Status |
| --- | --- | --- |
| AND / OR, `negated` | CBConditionAnd / Or, CBConditionNot | covered |
| IsForm | `IsForm` (self) | covered |
| IsActorBase | `IsForm` on `base` | covered |
| IsRace | `IsForm` on `race` | covered |
| HasKeyword | `HasKeyword` (self) | covered |
| IsEquipped (left/right) | `IsForm` on `equipped.left/right` | covered |
| IsEquippedHasKeyword | `HasKeyword` on `equipped.left/right` | covered |
| IsEquippedType | needs `WeaponType` test, or Relay `EquippedType` | gap |
| IsEquippedShout / IsEquippedPower | needs `equipped.voice` scope | gap |
| IsWorn | `IsForm` on `worn` | covered |
| IsWornHasKeyword | `HasKeyword` on `worn` | covered |
| IsWornInSlotHasKeyword | `HasKeyword` on `worn(slot)` | covered |
| HasMagicEffect | `HasMagicEffect` | covered |
| HasMagicEffectWithKeyword | `HasMagicEffectWithKeyword` | covered |
| CompareValues (actor value) | `ActorValue` | covered |
| CompareValues (graph variable, global, literal) | Relay `GraphVar` / globals | via Relay |
| Level | `ActorValue` or Relay | needs check |
| HasPerk, HasSpell | new tests on Actor | gap |
| IsInFaction, FactionRank | new tests on Actor | gap |
| IsClass, IsCombatStyle, IsVoiceType | `IsForm` on new scopes (`class`, `combatStyle`, `voiceType`) | gap |
| IsFemale, IsChild, IsUnique, IsPlayerTeammate | Relay, or new tests on NPC / Actor | gap |
| IsInCombat, IsSneaking, IsRunning, IsSprinting, IsInAir, IsWeaponDrawn, IsAttacking, IsBlocking, IsSwimming, IsOnMount | Relay (actor state) | via Relay |
| IsMovementDirection, CurrentWeather, CurrentGameTime, LightLevel, SubmergeLevel | Relay | via Relay |
| IsInInterior, IsParentCell, IsWorldSpace, IsInLocation, LocationHasKeyword | new `cell` / `location` / `worldspace` scopes | gap |
| HasRefType | new test on Reference | gap |
| IsCurrentPackage, CurrentPackageProcedureType | new `package` scope | gap |
| Scale, Height, Weight | Relay | via Relay |
| IsReplacerEnabled, Random | no condition: the converter resolves them | converter |

`gap` rows need a scope or test added to `conditions.json` and the evaluator (engine-tests keeps the two in step), or a
Relay function; `via Relay` rows need Relay to carry the state, which it may already.
