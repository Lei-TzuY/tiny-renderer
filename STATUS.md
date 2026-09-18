# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 90; Milestone 91 candidate

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view. Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestones 61–71 expand canonical asset/material semantics with bounded OBJ vertex color, MTL emission, deterministic anisotropy, glossy environment-reflection mip policies, and owned `map_Ks` / `map_Ke` / `map_Ns` material textures while preserving shared sampler, prepared ownership, direct/environment shading, inspection/fingerprint, and fail-closed validation contracts.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. Milestones 74–80 promote prepared-list execution into bounded flat-scene and CLI transactions with combined-bounds auto-fit, optional explicit perspective camera, scene material/shading overrides, explicit `opaque` / `source-alpha` / `alpha-to-coverage` policies, and `ordering mixed-transparency`. Mixed transparency executes opaque/A2C work in its depth-writing phase and source-alpha work in stable far-to-near order, with target-dependent preflight before framebuffer mutation.

Milestone 81 establishes the prepared per-draw spatial contract. `PreparedSpatialSubmission` owns the same validated `PreparedModelSubmission` snapshot plus one immutable object-space AABB/center record for each canonical `MaterialDraw`. Milestone 82 makes that per-draw plan executable through complete target preflight and canonical `draw_mesh_range` submission. Milestone 83 adds conservative prepared-draw frustum visibility: all eight prepared AABB corners are transformed to homogeneous clip space and a draw is rejected only when every corner is provably outside the same clip half-space; boundary and numerically ambiguous cases are retained.

Milestone 84 consumes conservative visibility in the real mixed-transparency offline source-alpha phase while retaining complete unfiltered-plan target preflight before mutation. Milestone 85 extends the same contract across the complete mixed-transparency transaction: opaque/A2C work is flattened in caller/canonical order, source-alpha work remains stable far-to-near, both complete plans are preflighted before clear/environment/geometry mutation, and only visibility-retained subsets execute through the existing prepared draw executor. A 4x mixed scene with off-frustum depth-writing work is regression-locked to exact resolved byte/hash and per-sample RGB/depth/stencil equivalence against manual omission, while target-invalid off-frustum work still rejects fail closed.

Milestone 86 promotes those one-off draw vectors into an address-stable `PreparedScenePlan` that owns prepared spatial submissions once, reevaluates caller-order/back-to-front planning and conservative visibility for each camera, binds each evaluation to the exact view/projection matrices that produced it, and preserves complete unfiltered-plan preflight before visible execution.

Milestone 87 gives that reusable plan a real offline mixed-transparency consumer. `PreparedOfflineMixedScene` owns the address-stable plan plus validated offline settings, explicit cameras reevaluate order/visibility without rebuilding canonical model/material/texture/spatial snapshots, environment-reflection viewer position is rebound per camera through bounded execution overrides, complete unfiltered phases remain preflighted before clear/environment/geometry mutation, and one-shot mixed rendering delegates to the same prepared-scene orchestration. Exact 4x resolved and per-sample equivalence is regression-locked across distinct cameras and retained source-asset lifetime.

Milestone 88 promotes reusable single-camera execution into a bounded ordered multi-camera transaction. `render_prepared_scene_sequence` validates every camera, reevaluates camera-dependent order/visibility, binds reflection overrides, and target-preflights the complete camera span before the first returned frame begins fragment execution. Returned order exactly follows caller A/B/A order, repeated cameras remain exact-deterministic, and total returned ownership is bounded to 16 Mi resolved pixels. The implementation reuses `PreparedOfflineMixedScene`, `PreparedSceneEvaluation`, complete-plan preflight, and the established single-camera consumer; it makes no throughput or batching-speed claim.

Milestone 89 adds a controlled offline benchmark harness around the reusable mixed-scene pipeline without introducing an acceleration fast path. The harness measures complete preparation, camera evaluation plus scene-level preflight, and canonical lower-level validation/submission/raster phases separately; framebuffer allocation/clear remains outside the submission timing region, every measured iteration verifies one deterministic sequence hash, CI only smoke-executes the harness, and no CI timing is treated as performance evidence.

