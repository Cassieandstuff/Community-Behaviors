# CB-9: new set-data sets/projects are served with lowercased names

**Status:** OPEN
**First seen:** 2026-10-11   **Area:** converter + runtime (setdata serve)

## Symptom
Not yet observed in game — found by code audit. A mod that ADDS a set to a vanilla project (e.g. a Nemesis
set file `BFCO_X.txt`) is served in the merged animationsetdatasinglefile as `bfco_x.txt`; a mod that adds a
NEW project (a custom creature) gets an all-lowercase header (`mycreaturedata\mycreature.txt`). Edits to
EXISTING vanilla projects/sets are unaffected.

## Root cause
Original case is lost twice on the way from Nemesis input to the served file:
- The converter writes the split set files with lowercased names:
  `src/converter/convert/NemesisSetDataConvert.cpp:179` (project dir) and `:181` (set file name).
- The server rebuilds the asdsf header and every set name from bundle LISTINGS, not from file content:
  `src/core/serve/AnimationSetDataServer.cpp:130` (`proj.header = dirName + …`), `:138` (`set.name = fn`).
  Packed bundles list lowercase (HkyArchive keys are folded; `filesUnderOrig` exists but isn't used here),
  so even a hand-authored, correctly-cased bundle loses case once packed. Same pattern for `movesets/*.yaml`
  (`:265` → `ParseMovesetsYaml` builds the header from the file stem, `AnimSetDataYaml.cpp:100`).
- `MergeInto` matches projects/sets case-insensitively and keeps the BASE string, which is why vanilla
  projects/sets survive and only additions carry the folded name.

## Ruled out
- The setdata path CRC lowercasing (`codec/crc/Crc.cpp`) — that is the engine's own hash rule (verified vs
  vanilla), not this bug.

## Disposition
Unverified whether the engine's lookup of a set/project by name is case-sensitive (the server comment at
`AnimationSetDataServer.cpp` ~518 says the singleton is keyed by the header's file stem). Per the CLAUDE.md
case directive, fix regardless: list with original case (`filesUnderOrig` / an original-case
`subdirs`/`files`), and have the converter write original-case names.
