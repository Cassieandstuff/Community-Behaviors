# CB-3: shield lowers while moving with block held

**Status:** INVESTIGATING — **all compiled content exonerated (2026-09-13); cause is the SERVE/DELIVERY layer**
**First seen:** 2026-09-11   **Area:** serve (byteserve / project-redirect / above-OAR delivery)

## ⚠️ 2026-09-13 UPDATE — the boneWeights theory below is SUPERSEDED

A decisive `tree-diff` sweep (CB cache vs Pandora output vs true-vanilla 1170) this session **rules out
every compiled artifact CB serves**:

- **Behaviors** — served Pandora's **byte-identical** `weapequip` + the other 5 patched graphs + Pandora's
  `DefaultMale`/`DefaultFemale` via CB's cache (warm path, verified md5-match, no regen). **Shield still
  dropped.** Pandora's own bytes, delivered by CB, drop the shield → it is **not the graph content**, so it
  is not the boneWeights placement and **the Pandora-compat shim does not fix this bug**. (CB's cache
  `weapequip` boneWeights already equalled Pandora's — the only CB↔Pandora graph delta left was cosmetic
  `characterPropertyInfos` role-flag values, which CB matches *vanilla* on; Pandora is the outlier.)
- **Skeleton** — CB's served `skeleton.hkx` is **byte-identical** to XPMSSE (127 records, 0 diff). Ruled out.
- **Character** — CB ≈ Pandora (only the appended-mod-anim roster tail at idx 1656+ differs). Bone masks /
  IK drivers identical. Ruled out.
- **animdata** — CB == vanilla on all 17322 shared records (+80 additive). **setdata** — CB serves vanilla,
  == Pandora. Ruled out.
- **Native animations** — CB has **no active animation serve hook**; `meshes/CBanims/` is staged-only, not
  delivered. Ruled out.

