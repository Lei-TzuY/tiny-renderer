# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Architecture frontier: Milestone 94 bounded programmatic timeline evaluation

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

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence. The historical `milestone-73-prepared-spatial-metadata` branch is superseded by integrated M81, and historical M88/M90/M91 sequence branches are completion history rather than live implementation surfaces. The authoritative state is always the exact integrated `main` commit plus its CI evidence.

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

## Milestone 91 — prepared frame-at-a-time camera sequences

M91 is integrated on exact `main` commit `90187dc3ac158a2cf020d1ca68608d0a1ace8c79`. Push CI run `35356841992` completed successfully on Linux, macOS, and ASan/UBSan.

- `PreparedOfflineCameraSequence` retains the address-stable prepared-scene plan through shared ownership, snapshots validated offline settings, and owns the ordered cameras, camera-dependent `PreparedSceneEvaluation` records, and reflection execution overrides. Existing environment textures remain borrowed resources under the established offline-settings lifetime contract.
- `prepare_offline_camera_sequence` validates the complete camera span, derives exact view/projection matrices, reevaluates caller-order/back-to-front planning plus conservative visibility, binds camera-specific reflection viewer positions, and target-preflights every complete unfiltered evaluation before returning.
- The public prepared-plan API is bounded to 256 camera metadata records. `render_prepared_camera_sequence_frame` executes one deterministic indexed entry through the existing environment and prepared-draw/range/raster paths without repeating scene-level evaluation or transaction preflight; lower-level fail-closed guards remain active.
- The historical `render_prepared_scene_sequence` compatibility helper preserves its original 16 Mi aggregate resolved-pixel ownership contract rather than inheriting the 256-camera metadata limit.
- M90 CLI sequence execution prepares the complete sequence before output and renders/writes one indexed frame at a time, preserving deterministic sidecar order and later-invalid zero-output behavior without retaining every framebuffer simultaneously.
- Environment background validation matches the diffuse/reflection radiance contract by validating the complete source radiance field before execution.
- Regression evidence locks exact A/B/A resolved and per-sample equivalence, repeated indexed-frame determinism, invalid index rejection, source-scene lifetime, later-invalid camera rejection before fragment shading, reflection rebinding, large-target ownership boundaries, preserved compatibility capacity, and existing file-driven CLI outputs.
- M91 is an ownership/data-plane capability, not a speedup claim; it adds no parallel/asynchronous execution, interpolation/keyframes, streaming input, acceleration structure, or second renderer path.

## Milestone 92 — bounded prepared per-frame affine scene transforms

M92 promotes M91 from camera-only variation to frame-varying model transforms while preserving immutable prepared model/material/texture ownership and object-space spatial metadata.

- `evaluate_prepared_scene_plan` accepts an optional exact per-entry model-transform overlay. A non-empty overlay must match the prepared scene entry count; the legacy overload delegates with no overlay and preserves prepared entry transforms.
- Each selected frame transform is copied into the existing `PreparedDrawOrderEntry`, so caller-order flattening, stable far-to-near source-alpha ordering, conservative eight-corner AABB frustum testing, complete-plan preflight, and final draw execution consume one consistent matrix rather than stale prepared-entry state.
- `OfflineSceneFrameState` pairs one validated camera with one complete transform record. `prepare_offline_frame_sequence` reuses `PreparedOfflineCameraSequence` ownership and indexed frame execution rather than introducing a second sequence or raster path.
- Every explicit frame transform record must align exactly with `PreparedScenePlan::entries()`. Existing prepared-spatial validation rejects non-finite/projective model transforms and position-changing vertex programs because immutable object-space bounds cannot represent them.
- The common preparation transaction validates all cameras/transforms, reevaluates order and visibility per frame, binds camera-dependent reflection overrides, and target-preflights every complete unfiltered frame plan before any indexed framebuffer can execute. Camera-only M91 preparation delegates to the same transaction core.
- 4x mixed-transparency regression coverage combines alpha-to-coverage caller-order work with two source-alpha draws whose far-to-near order flips between frames. Every prepared indexed frame is exact resolved/per-sample RGB, depth, and stencil equivalent to manually rebuilding the same per-frame one-shot scene.
- An A/B/C/A sequence locks deterministic repeated-frame output; C moves every draw outside the frustum and must equal an exact clear target, proving visibility is reevaluated instead of reusing an earlier frame's subset.
- Transform-count mismatch and a later finite projective transform reject the complete preparation transaction; valid finite affine transforms remain accepted.
- The slice is programmatic and deterministic. It does not add interpolation/keyframes, skeletal deformation, projective model transforms, position-changing vertex programs, parallel execution, or performance claims.

## Milestone 93 — strict file-driven affine frame state

M93 closes the external frame-state gap without adding another renderer, sequence executor, or interpolation path.

