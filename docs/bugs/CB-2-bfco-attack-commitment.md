# CB-2: BFCO attack-commitment lost — 2nd combo attack ice-skates; shield lowers while moving

**Status:** INVESTIGATING
**First seen:** 2026-09-07 (in CB; inherited from Behavior Relay BR-39, 2026-09-04)   **Area:** runtime / behavior-graph merge

## Symptom
Two symptoms, believed one root cause (a movement-suppression state that gets overridden):
1. **Ice-skate (attack commitment):** first attack in a combo commits correctly; the moment the
   **second** attack is midway through its animation, the character **slides in whatever direction the
   left stick points**. Only ever the second attack of a combo. The slide is *stick-directed locomotion
   leaking in*, NOT the attack's own root motion failing — the attack lunge (AMR) is fine; movement that
   should be suppressed is bleeding through.
2. **Shield drop:** while moving with the block button **held**, the shield lowers. Standing still with
   block held, it stays up.

Combined shape: a state that should suppress locomotion (attack commitment; block) is overridden by
movement in specific transitions → suspected **state machine wrapped in a modifier**.

Environment when reproduced: latest CB build (contains the BR-39 fix), AMR installed/enabled, author's
machine. Load order patches `1hm_behavior` with BFCO + DMCO + Precision + SBF (multi-mod).

## Root cause
Known lineage (Behavior Relay BR-39, `7a002fec` in Skyrim-Content-Tools): a state's `transitions`
array was unguarded, so 2+ mods editing it hit the UnionArray path with exact-dedup. A mod that *edits*
a base transition (e.g. BFCO `attackStop→0`, priority 0→155) yields a different value, so dedup keeps
**both** the stale base edge and the edit → the stale "exit attack / exit block" edge survives and
fires → commitment lost + shield drop.

BR-39 fix (element-wise **compose + tail-append** for `transitions`/`states`, gated by schema
`merge: compose`) is **present in CB** in BOTH merge adapters:
- runtime: `src/havok-model/havok/model/yaml/YamlBehaviorLoader.cpp` `mergeLayers` (~line 510, `composeArray`)
- converter: `src/havok-model/havok/model/BashMerge.h` `composeArrayInto` (~line 197)
Offline byte-gated vs Pandora: AttackState 30/30, BlockState 26/26.

**Yet the bug still reproduces in-game on a build that has the fix.** So the true in-game cause is one of:
1. The failing construct is NOT AttackState/BlockState but the **modifier-wrapped state machine** that
   gates 2nd-attack commitment — outside what BR-39's byte-gate covered.
2. The compose condition (`changers.size() > 1` on that exact array in this load order) doesn't trip for
   the failing state (single-mod edit → a different merge path that can still keep a stale edge).
3. A distinct base/runtime path (cf. BR-29: block-while-moving shield-drop on a Skyrim.hky-only run may
   be char-property / ER-state, not the multi-mod transition union).

## Ruled out
- **AMR disabled** (Behavior Relay BR-13) — not the cause here; AMR is installed/enabled.
- **Build predates the fix** — not the cause; running latest CB (BR-39 present, both adapters).
- **Served/compiled data byte-faithfulness** — compile byte-identical to vanilla; 96/96 BFCO clip
  generators byte-match Pandora; served motion verbatim.
- **animationdata motion for locomotion** — red herring (engine strips X/Y root translation from
  locomotion anims; planar movement is the SpeedSampled path, cf. BR-1).
- **`.br` project rename / SpeedSampler key** (BR-1) — fixed via byte-substitution serve; data identity
  no longer altered.
- **Clip-name case lowercasing** (BR `86942b11`), **alphabetical project sort** (`41c45014`),
  **split shared StateInfos** (`b78b5a52`), **combo symbol-table desync** (`f673021c`),
  **trigger-interval event ids** (`d37b68df`) — all previously fixed; verify none regressed in CB.
- **adsf clip↔motion binding** — is name-resolved (`ResolveMotionIndices`, first-occurrence-wins);
  sharp edge is duplicate clip names collapsing motions onto the first index — a *secondary* suspect.

## Session findings (2026-09-07)
- **Reusable offline harness:** `havok-core-cli hky-merge-compile <unit> <master.hky> <delta.hky…> -o out
  --schema <Havok>` reproduces the runtime `LoadMerged` for one serve-path unit. Bundles live in
  `<SKYRIM_MODS_FOLDER>/Output_BR/community_behaviors/plugins/` + the master in `Community Behaviors/`.
