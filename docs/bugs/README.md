# Bug log — Community Behaviors

The authoritative, checked-in record of what's **currently broken**. One file per open bug; this file
is the thin index (one line each). Kept lean on purpose:

- **Only OPEN / INVESTIGATING bugs live here.** When a bug is fixed, it **leaves** — delete its file and
  its index line in the *same commit* as the fix, and let that commit's message + ref be the record
  (git history is the archive; a fixed bug does not accumulate here). Don't keep a "Fixed" section.
- **One file per bug**, `docs/bugs/CB-<n>-<slug>.md`, using the template below. The file is the detail;
  this index is just the pointer.
- **Sync is mandatory, in the same change as the code:** find a bug → add its file + index line;
  a finding shifts a lead → edit the file; fix it → remove both.

This replaces the old monolithic `bugs/README.md` model, whose sprawl was mostly fixed-bug sediment.

## Open

- [CB-1](CB-1-converter-tagfile-unsupported.md) — INVESTIGATING (deferred): converter can't read Havok
  **tagfile** format; a few CreationClub assets ship as tagfiles and are skipped by the master build.
- [CB-3](CB-3-shield-drop-boneweights.md) — INVESTIGATING: shield lowers while moving with block held.
  **2026-09-13: all compiled content exonerated** — serving Pandora's byte-identical behaviors + character
  via CB still drops it; skeleton/animdata/setdata match; native anims not served. Cause is the **serve/
  delivery layer**: byteserve byte-swaps every owned open (project, character, behaviors) to its
  community_behaviors_cache twin under the vanilla identity (no `.br` rename), loading the whole tree above
  OAR. Not graph content. (Earlier boneWeights-placement root cause is superseded — see the doc's top section.)
- [CB-4](CB-4-dragon-footik-legs-gains-lost.md) — OPEN: dragon's `hkbFootIkModifier` loses `legs`/`gains` in the
  .hky (EmitHky's generic modifier path drops struct/struct-array fields).
- [CB-5](CB-5-spriggan-istate-statedata-lost.md) — OPEN: spriggan's `BSIStateManagerModifier` loses `stateData`
  in the .hky (same root cause as CB-4).
- [CB-6](CB-6-existing-snippet-truncates-roster.md) — OPEN: one mod's Nemesis `numelements="existing"` array snippet
  REPLACES the whole array when it's the only changer (trflt alone truncates 0_master's events 1217 -> 7).

## Per-bug file template

```markdown
# CB-<n>: <one-line title>

**Status:** OPEN | INVESTIGATING | (deferred)
**First seen:** <YYYY-MM-DD>   **Area:** <converter | runtime | schema | …>

## Symptom
What is observed (the error, the in-game effect).

## Root cause
What's actually wrong, once known. `file:line`.

## Ruled out
Leads checked and eliminated (so they're not re-checked).

## Disposition
Fix plan, or why it's deferred / won't-fix-for-now.
```
