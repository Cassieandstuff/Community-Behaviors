# CB-5: spriggan's BSIStateManagerModifier loses `stateData` in the .hky

**Status:** OPEN
**First seen:** 2026-09-28   **Area:** decompile (EmitHky) / Skyrim.hky content

## Symptom

`actors/spriggan/behaviors/sprigganbehavior.hkx` → node `Spriggan_iStateManager` (`BSIStateManagerModifier`).
The vanilla binary carries a `stateData` struct array (each entry: `pStateMachine` → a state machine, `StateID`,
`iStateToSetAs`); the Skyrim.hky unit has none. Its `bindings:` still target `stateData:0/iStateToSetAs` and
`stateData:1/iStateToSetAs`. A CB compile of this graph builds the iState manager with no state data, so the
iState variable is never set per state. 3 `BSIStateManagerModifier` nodes in vanilla are affected.

Found by `BehaviorConverter --reflect-dump` (generic schema-driven emit diffed against EmitHky).

## Root cause

No dedicated renderer → the generic `renderModifier` drops struct arrays:
`Havok/core/cpp/decompile/HavokModel.cpp:947` (`default: break;`). The loader has a handler for the class
(`YamlBehaviorLoader.cpp:1368`), so the loss is on the emit side.

## Ruled out

- Empty in vanilla — no: the entries carry state-machine refs and state ids.

## Disposition

Fixed by the reflective .hky format work, as [CB-4](CB-4-dragon-footik-legs-gains-lost.md) (same root cause).