- **BR-39's exact mechanism is NOT the live cause.** Merging master + BFCO + DMCO + SBF emitted **zero**
  union-collision diagnostics on the attack transitions — compose is handling them (silently). So the
  surviving-stale-transition-edge path isn't what fires in this stack.
- **Commitment mechanism located:** BFCO wraps each attack generator in an `hkbModifierGenerator`
  (`AttackForwardSprint_MG` / `AttackPowerForwardSprint_MG`) whose modifier is a `hkbModifierList`
  (`bfco$921/922/923` = `BFCO_AttackModifierList_norP/sp/spN`, stacking `bfco$931/934/936/941/943/944/962`).
  This is the "state machine wrapped in a modifier."
- **Block gate located:** `BFCO_IsBlockingModf` (`bfco$960`, `BSIsActiveModifier`) writes variable **109
  `BFCO_IsBlocking`** and checks node-active-state — the shield-drop hinge.
- **Binding membrane EXISTS:** CB name-resolves each binding's `variableIndex` from its `variable` name at
  compile (`SchemaBuilder.cpp:107` Stage-4; symbol table `HavokModel.cpp:1454`). So the lead is not "no
  membrane" but whether the **merged variable table order/content** and the **node-id refs inside
  `BSIsActiveModifier`/the commitment modifiers** resolve correctly for these BFCO nodes.

## Session findings (2026-09-07, cont.) — compile-trace built, reference theories ruled out
Built the name-annotated compile-trace (`havok/model/CompileTrace.*`; `hky-merge-compile --trace`).
Traced the merged `1hm_behavior` across the **full 7-layer** stack (master + SBF + BFCO + DMCO +
Precision + TDM + Payload). Result — **both reference-integrity hypotheses are RULED OUT:**
- **Variable-binding desync: ruled out.** Every BFCO variable's table index equals its binding index
  even across all 6 mods (`BFCO_IsBlocking` table 109 == binding 109; `BFCO_AttackSpeed` 107/107;
  `attackPowerStartTime` 26/26). The merge keeps bindings pointing at the right variables.
- **BR-39 transition union: ruled out (live).** Zero union-collision diagnostics on the attack
  transitions across the stack — compose handles them.
⇒ The bug is **structural / behavioral**, not a broken variable or transition reference.

Added a topology tap (generator/modifier/state edges + transitions). It first *looked* like the combo
states had transitions duplicated ~31× (`Bfco_AttackPower123_State` etc.) — but that was a **trace
artifact**: those edges share event+toState+effect and differ only in `toNestedStateId` (combo stages
1..N via `BFCO_NextIsAttackN`), `priority`, `condition`, and `triggerInterval`. Verified against BFCO's
raw Nemesis source (the shared array `#bfco$105`, 80 transitions): CB preserves all **80**, distinct,
with every distinguishing field intact. **Transition-array fidelity is confirmed — RULED OUT.** (The
trace now renders those fields so combo edges no longer look like dupes.)

## New leading hypothesis
The merged graph's internal reference integrity for the BFCO commitment/block modifiers — either a
variable-name that doesn't resolve against the merged variable table (falls back / wrong index), or a
`BSIsActiveModifier` node-id ref that isn't remapped to the merged node — so `BFCO_IsBlocking` (and the
commitment gate) read wrong. One cause fits both symptoms. Decisive test: diff CB's merged `1hm_behavior`
(commitment `hkbModifierGenerator` subtree + the block `BSIsActiveModifier` + the variable table) against
**Pandora's** output for the same unit (source-of-truth).

## Disposition
Reference-integrity is now cleared (variables + transitions merge correctly), so the diagnostic narrows
to **structure/topology**: compare CB's merged `1hm_behavior` against **Pandora's** output (source-of-
truth) on the attack state machine's topology — which generator each combo state routes to (does the
2nd-combo-attack state route to a `*ForwardSprint_MG` modifier-generator like the 1st, or to a bare
generator that omits the commitment modifier?), the `hkbModifierGenerator` wrapping, and the block
sub-graph the shield gate reads. The compile-trace can be extended to also emit node-id references /
generator edges per node (next tap) so this topology diff is greppable too. Needs: the real MO2 load
order of the six 1hm mods + Pandora's converted `1hm_behavior` export.
