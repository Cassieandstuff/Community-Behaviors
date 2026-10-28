# CB regen rules: bone data in the character (TODO)

Branch `claude/regen-rules-870c8a` (merged into local `main` at 4e602846, not pushed). Goal: characters with
different rigs share one behavior graph because bone data lives in character properties, resolved per
character, not inline in the behavior.

## Built and gated

| Commit | What |
|---|---|
| 037285ff | `user: <value>` schema marker (game-filled fields); `hkbClipGenerator.animationBindingIndex`, `BSSynchronizedClipGenerator.sAnimationBindingIndex` render `user`. New `tools/engine-tests` (ctest). |
| c03fad3e | `CB_SCHEMA_VERSION` 1.0.0-rc.5 (the `.hky` text form changed; `charprop` below also rides on rc.5). |
| 3af34526 | Rule framework: `kind: rule` descriptors (`schema/metadata/rules/`), `CB::engine::rules` (op registry with Apply + Inverse, unit-set Context, behavior-reference walk, ordered apply / reversed inverse). Regen applies rules after the emit and re-emits changed units; the served-bytes gate reverses them in memory before comparing. |
| 4f7c184e | `charprop: <class>\|bone` schema key on the bone-set pointers and every behavior `boneref`; bound + neutral members write `{ charprop: '<name>' }` (binding folded in, empty binding set omitted). Rule `hoistbonedata` / op `HoistBoneData`. `CharPropViolations` check; strict regen refuses inline bone data. |
| 656391ef | Roster-file entries (`legs.yaml`) parse in their bindable's scope; past-rig bone data keeps positional names. |
| e9a98113 | Converter: mod graphs (Nemesis merges and precompiled loose graphs) run the converter-stage rules against the hoisted base before their patch is derived (`rules::ModGraphRules`); added properties go into the bundle's character patch (one patch per bundle + character, roster and properties together). |

Gates run (scratch, never deployed):
- Full vanilla regen, `--strict`, into `D:\CB-tmp\regen-rules\full`: 2,561 sites moved into 807 new properties,
  0 left inline, served-bytes gate 1566/1566. 19 graphs no character runs keep their inline data.
- Vanilla humanoid subset: 505 sites, 134 properties, gate 24/24; every file the rule does not touch is identical to
  a build without it (node ids stable).
- `--hky-roundtrip --all-kinds` on the humanoid subset without the rule: 24/24.
- `engine-tests` (ctest): `user` emit/parse, rule descriptors / order / cycles, unit-set relations, re-emit
  fidelity, a reversible test op through apply → re-emit → reverse, the hoist over two characters with differently
  ordered rigs, and a mod graph through the converter path against a packed hoisted base.

## Left

1. **Nothing verified in game.** The hoisted Skyrim.hky has never been served to the engine. Check that character-
   property bindings resolve on every rig (the claim rests on `hkbVariableBindingSet_CopyVariableOrMember`
   0x140b048e0), and that INT32 bone-index properties bound to int16 members behave.
2. **Converter path untested on a real load order.** It needs the hoisted Skyrim.hky installed as the base and the
   output pointed at a scratch folder. The top level has decided the converter retires in favour of BK: the
   forward rules (`ModGraphRules`) then belong in BK's import.
3. **Graphs a mod adds** (whole new units the converter emits) are not hoisted: no base character reaches them.
4. **Re-pointed vanilla bindings:** where the base regen re-bound a vanilla-bound site to a new property (a runner
   lacked the preset and the run had already given it one with another fallback), a mod graph's copy of that site
   binds the vanilla preset again. Not counted yet; count rebinds in the op summary first.
5. **Name clashes inside one mod:** two graphs of one bundle minting the same name for different data fail that
   character patch (logged).
6. **Bone-keyed rosters** (`BSLookAtModifier.bones` / `eyeBones`, `hkbKeyframeBonesModifier.keyframeInfo`): once their
   key bone is hoisted (-1) they write as plain lists, so a mod patch replaces the whole list instead of entries.
   Open design question: key them by the property instead.
7. **Character units render bone properties numerically** (`'Bone_NPC_Head_Head': {type: VARIABLE_TYPE_INT32, value: 36}`);
   rendering the bone name needs a character-side marker.
8. `hkbTwistModifier.boneChainIndices` / `parentBoneIndices` are left alone (engine-filled, empty in vanilla, and an
   `hkArray<int16>` member cannot bind a property).

## CLAUDE.md additions awaiting approval

Layout, under `tools/` (append to its list):

```
                             ... engine-tests (ctest: unit tests over the engine lib, run against the source schema/) ...
```

i.e. the `tools/` line becomes:

```
tools/                       dev-only, never shipped: hky-tool, cbdiff, cbmatch (byte-matches havok/ against
                             the exe), engine-tests (ctest unit tests over the engine lib), x64dbg-autoattach,
                             blender-control-rig. ...
```

Namespacing, the engine purpose list gains `rules`:

```
- **`<purpose>` is a role word**, the concern the code serves: `discover resolve serve bootstrap debug
  conditions` (core) · `hooks relay` (platform) · `compile decompile codec merge schema rules` + `skeleton anim
  animdata animsetdata linker` + `text disk exec` + `locomotion` (engine). `rules` = reversible transforms over a
  whole unit set, declared as `kind: rule` data and run by registered ops (regen applies + gate reverses; the
  converter applies forward).
```

Layout, `schema/` paragraph: `metadata/` also holds `rules/` (`kind: rule` descriptors).

## Resume

- Build without deploying: `cmake --preset release -B build/rules -DBUILDER_DEPLOY=OFF`, then
  `cmake --build build/rules --config RelWithDebInfo --target engine-tests behavior-converter hky-tool`.
- Tests: `ctest -C RelWithDebInfo` in `build/rules` (or `build/rules/bin/RelWithDebInfo/engine-tests.exe schema`).
- Scratch regen: `BehaviorConverter.exe --regen-master "D:\CB Regen Source\Skyrim.hky" D:\CB-tmp\regen-rules\full\Skyrim.hky
  --strict --keep-unpacked D:\CB-tmp\regen-rules\full\keep` (about 3 minutes). Small subsets with real rigs live in
  `D:\CB-tmp\regen-rules\{chicken,human}`.
- Code: `src/engine/rules/` (Rules.h framework, HoistBoneData.h op), `src/engine/decompile/HavokModel.cpp`
  (charprop fold: `foldFor`, `charPropValue`, `appendCharPropBindings`), `src/converter/Converter.cpp` (regen hook,
  gate, `ModGraphRules` use, `FlushRosters`), `schema/metadata/rules/hoistbonedata.yaml`.
- Next step by priority: an in-game check of a hoisted Skyrim.hky (item 1), then the BK-import home for the forward
  rules (item 2).
