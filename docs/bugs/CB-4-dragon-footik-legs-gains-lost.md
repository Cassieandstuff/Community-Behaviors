# CB-4: dragon's hkbFootIkModifier loses `legs` + `gains` in the .hky

**Status:** OPEN
**First seen:** 2026-09-28   **Area:** decompile (EmitHky) / Skyrim.hky content

## Symptom

`actors/dragon/behaviors/dragonbehavior.hkx` → node `FootIKModifier_TakeOff` (`hkbFootIkModifier`). The vanilla
binary carries a `gains` struct (onOffGain 0.2, …) and a `legs` struct array; the Skyrim.hky unit has neither.
Its own `bindings:` still target `legs:0/footPlantedAnkleHeightMS`, `gains/worldFromModelFeedbackGain`, … —
bindings into members that no longer exist. Any CB compile of this graph (runtime, when a mod layers it, or
the converter's base-unit compile) builds the modifier with default gains and no legs.

Found by `BehaviorConverter --reflect-dump` (a generic schema-driven emit diffed against EmitHky): the only
two `hkbFootIkModifier` nodes in vanilla, both dragon.

## Root cause

`hkbFootIkModifier` has no dedicated renderer, so EmitHky falls to the generic `renderModifier`, which renders
only scalars / strings / vectors / pointers / `hkbEventProperty` structs and drops every other kind:
`Havok/core/cpp/decompile/HavokModel.cpp:947` (`default: break;`) and the struct case at `:934`.
The loader has a handler for the class (`YamlBehaviorLoader.cpp:1310`), so the loss is on the emit side.

## Ruled out

- Empty in vanilla (like keyframeInfo / twist arrays) — no: dragon's gains and legs carry real values.

## Disposition

Fixed by the reflective .hky format work (generic schema-driven emit emits every non-ignored field; the
base-unit round-trip gate in RegenerateMaster then covers it). Same root cause as [CB-5](CB-5-spriggan-istate-statedata-lost.md).
