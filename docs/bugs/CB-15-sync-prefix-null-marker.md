# CB-15: converted Nemesis mods break paired animations (sync role prefix set to `&#9216;`)

**Status:** INVESTIGATING (converter fix landed, bundles reconverted; in-game check together with CB-16)
**First seen:** 2026-10-25   **Area:** converter (Nemesis/Havok-XML ingest)

## Symptom
Paired animations whose lead side has an empty role prefix fail with a converted Nemesis mod in the load order.
Seen: mounting a horse (the rider jitters in place and never mounts); reported: the paired animation in the
intro (Ralof/Hadvar cutting the player's bonds, `pa_OffsetBoundStandingCut`). Likely since 0.5.0, when the
converter began deriving field patches against the reflective master.

## Root cause
Havok's XML writer spells a null string `&#9216;` (U+2400). `xml::detail::appendDecoded`
(`src/engine/common/Xml.h`) decoded only the five named entities, and `fillObject`
(`src/engine/decompile/HavokModel.cpp`) took a string `<hkparam>` verbatim, so an unset
`BSSynchronizedClipGenerator.syncAnimPrefix` read as the 7-character string `&#9216;`. Against the master's `''`
that is a change, and the converter emitted a field patch `SyncAnimPrefix: '&#9216;'` for every empty-prefix
sync clip a mod's patch file carries whole. At runtime the clip then binds and samples its role by a prefix no
animation track has.

In the Dragon Break load order: Precision.hky and Payload Interpreter.hky carry 157 such patches each: 139 in
`1hm_behavior`, 17 in `0_master` (HorseEnter `0001x06cd`, dismount, dragon mount, killmoves …), 1 in
`mt_behavior` (`0001x1136`, pa_OffsetBoundStandingCut). No other converted bundle has the marker.

## Ruled out
- adsf: every paired clip's record and motion match vanilla (half-float rounding aside); the `Mount` record's
  extra triggers are `0_master`'s own graph triggers (dup-name pick), not the cause.
- Rosters: `Paired_Mount.hkx` / `Paired_OffsetBoundStandingCut.hkx` are in the served defaultmale,
  defaultfemale and horse characters.
- Set data: CB serves none in that load order; the engine reads vanilla's.
- The Advanced Controller's ground states and planes: handing the player to vanilla under `bIsSynced` /
  `bAnimationDriven` did not stop the mount jitter.
- The jitter that stayed after the converter fix is [CB-16](CB-16-sync-mark-default-prefix.md), the same
  failure through another path. As of 2026-10-26 no bundle in the Dragon Break profile carries `&#9216;`
  (Output_Community Behaviors reconverted; Payload Interpreter is disabled and ships no bundle).

## Disposition
Fixed in the converter: numeric character references decode, and the U+2400 null marker reads as `''` for
string and string-array fields. Existing bundles keep the bad patches until reconverted. Close with CB-16,
once a build with its fix mounts a horse and plays the intro's bonds cut in game.
