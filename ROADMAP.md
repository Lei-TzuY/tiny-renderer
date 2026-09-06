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
- Front-face winding is defined in normalized device coordinates after homogeneous clipping and perspective divide, before the framebuffer's top-left-origin viewport transform can invert Y orientation.
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

### Milestone 28 — material opacity and source-alpha RGB blending

- `MaterialState` owns a finite `[0,1]` opacity with default `1.0`, preserving established opaque aggregate initialization and framebuffer output.
- The bounded MTL parser accepts one optional `d <opacity>` dissolve value per material in both legacy Kd and rich map_Kd paths, defaults missing opacity to one, and rejects malformed, duplicate, non-finite, negative, or greater-than-one values deterministically.
- Fragment shading carries `ShadedFragment{rgb, opacity}` so opacity remains independent from RGB lighting/material multiplication and no destination-alpha attachment is implied.
- `BlendFactor::{SourceAlpha, OneMinusSourceAlpha}` replicate the verified fragment opacity scalar across RGB factors while all previous M27 factor/operation semantics remain unchanged.
- `Framebuffer::test_and_write` validates source opacity before stencil/depth/color mutation and keeps the complete stencil → depth → blend → write-mask ownership sequence centralized.
- Direct raster, shared range/model preflight, and framebuffer-independent prepared-model construction reject invalid material opacity before mutation.
- File-driven OBJ/MTL opacity is preserved through `MaterialDraw` and renders byte/hash-equivalently to the same programmatic material under caller-selected source-alpha blending.
- Regression coverage locks opacity 0/0.5/1 analytic composition, opaque-default compatibility, invalid material/fragment/MTL rejection, depth-write-disabled transparent passes, stencil/scissor interaction, homogeneous clipping continuity, prepared-list equivalence, and caller-order significance.
- The milestone enables bounded caller-ordered transparency only; it does not add destination alpha, alpha textures, automatic sorting, order-independent transparency, or physically based transmission.

### Milestone 29 — deterministic 4× multisampling

- `Framebuffer` owns explicit `SampleCount::{One, Four}` target state; single-sample remains the default and preserves established byte/hash output.
- 4× mode stores RGB, depth, and stencil independently per sample and uses the fixed quarter-offset 2×2 pattern `(0.25,0.25)`, `(0.75,0.25)`, `(0.25,0.75)`, `(0.75,0.75)`, represented exactly in the existing 1/256 fixed-point coverage domain.
- Top-left coverage, barycentrics, perspective interpolation, depth evaluation, material shading, stencil operations, blending, and color masks execute per covered sample through the same triangle raster path.
- `Framebuffer::test_and_write_sample` centralizes per-sample stencil → depth → blend → RGB ownership; the legacy pixel-level ownership entry point rejects multisample targets instead of silently touching only one sample.
- RGB output resolves four float sample colors by deterministic equal-weight averaging; 1× resolve is a direct copy, while sample-specific color/depth/stencil inspection remains available for regression evidence.
- Viewport/scissor remain pixel-space half-open state, and model/prepared/list submission inherits multisampling from the framebuffer target without adding sample state to model render options or duplicating geometry/material paths.
- Framebuffer pixel/sample allocation is overflow-checked before storage allocation, and unsupported sample counts fail closed.
- Regression coverage locks 1× deterministic compatibility, quarter-edge coverage, exact shared-edge sample ownership, per-sample depth/stencil isolation, source-alpha-before-resolve behavior, clipped/scissored multisample side effects, prepared-list equivalence, and storage validation.
- This first multisample slice does not claim configurable sample locations, alpha-to-coverage, sample masks, centroid interpolation, temporal antialiasing, coverage-shading optimization, or performance improvement.

### Milestone 30 — owned MTL `map_d` opacity textures