**Conclusion:** everything CB *compiles* reproduces Pandora/vanilla, yet CB drops the shield and Pandora
(loose files) does not. The differentiator is **how CB delivers the tree** — byteserve (Func3 byte-swap) +
the synthesized `.br.hkx` project redirect, above OAR — not any file's content. Next session starts here:
instrument the serve path, compare CB's above-OAR whole-tree delivery vs Pandora's loose-file load for the
locomotion+shield blend at runtime (does an OAR/DAR movement replacement that supplies the shield-arm pose
fail to reach CB's served tree?). All diff outputs saved under `D:\cb-diffs\` (see `SHIELD-HUNT-FINDINGS.md`).

Everything below is the 2026-09-11 investigation, retained for history — its *root cause is disproven* as
the shield cause, though the boneWeights-placement difference it documents is real (and the shim that
matches Pandora's placement is a valid converter-parity change, just not this bug's fix).

---

## Symptom
With BFCO + Dodge MCO-DXP enabled, holding block **while moving** lowers the shield (the shield
arm goes slack during locomotion). Only under Community Behaviors — Pandora, and vanilla, keep the
shield up. Bundled with the CB-2 ice-skate originally, but a *separate* root cause: CB-2's two fixes
(binding-removal in mergeLayers, expression eventMode in the converter) did not resolve it.

## Root cause
**Corrected 2026-09-11 — the earlier "CB lands the mod edit on the WRONG bone" premise is FALSE.**
Tool-authoritative, name-keyed diff (`tree-diff` on the patched, Weapon-Styles-merged weapequip; bone
names resolved against the character `bonelist.yaml`) shows CB places the mod's weights on the
*anatomically correct* bones. The defect is a **positional 98-vs-99 length/alignment ambiguity** that
CB and Pandora resolve differently in one local window — not a missing membrane on CB's side.

Instrument: `Weapon Styles - DrawSheathe`'s `draws/weapequip/#0109.txt` — an `hkbBoneWeightArray` of
**98** elements that replaces child1 (`1HM_Locomotion_BFR`) of the `1HMEquip_LocomotionBlend` blender
(child0 = `Weap_EquipBehavior` draw pose). Parsed against vanilla names, the mod's edit is coherent:
it zeros the **entire left arm** off locomotion — `L Clavicle, L UpperArm, L Forearm, L Hand, Shield,
L Pauldron, both L ForearmTwists, both L UpperarmTwists, every left finger, AnimObjectL` (idx 42 =
`Shield` anchors the array to the vanilla order). Its four twist-block zeros are idx **52,53,54,55**
= `L ForearmTwist1/2` + `L UpperarmTwist1/2`.

Patched result, both sides diffed through the SAME character bonelist — CB and Pandora agree on **66 of
68** zeroed bones; the *only* divergence is a local transposition of the twist window:

| bone | mod intent | CB (patched) | Pandora (patched) |
|---|---|---|---|
| `NPC L Toe0` (50) | keep loco | **loco** ✓ | zeroed ✗ |
| `NPC R Toe0` (51) | keep loco | **loco** ✓ | zeroed ✗ |
| `NPC L ForearmTwist1/2` (52,53) | zero | zeroed ✓ | zeroed ✓ |
| `NPC L UpperarmTwist1/2` (54,55) | zero | **zeroed** ✓ | loco ✗ |

CB zeros exactly the mod's intended {52,53,54,55}; Pandora zeros {50,51,52,53} — the same 4-wide block
shifted **2 earlier**, sparing the UpperarmTwists and hitting the toes instead. In-game (user-confirmed):
zeroing the shield-arm UpperarmTwists in the locomotion child starves the shield-arm twist while
moving+blocking → **shield drops**. Pandora's shift spares them → shield stays up. So CB is *byte-faithful
to the mod*, and the mod's literal intent is what drops the shield; Pandora's local misalignment
accidentally-or-conventionally avoids it. The parity/ground-truth target is Pandora (shield up).

The seed is the **98-element mod array vs 99-slot compiled boneWeights**: overlaying 98 authored weights
onto 99 slots is ambiguous, and CB (align-from-0) vs Nemesis/Pandora resolve the twist window 2 apart.
A NAME-anchored array removes the ambiguity entirely — each weight binds to its named bone and the 98/99
count stops mattering — which is why the bone-name membrane remains the fix, now for a sharper reason.

## Ruled out
- **XPMSSE clobbering the core bone index** — DISPROVEN with the shipped `XPMSSE.hky`'s own
  `skeleton/character/bonelist.yaml`: it defines ONLY the 27 *added* bones (Items, Wings, Belly, Anus…)
  as an append list at indices **99+**; its header comment states append-only intent. Core 0–98 is
  untouched, so it cannot shift the 50–55 window.
- **Master regen permuting the twist/toe block** — DISPROVEN independently of CB: the XPMSSE
  `skeleton.hkx` *binary* string order (47 `L MagicNode` → 48 `R MagicNode` → 49 `Head MagicNode` →
  50 `L Toe0` → 51 `R Toe0` → 52 `L ForearmTwist1` → 53 `L ForearmTwist2` → 54 `L UpperarmTwist1` →
  55 `L UpperarmTwist2` → 56 `R ForearmTwist1`) is byte-identical to CB's `bonelist.yaml`. CB's compiled
  order is faithful to vanilla; the regen did not scramble it.
- **"CB lands the edit on the wrong bone"** (the original premise) — DISPROVEN: CB's placement matches
  the mod's literal intent bone-for-bone (66/68 agree with Pandora; the 2 CB-only zeros are the exact
  bones the mod names). Pandora is the side that transposes.
- **The CB-2 roots** (mergeLayers binding-removal, hkbExpressionData eventMode) — both fixed; shield-drop
  persisted. Separate bug. [[cb2-ordered-union-arrays]]

## Mechanism (root cause — SOLVED)
Not a skeleton/membrane gap at all. It is how the Nemesis `#0109` patch's array edit is *placed*.
Pandora applies a Nemesis edit to a numeric text-array param (`PackFileEditor.ReplaceText`) as an
**occurrence-counted text replacement**, not a positional element edit: the block's ORIGINAL text
becomes a whitespace-flexible regex, and Pandora replaces the **Nth non-overlapping** match of it in
the array, where N = how many times ORIGINAL already occurs in the (prior-edits-applied) text before
the block. When the edited window lies inside a longer run of identical values, that non-overlapping
match snaps to the run's *start*, landing the edit early.