- `tiny-renderer-frame-sequence-v1` is a strict bounded sidecar whose ordered records contain the established 12-scalar camera state plus exactly one complete row-major affine model matrix per prepared scene entry, terminated by an explicit `end`.
- Parsing rejects missing/wrong headers, unknown directives, missing/extra/trailing tokens, unterminated frames, non-finite camera or matrix values, finite projective matrices, transform-count mismatch, more than 256 frames, and an expected scene-entry count above the existing 256-entry scene bound. The model-count bound is checked before per-frame transform storage is reserved.
- The CLI exposes `--frame-sequence FILE` only for `.trscene` input using `ordering mixed-transparency`, rejects embedded-manifest camera conflicts, and makes `--frame-sequence` mutually exclusive with the historical `--camera-sequence` compatibility path.
- Imported model/material/texture/spatial ownership is still prepared exactly once through `PreparedOfflineMixedScene`. Parsed frame records feed the existing M92 `prepare_offline_frame_sequence` transaction, so camera-dependent ordering, conservative visibility, reflection rebinding, complete target preflight, and indexed frame execution remain on the established path.
- The complete sidecar and prepared frame transaction are accepted before indexed output begins. A malformed later frame therefore leaves zero earlier indexed outputs; successful output order/naming remains deterministic `STEM_0000.EXT`, `STEM_0001.EXT`, and so on.
- File-driven A/B/A execution is regression-locked both at the CLI byte boundary and against independently constructed programmatic M92 frame records. The latter comparison covers resolved RGB/hash plus every 4x sample's RGB, depth, and stencil attachments.
- Frame matrices remain exact independent samples. M93 adds no interpolation, delta accumulation, Euler decomposition, hierarchy, keyframe curves, timing semantics, streaming, parallel execution, or performance/animation-quality claim.

## Milestone 94 — bounded programmatic timeline evaluation

M94 defines interpolation semantics programmatically before any file syntax is allowed to depend on them.

- `OfflineSceneTimelineKeyframe` binds one finite scalar time to the already-validated M92 frame state: camera plus one complete affine model transform per prepared scene entry.
- Timelines require 2..256 strictly increasing finite keyframes and accept at most 256 requested sample times. Duplicate/non-increasing times, non-finite times, underspecified spans, inconsistent transform counts, oversized model ownership, and out-of-domain sample requests reject deterministically.
- Exact keyframe requests copy stored frame state without interpolation arithmetic. Interior samples linearly interpolate camera scalar/vector components and the affine top 3x4 while preserving the affine bottom row exactly as `[0,0,0,1]`.
- Every keyframe and every interpolated camera/affine transform is revalidated before prepared-scene evaluation. A later invalid interpolated camera therefore rejects the complete requested timeline transaction before any earlier sample can execute fragments.
- `prepare_offline_timeline_sequence` checks keyframes against prepared-scene entry ownership, samples the complete requested sequence in caller order, and delegates directly to the existing M92 `prepare_offline_frame_sequence` transaction. No second renderer, prepared-scene owner, or execution path is introduced.
- Regression coverage locks exact endpoint/interior/repeated sampling, affine bottom-row preservation, 4x mixed-transparency equivalence against explicit M92 frame records, camera/model interpolation effects on ordering/visibility, bounded-count rejection, projective-keyframe rejection, empty-request validation, and complete fail-closed behavior.
- M94 is an abstract finite scalar-time API. It adds no file syntax, frame-rate/wall-clock semantics, Euler/quaternion decomposition, easing, looping, extrapolation, hierarchy/skeletal animation, parallel execution, or performance/animation-quality claim.

## Promotion after Milestone 94

The next architectural promotion should expose **strict file-driven timeline/keyframe state** while keeping M94 as the sole interpolation authority. The file layer may serialize validated keyframes and explicit requested sample times, but it must not define a second interpolation model or bypass M92/M94 preparation.

A Milestone 95 slice should require:

- one strict bounded sidecar format with an explicit version header, finite strictly increasing keyframe times, the existing 12-scalar camera state, exactly one complete affine model matrix per prepared scene entry, and explicit requested sample times;
- parsing rejects missing/wrong headers, unknown directives, malformed/trailing tokens, duplicate/non-increasing/non-finite keyframe times, incomplete/extra transforms, projective/non-finite matrices, oversized keyframe/sample counts, and sample requests outside the declared keyframe domain before indexed output begins;
- parsed state maps directly into `OfflineSceneTimelineKeyframe` plus sample-time records and then calls M94 `prepare_offline_timeline_sequence`; the file syntax must not reimplement interpolation, camera validation, scene evaluation, or rendering;
- CLI integration is explicit and mutually exclusive with the existing exact camera/frame sequence modes, and is available only where the reusable mixed-transparency prepared-scene transaction is valid;
- the complete file, timeline sampling, prepared-scene evaluation, and target preflight succeed before the first output file is emitted, so malformed later keyframes/sample requests leave zero earlier indexed outputs;
- deterministic regressions compare file-driven output against independently constructed programmatic M94 keyframes across endpoint/interior/repeated samples, including exact resolved/hash and 4x per-sample RGB/depth/stencil equivalence;
- M95 does not add easing curves, looping, extrapolation, frame-rate scheduling, async/parallel execution, hierarchy/skeletal animation, new renderer paths, or performance/animation-quality claims.

