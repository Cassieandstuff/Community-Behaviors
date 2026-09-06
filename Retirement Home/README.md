# Quarantine

**Do not build new things on anything in here.** This is the retirement home for legacy code
that Community Behaviors still depends on but is actively working to delete.

## What lives here

- **`havok-core/`** — the original typed-object Havok world: hand-written `hkb*` / `hka*` / `hkp*`
  classes plus the `BehaviorBuilder` / `CharacterBuilder` / `BehaviorDecompiler` / `PatchConverter`
  machinery. It is the *offline / legacy backbone* — the home of the hard-to-port converter
  decompilers and the byte-for-byte parity oracle that the data-driven stack (`../src/havok-*`) is
  gated against.

## Why it's quarantined

Every new component belongs in the **data-driven stack** (`src/havok-framing`, `havok-schema`,
`havok-io`, `havok-model`, `havok-pipeline`) — a class described **once as data** (the `Havok/`
schema tree) and consumed by generic byte-exact serialization + a SchemaObject model. `havok-core`
is being **dissolved** into that stack, not extended. Anchoring shared infrastructure here is how
"fixed" bugs quietly come back: two code paths, no single owner.

## The rule

- **Never add a new feature, class, or shared helper to `havok-core/`.** Put it in the data-driven
  stack. When a `havok-core` component earns its retirement, it moves *out* of here, not deeper in.
- The one cut that actually kills it: a **schema-driven decompiler** in `havok-model` (the last
  readers into the typed classes). Once that lands and the round-trip closes, this whole folder gets
  deleted.

If you find yourself reaching in here to build something new — stop, and give it a proper home
in `src/`.