Milestone 90 is integrated on exact `main` commit `91481710eb641b49d7f6224d0b7de49985961d61`. Push CI run `35351082173` completed successfully on Linux, macOS, and ASan/UBSan.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence. The historical `milestone-73-prepared-spatial-metadata` branch is superseded by integrated M81. Milestone 91 is the active implementation surface on branch `milestone-91-prepared-camera-sequence-plan` / PR #107; historical M88/M90 camera-sequence branches are completion history, not parallel implementation surfaces.

## Milestone 90 — file-driven reusable camera sequences (integrated)

M90 promotes the reusable camera-sequence transaction from programmatic API-only use into a strict bounded file-driven offline workflow. It does not add another renderer or benchmark path: imported scene assets are prepared once, the complete sidecar camera list is validated through the existing camera contract, and execution delegates to `render_prepared_scene_sequence`.

Acceptance surface:

- `tiny-renderer-camera-sequence-v1` is a strict sidecar format with repeated exact 12-scalar camera records, full-line comments/blank lines, finite camera validation, deterministic line diagnostics, no trailing tokens, at least one camera, and a maximum of 256 camera records;
- `tiny_renderer_render ... --camera-sequence FILE` accepts the sidecar only for `.trscene` input using `ordering mixed-transparency` and rejects conflict with an embedded manifest camera;
- the CLI loads every scene asset and the complete camera sidecar before execution, prepares one `PreparedOfflineMixedScene`, and reuses the existing bounded sequence transaction rather than rebuilding canonical model/material/texture/spatial ownership per frame;
- rendered frames preserve sidecar order and are written as deterministic `STEM_0000.EXT`, `STEM_0001.EXT`, ... outputs while the unsuffixed output path remains unused;
- file-driven A/B/A execution is byte-deterministic: repeated A frames are identical, B is observably distinct, and repeated complete CLI runs produce identical frame bytes;
- a later invalid sidecar camera rejects before any earlier frame is rasterized or any indexed output file is written;
- parser regressions lock version-header strictness, trailing-token rejection, camera-count bounds, and later-camera validation; CLI regressions cover real mixed-transparency OBJ/MTL assets through 4x execution;
- the existing 16 Mi resolved-pixel in-memory sequence bound remains authoritative for M90. No streaming, parallel frame execution, interpolation/timeline semantics, BVH/occlusion acceleration, or throughput claim is added.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate, plan, filter, and submit rather than duplicating ownership semantics.
- Model/prepared/list/draw-plan/scene-evaluation submission validates complete required state before writes whenever later invalid state could otherwise partially commit earlier work.
- Prepared draw execution reuses canonical model material mapping and `draw_mesh_range`; spatial and scene-plan layers own metadata/planning/visibility, not material or framebuffer semantics.
- Prepared spatial metadata is derived once from the same owned canonical model snapshot later submitted; caller-order flattening, transparency ordering, visibility, reusable scene evaluation, and camera-sequence evaluation consume that metadata instead of rescanning triangle/index data.
- Spatial ordering, visibility, and execution reject geometry mutations they cannot represent, including vertex-program position changes and projective model/view transforms in the bounded contract.
- Conservative visibility may retain false positives but must not reject a draw unless its prepared bound proves it cannot intersect the homogeneous clip volume.
- Visibility is an execution-selection optimization only; it must not weaken validation of the complete transaction being represented.
- Camera-dependent visibility state is bound to the exact matrices that produced it; reusable ownership does not imply reusable camera-derived subsets.
- Reusable offline execution may overlay camera-dependent reflection viewer state, but canonical prepared mesh/material/texture/spatial ownership remains immutable and is not rebuilt per camera.
- Reusable camera-sequence execution validates and target-preflights the complete camera batch before any frame fragment execution; sequence orchestration must not weaken per-frame fail-closed semantics.
- Offline mixed-transparency orchestration owns transparency policy classification: opaque/A2C retain caller/canonical order and source-alpha uses stable global far-to-near order. The reusable scene layer provides the lower-level execution phases without inventing transparency semantics.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into a new capability.
- Measurement helpers may expose phase boundaries only if production validation semantics remain intact; benchmark code must not gain a faster correctness path than normal rendering.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Milestone 91 candidate — prepared frame-at-a-time camera sequences

