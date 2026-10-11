# CB-10: author roster fold dedups by exact case, the engine by lowercase filename

**Status:** OPEN
**First seen:** 2026-10-11   **Area:** runtime (character roster)

## Symptom
Not yet observed — found by code audit. An authored `animationnames` roster line that differs from a base
entry only by case (or only by folder) is APPENDED as a new slot instead of being recognised as present. Its
filename then collides with the base entry: the base slot goes dead and the roster is padded by one, shifting
every later index (the phantom-padding hazard — cf. the female `male\MT_*` locomotion bind).

## Root cause
`Resolver::FoldAuthorRoster` (`src/core/resolve/Resolver.cpp:1408`) unions with an exact-string
`unordered_set`. The engine's LoadClips keys roster slots by lowercase FILENAME (folder ignored) and a later
insert overwrites an earlier one (RE'd in Ghidra: `LoadClips_RosterKey_LowercaseFilenameInPlace`,
`…InsertLastWins`). The converter already dedups case-insensitively (`src/converter/Converter.cpp:238`,
`:1284`), so the two sides disagree; the Resolver's exact-case rationale is refuted by LoadClips folding both
sides.

## Ruled out
—

## Disposition
Dedup the fold by the engine's key (lowercased filename, the converter's `fileKey`), keeping the base
entry's original string. Verify against a roster with a case-only duplicate.
