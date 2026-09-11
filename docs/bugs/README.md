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