`#0109` zeros a 4-wide `1 1 1 1` block authored at elements **52–55** (`L ForearmTwist1/2` +
`L UpperarmTwist1/2`), sitting inside vanilla's 6-long run of ones at 50–55 (toes + both twist pairs).
- **CB** applies it by element index → zeros **52–55** (faithful to the mod's bytes) → the shield-arm
  UpperarmTwists lose locomotion → shield drops while moving+blocking.
- **Pandora** occurrence-counts → the Nth `1 1 1 1` match snaps to element **50**, zeroing **50–53**
  (toes + forearm twists) and sparing the UpperarmTwists → shield stays up.

Both were confirmed against the compiled outputs (name-keyed `tree-diff`), and Pandora's placement was
reproduced from first principles (occurrence index 6 → element 50). It is a deterministic **Pandora/Nemesis
bug**, not intended handling — there is no malformed-array logic in Pandora's source; the shift is an
emergent artifact of the occurrence-count + non-overlapping regex. But it is the behavior the entire
Nemesis/Pandora-authored mod corpus was validated against.

## Disposition — Pandora-compat shim in the converter (built, unit-verified; pending in-game)
The fix is **not** the bone membrane (CB's skeleton order is correct-to-vanilla; the weights are placed
exactly where the mod's bytes say). It is a **converter-only compatibility shim** that reproduces
Pandora's occurrence-counted placement so converted bundles match the ecosystem:
- `src/havok-framing/havok/compat/PandoraCompatShim.{h,cpp}` — `havok::compat::ApplyNemesisTextArrayEdits`,
  a faithful port of `NemesisParser.ParseReplaceEdit` + `PackFileEditor.ReplaceText` (regex-with-`\s*`,
  count in the mutated prefix, replace the Nth non-overlapping match). Unit-verified: reproduces Pandora's
  compiled `weapequip` child1 boneWeights **bit-for-bit** (all 98 elements).
- Wired at the two CONVERTER patch-application sites, immediately before the existing positional strip:
  `HavokModel.cpp` `parseSourcesMerged` (the schema-native delta path) and `PatchConverter.cpp`
  `ConvertPatch`. Element/ref params and INSERT-only blocks are left to `xml::StripPatchOriginals`.
- **Compiler untouched**: the shim is only *called* from offline ingest; the runtime Resolver/compiler
  consumes pre-baked `.hky` and never applies raw Nemesis text (boundary confirmed).

Remaining: build converter → reconvert the load order → confirm shield stays up in-game, then this bug
leaves (delete file + index line in the fix commit).

## Disposition
Build the bone-name membrane — the "fourth roster", sibling to variables/events/anim-names, sourced from
the skeleton's per-bone yamls (`skeleton.hkx/bones/*.yaml`), never from the optional `index.yaml`/`bonelist`
compat shim directly (a fully CB-native creature has no shim; the order is hierarchy-derivable):
1. **Decompiler renders bone names** for bone-ref fields. DONE for `boneWeights` (BehaviorDecompiler.cpp
   `boneWeightsBlock` — `byBone:` name-keyed map when a skeleton is set, raw `values:` otherwise);
   `boneRef` already named the bone-*index* fields. This also makes the tree-diff legible (`byBone.'NPC L
   UpperarmTwist1'` instead of "position 54") — the diagnostic that cracked this bug.
2. **Converter** interprets an incoming mod's raw-positional `boneWeights` against the reference skeleton →
   names (so the bundle is skeleton-agnostic, carrying names not positions).
3. **A `ResolveBoneBindings` pass** at the compile boundary (sibling to `ResolveBehaviorBindings`) re-lands
   names → the FINAL compiled skeleton index, via a per-bone membrane spawned from the bone yamls. One
   shared order-derivation used by both the skeleton emit and this pass, so decompile↔recompile round-trips
   (byte-gate holds: a no-skeleton decompile is unchanged).

Evidence + full design discussion in the session; tool: `tree-diff <A.hkx> <B.hkx> --domain behavior
--skeleton <skel.hkx>` now names the weights. [[tree-diff-semantic-diff-tool]]
