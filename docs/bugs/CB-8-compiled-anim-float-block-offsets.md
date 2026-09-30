# CB-8: compiled animations with float tracks have no floatBlockOffsets

**Status:** OPEN (latent)
**First seen:** 2026-09-30   **Area:** compile (animation)

## Symptom
None observed yet. A clip compiled by CB and SERVED to the engine that has float tracks (annotation-driven
morph/float channels) would read its float data from garbage.

## Root cause
The game reads a block's float-track data at `blockOffsets[b] + floatBlockOffsets[b]` (Ghidra FUN_140c33100).
`CompressedResult` (`Havok/core/cpp/codec/spline/SplineCompressor.h:18`) carries only `blockOffsets`, and
`Havok/core/cpp/compile/AnimationCompiler.cpp:59` leaves `floatBlockOffsets` empty, so the engine indexes past
an empty array. The compressor already writes float tracks inline after each block's transform tracks
(4-byte aligned, mask 0x12 / 0x03), so the offset just isn't recorded.

## Ruled out
- The decompressor side: float tracks decode correctly (the 4-byte align + mask-derived quantization fix,
  which unblocked the 10 vanilla clips the round-trip used to skip).

## Disposition
Record each block's float-section start (relative to its block) in the compressor, return it on
`CompressedResult`, and write it to `floatBlockOffsets` in `AnimationCompiler`. Gate: compile a vanilla clip with
float tracks and byte-compare the offsets against vanilla. Matters once compiled animations are served (the
anim-decompile ungate in the staged release).
