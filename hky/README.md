# `hky/` — source YAML trees for mutable BR bundles

This folder holds the **uncompressed, git-tracked YAML tree** for every Behavior Relay bundle
— including the vanilla master. Each `<Bundle>.hky/` subdirectory is a bundle authored in-source;
the build compresses it into a single-file `<Bundle>.hky` (a deflate zip — the same layout the
runtime `HkyArchive` reads) that ships in the mod's `behavior_relay/plugins/` folder.

## Every bundle packs from source YAML at build — including `Skyrim.hky`

There is no frozen checked-in blob anymore. The vanilla master (`Skyrim.hky`, the "Skyrim.esm of
behaviors" every native bundle deltas against) lives ENTIRELY as its source tree (`hky/Skyrim.hky/`)
and packs like any other bundle; its node ids are baked into that YAML (behaviors numbered by the
tagfile `#NNNN` oracle) so per-mod deltas merge against them. (Historically it was a pre-packed
artifact rebuilt via `BehaviorConverter --build-base`; that binary is gone — edit the YAML tree.)

`Behavior Relay.hky` is a manifest-only anchor for now (it masters `Skyrim`); it holds no shared
behavior graph. Feature content lives in the feature's OWN bundle — the FNIS converter emits
`FNIS.hky` (masters `Skyrim`; a `0_master` delta whose FNIS state routes straight to its
`FNIS.hkx`), and Engine Relay ships its relay hub in its own `enginerelay.hky`. There is
deliberately no shared intermediate relay: FNIS goes `0_master -> FNIS.hkx` directly; the relay is
an ER concern, packaged by ER. Bundles reach into vanilla by NAME via the merge's ownership
inversion (`parents:` inserts a state, `entryTransitions:` inserts the wildcards that enter it), so
a bundle never edits the id-keyed vanilla node.

## Convention

```
src/SKSE/Behavior Relay/hky/<Bundle>.hky/        <- authored tree (this folder)
  manifest.json                                  <- identity + declared masters
  meshes/actors/.../<graph>.hkx/                 <- graph units (behavior.yaml + node dirs)
  animationnames/<character>.txt                 <- optional roster drops
        │
        │  build: havok-core-cli hky-pack "<Bundle>.hky" -o <build>/packed-hky/behavior_relay/plugins/<Bundle>.hky
        ▼
<mod>/behavior_relay/plugins/<Bundle>.hky        <- packed, shipped beside the other bundles
```

The packed `.hky` is a **build artifact** — it lands under the BUILD dir
(`build/<preset>/src/SKSE/Behavior Relay/packed-hky/behavior_relay/plugins/`), never the source
Data tree, and is not checked in; only the source tree here is. `HKY_STAGE_DIR` (see the plugin's
`CMakeLists.txt`) wires that build dir into both the MO2 dev-deploy and the release zip. Add files
under `<Bundle>.hky/` and re-run the plugins build (a `configure` re-glob picks up new files); the
packer re-runs and the deploy/zip carry the fresh bundle.

`manifest.json` masters use bare bundle stems (`"Skyrim"`), and `Skyrim` is implicit for every
non-Skyrim bundle even if omitted — declaring it is explicit and self-documenting.