- The bounded rich MTL path accepts one optional `map_d <filename>` per material with the same sibling-only path safety and deterministic malformed/duplicate diagnostics used for mapped diffuse assets; the legacy strict material-only loader still rejects map directives.
- `MaterialAssetDefinition`, `MaterialDraw`, and compatibility material batches retain optional opacity-texture ownership, while diffuse and opacity roles share one decoded texture cache so identical referenced files reuse the same `shared_ptr`.
- `TextureBinding` keeps one UV channel pair and one sampler for both texture roles; opacity-only materials therefore reuse the established texture-coordinate path rather than introducing independent opacity UV state.
- Direct and shared range/model preflight require valid finite-safe UV varyings whenever either RGB texture sampling or opacity sampling needs them, so malformed opacity bindings reject before framebuffer mutation.
- A sampled opacity map contributes `clamp((r + g + b) / 3, 0, 1)` in the current linear teaching space, and final fragment opacity is uniform material `d` multiplied by that scalar. This is a deterministic arithmetic rule, not a luminance or colorimetric claim.
- Lighting remains RGB-only; sampled opacity travels independently through the existing `ShadedFragment` and source-alpha framebuffer ownership path.
- Direct model submission, prepared plans, instance batches, and heterogeneous prepared lists retain opacity-texture lifetime and reuse the same raster/shading path on single-sample and 4× targets.
- Regression coverage locks rich/legacy MTL strictness, path rejection, cross-role/material texture deduplication, retained lifetime, the documented scalar rule, fail-closed invalid UV state, imported-versus-programmatic 1×/4× equivalence, and exact prepared-list sample storage on 4× targets.
- The milestone does not add destination alpha, alpha test/discard, alpha-to-coverage, automatic transparency sorting, order-independent transparency, independent opacity UV transforms/samplers, new image formats, or physically based transmission.

### Milestone 31 — deterministic alpha-to-coverage

- `AlphaToCoverageState` is explicit raster/model state, disabled by default, and propagates through direct triangles/meshes, selected ranges, direct/prepared models, instance batches, and heterogeneous prepared lists without introducing a second raster path.
- Enabling alpha-to-coverage requires the existing deterministic 4× framebuffer target; single-sample execution rejects during direct/shared preflight before RGB, depth, or stencil ownership can mutate.
- Fragment opacity is quantized by the documented rule `floor(clamp(alpha, 0, 1) * 4 + 0.5)`, giving exact 1/2/3/4-sample transition thresholds at 0.125, 0.375, 0.625, and 0.875.
- Surviving samples use the existing fixed sample-index order 0 → 1 → 2 → 3. Geometric top-left coverage, interpolation, depth calculation, and per-sample fragment shading occur first; alpha-rejected samples exit before `Framebuffer::test_and_write_sample`.
- Rejected samples therefore cannot run stencil compare/update, depth test/write, blending, or RGB write-mask ownership, while accepted samples retain the original fragment opacity and may still use caller-selected source-alpha blending.
- MTL `d` and `map_d` feed the same per-sample opacity calculation, so spatially varying opacity is evaluated at the existing multisample shading positions rather than after resolve.
- Regression coverage locks all threshold boundaries, fixed sample ownership, disabled-state behavior, fail-closed 1× execution, depth/stencil immutability, explicit source-alpha composition, per-sample opacity-map variation, scissor/range propagation, exact prepared-list equivalence, and whole-list preflight of a later incompatible A2C entry.
- The milestone does not add alpha test/discard, programmable sample masks/locations, temporal dithering, centroid interpolation, destination alpha, transparency sorting/OIT, or antialiasing quality/performance claims.

### Milestone 32 — deterministic directional shadow mapping

