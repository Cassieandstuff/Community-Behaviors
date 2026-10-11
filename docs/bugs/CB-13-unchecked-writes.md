# CB-13: unit-tree and converter writes fail silently

**Status:** OPEN
**First seen:** 2026-10-11   **Area:** engine + converter (output)

## Symptom
Not yet observed — found by code audit. A write failure (MAX_PATH, a locked file, disk full) while emitting a
`.hky` unit silently drops nodes/rosters from that unit and reports success; a failed motion-reunion rewrite
leaves `animation.yaml` empty or truncated.

## Root cause
- `EmitHkyReflective` and `DeriveHkyPatch` write `graph.yaml`, node files and roster files through temporary
  `std::ofstream(...) << body` with no open/write check, and ignore the `create_directories` error
  (`src/engine/decompile/HavokModel.cpp:930-931`, `:2645-2648`). The served-bytes gate catches this for
  Skyrim.hky; nothing catches it for converter-built mod bundles.
- The converter's motion reunion truncates `animation.yaml` and rewrites it unchecked
  (`src/converter/Converter.cpp:2556`); the bake copy (`:2027`, `:2054`) returns OK even if the source is
  missing.
- Several writers test the stream before the destructor's final flush, so a close-time error is missed
  (the WriteFile copies in AnimationDataServer / AnimationSetDataServer, NemesisSetDataConvert `WriteAll`,
  `CompileAnimationToFile`, SkeletonCompiler).

## Ruled out
- The Resolver's cache writers (flush before checking) and the sentinel-last ordering — those are sound.

## Disposition
Route writes through one `CB::engine::disk::WriteFile` (write, flush, close, check; optional temp+rename for
files a later stage depends on) and propagate failure out of the emitters.
