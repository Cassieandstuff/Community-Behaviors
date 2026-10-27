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
- [CB-7](CB-7-fnis-not-on-formid-format.md) — OPEN: FNIS conversion isn't on the FormID .hky format yet; a load
  order with FNIS lists skips them (the converter logs `FNIS: FAILED`).
- [CB-9](CB-9-setdata-names-lowercased.md) — INVESTIGATING (fix landed, unexercised): set-data sets/projects a
  mod ADDS were served lowercased; listings and the converter now keep original case.
- [CB-11](CB-11-cache-file-case.md) — INVESTIGATING (fix landed, needs an in-game check): cache files were
  written lowercase but swapped open with the request's raw case; every swap now names the exact file written.
- [CB-14](CB-14-fall-animation-lingers.md) — OPEN (deferred): with the ground states on, the fall animation
  sometimes lingers up to 2 s after a landing; the controller lands correctly, the graph side is suspected.
- [CB-15](CB-15-sync-prefix-null-marker.md) — INVESTIGATING (converter fix landed, bundles reconverted):
  converted Nemesis mods patched every empty sync role prefix to `&#9216;`, breaking paired animations (horse
  mount, the intro's bonds cut).
- [CB-16](CB-16-sync-mark-default-prefix.md) — INVESTIGATING (fix landed, needs an in-game check): Animation64's
  sync-mark reader read an unset role prefix as `""` instead of the exe's `"NPC"`, so the game rejected every
  paired idle with an unprefixed role (the horse mount jitter).

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