- `DepthTexture2D` is a first-class immutable single-sample depth resource with non-zero dimension, overflow-safe storage, coordinate bounds, and finite normalized `[0,1]` depth validation.
- `capture_depth_texture` copies one validated single-sample framebuffer depth attachment and rejects multisample targets or non-finite unused depth, making a finite far clear value an explicit shadow-capture contract instead of silently persisting infinity or sample zero.
- `render_directional_shadow_map` generates reusable light-space depth through the existing `Rasterizer` geometry, clipping, fixed-point coverage, face-culling, and depth-ownership path while masking RGB writes; it does not contain a second triangle rasterizer.
- `ShadowState` carries shared owned depth-map lifetime, one finite light view-projection transform, and a finite non-negative comparison bias. Disabled state leaves previous rendering behavior unchanged.
- Shadow-enabled camera rendering requires separate model/view/projection transforms and an enabled directional light. MVP-only direct/range/model/prepared paths reject during validation before framebuffer mutation rather than guessing object-to-world data.
- Camera homogeneous clipping linearly carries the generated light clip coordinate at newly created vertices; screen interpolation stores `light_clip * camera_inv_w` and reconstructs the projective light coordinate with the same camera reciprocal-w denominator before the light-space perspective divide.
- Fragments outside the light clip volume are unshadowed. In-range fragments use a deterministic nearest shadow texel and the binary rule `fragment_depth - bias <= stored_depth`.
- Directional ambient remains unshadowed; only the Lambert diffuse term is multiplied by shadow visibility. Material RGB/opacity, source-alpha blending, alpha-to-coverage, camera depth/stencil/scissor, and framebuffer ownership retain their established stages.
- Direct triangles/meshes, selected ranges, direct/prepared models, prepared instance batches, and heterogeneous prepared lists share the same shadow-aware raster/preflight path; prepared plans retain the shared shadow resource beyond the source binding lifetime.
- Regression coverage locks depth-resource capture, known occluder/receiver depth and visibility, bias behavior, outside-light-frustum semantics, camera-clipping continuity against manually clipped geometry, direct/prepared/list equivalence, fail-closed invalid bindings/MVP-only execution, and exact 4× shadow plus alpha-to-coverage sample ownership.
- This first shadow slice does not add PCF, cascades, cubemap/point-light shadows, soft/variance/moment shadows, normal-offset bias, automatic light-frustum fitting, alpha-tested casters, or performance/visual-quality claims.

### Milestone 33 — deterministic fragment alpha test and cutout shadows

- `AlphaTestState` is explicit raster/model state, disabled by default, with a finite normalized threshold. A sample survives exactly when final fragment opacity is greater than or equal to the threshold.
- Fragment opacity remains the established material `d` multiplied by optional `map_d` sampling; alpha testing adds no independent texture, UV, sampler, or hidden material channel.
- The alpha test runs after per-sample interpolation and fixed material/texture/lighting opacity evaluation, but before alpha-to-coverage and before `Framebuffer::test_and_write_sample`; a discarded sample therefore cannot mutate stencil, depth, blend destination RGB, or color ownership.
- Direct triangles/meshes, selected ranges, direct/prepared models, instance batches, and heterogeneous prepared lists propagate and validate the state while disabled behavior retains the established pipeline.
- Alpha test composes deterministically with 4× alpha-to-coverage: binary discard executes first, then surviving samples are filtered by the existing fixed A2C sample mask without rewriting source opacity.
- `render_directional_shadow_map` is material-draw aware for cutout casters. It borrows each prepared draw's material, opacity texture, UV channels, sampler, and alpha-test state while continuing to route geometry and depth ownership through the existing `Rasterizer` with RGB writes masked.
- The shadow pass preflights every entry and material draw before clearing or writing the internal light framebuffer, so malformed later opacity bindings reject before any light-depth execution begins.
- Regression coverage locks exact threshold equality versus below-threshold discard, RGB/depth/stencil immutability, invalid-threshold fail-closed behavior, 4× alpha-test→A2C ordering, prepared-list/sequential equivalence, a file-driven `map_d` card whose camera silhouette and light-depth cutout agree, disabled solid-caster compatibility, and later invalid shadow UV preflight.
- This slice does not add stochastic transparency, destination alpha, sorting/OIT, PCF/soft shadows, programmable sample masks/locations, or a general shader language.

### Milestone 34 — bounded programmable fragment stage

