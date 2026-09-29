# CB-6: a single mod's `numelements="existing"` array snippet replaces the whole array

**Status:** OPEN
**First seen:** 2026-09-28   **Area:** converter (MergeTagfiles / BashMerge `decideParam`)

## Symptom

`dragon test animation patch` (Nemesis code `trflt`) patches `0_master` `#0106` (hkbBehaviorGraphStringData) with a
Nemesis snippet: `eventNames numelements="existing"`, where six trailing vanilla names are context lines and one new
name (`CF_PlayHumanTestAnim`) sits inside `MOD_CODE`. `#0108` does the same for `eventInfos`.

Merged ALONE (`MergeTagfiles(base, trflt)`, the path `ConvertModDelta` takes per mod), 0_master's event roster
becomes those **7 names** (vanilla has 1217) with **3 eventInfos**. Every event id in the graph now names the wrong
event, e.g. clip trigger id 0/1 = `QSTDLC2BoatArrivalCreakB04`/`C01` instead of `FootLeft`/`FootRight`.
Merged with other mods that also touch the array, the union path hides it (the all-mods merge keeps FootLeft at 0).

Found by `BehaviorConverter --multilayer-roundtrip` with `CB_MULTILAYER_ONLY=evfmgo,trflt CB_MULTILAYER_TRACE=1`
(the `one_trflt/` dump).

## Root cause

`CB::core::merge::decideParam` (`Havok/core/cpp/interface/BashMerge.h`): an array changed by exactly ONE layer is
`ReplaceArray` ("the mod's array is its complete intended value"). A `numelements="existing"` array is NOT complete —
it is context lines plus additions, positioned by matching the context against the base. After
`StripPatchOriginals` the snippet is taken verbatim as the whole array.

## Ruled out

- Context mismatch: the six context names ARE vanilla's roster tail (indices 1211-1216).

## Disposition

Converter-side fix: anchor an `existing` snippet against the base array (context lines locate it; the `MOD_CODE`
lines are inserted there) before `decideParam` sees it. In-game impact of an already-converted bundle unverified.
A name-keyed vocab record can't express this (rosters only union by name), but a field patch DERIVED from the
corrupt single-mod merge inherits it.
