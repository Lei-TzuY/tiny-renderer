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

### Milestone 25 — explicit viewport and scissor state

- `RasterRect` / `ViewportState` make the pixel-space viewport and optional sample scissor explicit while preserving the complete framebuffer as the default viewport.
- NDC-to-screen conversion maps into a validated viewport; front-face classification remains in post-clip NDC before the top-left viewport transform can invert Y orientation.
- Scissoring restricts raster samples to integer half-open bounds without changing homogeneous clipping, fixed-point top-left coverage, barycentrics, interpolation, depth testing, or shading.
- A present viewport must have non-zero extent; a present zero-area scissor is a legal deterministic empty clip.
- Rectangle addition overflow and framebuffer containment are validated separately and fail closed before mutation rather than silently clamping invalid state.
- Shared range/model preflight resolves target-dependent state before writes, while prepared-model construction validates framebuffer-independent rectangle definitions.
- Viewport/scissor state propagates through direct triangles/meshes, selected ranges, direct/prepared model submission, instance batches, and heterogeneous prepared lists without a second raster path.
- Regression coverage locks default full-frame byte/hash compatibility, translated sub-viewport color/depth equivalence, half-open scissor ownership, viewport/scissor composition, clipped+cull ordering, range execution, zero-area scissor behavior, invalid-state rejection, list-wide fail-closed preflight, and prepared list/instance propagation.

### Milestone 26 — explicit stencil test and update stage

- `Framebuffer` owns an 8-bit stencil attachment with deterministic clear/read behavior while stencil testing remains disabled by default.
- `StencilCompare::{Never, Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual, Always}` compares masked reference and stored values through one framebuffer primitive.
- `StencilOp::{Keep, Zero, Replace, IncrementClamp, DecrementClamp, Invert}` plus a write mask model stencil-fail, depth-fail, and full-pass state transitions.
- Per-fragment ordering is centralized in `Framebuffer::test_and_write`: coverage/scissor resolve first in the rasterizer, stencil rejects before depth, depth rejection selects the depth-fail operation, and a full pass performs the pass operation before the existing depth/color ownership updates.
- `depth_test_and_write` remains a stencil-disabled compatibility wrapper instead of creating a second ownership implementation.
- Stencil state is validated fail-closed and propagated through direct triangles/meshes, selected ranges, direct/prepared model submission, instance batches, and heterogeneous prepared lists.
- Regression coverage locks read/write masks, every compare and operation, saturating increment/decrement, stencil/depth/pass ordering, depth-write-disabled interaction, stencil-mask prepass execution, scissor-bounded side effects, range propagation, prepared-list equivalence, invalid-state rejection, and disabled-stencil byte/hash compatibility.
- The existing clipping, viewport, fixed-point coverage, interpolation, culling, material/texture shading, and depth semantics remain on the same raster path.

### Milestone 27 — explicit RGB blending and color write masks

- `BlendState` adds bounded RGB source/destination factors, a validated constant blend color, blend operation, and independent red/green/blue write mask while disabled/default state remains replacement-compatible.
- Factors cover zero/one, source/destination color and their inverses, plus constant color and its inverse; the first slice is deliberately RGB-only and makes no alpha/transparency claim.
- `BlendOp::{Add, Subtract, ReverseSubtract, Min, Max}` is centralized in the framebuffer ownership primitive. Min/max intentionally ignore factors and compare unfactored source/destination RGB components under the renderer's documented teaching contract rather than claiming hardware-API equivalence.
- Float framebuffer blend arithmetic is not silently clamped; the existing RGB8 export conversion remains the bounded display/output boundary.
- The color write mask is applied only at final RGB storage, so preserved channels retain destination values while passing depth writes and stencil pass operations still occur normally.
- Blend validation and state propagation cover direct triangles/meshes, selected ranges, model submission, prepared plans, instance batches, and heterogeneous prepared lists without duplicating blend math outside `Framebuffer::test_and_write`.
- Regression coverage locks disabled byte/hash compatibility, analytic factor math, every blend operation, non-commutative draw ordering, channel masking, depth/stencil interaction, scissor-bounded blending, selected ranges, prepared-list equivalence, and fail-closed invalid state.

## Next frontier — Milestone 28

The next architectural promotion is **material opacity and source-alpha RGB blending**. The renderer now has deterministic RGB composition but no fragment opacity source, so conventional caller-ordered transparent material rendering cannot be expressed without inventing alpha outside the verified material/shading pipeline.

Acceptance for that slice should require:

- `MaterialState` gains a finite `[0,1]` opacity with default `1.0`, preserving every existing opaque framebuffer/hash result;
- the bounded MTL path accepts one optional `d <opacity>` dissolve value per material, rejects duplicates/non-finite/out-of-range values deterministically, and defaults missing `d` to fully opaque;
- fragment shading carries material opacity as a separate scalar alongside RGB rather than multiplying RGB or pretending the RGB framebuffer stores destination alpha;
- `BlendFactor` gains source-alpha and inverse-source-alpha factors that replicate the fragment opacity scalar across RGB factor components; destination-alpha factors remain out of scope because no destination alpha attachment exists;
- the framebuffer ownership primitive receives validated fragment opacity and keeps stencil/depth/blend/write-mask ordering centralized, with default opacity `1.0` preserving all existing direct calls;
- model/prepared submission preserves imported opacity through `MaterialDraw`, and a file-driven MTL opacity fixture renders equivalently to a programmatic material under caller-selected source-alpha blending;
- deterministic regressions cover opacity `0`, `0.5`, and `1`, source-alpha/inverse-source-alpha analytic composition, caller draw order, depth writes disabled for transparent passes, stencil/scissor interaction, clipping continuity, prepared/list propagation, and fail-closed invalid material/MTL opacity;
- this milestone enables bounded caller-ordered transparency but does not claim sorting, order-independent transparency, destination alpha, alpha textures, or physically based transmission;
- no performance claim is made without a controlled benchmark.

## Deliberate later work

General/full OBJ and MTL syntax, polygon triangulation, relative OBJ indices, smoothing/generated normals, multiple material libraries, general image formats, mipmaps, anisotropic filtering, sRGB handling, anti-aliasing/MSAA, destination alpha, alpha textures, transparency sorting/OIT, specular/PBR lighting, shadows, normal maps, programmable shaders, GPU acceleration, and a general scene graph remain outside the current bounded CPU teaching architecture until a higher-value integration milestone justifies them.
