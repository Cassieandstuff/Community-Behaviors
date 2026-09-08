# `debug/` — compile-trace probe definitions

Probes are **data**, not code. Each `*.yaml` here declares WHERE / WHEN / HOW the compile-trace
(`havok::model::trace`, see `src/havok-model/havok/model/CompileTrace.*`) should log compiler activity.
They ride on top of the schema the way `enums/` does — they reference class and field names — but carry
no `fields:`, so the class-schema loader (`SchemaRegistry::LoadDir`) skips this subtree; the trace loads
them itself.

Because they're loaded from the **deployed** `Havok/` tree at runtime, editing a probe changes what's
logged with **no recompile** — redeploy the schema (or edit it in place), toggle `enabled`, and grep the
log. Records are single-line and greppable:

```
[CC] <phase> <unit> <class>#<name> <field> <action> <detail>
```

## Probe file format

```yaml
name:        commitment                 # identity (informational)
description: >                          # what this probe is for
  Free text.
enabled:     true                       # false => this probe contributes nothing

# WHEN — which trace phases to emit. Omit / empty = all phases.
#   vars   the merged variable table (idx -> name)
#   bind   a node binding (memberPath -> variable name + resolved index)
#   edge   graph topology (generator / modifier / state / transition edges)
#   merge  a load-order merge decision (compose / union / last-writer)
#   visit  a node visited during compile
phases: [bind, edge, vars]

# WHERE — only nodes of these classes. Omit / empty = all classes.
classes:
  - hkbEventDrivenModifier
  - BSIsActiveModifier

# WHERE (refine) — a record is kept only if its unit / class / name / detail CONTAINS one of these
# substrings (case-sensitive). Omit / empty = no name filter.
match:
  - bAnimationDriven
  - AttackModifierList
```

## Semantics
A record is emitted when **any enabled probe** matches it: its `phase` is in that probe's `phases`
(or `phases` is empty) **and** its `class` is in `classes` (or `classes` is empty) **and** (`match` is
empty **or** some `match` substring occurs in the record's unit/class/name/detail). With **no** probe
files present (or none enabled), the trace falls back to emitting everything the sink is given
(subject to any `SetFilter`).

## Runtime half — the `watch:` list

The keys above steer the OFFLINE compile trace ([Debug] `bCompileTrace`). A probe may ALSO carry a
`watch:` list — behavior-graph variables to sample **in-game, per frame** ([Debug] `bRuntimeTrace`):

```yaml
watch:
  - { var: bAnimationDriven, kind: int }    # kind: int | float | bool (default int)
  - { var: BFCO_IsBlocking,  kind: int }
  - { var: SpeedSampled,     kind: float }
  - Speed                                    # bare scalar => an int var
```

Each frame CB reads those variables off the player's live graph and logs every **change** to
`Data\community_behaviors\runtime_trace.log`, greppable as:

```
[RT] <ms-since-arm> <var> <kind> <old> -> <new>
```

So `grep bAnimationDriven runtime_trace.log` shows exactly when commitment flips during an attack.
Runtime sampling piggybacks the SKSE Menu Framework present hook (the locomotion HUD), so it needs SMF
installed; it is inert unless `bRuntimeTrace` is on. Variable names are **case-sensitive** (Havok).

## HOW (roadmap)
v1 selects *which* records appear. A later `log:` key will let a probe name specific fields to expand
(the "how" dimension) — e.g. dump a modifier's activate/deactivate event ids, or a state's full
transition tail — beyond the default record shape.