- `FragmentProgram` is a first-class immutable CPU extension point with shared lifetime semantics; omitting a program bypasses the extension and preserves the established fixed fragment path.
- A program receives read-only interpolated varyings, fixed material/texture/lighting/shadow RGB and opacity, absolute sample position, sample index, and depth. It returns RGB, opacity, and an explicit discard bit without any framebuffer handle or ownership API.
- Stage ordering is fixed as geometric coverage/interpolation → fixed shading → optional fragment program → program discard → alpha test → alpha-to-coverage → framebuffer stencil/depth/blend/color ownership.
- Program RGB output must be finite and opacity finite within `[0,1]` before the sample may reach any ownership stage; a program discard exits before alpha testing, A2C, stencil, depth, blending, or color writes.
- Direct triangle/mesh/range submission validates static program configuration before writes. Direct model preparation performs the same framebuffer-independent validation, and prepared plans retain the program `shared_ptr` independently of the caller's temporary handle.
- Direct/prepared model execution, instance batches, and heterogeneous prepared lists propagate the same program state through the existing raster path rather than introducing a programmable side pipeline.
- Regression coverage locks no-program versus identity-program RGB/depth/stencil equivalence, varying-driven color transformation, opacity rewrite feeding alpha test and deterministic 4× A2C, discard with zero ownership side effects, fixed quarter-sample position/index visibility, invalid output rejection, range/prepared static validation, retained lifetime, and prepared-list/sequential equivalence.
- The milestone is deliberately a bounded CPU teaching interface. It does not claim GLSL/HLSL/SPIR-V compatibility, vertex programmability, derivatives, engine-managed arbitrary resource bindings, JIT compilation, GPU execution, or performance parity.

### Milestone 35 — bounded programmable object-space vertex stage

- `VertexProgram` is a first-class immutable CPU extension point with shared lifetime semantics; omitting it leaves the fixed geometry path unchanged.
- A program receives source object-space position plus the source `VaryingPack` and returns object-space position plus varyings before model/view/projection, normal-matrix, camera clipping, or light-space shadow transforms run.
- Output position and active varying values must remain finite within the renderer's bounded numeric envelope; varying channel count and interpolation qualifiers are preserved exactly so existing UV, normal, and interpolation validation remains authoritative.
- Direct triangles execute the program before downstream validation; mesh and range submission prepare the complete canonical mesh once per submission so each source vertex is processed once and the established all-vertex range-validation contract is retained.
- Direct models, prepared instance batches, and heterogeneous prepared lists prepare every programmed mesh before the first framebuffer write. A malformed later entry therefore cannot partially commit an earlier valid model.
- Prepared plans retain the vertex-program `shared_ptr` through `ModelRenderOptions`, while nested execution rasterizers consume already-programmed geometry with an empty vertex binding so model/range orchestration cannot accidentally process vertices twice.
- Directional shadow-map generation uses the same programmed object-space geometry before applying the existing light view-projection and homogeneous clipping path, preventing camera and shadow silhouettes from silently diverging.
- M34 fragment programs observe varyings rewritten by M35 after the established perspective/no-perspective/flat interpolation machinery; no parallel programmable raster path is introduced.
- Regression coverage locks identity compatibility, manual-versus-programmatic deformation equivalence, varying rewrite visibility at the fragment stage, post-deformation homogeneous clipping, once-per-source-vertex mesh/range execution, invalid output fail-closed behavior, prepared lifetime, heterogeneous-list whole-batch rejection, static prepare validation, and camera/shadow silhouette agreement.
- The milestone does not expose arbitrary clip-space replacement, programmable projection matrices, tessellation/geometry stages, derivatives, mutable buffers, a shader language/JIT, GPU execution, or performance/shader-API conformance claims.

### Milestone 36 — bounded tangent-space normal mapping

