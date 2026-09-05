# tiny-renderer roadmap

This roadmap tracks executable architectural capabilities rather than release marketing. A milestone is considered complete only after its implementation and regression coverage are integrated and the exact `main` commit passes the repository's Linux, macOS, and ASan/UBSan CI gates.

## Completed capability chain

Milestones 1–16 established the CPU raster pipeline, indexed meshes, generalized varyings, deterministic fixed-point coverage, interpolation qualifiers, in-memory textures, bounded OBJ/PPM import, inverse-transpose normals plus directional Lambert lighting, OBJ normals, runtime material albedo, bounded MTL import, explicit base-color source selection, owned `map_Kd` textures, canonical `ModelAsset` draw ranges, and direct range execution.

### Milestone 17 — single-pass canonical OBJ model parsing

- Rich OBJ geometry and material metadata are produced by one canonical parse result rather than two cross-checked source scans.
- Legacy geometry-only `load_obj` behavior remains source-compatible and continues to ignore material metadata syntax.
- Ordered face-material association, deterministic source-line diagnostics, Kd-only rendering, and mapped-texture rendering remain regression-covered.

### Milestone 18 — first-class `ModelAsset` submission

- Runtime `ModelAsset` / `MaterialDraw` types are independent of the OBJ loader.
- `draw_model_asset` owns the orchestration for material/texture/base-color state while every accepted draw still executes through `draw_mesh_range`.
- Complete model/draw state is preflighted before any framebuffer write, preventing a malformed later draw from partially committing earlier draws.

### Milestone 19 — prepared model submission plans

- `PreparedModelSubmission` owns a validated snapshot of canonical topology, draw records, render options, and shared texture lifetime.
- Static preparation is framebuffer-independent; dynamic framebuffer/transform/light validation remains fail-closed at execution.
- Prepared execution is reusable and byte/hash-equivalent to direct model submission.

### Milestone 20 — shared range/model raster preflight

- Range and model dynamic submission validation share one internal preflight implementation.
- `rasterizer_range.cpp` is orchestration-only rather than maintaining a mirror validator.
- Model preflight no longer depends on submitting an empty draw range as a validation side effect.
- Selected-index semantics and the established all-vertex varying/UV/normal validation contract remain regression-locked.

### Milestone 21 — prepared model instance batches

- `draw_prepared_model_instances` accepts caller-owned spans of model transforms or precomposed MVP transforms without introducing a scene graph or duplicating mesh/material/texture storage.
- A non-empty batch preflights every instance × material-draw dynamic state before the first framebuffer mutation.
- Execution order is deterministic: instance input order, then canonical material-draw order within each instance.
- Batch rendering is byte/hash-equivalent to the same prepared instances submitted sequentially.
- Equal-depth lit instances make input order observably deterministic under the renderer's strict `<` depth rule.
- A singular later lighting transform rejects the entire batch before earlier valid instances can write; empty batches are deterministic no-ops.
- MVP-only batches preserve the existing rule that directional lighting requires separate model/view/projection transforms.

### Milestone 22 — heterogeneous prepared submission lists

- `PreparedModelListEntry` borrows one prepared plan plus a model transform; the prepared plan remains the owner of canonical mesh, material, and texture state.
- `draw_prepared_model_list` accepts an ordered span of heterogeneous entries with shared view/projection state, without introducing a persistent command buffer or scene graph.
- List structure is validated first, including explicit rejection of null prepared-plan entries.
- Every entry and all of its material draws are dynamically preflighted before the first framebuffer mutation, so a malformed later entry cannot partially commit earlier models.
- Execution preserves exact caller entry order and each prepared plan's canonical material-draw order.
- Kd-only and mapped-texture prepared assets in one list are byte/hash-equivalent to the same sequential prepared submissions.
- Equal-depth differently colored prepared plans make caller order observably deterministic under strict `<` depth testing.
- Empty lists are deterministic no-ops; no new raster path or performance claim is introduced.

## Next frontier — Milestone 23

The next architectural promotion is **explicit face-culling state with a defined front-face convention** in the raster core. The current renderer normalizes both triangle orientations and therefore cannot model the culling stage present in conventional graphics pipelines.

Acceptance for that slice should require:

- explicit `CullMode` state supporting at least `None`, `Back`, and `Front`, with legacy behavior remaining `None` by default;
- an explicit clockwise/counter-clockwise front-face convention defined in a coordinate space that is independent of the framebuffer's top-left image origin;
- culling after homogeneous clipping/perspective divide so clipped fan triangles obey the same visible-face contract as unclipped geometry;
- winding/culling behavior proven for both submitted orientations, clipped triangles, indexed meshes/ranges, and model-list execution without creating a parallel draw path;
- degenerate triangles remain discarded deterministically rather than being misclassified as a face;
- invalid culling enum/state fails closed before framebuffer mutation;
- the baseline sample and all prior no-culling framebuffer/hash contracts remain unchanged under default state.

## Deliberate later work

General/full OBJ and MTL syntax, polygon triangulation, relative OBJ indices, smoothing/generated normals, multiple material libraries, general image formats, mipmaps, anisotropic filtering, sRGB handling, anti-aliasing, configurable depth functions/reversed-Z, specular/PBR lighting, shadows, normal maps, programmable shaders, GPU acceleration, and a general scene graph remain outside the current bounded CPU teaching architecture until a higher-value integration milestone justifies them.
