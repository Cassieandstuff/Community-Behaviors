# CB Control Rig Importer (Blender)

Imports a portable **Community Behaviors control rig** straight from a `control/rig.yaml` into
Blender as an armature — no Havok binary, no compiled dependency. Scan a load order, pick an
actor, get the rig.

This is the Blender end of CB's *portable control rigging* pipeline. The C++ side
(`havok-core-cli control-decompile`) owns the Havok binary boundary and emits `control/rig.yaml`;
that YAML is the **language-agnostic interchange format**. This addon only reads YAML and builds
bones, so it links nothing and runs in stock Blender.

## What `rig.yaml` carries

```yaml
nodes:
  - name: "NPC L FootBox"
    transform: { translation: [x,y,z], rotation: [x,y,z,w], scale: [x,y,z] }  # LOCAL, or the string `derive`
    poseSource: skeleton | nif | derived      # present when posed
    poseHint:   nif | aux | convention        # present when transform is `derive` (unresolved)
    handles: [ { name, variant }, ... ]        # bound objects (rigidbody/shape), optional
    children: [ <node>, ... ]                   # recursive hierarchy
```

Transforms are **local to parent**; the addon forward-kinematics-composes them to world space
(quaternion math mirroring `havok-core sct::SkeletonMath`). A `transform: derive` node has no
recovered pose, so it's placed on its parent (identity local) and tagged, rather than dropped.

## Install

**Fastest (this machine):** set `BLENDER_PLUGINS_DIRECTORY` to your Blender addons folder once,
then deploy — the convention is one subfolder per plugin under that path:

```
set BLENDER_PLUGINS_DIRECTORY=%APPDATA%\Blender Foundation\Blender\4.2\scripts\addons
python deploy.py                 # copies cb_control_rig/ -> $BLENDER_PLUGINS_DIRECTORY/cb_control_rig/
python deploy.py <addons-dir>    # or pass the folder explicitly (overrides the env var)
```

`deploy.py` replaces any prior install and never copies `__pycache__`. Then in Blender:
Preferences → Add-ons → enable **"Community Behaviors — Control Rig Importer"** (or Edit →
reload scripts if it was already enabled).

**Manual alternative:** zip the `cb_control_rig/` folder (so the zip contains
`cb_control_rig/__init__.py` + `rigcore.py`) and Install… it via Preferences → Add-ons.

## Use

1. In the 3D viewport, open the sidebar (**N**) → **CB** tab.
2. Set **Load Order / Rig Root** to a folder to scan — any of:
   - the deployed CB data (once regen carries `control/`),
   - a single mod folder,
   - the offline `D:\cb-diffs\control-rig-scattershot*` tree.
3. **Scan for Control Rigs** → pick an **Actor** from the dropdown → **Import Control Rig**.

Each bone is tagged with `cb_pose_source` (and colored where Blender supports bone colors):
green = skeleton (authoritative), blue = nif, yellow = derived-by-convention (refine me),
red = unresolved hint (needs authoring). Bound objects land in `cb_handles`.

## Layout

- `cb_control_rig/rigcore.py` — pure Python, **no `bpy`, no PyYAML**. Parser + FK compose +
  load-order scan. This is the testable, reusable core.
- `cb_control_rig/__init__.py` — the Blender addon: maps `rigcore` output onto armature data.
- `test_rigcore.py` — headless validation (run in CPython with PyYAML).

## Validation

`python test_rigcore.py` (CPython + PyYAML), against the vanilla bestiary emitted to
`D:\cb-diffs\control-rig-scattershot3`:

- the dependency-free reader matches PyYAML **194/194** rigs (Blender ships no PyYAML),
- FK compose is finite across all **3364** nodes, node counts intact,
- the L/R `FootBox` mirror survives world compose (**X = −12.74 / +12.74**).

## Roadmap

- **Animation export** — bake an edited armature back to CB animation YAML for the compiler.
- Consume `control/` directly from the deployed load order once regen emits it into the bundles.
