# CB-12: UTF-8 BOM breaks packed units and line-based text files

**Status:** OPEN
**First seen:** 2026-10-11   **Area:** engine + runtime + converter (text input)

## Symptom
Not yet observed — found by code audit. A unit YAML saved with a UTF-8 BOM works while its bundle is loose
and breaks once the bundle is packed. A BOM'd `animationnames/*.txt` injects `"\xEF\xBB\xBFAnimations\…"`
as a roster entry (binds nothing, pads the roster); a BOM'd `loadorder.txt` mis-keys its first line.

## Root cause
- `IUnitSource::read` promises "A UTF-8 BOM, if present, is stripped" (`src/engine/interface/UnitSource.h:22`).
  `DiskUnitSource` does; `ZipUnitSource::read` returns the archive bytes raw
  (`src/engine/codec/serialization/HkyArchive.cpp:46`), and neither packer strips BOMs.
- No line reader strips a BOM: the Resolver's roster readers, every `loadorder.txt` reader (Resolver,
  AnimationDataServer, AnimationSetDataServer, LoadOrderUI), the converter's roster-drop reader.
- Also inconsistent: `SkeletonYaml`'s reader and the schema loader don't strip it; AnimationYamlLoader and
  UnitSource do.

## Ruled out
—

## Disposition
Fold into the shared text/disk helpers (`CB::engine::text` / `CB::engine::disk`): one `StripBom`, applied by
`ZipUnitSource::read` (honouring the contract) and by the shared line splitter; `--metatext-roundtrip`'s raw
read must stay byte-exact.