- Material draws may own one optional normal map imported through bounded rich-MTL `map_Bump`, sharing existing UV/sampler, decoded-texture deduplication, and prepared lifetime ownership.
- Required tangent frames derive deterministically from canonical object-space triangle positions, UVs, and geometric normals. Degenerate or numerically unstable UV/geometry bases reject before framebuffer mutation.
- Tangent/bitangent vectors use the model linear transform while geometric normals retain inverse-transpose transformation, including non-uniform scale.
- The sampled tangent-space normal feeds the established fixed lighting path and composes with clipping, fragment programs, alpha stages, depth/stencil/blend ownership, prepared instances/lists, and directional shadows without a second raster path.

### Milestone 37 — view-dependent Blinn-Phong specular lighting

- `MaterialState` carries bounded specular RGB and shininess, with strict bounded MTL `Ks` / `Ns` import.
- Fixed lighting carries finite viewer position and perspective-correct world-space fragment position only when required.
- Ambient + Lambert diffuse + Blinn-Phong half-vector specular shading uses the same normal-mapped shading normal and established shadow visibility.
- Vertex-program deformation precedes world-position derivation; optional fragment programs still run after complete fixed shading and before ownership stages.

### Milestone 38 — bounded point-light shading

- `PointLight` adds finite world-space position/viewer state, bounded ambient/diffuse coefficients, and finite non-negative linear/quadratic attenuation.
- Diffuse/specular attenuation is exactly `1 / (1 + linear*d + quadratic*d^2)` while ambient remains deliberately unattenuated.
- Point lights reuse the established perspective-correct world-space position, Blinn-Phong material state, tangent-space normal mapping, shared preflight, and direct/range/model/prepared/list execution path.
- MVP-only fixed lighting and malformed later prepared-list transforms remain fail-closed before framebuffer ownership.

### Milestone 39 — bounded deterministic multi-light accumulation

- `FixedLightCollection` is fixed-capacity and caller ordered, supporting up to four directional/point records while remaining mutually exclusive with legacy single-light state.
- All records are validated before writes, including selected payload, finite light/viewer state, attenuation, coefficients, supported type, capacity, and one shared normal binding.
- Ambient + Lambert diffuse + Blinn-Phong specular contributions accumulate in exact caller order after world position and shading normal are derived once per sample.
- One existing directional shadow resource may be associated with one directional record; fragment programs still run exactly once after complete fixed-light accumulation and before discard/alpha/A2C/framebuffer ownership.
- Direct/range/model/prepared/list execution uses shared validation; deterministic regressions cover legacy equivalence, analytic accumulation, mixed shadow behavior, program ordering, and whole-list fail-closed semantics.

After M39, the canonical OBJ parser was expanded with bounded signed relative face indices, bounded polygon triangulation, and smoothing groups with deterministic generated normals while retaining the same strict legacy/rich parser architecture and diagnostics.

### Milestone 40 — bounded point-light cubemap shadows

- `DepthCubemap` owns six equal non-zero depth faces, finite normalized depth, six finite face view-projection transforms, overflow-safe storage, and deterministic dominant-axis face addressing with X → Y → Z tie precedence.
- The cubemap also owns the exact finite world-space point-light position used for capture. Static/shared validation requires that capture position to exactly match the associated fixed point light, preventing face selection and stored projection matrices from silently using different origins.
- `render_point_shadow_cubemap` captures six canonical 90-degree views through the existing prepared geometry, vertex-program, alpha-test/opacity, clipping, culling, and depth raster path; prepared vertex geometry is reused across the six faces.
- `PointShadowState` plus `FixedLightCollection::shadowed_point_index` associate at most one cubemap with one point-light record in this slice. Missing maps, invalid bias, out-of-range/cross-type association, origin mismatch, and MVP-only execution reject before mutation.
- Camera shading selects/projectively samples the cubemap from the established perspective-correct world position and modulates only the associated point light's diffuse/specular term. Ambient and unrelated lights remain independent and caller-order accumulation is preserved.
- Direct triangles/meshes/ranges, direct/prepared models, instances, and heterogeneous lists propagate the same state and owned lifetime; later malformed entries fail the complete list before earlier framebuffer ownership.
- Regression coverage locks six axial/tie face rules, capture depth, near/far validation, origin ownership, mismatched-origin rejection before color/depth/stencil writes, selective multi-light shadowing, prepared lifetime/list equivalence, and malformed association/list behavior.
- The milestone does not claim PCF/soft shadows, cubemap seam filtering, multiple point-cubemap associations, spotlight support, GPU cubemap APIs, automatic shadow allocation, physically based lighting, or performance parity.

