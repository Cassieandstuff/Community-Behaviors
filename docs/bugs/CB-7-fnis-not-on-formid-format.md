# CB-7: FNIS conversion not yet on the FormID .hky format

**Status:** OPEN
**First seen:** 2026-09-29   **Area:** converter

## Symptom
A load order with FNIS list files (`FNIS_*_List.txt` under `meshes/actors/character/animations/`) converts
with `FNIS: FAILED — … not yet on the FormID .hky format — skipped`; no FNIS.hky is written, so FNIS
animations don't play through Community Behaviors.

## Root cause
The consumer switch moved every unit to the reflective FormID format (a unit is `graph.yaml` +
`<category>/<id>.yaml`; mods ship patches derived against the base unit). The FNIS leg hand-wrote the retired
format (`behavior.yaml`, `data/graphdata.yaml`, name-keyed node files, a `0_master` delta with `parents:` and
`data/additive.yaml`), which nothing reads any more, so its emitters were removed
(`APP/Behavior Converter/src/convert/FnisConverter.cpp`). List scanning + grouping (`FnisListParser`) remain.

## Ruled out
- Hand-writing reflective YAML the same way: every field of every new node must be stated (a schema object
  defaults to zeros — e.g. a state machine's `maxSimultaneousTransitions` 0 never transitions), and the
  vanilla-shaped defaults lived in the retired SchemaBuilder.

## Disposition
Port: build the FNIS container graph as schema objects (FNIS_RootSM, one clip + state per animation,
global-wildcard entries, chain triggers) with vanilla-shaped defaults, emit it with
`DecompileBehaviorReflective`; build the `0_master` edit (the `CB_FNIS_State` state → RBG → FNIS.hkx, its events
+ variables) as a `GraphEdit` on the base unit and `DeriveHkyPatch` it; add the roster through the converter's
`RosterSink`.
