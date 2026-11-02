# CB-18: Animation 64's wide slot disagrees with the 16-bit index

**Status:** OPEN
**First seen:** 2026-11-02   **Area:** runtime (Animation 64 on the SDK build)

## Symptom
The log warns that "code outside CB wrote the old field" for many clips. Most read 0 in the 64-bit slot, which an unconverted writer explains. Some read non-zero, such as `MT_WalkForwardRight.hkx` with 16-bit 1103 and 64-bit 855. No visible in-game fault so far.

## Root cause
Unknown. Suspected, unverified: the slot lies past objects allocated at the old size, or a writer the SDK build runs is not converted.

## Ruled out
Nothing yet.

## Disposition
Find who allocates and writes these clips before treating Animation 64 as verified on the SDK.