M91 promotes the M90 file-driven batch into a prepared transaction whose camera-dependent planning and complete target preflight happen once before any indexed frame is allowed to execute. The core capability is bounded ownership: callers and the CLI can consume one framebuffer at a time without rebuilding canonical model/material/texture/spatial state or retaining the entire rendered sequence.

Current candidate behavior:

- `PreparedOfflineCameraSequence` retains the address-stable prepared-scene plan through shared ownership, snapshots validated offline settings, and owns the ordered cameras, camera-dependent `PreparedSceneEvaluation` records, and reflection execution overrides. Existing environment textures remain borrowed resources under the established offline-settings lifetime contract.
- `prepare_offline_camera_sequence` validates the complete camera span, derives exact view/projection matrices, reevaluates caller-order/back-to-front planning plus conservative visibility, binds camera-specific reflection viewer positions, and target-preflights every complete unfiltered evaluation before returning.
- The public prepared-plan API remains bounded to 256 camera metadata records. `render_prepared_camera_sequence_frame` then executes one deterministic indexed entry through the existing environment and prepared-draw/range/raster paths without repeating scene-level evaluation or transaction preflight; lower-level fail-closed draw/range validation remains active.
- The historical `render_prepared_scene_sequence` compatibility helper preserves its original 16 Mi aggregate resolved-pixel ownership contract rather than inheriting the new 256-camera metadata limit. A 257-camera 1x1 regression locks that compatibility boundary while both APIs share one preparation implementation.
- M90 CLI sequence execution now prepares the complete sequence before output and renders/writes one indexed frame at a time, preserving deterministic sidecar order and later-invalid zero-output behavior without retaining every framebuffer simultaneously.
- Environment background validation now matches the existing diffuse/reflection radiance contract by validating the complete source radiance field before execution. A negative background texel therefore cannot remain latent until a later camera after earlier sequence frames have already executed.
- Regression evidence locks exact A/B/A resolved and per-sample equivalence against individual reusable rendering and the compatibility vector API, repeated indexed-frame determinism, invalid index rejection, source-scene lifetime, later-invalid camera rejection before fragment shading, reflection rebinding, the 65-camera large-target ownership distinction, preserved >256-camera compatibility when the historical pixel bound allows it, and existing file-driven CLI outputs.
- This milestone is an ownership/data-plane capability, not a speedup claim. It adds no parallel/asynchronous execution, interpolation/keyframes, streaming input, BVH/occlusion acceleration, or second renderer path.

## Promotion after Milestone 91

The next architectural promotion should move from camera-only frame variation to **bounded prepared per-frame affine scene transforms** rather than adding another sequence wrapper.

Milestone 92 should make frame-varying model transforms explicit while preserving the existing prepared ownership and spatial invariants:

- immutable `PreparedSpatialSubmission` model/material/texture snapshots and object-space per-draw AABBs are prepared once; a frame supplies a bounded transform record aligned with the prepared scene entries rather than rebuilding meshes or spatial metadata;
- every frame transform must be finite and affine, and position-changing vertex programs remain rejected by the prepared-spatial path because canonical object-space bounds cannot represent them;
- camera/order/frustum evaluation must consume that frame's transforms when computing view depth and transforming AABB corners, so no evaluation may reuse stale model matrices or stale visibility/order results;
- a prepared frame sequence must validate all camera/transform records and target-preflight all complete unfiltered frame plans before the first framebuffer or indexed output can execute, preserving the M91 transaction boundary;
- caller-order opaque/A2C execution and stable far-to-near source-alpha ordering must remain byte/sample-equivalent to manually constructing the same per-frame prepared submissions;
- the first slice should be programmatic and deterministic; file-format timeline syntax, interpolation/keyframes, skeletal deformation, projective transforms, position-changing vertex programs, parallel execution, and performance claims remain later work unless independently justified.

This promotion reuses the reason M81 spatial metadata remains valuable: bounds are object-space and can be transformed per evaluation. It must not cache a camera/model-dependent draw plan across frames whose transforms differ.
