# MetaHuman Faces in Skyrim: Design

Nov 4, 2026 · Cassie. Exported from the Claude Docs artifact
<https://claude.ai/artifact/2u8XtLr49mem95uj9LxdsM> (rev 6); the artifact also has the rig evaluation diagram.

MetaHuman's solver is cheap on the CPU, so the real work is deforming the mesh. Only LOD 0 needs GPU blendshapes; every other LOD is joints alone.

## How MetaHuman works

A MetaHuman face is a data-driven rig: animation sets a small set of controls, and RigLogic expands them into thousands of joint and blendshape values. The DNA file holds the mesh, the skinning and the matrices that make this possible.

Rig evaluation: GUI controls → raw controls → PSDs (corrective poses) → joints (sparse matrices) and conditional outputs → joint transforms, blendshape weights and animated map weights.

RigLogic only computes numbers. It does not touch vertices, so applying the deltas and skinning is the host engine's job, which is the part we have to build. Blendshapes correct what joints can't do, such as creases and lip shapes, and only the highest LOD has them. Sources: OpenRigLogic's `docs/design` and its `RigInstance.h`, plus `dna-probe`. The 9-values-per-joint reading (7,830 / 870) is inferred from those counts.

## What the head asset holds

Blendshape deltas exist only on four LOD 0 meshes, and 98.5% of them are on the head. From `dna-probe` on `asi22_Head.dna`, which has 870 joints and 50 meshes.

| LOD | Meshes | Vertices | Head mesh vertices | Max influences (head) | RigLogic SSE (µs) |
| --- | --- | --- | --- | --- | --- |
| 0 | 9 | 33,845 | 24,049 | 12 | ~58 |
| 1 | 9 | 18,649 | 11,977 | 12 | ~44 |
| 2 | 8 | 9,283 | 5,999 | 12 | ~28 |
| 3 | 7 | 3,789 | 2,548 | 8 | ~23 |
| 4 | 5 | 1,796 | 1,291 | 8 | ~11 |
| 5 | 4 | 788 | 564 | 8 | ~11 |
| 6 | 4 | 383 | 270 | 4 | ~8 |
| 7 | 4 | 211 | 137 | 4 | ~7 |

| LOD 0 mesh | Blendshape targets | Deltas |
| --- | --- | --- |
| Head | 737 | 2,410,738 |
| Teeth | 41 | 21,670 |
| Cartilage | 76 | 13,545 |
| Each eye | 2 | 386 |

- A head target moves about 3,270 vertices on average, so targets are sparse.
- The head has 24,049 positions but 24,408 layouts. Morphing runs on positions, while a draw needs one vertex per layout.
- A vanilla head in Skyrim has 898 vertices in the engine notes' example, so LOD 3 is already about 3 times that.
- Stored as a vertex index plus three floats, 2.45M deltas take about 39 MB per unique DNA before compression. That is calculated, not measured.

## What Community Shaders has today

The fork has no GPU blendshape code: a search of `src`, `package` and `docs` at `c5b86490` found none, so this part is new work. What it does provide is the plumbing a compute morph would sit on.

| Piece | State at `c5b86490` | Use for MetaHuman |
| --- | --- | --- |
| Face morphing | Still the engine's CPU job. DCLF copies the result into a positions buffer | The second stream is where GPU output would go |
| Second vertex stream | One float4 per vertex, uploaded from the CPU | Needs a compute-writable buffer, normals and a previous position |
| Skinning | Done in the vertex stage, four influences per vertex | Too few for LOD 0 to 2, which use up to 12 |
| GPU bone palettes | Roadmap item 1.6, "Not started" | Not available; the morph pass needs its own joint matrices |
| Compute passes | Supported by `Api/Passes.h` and the frame schedule | Hosts the morph pass |

The closest existing technique is vertex pulling, where every DCLF vertex stage loads its attributes from device addresses. That lets a compute-filled record reach the draws with little shader change.

Skinning palette work could be on a branch I haven't seen. I searched only this checkout, not Panthuncia's other branches.

## GPU blending design

Proposal: accumulate active blendshape deltas in a single compute dispatch, then resolve, skin and rebuild normals per head. This is a first design and none of it has been measured.

**Data per unique DNA, immutable on the GPU**

- Deltas grouped by target: vertex index and position delta.
- A target table: first delta and delta count, per target.
- A channel-to-target map, from the DNA's blendshape mapping.
- Neutral positions, skin weights and indices, and face topology for normals.

Heads sharing a DNA share this data. Per-NPC DNAs would not, which makes the 39 MB figure the main argument for a shared archetype or compressed deltas.

**Data per head, per frame**

- Active targets and their weights, gathered on the CPU from RigLogic's channel outputs.
- Joint matrices, built from RigLogic's joint outputs.

**Compute steps**

1. **Clear** an accumulation buffer of three int32 values per position.
2. **Accumulate:** for each delta of each active target, add weight times delta as fixed-point, using atomic adds. Fixed-point lets one dispatch cover all targets with no barrier between them.
3. **Resolve:** add the sum to the neutral position.
4. **Skin** with up to 12 influences into head-bone space.
5. **Rebuild normals and tangents** from the deformed positions.
6. **Write** the stream record per layout, copying the old position to the previous-position field first.

**LOD policy**

| Distance | Morph | Skin | Rig update |
| --- | --- | --- | --- |
| LOD 0 | Blendshapes and joints | Compute | Every frame |
| LOD 1 to 2 | None: no targets in the asset | Compute (up to 12 influences) | Every frame |
| LOD 3 to 5 | None | Compute (up to 8) | Reduced rate possible |
| LOD 6 to 7 | None | Vertex stage possible (4) | Reduced rate possible |

A switch from LOD 0 to LOD 1 removes blendshapes at once, so a visible pop is the risk to test.

The per-frame cost should scale with active deltas rather than the 2.45M total, so skipping zero-weight targets is the key saving. Unmorphed output could also be skipped for heads whose controls haven't changed.

## Open questions

- [ ] How many targets are active in real expressions and speech? This sets the GPU budget; the probe only drove controls with synthetic values.
- [ ] Does the DNA carry normal deltas, or only position deltas? The design assumes normals are rebuilt.
- [ ] Is atomic fixed-point accumulation fast enough on the target GPU, or is a per-vertex sorted layout better?
- [ ] Are per-NPC DNAs affordable, or do NPCs need a shared archetype plus their own neutral offsets?
- [ ] How is the neck seam matched to the body?
- [ ] Should the 82 animated maps (such as wrinkles) drive materials in the first version?
- [ ] Has Panthuncia done related work on another branch? Worth checking before building.
