# TODO: MetaHuman faces (RigLogic) — CB side

Status 2026-10-28: designed with the user; **no face runtime code is built**. The full design, licence limits and
open questions are in Behavior Kit's `docs/todo/riglogic-faces.md` (branch `claude/riglogic-faces-320bbe`). This file
holds only what lands in CB. Delete it when the work lands (repo policy: no design docs survive their work).

## Decided (user, 2026-10-27/28)

- RigLogic (openriglogic, MIT) runs **in game in CB**, driven by Skyrim's own final face channels
  (`BSFaceGenAnimationData` expression/modifier/phoneme keyframes) mapped to MetaHuman raw controls by an authored
  table that BK edits.
- Face bones are **not** in the default skeleton: CB adds them to the character like the Khajiit tail, parented to
  `NPC Head [Head]`.
- The face character (`meshes/actors/character/characters/facebones.hkx`, rig
  `character assets/facebones.hkx`) **shares** `Behaviors\0_Master.hkx`; that relies on `hoistbonedata`
  (see `cb-regen-rules.md`).
- Per-NPC DNAs replace FaceGen NIFs: `meshes/actors/character/facegendata/facegeom/<plugin>/<formid>.dna`, read
  through the game's file system; Skyrim axes, translations in cm, scaled by 0.7 on load; only the Definition +
  Behavior layers are needed at runtime. If no DNA exists, the vanilla NIF is used.

## To build in CB

1. **Face attachment:** create the face NiNodes under `NPC Head [Head]` from the DNA joint list and neutral pose, then
   rebind head parts by name with `BSFaceGenNiNode::FixSkinInstances` (vfunc 0x3E, AE 0x140432280; unresolved names
   keep the head NIF's own static bones).
2. **Face graph holder:** a holder joined to the actor's `BSAnimationGraphManager` as a sub-manager, like
   `TailAnimationGraphManagerHolder` (biped graph setup AE 0x1402187a0; attach/detach 0x14054f230 / 0x14054f2e0;
   events forwarded to sub-managers 0x140ba4bd0; parent channels bound 0x140ba5d10). Vanilla only does this for the
   tail slot, so CB creates the holder itself.
3. **RigLogic node:** a CB graph node that reads the face channels, runs `rl4::RigLogic::calculate`, and writes DNA
   neutral + RigLogic delta as each face bone's full local transform (so per-NPC neutrals don't depend on the rig's
   single reference pose).
4. **Head build from DNA:** base head NIF (shaders, textures, UVs) + the per-NPC DNA's vertex data and skin; skin bind
   from the DNA neutral.
5. **Stale-DNA check:** compare a hash of the winning facegeom NIF (written by BK into the DNA descriptor metadata)
   at load; on mismatch use the vanilla NIF and warn.
6. **Distance gating** like vanilla: expressions/modifiers within `fLodDistance:LOD` (500), phonemes within
   `fTalkingDistance:LOD` (2000) (vanilla face morph job 0x140432550).

## Rule findings (against `cb-regen-rules.md`)

Found while pointing the scratch face character at 0_master (`D:\CB-tmp\facebones\`, merge-check 4/4 OK against the
rule regen `D:\CB-tmp\regen-rules\full\Skyrim.hky`). Not yet sent to the user's regen-rules session; the user has not
decided whether to.

- **A character a bundle adds to a vanilla graph** gets none of the hoisted properties. 0_master's tree binds 170
  (63 masks default 1, 84 default 0, 5 bone sets, 16 bone refs, IsThirdPerson, IsMale); facebones declares none, so
  every binding keeps the behavior's neutral value instead of each mask's own default. Different from
  `cb-regen-rules.md` item 3 (graphs a mod adds). Belongs with the forward rules wherever they land (BK import, or
  CB at load).
- Single bone refs render numerically in characters: `cb-regen-rules.md` item 7.
- Differences from the decided design: moved fields render `{ charprop: '<name>' }` (decided: `user`), and new names
  carry an extra ordinal (`MT_Jump_SprintBlend_0_2`; decided `{node}_{child}` = `MT_Jump_SprintBlend_0`).
- Animations still bind by index per file; the character project folder now binds male, female and face rigs, so
  body clips log "ambiguous" (still resolved against the female rig) and a face clip there would bind a body rig.
  Candidate: bind an animation to the characters whose `animationNames` list it before the project-folder fallback.