### Milestone 41 — bounded deterministic spotlight shading

- `SpotLight` integrates finite position/direction, cosine-space cone falloff, distance attenuation, shared normal binding, and Blinn-Phong response through the existing fixed-light path.
- Caller-order accumulation with directional/point records, fragment-program ordering, normal mapping, prepared/list execution, and fail-closed validation are regression-covered.

### Milestone 42 — bounded deterministic spotlight shadow mapping

- `SpotShadowMap` owns the validated spotlight capture identity plus immutable depth data; `render_spot_shadow_map` reuses prepared geometry, vertex programs, alpha-tested cutouts, homogeneous clipping, culling, and the established depth path.
- One typed spotlight association selectively modulates that record's diffuse/specular contribution while preserving ambient semantics and unrelated lights; prepared/list lifetime and fail-closed capture/association validation are covered.

After M42, bounded interop slices added multiple sibling OBJ material libraries and a strict uncompressed 24-bit TGA decoder behind the shared texture-image dispatch path. These are integrated capabilities, not a replacement rendering path.

### Milestone 43 — deterministic RGB light color

- Directional, point, and spotlight records carry bounded finite `[0,1]` RGB color with white defaults.
- Ambient, Lambert diffuse, and Blinn-Phong specular use one component-wise light-color rule before exact caller-order accumulation; attenuation, cone falloff, and shadow visibility remain scalar factors.
- Legacy singular lights, fixed-light collections, shadows, normal mapping, fragment programs, direct/range/model/prepared/list submission, and fail-closed validation stay on the existing path.
- Regression coverage locks white compatibility, colored diffuse/specular, mixed-light accumulation, shadow/program ordering, invalid color before writes, and prepared-list equivalence.
- This milestone does not make spectral, color-temperature, sRGB, HDR photometry, PBR, GPU, or performance claims.

## Next frontier — Milestone 44

The next architectural promotion is **bounded per-record typed shadow bindings for fixed-light collections**. The current topology permits one associated shadow resource per light type, which prevents two same-type lights from being independently shadowed even though multi-light accumulation is already caller ordered and fixed-capacity.

Acceptance for that slice should require:

- each active fixed-light record may carry at most one matching typed shadow binding while preserving the collection's fixed capacity and caller order;
- multiple directional, point, or spotlight records of the same type can retain independent validated shadow resources and biases without adding a second shading loop;
- resource type/capture identity mismatches, missing resources, invalid bias, and malformed later prepared-list entries reject before any framebuffer write;
- prepared plans own all bound shadow lifetimes, and direct/range/model/instance/list execution remains equivalent to sequential submission;
- visibility modulates only the bound record's direct colored contribution while ambient and unrelated lights remain unchanged;
- existing singleton shadow state remains source-compatible or receives one explicit deterministic migration path with regression evidence;
- no PCF/soft shadows, cascades, cookies/IES, automatic shadow allocation, unbounded lights, scene graph, GPU API, or performance claim is added.

## Deliberate later work

General/full OBJ and MTL syntax, general image formats beyond the bounded PPM/TGA path, mipmaps, anisotropic filtering, sRGB handling, destination alpha, transparency sorting/OIT, programmable sample locations/masks, centroid interpolation, temporal antialiasing, PCF/cascaded/soft shadows, cubemap seam filtering, cookie/IES lighting, spectral/color-temperature lighting, physically based BRDFs/IBL, general bump/parallax/displacement mapping, full shader languages/derivatives/JIT, GPU acceleration, and a general scene graph remain outside the bounded CPU teaching architecture until a higher-value executable milestone justifies them.
