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

## Disposition
Diagnostic (source-of-truth, per CLAUDE.md — external tools / Pandora XML, NOT CB's own decompiler as
the lens): identify the **state machine wrapped in a modifier** that gates 2nd-attack commitment (and
the block state), then diff **CB's merged `1hm_behavior` vs Pandora's output** on THAT node —
its transitions, its variable bindings, and the wrapping modifier — not just AttackState/BlockState.
Confirm whether compose fires for that array in the multi-mod load order (`changers.size() > 1`).
Prime lead: the modifier-wrapped SM's transitions/variables diverge from Pandora, or fall through the
single-changer merge path. Secondary: duplicate-clip-name motion collision.
