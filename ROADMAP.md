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

### Milestone 23 — explicit face culling and front-face state

- `CullMode::{None, Back, Front}` and `FrontFace::{CounterClockwise, Clockwise}` are first-class raster state; legacy behavior remains `None` by default.
- Front-face winding is defined in normalized device coordinates after homogeneous clipping and perspective divide, before the top-left framebuffer viewport transform can invert Y orientation.
- Each triangle in a clipped polygon fan is classified independently before viewport conversion and fixed-point orientation normalization.
- `CullMode::None` bypasses the new NDC classification path so established tiny-triangle and no-culling framebuffer/hash behavior remains unchanged.
- Culling state propagates through direct triangles, meshes, selected ranges, direct/prepared model submission, instance batches, and heterogeneous prepared lists without creating a parallel raster path.
- Shared preflight rejects unknown cull/front-face state before framebuffer mutation; prepared-model construction validates the state without a framebuffer.
- Regression coverage locks both submitted windings, both front-face conventions, front/back culling, clipped triangles, mesh/range execution, model-list propagation, degenerate projected geometry, and default byte/hash compatibility.

### Milestone 24 — explicit depth-test state and reversed-Z-capable execution

- `DepthCompare::{Less, LessEqual, Greater, GreaterEqual, Always, Never}` and explicit depth-write enable/disable state model the framebuffer depth stage directly.
- Legacy strict `Less` with depth writes enabled remains the default and is regression-locked byte/hash-identically to the pre-state pipeline.
- All comparison semantics are centralized in `Framebuffer::depth_test_and_write`; triangle, range, model, prepared-instance, and heterogeneous-list layers only propagate validated state.
- Passing fragments with depth writes disabled update color without mutating stored depth.
- Equal-depth regressions distinguish strict `Less` first-owner behavior from non-strict `LessEqual` later overwrite behavior.
- Reversed-Z execution uses the same clipping, perspective divide, NDC-to-depth mapping, coverage, culling, interpolation, and shading path: a reversed clip-space z projection plus zero clear and `Greater` comparison resolves the reference overlap scene byte/hash-identically to conventional forward-Z `Less`.
- Depth state is carried by `ModelRenderOptions` and prepared plans, including heterogeneous prepared lists.
- Unknown depth comparison state is rejected before framebuffer mutation and during framebuffer-independent prepared-model construction.

## Next frontier — Milestone 25

The next architectural promotion is **explicit viewport and scissor state**. The viewport transform is currently hard-wired to the full framebuffer and rasterization has no explicit scissor rectangle, so callers cannot render deterministic sub-views or restrict fragment ownership without changing geometry.

Acceptance for that slice should require:

- a bounded viewport state defining pixel-space origin and non-zero extent, with the default exactly covering the full framebuffer and preserving all existing framebuffer/hash output;
- NDC-to-screen conversion maps into the configured viewport while front-face classification remains defined in pre-viewport NDC and therefore independent of top-left framebuffer orientation;
- an optional integer scissor rectangle that rejects samples outside its half-open bounds without changing clipping, barycentrics, interpolation, depth, or shading semantics;
- viewport/scissor bounds are overflow-safe and validated fail-closed before framebuffer mutation, including through range/model/prepared/list preflight;
- deterministic regressions cover a sub-viewport transform, scissor edge ownership, viewport-plus-scissor composition, clipped geometry crossing a viewport boundary, and default full-frame compatibility;
- viewport/scissor state propagates through direct triangles/meshes, selected ranges, prepared instances, and heterogeneous prepared lists without a second raster path;
- empty or invalid rectangles have an explicit contract rather than being silently normalized or clamped;
- no performance claim is made without a controlled benchmark.

## Deliberate later work

General/full OBJ and MTL syntax, polygon triangulation, relative OBJ indices, smoothing/generated normals, multiple material libraries, general image formats, mipmaps, anisotropic filtering, sRGB handling, anti-aliasing, specular/PBR lighting, shadows, normal maps, programmable shaders, GPU acceleration, and a general scene graph remain outside the current bounded CPU teaching architecture until a higher-value integration milestone justifies them.
