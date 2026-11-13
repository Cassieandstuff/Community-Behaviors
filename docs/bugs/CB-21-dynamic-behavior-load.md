# CB-21: a bound dynamic behavior never loads

**Status:** OPEN
**First seen:** 2026-11-12   **Area:** runtime (dynamic generators)

## Symptom
A `CBDynamicBehaviorReferenceGenerator` whose actor is bound under its key logs "bound but not loaded" and plays its
fallback (or nothing). The binding registry, the generator's resolution and its fallback work; the load does not.

## Root cause
CB has no seam yet for the exe's behavior-reference load: the step that reads a behavior file for a character, links
its events and variables into the character's by name, and hands back its root graph. `LoadBehavior` in
`src/features/Dynamic Generators/Bindings.cpp` is a stub that returns null.

## Ruled out
Nothing yet. No engine.yaml row covers the load; `platform/game/AnimationLoad.h` covers clips only.

## Disposition
RE the exe's behavior-reference load (where a project's `hkbBehaviorReferenceGenerator::m_behavior` gets filled), pin
it in engine.yaml, and call it from `Bind` (the actor's character, when its graph exists) so loading happens at bind
time, never at activation. `Acquire` then only finds the loaded graph.
