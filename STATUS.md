# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Architecture frontier: Milestone 114 bounded glTF animation collection

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

## Milestone 95 — strict file-driven timeline state

M95 exposes M94 timeline semantics through one strict bounded external sidecar and the existing mixed-transparency CLI transaction without adding another interpolation or render path.

- `tiny-renderer-timeline-v1` contains at least two finite strictly increasing `keyframe TIME <camera>` records. Every keyframe owns exactly one complete row-major affine `model` matrix per prepared scene entry and is terminated by explicit `end`; one or more ordered `sample TIME` directives follow all keyframes.
- The timeline loader shares M93's strict finite-number, camera, and affine-matrix parsing core while retaining timeline-specific line diagnostics. It rejects missing/wrong headers, unknown directives, malformed/trailing tokens, incomplete/extra models, unterminated records, non-finite or non-increasing keyframe times, finite projective transforms, keyframes after sampling begins, oversized keyframe/sample counts, and out-of-domain sample requests.
- Expected scene-entry ownership is bounded before per-keyframe transform storage is reserved. Keyframes and samples retain the established M94 256-record limits.
- Parsing owns syntax only. It returns `OfflineSceneTimelineFile { keyframes, sample_times }`, reuses M94's semantic keyframe validator, and performs no interpolation or prepared-scene evaluation.
- The CLI adds `--timeline-sequence FILE` only to the existing `.trscene` / `ordering mixed-transparency` sequence transaction with no manifest camera. Camera-only, exact-frame, and timeline sidecars form one mutually exclusive option group.
- Parsed timeline state feeds M94 `prepare_offline_timeline_sequence`, which samples through the established M94 semantics and delegates the complete result to the M92 affine-frame preparation transaction. Indexed rendering and output naming continue through the existing `PreparedOfflineCameraSequence` executor.
- File-driven endpoint/interior/repeated/final samples are regression-locked against independently constructed programmatic M94 keyframes with exact resolved RGB/hash and every 4x sample's RGB, depth, and stencil state. A repeated requested time is byte-deterministic at the CLI boundary.
- A later invalid sample rejects the complete file/CLI transaction before any earlier indexed output is written, and conflicting sequence sidecar modes reject before output.
- M95 adds no interpolation policy, easing, looping, extrapolation, frame-rate/wall-clock scheduling, hierarchy/skeletal animation, asynchronous/parallel execution, new renderer path, or performance claim.

## Milestone 96 — bounded programmatic hierarchical transform evaluation

M96 adds one bounded local-to-world transform layer above prepared-scene ownership while preserving M92 as the only frame execution transaction.

- `OfflineSceneHierarchy` owns one immutable parent record per prepared scene entry. A missing parent marks a root; arbitrary parent-before/after entry ordering is valid, while out-of-range references, self-parenting, cycles, and topology size above the established 256-entry scene bound reject deterministically.
- `OfflineSceneHierarchicalFrameState` keeps dynamic state separate from topology: one validated camera plus exactly one complete affine local transform per hierarchy entry.
- Every local transform is validated as finite affine state before hierarchy evaluation. World transforms resolve deterministically as `parent_world * local`; roots preserve their local transform exactly.
- The resolver memoizes parent results rather than depending on entry order and revalidates every composed world matrix as finite affine state, so arithmetic overflow during otherwise-valid local composition fails closed before prepared-scene evaluation.
- `prepare_offline_hierarchy_sequence` first checks hierarchy/prepared-scene ownership and the established bounded frame count, resolves the complete requested local-frame batch into M92 world-transform records, then delegates directly to `prepare_offline_frame_sequence`.
- Camera-dependent ordering, conservative visibility, environment-reflection rebinding, target preflight, indexed frame ownership, and all raster execution remain unchanged because M96 introduces no alternate scene or draw path.
- Regression coverage locks root-only exact equivalence against explicit M92 world frames across resolved RGB/hash and every 4x sample's RGB/depth/stencil state; a chain whose root appears after its children in entry order is exact-equivalent to independently composed world frames, and root motion observably moves the child chain.
- Structural parent errors, prepared-scene ownership mismatch, local-count mismatch, a later projective local transform, and finite affine locals whose multiplication overflows all reject before any fragment execution. The existing 256-frame bound is enforced before resolved-world frame allocation.
- M96 is programmatic only. It adds no hierarchy file syntax, skeletal joints/skin weights, constraints, inverse kinematics, mutable topology, hierarchy-aware asset ownership, parallel execution, or performance claim.

## Milestone 97 — bounded programmatic hierarchical timeline evaluation

M97 composes the independently verified M94 timeline and M96 hierarchy layers without introducing another scene or render path.

- `OfflineSceneHierarchicalTimelineKeyframe` binds one finite scalar time to one validated camera plus exactly one affine local transform per fixed hierarchy entry; topology remains a separate immutable `OfflineSceneHierarchy`.
- Hierarchical timelines retain M94's 2..256 strictly increasing keyframe bound and 0..256 caller-ordered sample bound. Every keyframe camera/local transform is validated before sampling, including empty sample requests.
- Exact keyframe requests copy stored camera/local state without interpolation arithmetic. Interior samples reuse M94's documented linear camera interpolation and affine top-3x4 interpolation while preserving the affine bottom row exactly.
- Interpolation occurs in local-transform space first. Sampled locals are then delegated to M96, which resolves `parent_world * local` for the fixed topology and revalidates every composed world transform before M92 preparation; no world-transform interpolation shortcut exists.
- `prepare_offline_hierarchy_timeline_sequence` validates hierarchy/prepared-scene ownership, samples the complete requested local-state batch, resolves the complete hierarchy batch, and only then delegates to M92's existing prepared-frame transaction.
- Root-only hierarchical timelines are exact resolved/hash and 4x per-sample RGB/depth/stencil equivalent to the same M94 timeline. Arbitrary forward parent references remain valid, repeated/out-of-order sample requests are deterministic, and a parent-scale/child-translation regression proves local interpolation precedes composition.
- Later non-finite interpolated locals and finite interpolated locals whose parent composition overflows reject the complete transaction before any earlier requested sample executes fragments. The established bounded sample limit is enforced before sample allocation.
- Camera-dependent ordering, conservative visibility, environment-reflection rebinding, target preflight, indexed execution, and raster ownership remain unchanged on M92. M97 adds no hierarchy/timeline file syntax, easing, looping, extrapolation, wall-clock/frame-rate semantics, topology animation, skeletal animation, constraints/IK, parallel execution, or performance claim.

## Milestone 98 — strict file-driven hierarchical timeline transaction

M98 makes the M97 hierarchy/timeline transaction externally usable through one strict bounded sidecar and the existing mixed-scene CLI path without duplicating interpolation, hierarchy resolution, or raster execution.

- `tiny-renderer-hierarchy-timeline-v1` begins with exactly one explicit `parent ENTRY root|PARENT_ENTRY` record per prepared scene entry. Parent records may appear in arbitrary order because entry ownership is explicit; duplicate/missing entry records, out-of-range references, self-parenting, cycles, and scene-count mismatches reject deterministically.
- Topology must be complete before dynamic state begins. The parser constructs the existing immutable `OfflineSceneHierarchy`; it performs no world-transform evaluation.
- Each `keyframe TIME <camera>` owns exactly one finite affine `local` matrix per hierarchy entry and terminates with `end`. Two through 256 strictly increasing keyframes and one through 256 caller-ordered `sample TIME` records retain the M94/M97 bounds.
- The file layer reuses the established finite-number, camera, and affine-matrix parsing core. It performs no interpolation and no local-to-world composition; the parsed `OfflineSceneHierarchicalTimelineFile` delegates directly to M97 `prepare_offline_hierarchy_timeline_sequence`.
- `--hierarchy-timeline-sequence FILE` is a fourth mutually exclusive sequence mode beside camera-only, exact-frame, and flat-timeline sidecars. It is accepted only by the existing mixed-transparency prepared-scene transaction and reuses the established indexed output loop.
- File-driven hierarchical keyframes with arbitrary parent record order are exact resolved/hash and 4x per-sample RGB/depth/stencil equivalent to an independently constructed M97 programmatic topology/keyframe/sample transaction.
- Strict regression coverage rejects duplicate/missing/out-of-range/self/cyclic topology, local-count mismatch, and projective local state. A later finite-local composition overflow rejects the complete CLI preparation before any earlier indexed output exists, preserving the no-partial-output transaction boundary.
- M98 adds no topology interpolation, mutable topology, skeletal joints/skinning, easing/looping/extrapolation, asynchronous streaming, parallel execution, alternate raster path, or performance/animation-quality claim.

## Milestone 99 — bounded programmatic transform graph

M99 removes the one-hierarchy-node-per-render-entry restriction without creating another renderer. Transform topology is now independently bounded and may include non-renderable group/pivot nodes; only the final mapped render-entry world transforms are delegated into the established M92 transaction.

- `OfflineSceneTransformGraph` owns up to 512 immutable topology nodes and a prepared-entry-ordered render binding table. Render binding count remains bounded by the existing 256 scene-entry contract.
- Parent topology accepts arbitrary node order and rejects out-of-range references, self-parenting, cycles, render bindings outside graph ownership, and duplicate render-entry node bindings.
- `OfflineSceneTransformGraphFrameState` carries one validated camera plus exactly one complete finite affine local transform per graph node.
- Graph preparation resolves `parent_world * local` for **every** graph node, including transform-only nodes that are not directly mapped to rendering. Hidden invalid/projective state therefore cannot bypass validation merely because no draw references that node.
- Only after the complete graph frame resolves successfully are world matrices extracted in prepared-scene entry order through the graph's render bindings. The complete resulting batch is then delegated to M92 `prepare_offline_frame_sequence`.
- A 1:1 graph is exact resolved/hash and 4x per-sample RGB/depth/stencil equivalent to M96 hierarchy execution.
- A three-node graph with one non-renderable pivot and two render-bound children is exact-equivalent to independently composed M92 world matrices, and moving/scaling the pivot observably moves both render descendants.
- Regression coverage locks render-binding cardinality, local-transform cardinality, bounded node/frame ownership, hidden transform-only projective rejection, and later finite transform-only ancestor composition overflow before any prepared sequence can execute.
- Camera-dependent ordering, conservative visibility, reflection rebinding, target preflight, indexed execution, and raster ownership remain unchanged on M92. M99 adds no graph file syntax, mutable/reparenting topology, skeletal deformation, constraints/IK, animation blending, parallel execution, or performance claim.

## Milestone 100 — bounded programmatic transform-graph timeline

M100 adds time-domain evaluation over M99's decoupled transform graph while keeping one interpolation semantic core and the established M92 execution transaction.

- `OfflineSceneTransformGraphTimelineKeyframe` binds one finite scalar time to one validated camera plus exactly one graph-local affine transform per immutable graph node.
- Graph timelines require 2..256 finite strictly increasing keyframes and accept 0..256 caller-ordered finite sample requests. Every keyframe is fully validated even when the requested sample span is empty.
- M94 flat timelines, M97 hierarchical timelines, and M100 graph timelines now share one exact-keyframe/bracketing/interpolation-parameter core. The three public adapters own only their respective transform-vector interpolation, preventing a third independent timeline semantic path.
- Exact keyframe requests copy stored state without interpolation arithmetic. Interior graph samples reuse the established M94 camera interpolation and affine top-3x4 interpolation with exact affine bottom-row preservation.
- All graph-node locals are interpolated independently **before** topology composition. Every sampled graph frame is then delegated to M99, which resolves and validates every transform-only/render-bound node before extracting prepared-entry worlds and entering M92.
- A 1:1 graph timeline is exact resolved/hash and 4x per-sample RGB/depth/stencil equivalent to the corresponding M97 hierarchical timeline for endpoint, interior, repeated, and out-of-order requests.
- A transform-only animated pivot regression proves local-interpolate-then-compose semantics: its midpoint differs from endpoint-world interpolation and is exact-equivalent to an independently composed M92 world frame.
- Validation regressions cover underspecified/non-increasing keyframes, graph-local cardinality mismatch, prepared-scene render-binding mismatch, projective keyframes, out-of-domain/oversized sample spans, and valid empty requests.
- A pair of individually valid endpoint graph frames whose interpolated transform-only parent/child scales overflow only after midpoint composition rejects the complete timeline before any earlier requested sample can execute fragments.
- M100 adds no file grammar, easing/looping/extrapolation, topology animation/reparenting, skeletal deformation, constraints/IK, blending layers, parallel execution, alternate raster path, or performance claim.

## Milestone 101 — strict file-driven transform-graph timeline transaction

M101 exposes the complete M100/M99 graph-time semantics through one strict bounded sidecar and the existing mixed-scene indexed-output transaction. Parsing owns explicit file state only; interpolation, graph composition, prepared-scene evaluation, and raster execution remain delegated to the established programmatic layers.

- `tiny-renderer-transform-graph-timeline-v1` uses explicit `node NODE root|PARENT_NODE` topology and `bind ENTRY NODE` prepared-entry mappings. Records may appear in arbitrary order before dynamic state begins.
- Node ids are explicit contiguous indices from zero and remain bounded to 512. Missing/duplicate nodes, undeclared/out-of-range parents, self-parenting, cycles, missing/duplicate entry bindings, undeclared binding targets, and multiple entries bound to one graph node reject deterministically.
- Every `keyframe TIME <camera>` owns explicit order-independent `local NODE <16-value affine>` records and terminates with `end`. Exactly one validated local record per graph node is required, including transform-only nodes.
- The shared file parser primitives now expose a generic affine-record parser while preserving existing model-record diagnostics. Graph-local parsing reuses the same finite-number, camera, affine, and strict index grammar as earlier sequence formats.
- Parsing performs no timeline interpolation and no graph composition. It returns `OfflineSceneTransformGraphTimelineFile` and delegates directly to M100 `prepare_offline_transform_graph_timeline_sequence`, preserving M100 → M99 → M92 as the sole semantic/execution chain.
- `--transform-graph-timeline-sequence FILE` is a fifth mutually exclusive sequence mode on the existing `.trscene` / `ordering mixed-transparency` transaction. Indexed rendering and file naming reuse the established prepared-sequence output loop.
- The sequence input contract is now explicit for both hierarchical and transform-graph timeline modes: direct `.obj` input rejects immediately rather than silently falling through to ordinary model rendering.
- File-driven graph topology/bindings/keyframes/samples are exact resolved/hash and 4x per-sample RGB/depth/stencil equivalent to independently constructed programmatic M100 state. Repeated requests remain byte-deterministic at the CLI boundary.
- A file whose endpoint graph-local state is finite but whose requested interior parent/child composition overflows is rejected during complete M100/M99 preparation before any earlier indexed output exists.
- M101 adds no interpolation policy, mutable/reparenting topology, skeletal deformation, constraints/IK, easing/looping/extrapolation, asynchronous/parallel execution, alternate renderer path, or performance/conformance claim.

## Milestone 102 — bounded programmatic sparse transform-graph animation clip

M102 removes the dense-timeline requirement that every keyframe repeat camera plus every graph-node local transform. One immutable validated clip now owns a bounded global time domain, default graph-local state, an optional camera track, and independent transform tracks only for nodes that actually animate.

- `OfflineSceneSparseTransformGraphClip` validates its complete intrinsic ownership at construction: finite increasing clip domain, validated default camera, one finite affine default local per graph node, at most one transform track per node, and all track state before sampling can begin.
- Camera and transform tracks each require 2..256 finite strictly increasing keys and must cover the complete clip domain exactly. Sparse clips additionally cap aggregate animated key ownership at 4096 records; this is an in-memory resource bound, not a performance claim.
- Untracked graph nodes never acquire synthetic keys. Their default local matrices are copied bit-exact into every sampled complete graph-local frame.
- The M94/M97/M100 time-bracketing core is factored down to `sample_offline_keyframe_value`. Dense timeline sampling and sparse camera/transform tracks therefore share exact-keyframe passthrough, domain checking, bracketing, and interpolation-parameter semantics.
- Sparse camera tracks reuse the established camera interpolation primitive; transform tracks reuse the established affine top-3x4 interpolation with exact affine bottom-row semantics. M102 introduces no second interpolation formula.
- `sample_offline_sparse_transform_graph_clip` accepts at most 256 caller-ordered finite sample times. Every request materializes one **complete** graph-local frame by copying defaults first and overlaying only animated tracks.
- `prepare_offline_sparse_transform_graph_clip_sequence` validates clip/graph/prepared-entry ownership, materializes the complete requested sparse batch, then delegates only to M99 `prepare_offline_transform_graph_sequence`; graph composition, render-entry extraction, M92 ordering/visibility/preflight, and raster execution remain unchanged.
- A fully tracked sparse clip aligned to an equivalent dense M100 timeline is exact resolved/hash and 4x per-sample RGB/depth/stencil equivalent across endpoint, interior, repeated, and out-of-order requests.
- A clip with only one animated non-renderable pivot keeps both render-bound descendant local matrices bit-exact while observably moving both rendered descendants; it is exact-equivalent to independently materialized M99 graph-local frames.
- Validation regressions cover invalid domains/default state, underspecified or incomplete camera tracks, duplicate/out-of-range transform-track ownership, non-increasing/incomplete/projective transform tracks, aggregate key and sample bounds, graph-node ownership mismatch, and valid empty sample requests.
- A sparse clip whose endpoint locals are individually valid but whose requested interior transform-only parent/child composition overflows rejects the complete batch before any earlier requested sample can execute fragments.
- M102 adds no sparse-clip file syntax, easing curves, looping/extrapolation, topology animation/reparenting, skeletal skinning, animation blending/layers, parallel execution, alternate raster path, or performance claim.

## Milestone 103 — bounded two-clip local-space transform-graph blending

M103 composes two independently authored M102 sparse clips over one immutable transform graph without densifying either clip and without introducing a world-space blending shortcut.

- `blend_offline_sparse_transform_graph_clips` accepts two validated M102 clips, a caller-ordered bounded sample span, and one finite blend weight in `[0,1]`.
- Both clips are sampled independently through the complete M102 path before any passthrough or blend result is returned. A requested time that is invalid for either clip rejects the whole request, including blend weights `0` and `1`.
- The two clips must materialize equal graph-local ownership. Weight `0` returns the complete left sampled frames and weight `1` returns the complete right sampled frames without camera/local blend arithmetic.
- Interior weights reuse the established camera and affine interpolation primitives on **complete graph-local frames** only. Every graph node is blended in local space before any parent/child composition.
- `prepare_offline_sparse_transform_graph_clip_blend_sequence` validates prepared-entry bindings plus both clip node counts against the immutable graph, then delegates the fully blended local-frame batch to M99 `prepare_offline_transform_graph_sequence`; M99/M92 remain the only graph/world/preflight/raster execution path.
- Endpoint weights are exact resolved/hash and 4x per-sample RGB/depth/stencil equivalent to preparing the corresponding M102 source clip directly across repeated and out-of-order requests.
- An interior-weight case with one transform-only pivot and two render descendants is exact-equivalent to an independently constructed local-blend-then-M99 frame batch. Repeated requests remain deterministic and the blended pivot observably moves both descendants.
- Validation regressions cover non-finite/out-of-range weights, unequal clip graph-local ownership, sample times outside either clip domain even at endpoint weights, graph-node ownership mismatch, and valid empty sample requests.
- A batch whose first blended sample composes safely but whose later sample only overflows after local-space parent/child blending rejects the complete batch before any earlier sample can execute fragments. This also guards against replacing local-space blending with world-matrix blending.
- M103 adds no file syntax, per-node blend masks, additive blending, N-way layers, source-time remapping, easing/looping/extrapolation, topology animation/reparenting, skeletal skinning, parallel execution, alternate renderer path, or performance claim.

## Milestone 104 — bounded programmatic independent-time blend schedule

M104 lifts M103's same-source-time and constant-weight restriction while preserving the exact same local-space blend and M99/M92 transaction semantics.

- `OfflineSceneSparseClipBlendScheduleEntry` owns one output record with finite independent `left_time`, finite independent `right_time`, and one finite blend `weight` in `[0,1]`.
- A schedule contains at most 256 caller-ordered records. The complete schedule is checked for finite source times and valid weights before source sampling begins.
- Left and right source-time arrays are built independently and materialized as two complete M102 batches. Clips may therefore use different finite domains/durations without looping, extrapolation, or retiming hidden inside the sampler.
- M104 factors complete-frame combination into `detail::blend_offline_transform_graph_frame_state`. M103 constant-weight blending and M104 scheduled blending share this primitive, including exact weight-0/weight-1 passthrough and established camera/affine interpolation.
- No scheduled output frame is blended until **both** complete source batches have materialized successfully. A later invalid source time in either clip therefore rejects before any scheduled frame reaches M99/M92.
- `blend_offline_sparse_transform_graph_clip_schedule` blends each matched complete local frame using that schedule record's weight; parent/child composition remains absent from this layer.
- `prepare_offline_sparse_transform_graph_clip_blend_schedule_sequence` validates prepared-entry bindings and both clip node counts against the immutable graph, then delegates the complete scheduled local-frame batch to M99 and finally M92.
- A schedule whose left/right times are equal and whose weights are constant is exact resolved/hash and 4x per-sample RGB/depth/stencil equivalent to M103.
- A cross-fade regression uses different left/right clip domains, independently advancing source times, endpoint/interior weights, repeated requests, and out-of-order records; it is exact-equivalent to independently sampled M102 source frames, established local interpolation, and M99 preparation.
- Validation regressions cover unequal clip ownership, non-finite source times, invalid per-record weights, schedule bound overflow, graph-node ownership mismatch, later invalid right-source time, invalid right time hidden behind weight zero, and valid empty schedules.
- A first safe scheduled sample followed by a later independently timed local blend whose transform-only parent/child composition overflows rejects the complete batch before any earlier sample can execute fragments.
- M104 adds no file grammar, per-node masks, additive/N-way layers, easing/looping/extrapolation, topology animation/reparenting, skeletal skinning, parallel execution, alternate renderer path, or performance claim.

## Milestone 105 — bounded programmatic per-node blend mask

M105 adds independently controllable graph-local blend regions on top of M104 without changing source-time scheduling, transform-graph composition, or renderer ownership.

- `OfflineSceneSparseClipBlendMask` is immutable after construction and owns one finite camera weight plus at most 512 finite node weights, all constrained to `[0,1]`.
- Mask cardinality is graph-aligned at use time: masked blending rejects unless the mask owns exactly one node weight for every complete graph-local transform.
- M104 schedule validation/source sampling is factored into one shared `materialize_offline_sparse_clip_blend_schedule_sources` transaction. M104 and M105 therefore validate all schedule records and materialize both complete M102 source batches through the same path before any output blend exists.
- A masked frame uses `effective_weight = schedule.weight * mask.weight` independently for camera and each graph node. Effective weight zero copies the left source state exactly; effective weight one copies the right state exactly; interior values reuse the established camera/affine interpolation primitives.
- Masking is applied only to complete graph-local source frames. No parent/child composition, render-entry extraction, visibility operation, or world-space patch occurs inside the mask layer.
- `prepare_offline_sparse_transform_graph_clip_blend_schedule_masked_sequence` validates prepared-entry bindings, both clip node counts, and mask node ownership against the immutable graph, then delegates the complete masked local-frame batch to M99 and M92.
- An all-ones camera/node mask is exact resolved/hash and 4x per-sample RGB/depth/stencil equivalent to M104 across endpoint/interior/repeated schedule records.
- A regional mask blends a transform-only pivot, pins one render-bound child at zero local weight, partially blends another child, and pins the camera left. The pinned child's local transform remains bit-exact while its world transform still moves through the blended ancestor under ordinary M99 composition.
- The regional masked result is exact-equivalent to an independently materialized masked-local M99 reference, proving node-local control without a second world-transform path.
- Validation regressions cover non-finite/out-of-range camera or node mask weights, mask capacity overflow, mask/clip/graph ownership mismatch, later invalid source time, and valid empty schedules.
- A first safe frame followed by a later frame whose half-weight masked transform-only parent/child locals overflow only after M99 composition rejects the complete batch before any earlier fragment execution.
- M105 adds no file syntax, additive blending, N-way layer stacks, skeletal deformation, easing/looping/extrapolation, topology animation, parallel execution, alternate raster path, or performance claim.

## Milestone 106 — bounded programmatic single-pose linear-blend skinning

M106 moves the renderer beyond rigid transform-graph animation into bounded vertex deformation while preserving the existing object-space/raster execution chain.

- `SkinningState` is immutable after construction and owns one canonical-vertex-aligned binding table plus a bounded palette of 1..256 caller-supplied affine skin matrices.
- Every `VertexSkinBinding` owns 1..4 influences. Influence weights must be finite and non-negative with a finite positive total; joint indices must reference the owned palette.
- Skin matrices must contain only finite values and remain affine. The slice deliberately accepts already-composed skin matrices rather than claiming skeleton import, inverse-bind construction, IK, retargeting, or hierarchy solving.
- Object-space position deformation uses deterministic normalized linear-blend skinning. Weighted accumulation is performed in double precision, converted back to bounded float positions only after normalization, and rejects non-finite/projective/unsafe outputs beyond the established M35 `1e20` position envelope.
- `prepare_object_space_mesh` is the single model/shadow deformation gateway: canonical mesh → optional skinning → optional M35 `VertexProgram`. No-skin/no-program submissions retain the original zero-copy canonical mesh path.
- Direct model rendering, prepared models, prepared instances, heterogeneous prepared lists, and directional/point/spot shadow capture all consume that same prepared object-space mesh. Nested rasterizers continue with an empty vertex-program binding, so deformation cannot execute twice.
- Prepared submissions retain the shared immutable skin binding/palette lifetime independently from the caller's source handle.
- A one-joint identity skin is exact resolved RGB and per-sample depth/stencil equivalent to no-skin rendering, including prepared execution after the caller releases its original skin handle.
- A multi-joint pose is exact-equivalent to independently pre-deformed manual object-space geometry for direct and prepared execution. A separate regression composes skinning with M35 and proves skinning executes first.
- Heterogeneous list preparation materializes every complete skinned/programmed mesh before any draw executes. A later skin pose whose finite affine matrix produces an unsafe position therefore rejects the whole list before an earlier valid entry can mutate RGB/depth/stencil.
- Camera rendering and directional shadow capture are exact-equivalent to the same independently pre-deformed manual silhouette, proving both passes consume one deformation semantic path.
- Canonical prepared sorting/spatial visibility explicitly reject skinning because their immutable canonical bounds cannot represent position-changing deformation.
- M106 deforms **positions only**. Any enabled fixed-light path is rejected while skinning is active, preventing stale object-space normal/tangent varyings from being presented as correct skinned lighting. This also keeps normal-map shading outside the claimed surface until its normal semantics exist.
- M106 adds no skeletal file syntax, animated joint palettes, inverse-bind ownership, skinned normals/tangents, dual-quaternion skinning, morph targets, IK/constraints, GPU shaders, parallel execution, or performance claim.

## Milestone 107 — normal-aware linear-blend skinning

M107 removes M106's deliberate fixed-light restriction by extending the same object-space skinning pass to the renderer's existing normal-binding semantics.

- No second skin-normal channel API is introduced. Model preparation derives the unique active `NormalBinding` from the already-validated directional/point/fixed/environment-light contract and the material draws that actually consume it.
- Shadow-only preparation still supplies no normal binding, so depth capture continues to skin positions only while sharing the exact M106 silhouette path.
- When an active normal binding exists, every joint skin matrix is converted through the established finite/stable inverse-transpose `normal_matrix` primitive before any vertex is emitted. Singular or unstable joint linear transforms therefore fail closed for lit skinning without weakening position-only shadow semantics.
- The active normal channels must exist on every canonical vertex and keep one interpolation qualifier, matching the existing raster normal-binding contract.
- Each source normal must be finite and non-zero. The same 1..4 vertex influences used for position LBS weight the joint-transformed normals in double precision; the complete weighted result must remain finite/non-zero and is deterministically normalized before being written back to the exact existing varying channels.
- All non-normal varying values, varying count, interpolation qualifiers, triangle topology, and material ownership remain unchanged. Normal skinning occurs before M35 `VertexProgram`, so custom object-space vertex processing sees the already-skinned position/normal state.
- Direct models, prepared models/instances/lists, and their existing preflight transaction all route through one `prepare_model_object_space_mesh` helper. There is no separate lit-skinned renderer path.
- Identity skinning with an active fixed light is exact resolved RGB and per-sample depth/stencil equivalent to the canonical fixed-light path, including prepared execution.
- A non-uniformly scaled/rotated multi-joint pose is exact-equivalent to an independently materialized position+normal mesh for direct and prepared fixed-light rendering.
- A normal-map differential uses the same multi-joint pose and an independent manual mesh; exact framebuffer equivalence proves tangent frames are derived from already-skinned positions and tangent-space shading consumes the skinned geometric normal.
- Validation regressions cover out-of-range active normal channels, singular joint normal matrices, and two valid joint normal transforms whose weighted directions cancel to zero.
- Heterogeneous prepared-list regression places a valid lit skinned entry before a later singular-normal entry and proves the complete list rejects before earlier RGB/depth/stencil ownership.
- M107 remains programmatic single-pose LBS. It adds no skeleton topology, inverse-bind ownership, animated palettes, skeletal file import, dual-quaternion skinning, morph targets, IK/constraints, GPU execution, parallel path, or performance claim.

## Milestone 108 — bounded programmatic skeletal pose resolver

M108 adds explicit immutable skeletal ownership above M107 without creating a second deformation or raster path.

- `SkeletalRig` owns 1..256 joints, one optional parent per joint, one finite affine inverse-bind matrix per joint, and the canonical-vertex skin bindings that reference those joints.
- Joint declaration order is arbitrary. Construction rejects out-of-range/self parents, cycles, inverse-bind count mismatch, non-finite/projective inverse binds, empty/oversized rigs, empty vertex ownership, and influences that reference joints outside the rig.
- `SkeletalPoseState` owns one shared immutable rig plus exactly one finite affine local transform per joint. Intrinsic local validation occurs at construction, but hierarchy composition is intentionally deferred.
- Pose resolution recursively computes every world joint as `parent_world * local`, validates every composed world transform, then computes every final skin matrix as `world_joint * inverse_bind`.
- The resolved palette is materialized only through the existing `SkinningState` contract. M108 therefore cannot bypass M106/M107 influence, affine, position, normal, and joint-palette validation.
- `ModelRenderOptions` accepts either a direct resolved `SkinningState` or one deferred `SkeletalPoseState`, never both. Canonical mesh/binding cardinality is validated at prepared-model construction.
- The single `prepare_object_space_mesh` gateway resolves a deferred skeletal pose into a temporary immutable M107 skin state before any LBS/M35 work. Camera rendering, prepared models/instances/lists, and all directional/point/spot shadow capture therefore keep the established deformation/raster paths.
- Heterogeneous prepared-list preparation resolves every deferred pose and completes every object-space mesh before the first draw. A later valid-local pose whose `world * inverse_bind` overflows therefore rejects the complete list before an earlier valid entry can own RGB/depth/stencil.
- Root-only skeleton resolution is exact-equivalent to independently precomposed M107 skin matrices under normal-aware fixed lighting.
- An arbitrary-order chain whose logical root is declared after a child is exact-equivalent to independently composed M107 skin matrices for camera output and shadow depth, including after caller rig/pose handles are released; the prepared submission owns the shared immutable state.
- A bind-compatible two-joint pose whose world transforms cancel the owned inverse binds reduces exactly to canonical geometry, including blended vertex influence ownership.
- Validation regressions cover bounded capacity, inverse-bind cardinality, out-of-range/self/cyclic parents, projective inverse binds, invalid joint influence ownership, null/mismatched/projective poses, direct+skeletal binding conflict, mesh/binding ownership mismatch, parent-composition overflow, and final `world * inverse_bind` overflow.
- Canonical painter sorting and prepared spatial planning reject deferred skeletal poses because immutable canonical bounds cannot represent position-changing skeletal execution.
- M108 is programmatic single-pose skeletal resolution only. It adds no skeletal timeline, file/glTF skin import, retargeting, IK/constraints, dual-quaternion skinning, morph targets, GPU execution, parallel path, or performance claim.

## Milestone 109 — bounded programmatic skeletal local-pose timeline

M109 adds temporal joint-local state above M108 while deliberately reusing one shared affine timeline core instead of creating a skeletal-only interpolation path.

- `affine_timeline.hpp` now owns the low-level finite affine validator, top-3x4 affine interpolation primitive, and exact-keyframe/bracketing sampler used by both the established offline animation stack and skeletal animation.
- Existing M94/M100-facing `interpolate_offline_timeline_affine` and `sample_offline_keyframe_value` APIs remain intact but delegate to the shared generic primitive, preserving the established endpoint-copy and interior affine semantics without reverse-depending skinning on the offline renderer.
- `SkeletalPoseTimeline` owns one immutable M108 `SkeletalRig` plus 2..256 finite strictly increasing dense keyframes. Every keyframe owns exactly one finite affine local matrix per joint and is resolved once through M108 at construction so malformed endpoint poses cannot survive into sampling.
- Sampling accepts 0..256 caller-ordered finite times inside the closed keyframe domain. Exact keyframe requests copy the stored local matrices without interpolation arithmetic; repeated and out-of-order requests preserve caller order and are deterministic.
- Interior samples interpolate every joint's **local** top-3x4 affine state first, preserve the affine bottom row exactly, then construct a complete `SkeletalPoseState`.
- Every requested sampled pose is fully resolved through M108 before the sampled batch is returned. Hierarchy composition and `world_joint * inverse_bind` therefore remain downstream of local interpolation, and a later invalid sample prevents any partial batch from escaping.
- Root-only timeline samples under normal-aware fixed lighting are exact-equivalent to independently interpolated-local M108 poses, including exact first/final keyframe state.
- An arbitrary-order two-joint chain declares its child before its root, animates both parent scale and child translation, and proves the midpoint child world translation is exactly the local-interpolate-then-compose value rather than endpoint-world interpolation. The sampled result is exact-equivalent to an independently constructed M108 reference for fixed-light framebuffer output and directional shadow depth.
- The same midpoint regression makes the semantic difference observable: local interpolation produces child world X translation 2 while endpoint-world interpolation would produce 3.
- Validation regressions cover null rig, fewer than two or more than 256 keyframes, non-finite/non-increasing times, wrong local cardinality, projective locals, non-finite/out-of-domain samples, and more than 256 sample requests.
- A dedicated overflow construction keeps both endpoint poses executable while a later midpoint creates an overflowing hierarchy composition only after local interpolation. Sampling `{endpoint, midpoint}` rejects the complete batch before the caller can render the earlier valid request, and the sentinel framebuffer remains bit-exact.
- M109 is dense programmatic skeletal animation only. It adds no sparse skeletal tracks, clip blending, file/glTF animation parsing, easing/looping/extrapolation, retargeting, IK/constraints, dual-quaternion skinning, morph targets, GPU execution, parallel path, or performance claim.

## Milestone 110 — bounded glTF 2.0 skinned-asset interoperability

M110 turns the programmatic M108/M109 skeletal stack into a strict real-file interoperability path without adding a second mesh, hierarchy, skinning, lighting, shadow, or raster implementation.

- `GltfSkinnedAsset` projects one textual glTF 2.0 skinned mesh directly onto the existing `ModelAsset`, immutable M108 `SkeletalRig`, complete rest local pose, and optional normal-channel metadata.
- The importer accepts one sibling external binary buffer only. GLB, data/network URIs, directory traversal, URI schemes, extensions, sparse accessors, morph targets, multiple skins, animation objects, compressed geometry, primitive materials, and unsupported attributes are rejected rather than ignored as if supported.
- A bounded internal JSON parser rejects malformed syntax, non-finite/unrepresentable numbers, excessive nesting, unescaped control bytes, malformed Unicode escapes/surrogates, and duplicate object keys before schema projection.
- JSON is capped at 1 MiB and the external buffer at 64 MiB. Buffer-view, accessor, stride, element-size, absolute-offset, and range arithmetic uses checked add/multiply before any typed binary read; component alignment and bounded glTF stride constraints are enforced.
- The first import slice supports one indexed TRIANGLES primitive with float `POSITION` carrying required finite three-component `min`/`max`, optional float `NORMAL`, unsigned-byte/unsigned-short `JOINTS_0`, float `WEIGHTS_0`, and unsigned-byte/unsigned-short/unsigned-int indices. POSITION data must remain inside its declared bounds; unsupported accessor types, component modes, normalized accessors, or mismatched attribute counts fail closed.
- `WEIGHTS_0` must be finite, non-negative, and sum to one within the bounded importer tolerance. Zero-weight slots are removed; remaining 1..4 influences become the established `VertexSkinBinding`, with duplicate non-zero joints and joint indices outside the skin rejected.
- glTF node transforms support either column-major affine `matrix` or finite TRS state, never both. TRS composes as `T * R * S`; rotation is a finite unit XYZW quaternion. The complete node graph validates child ownership, range, self/cycles, and one-parent topology before skeletal projection.
- The one skin owns 1..256 unique joint nodes plus one required float MAT4 `inverseBindMatrices` accessor with exact joint-count ownership in this bounded subset. Imported matrices are converted from glTF column-major storage and validated through the established affine/M108 contracts.
- Skin joint declaration order is independent from node declaration/hierarchy order. For each joint, non-joint ancestors between it and the nearest joint ancestor are folded into that joint's local rest transform; the nearest joint ancestor becomes the M108 parent index. The resulting topology therefore reuses M108's arbitrary-order resolver rather than creating an importer-only hierarchy evaluator.
- The transform of the node instantiating a skinned mesh is deliberately not applied to the skinned vertex data; only the joint transforms drive skinning, matching the glTF skinning contract. The bounded slice also rejects a mesh instance that is itself a joint or an ancestor of a joint to keep ownership unambiguous.
- Imported `NORMAL` data maps onto smooth canonical varying channels 0/1/2; imported geometry becomes one ordinary `ModelAsset` draw. Fixed lighting, M35 object-space programs, M107 normal-aware LBS, prepared execution, and directional/point/spot shadow paths are unchanged.
- The in-repo fixture owns a real external binary blob. Its `skin.joints` order is child-before-root while the node graph is root→child, its inverse binds cancel the rest joint worlds, and its skinned mesh node carries a non-identity transform specifically to lock the ignored-mesh-node-transform rule.
- Differential regressions compare the imported asset and rig against a separately constructed programmatic `ModelAsset + SkeletalRig + SkeletalPoseState` under a non-trivial posed root scale/child translation. 4× fixed-light RGB/depth/stencil and directional-shadow depth must match exactly.
- Regression coverage also rejects malformed JSON, duplicate semantic keys, unsafe buffer URIs, unsupported primitive mode/normalized accessors, missing required attributes, truncated/excess buffer ranges, out-of-range joints, negative/non-unit-sum weights, projective inverse binds, invalid joint-node ranges, cyclic node graphs, and disconnected joint roots.
- Imported objects retain no dependency on source-file lifetime after load: a copied glTF+buffer may be deleted before ordinary M108 rendering.
- M110 is a deliberately bounded interoperability slice, not a general glTF conformance claim. It imports no textures/materials, scenes as runtime objects, animation samplers/channels, morph targets, GLB, extensions, or compressed geometry.

## Milestone 111 — bounded semantic TRS skeletal animation

M111 inserts the semantic animation layer required between static M110 glTF skin import and any file-driven animation channel import. It deliberately keeps interpolation in joint-local translation/rotation/scale space and delegates completed poses to M108 rather than extending M109's affine-matrix interpolation semantics.

- `Quaternion` is now a shared renderer primitive with finite unit-length validation, deterministic quaternion-to-matrix conversion, normalization, and shortest-path spherical interpolation. Antipodal endpoints are sign-canonicalized before interpolation; nearly identical endpoints use normalized linear fallback to avoid unstable spherical division.
- M110 static glTF node TRS no longer owns a private quaternion-to-matrix implementation. Imported rest-pose rotation now uses the same shared `Quaternion` validation/matrix path as semantic skeletal animation, preventing rest and animation rotation semantics from diverging.
- `SkeletalTrs` owns one joint-local translation, unit XYZW quaternion rotation, and scale. Local matrices are materialized only as `T * R * S` and pass the established bounded affine validator before entering M108.
- `SkeletalTrsClip` owns one immutable M108 rig, a finite strictly increasing clip domain, one complete default semantic TRS state per joint, and sparse independent per-joint translation/rotation/scale tracks.
- Each property track owns 1..256 finite strictly increasing keys inside the clip domain. At most one track of each property may own one joint, joint indices must belong to the rig, aggregate property ownership is capped at 4096 keys, and requested sample batches are capped at 256 times.
- Property key domains may be narrower than the clip domain. Before the first key and after the last key, the nearest semantic endpoint is held; interior translation/scale use component-wise LINEAR interpolation and interior rotation uses shortest-path quaternion slerp. This avoids undefined extrapolation while allowing different joints/properties to advance on independent key domains.
- Exact key requests return the stored semantic property value before matrix composition; no interpolation arithmetic is performed at an exact property key.
- Sampling first copies the complete default semantic pose, overlays every animated property for that request, composes all joint-local `T * R * S` matrices, constructs one complete `SkeletalPoseState`, and resolves it through M108. World-joint and final skin matrices are never interpolated.
- A complete requested batch remains transaction-local until every sampled pose has resolved successfully. A later finite-local pose whose parent composition overflows therefore rejects the batch before an earlier valid requested pose can be rendered.
- Regressions lock independent property-domain hold semantics, exact-key behavior, translation/scale LINEAR sampling, shortest-path antipodal quaternion handling, repeated deterministic requests, and all key/sample ownership bounds.
- A half-turn Z rotation sampled at the midpoint produces the semantic quarter-turn quaternion result and is observably different from M109-style affine matrix-element interpolation, which would collapse the same rotation basis toward a singular midpoint.
- An arbitrary-order two-joint chain declares the child before its parent, animates child translation plus parent rotation/scale, and proves semantic local interpolation happens before M108 hierarchy composition. The sampled pose is exact-equivalent to an independently constructed M108 local-pose reference for 4x fixed-light framebuffer output and directional-shadow depth.
- Validation covers null rig, invalid clip domain, default-pose cardinality, non-finite semantic vectors, non-unit quaternions, duplicate property ownership, out-of-range joints, non-increasing/out-of-domain keys, per-track/aggregate key capacity, non-finite/out-of-domain sample requests, and sample-batch capacity.
- M111 is programmatic and LINEAR-only. STEP/CUBICSPLINE, file/glTF animation samplers or channels, clip blending, retargeting, IK/constraints, dual-quaternion skinning, morph targets, GPU execution, and performance claims remain outside the slice.

## Milestone 112 — strict glTF 2.0 LINEAR skeletal animation interoperability

M112 connects the strict M110 textual glTF skin importer to the M111 semantic TRS evaluator without adding a second animation runtime.

- The static `load_gltf_skinned_asset_file` and animated `load_gltf_skinned_animated_asset_file` now share one JSON/buffer/accessor/node/skin projection pipeline. Static import continues to reject an `animations` member; animated import requires exactly one supported animation and returns the ordinary M110 asset plus one immutable M111 clip.
- M111 gains an immutable per-joint affine local-prefix layer, identity by default for all existing callers. A semantic local now materializes as `prefix * T * R * S`, with prefix cardinality and affine validity checked at construction and the composed local checked again before M108 resolution.
- The prefix closes a real M110/M111 integration gap: any static non-joint nodes between a joint and its nearest joint ancestor are folded into that joint's immutable prefix, while the joint node's own TRS remains semantic animation state. This preserves arbitrary affine static intermediary transforms without pretending they can be uniquely decomposed into animated TRS.
- A matrix-backed joint remains valid when static: its own matrix is folded into its immutable prefix and its semantic state is identity. Animation channels targeting a matrix-backed joint fail closed because an arbitrary affine matrix has no unique translation/rotation/scale decomposition for this bounded contract.
- Node declaration order and `skin.joints` order remain independent. The importer builds an exact node-index→M108-joint-index map before channel projection, so child-before-parent skin declarations do not affect animation ownership.
- Animated import accepts exactly one animation with 1..256 samplers and 1..256 channels. Sampler input/output accessor references are range checked and reuse the M110 checked bufferView/accessor window path.
- Sampler input is finite float SCALAR with 1..256 strictly increasing keys. Translation/scale output is finite float VEC3; rotation output is finite float VEC4 and is validated as unit quaternion by M111. Input/output counts must match exactly; normalized/sparse/unsupported component encodings remain rejected by the shared accessor contract.
- Missing sampler interpolation defaults to `LINEAR`; explicit `LINEAR` is accepted. STEP and CUBICSPLINE are rejected rather than silently approximated.
- Channels must target imported skin-joint nodes and exactly one of translation/rotation/scale. Non-joint targets, matrix-backed animated joints, unsupported target paths, bad sampler references, and duplicate joint/property ownership fail closed.
- The clip domain is the minimum first key through maximum last key across accepted channels and must be strictly increasing. Different properties may retain independent narrower key domains; M111 endpoint-hold behavior applies outside each property's own keys.
- Unanimated joint properties come from the imported node's glTF TRS defaults. Static non-joint ancestry stays in the immutable prefix, so animation changes only the target joint property and does not erase intermediary transforms.
- The checked-in animated fixture owns a real 372-byte sibling binary buffer. It deliberately keeps `skin.joints` in child-before-parent order, inserts a static non-joint intermediary between those joints, omits interpolation on one sampler to exercise the LINEAR default, and uses independent translation, rotation, and scale key domains.
- File-driven samples are exact-equivalent to a separately constructed programmatic M111 clip with the same M108 topology and static prefix across endpoint/interior/repeated/out-of-order requests. A midpoint fixed-light 4x framebuffer and directional-shadow depth are exact-equivalent to the independent programmatic reference.
- Negative regressions reject STEP/CUBICSPLINE, out-of-range sampler references, non-joint targets, unsupported target paths, duplicate target ownership, sampler count mismatch, non-increasing times, non-unit quaternion outputs, and animation of matrix-backed joints.
- A file-mutated two-joint animation makes both child and parent X scales reach `1e20` only at the later request. Loading remains valid, but sampling `{safe, overflow}` rejects the complete M111 batch during M108 hierarchy composition before the earlier pose can write framebuffer RGB/depth/stencil.
- M112 is one bounded textual glTF animation interoperability slice, not a general glTF conformance claim. Multiple animations, STEP/CUBICSPLINE execution, morph-weight channels, clip blending/layers, retargeting, IK/constraints, GLB/extensions/compression, GPU execution, and performance claims remain outside the milestone.

## Milestone 113 — bounded programmatic two-clip semantic TRS blending

M113 adds clip composition above M111/M112 without introducing matrix-space animation blending or a second skeletal execution path.

- `SkeletalTrsClip::sample_states` now exposes one complete sampled semantic TRS state together with its already-resolved M108 pose. Existing `sample()` remains source-compatible and simply projects those complete states back to the established pose-only result.
- Source sampling semantics are unchanged: defaults are copied first, sparse translation/rotation/scale tracks overlay semantic properties, immutable prefixes compose as `prefix * T * R * S`, and every sampled source pose is fully M108-resolved before it may participate in blending.
- `SkeletalTrsBlendRequest` owns an independent left source time, right source time, and one finite weight in `[0,1]`. Different clip domains and durations are accepted directly; the API performs no hidden normalization or phase remapping.
- Two clips must own exactly compatible M108 topology, inverse-bind matrices, and vertex skin bindings. Compatibility is structural rather than pointer-identity based, so separately constructed immutable rigs with byte/value-identical ownership are accepted.
- The two clips must also own exactly identical immutable local-prefix matrices. Prefix mismatch rejects before sampling because semantic blending cannot safely combine states that materialize under different static affine ancestry.
- The complete request list is validated first, then the entire left source-time batch and entire right source-time batch are sampled and M108-resolved before the first blended output is created. Even a request with weight exactly 0 or 1 cannot bypass validation of the unselected source batch.
- Weight 0 and weight 1 return the already-resolved left/right source pose directly, preserving exact sampled endpoint state without translation/scale interpolation or quaternion normalization at the blend stage.
- Interior translation and scale blend component-wise in semantic joint-local space. Rotation uses the existing shortest-path quaternion slerp, including antipodal sign canonicalization. Only after every joint's semantic T/R/S is blended is the common prefix applied and the result delegated through M108 hierarchy and skin-palette resolution.
- Local matrices, world-joint transforms, and final skin matrices are never interpolated.
- Regressions use left/right clips with different time domains, independent source times, repeated/out-of-order requests, 0/1/interior weights, separately allocated but structurally identical rigs, non-identity prefixes, arbitrary-order child-before-parent topology, and exactly antipodal half-turn quaternion source values.
- Every blended local pose is exact-equivalent to an independently constructed semantic reference. A representative interior blend is exact-equivalent for 4x normal-aware fixed-light framebuffer RGB/depth/stencil and directional-shadow depth.
- Validation regressions reject incompatible inverse binds, vertex binding ownership, local prefixes, non-finite/out-of-range weights, bounded request overflow, and later invalid source times on either side even when every request selects only the opposite endpoint.
- A dedicated fail-closed regression uses two individually executable clips: one carries a very large parent scale and the other a very large child scale. The endpoint request is safe, while only the later 0.5 semantic blend overflows during M108 parent-child composition. The full blend call rejects before the earlier endpoint can escape for rendering, preserving sentinel RGB/depth/stencil.
- M113 remains two-source programmatic semantic blending only. File selection among multiple glTF animations, per-joint masks/layers, additive animation, N-way graphs/state machines, retargeting, IK/constraints, STEP/CUBICSPLINE execution, GPU execution, and performance claims remain outside the slice.

## Milestone 114 — bounded glTF animation collection with explicit selection

M114 closes the real-file ownership gap above M112/M113 by importing an ordered bounded set of semantic clips while projecting mesh, rig, bind state, semantic defaults, and immutable local prefixes exactly once.

- The strict textual glTF path now exposes `GltfSkinnedAnimationCollection`, containing one ordinary M110 `GltfSkinnedAsset` plus an ordered vector of `GltfImportedAnimation { optional name, clip }`.
- Collection import accepts 1..16 animations. The root asset/buffer/accessor/node/skin projection executes once; every animation then reuses the same M108 rig, node→joint map, M111 default semantic pose, and immutable local-prefix state.
- M112's LINEAR sampler/channel parser is now a reusable per-animation projection. Every animation independently keeps the complete M112 restrictions: bounded samplers/channels, checked accessor windows, translation/rotation/scale joint targets only, semantic TRS joints only, unique joint/property ownership, finite strictly increasing input times, exact input/output cardinality, and LINEAR/omitted-LINEAR interpolation only.
- Animation array order is preserved exactly. Optional glTF animation names are retained as metadata; empty/missing/duplicate names do not become lookup keys or uniqueness requirements.
- Parsing is transactional. The collection remains local until every animation has fully constructed its M111 clip. A malformed later animation therefore rejects the complete import rather than returning an asset with an earlier partial clip set.
- The static M110 loader remains strict and rejects any `animations` member.
- The M112 exactly-one compatibility wrapper now delegates to the collection importer and then requires collection cardinality exactly one. It remains exact-equivalent for a one-animation file and rejects multi-animation files rather than silently choosing the first clip.
- The new two-animation fixture reuses the existing checked 372-byte external buffer. Clip 0, named `FullPose`, retains the M112 translation/rotation/scale animation over domain [0,1]. Clip 1, named `ChildShift`, references only the existing child-translation keys over [0.25,0.75], proving independent domains and property ownership without duplicating binary data.
- Regressions prove one-animation collection sampling is exact-equivalent to the legacy M112 wrapper, two-animation order/name/domain preservation, and acceptance of duplicate animation names as metadata.
- Both file-driven clips feed M113 directly with independent source times. Their endpoint/interior/repeated blend requests are exact-equivalent joint-for-joint to two separately constructed programmatic M111 clips.
- A representative file-driven interior M113 blend is exact-equivalent to the programmatic reference for 4x normal-aware fixed-light RGB/depth/stencil and directional-shadow depth.
- Negative regressions cover more than 16 animations, one valid animation followed by a bad later sampler accessor, invalid later non-joint channel ownership, and use of the exactly-one compatibility wrapper on a multi-animation asset.
- M114 adds no automatic selection policy, animation controller/state machine, blend masks/layers, additive animation, N-way graph, STEP/CUBICSPLINE execution, morph-weight channels, retargeting, IK/constraints, GLB/extensions/compression, GPU execution, or performance claim.

## Promotion after Milestone 114

Multiple real-file clips can now reach M113, but the semantic runtime and importer still deliberately reject glTF `STEP`. The next useful spec/runtime gap is therefore interpolation mode ownership, not another collection or controller shell. Milestone 115 should add **bounded semantic STEP interpolation and strict glTF STEP interoperability** without weakening the existing LINEAR path.

A Milestone 115 slice should require:

- introduce an explicit interpolation mode on M111 translation, rotation, and scale tracks, with the existing constructors/callers defaulting to LINEAR for source compatibility;
- LINEAR must retain current component lerp / shortest-path quaternion slerp behavior exactly; STEP must return the previous key's semantic value for interior times and preserve exact-key values without interpolation arithmetic;
- endpoint-hold semantics outside each sparse track's key domain remain unchanged and independent from interpolation mode;
- track validation must reject unsupported/invalid interpolation modes before sampling, and complete requested batches must remain transactional through M108;
- glTF sampler parsing should accept omitted/explicit LINEAR and explicit STEP, project each sampler's mode onto its resulting semantic track, and continue rejecting CUBICSPLINE rather than approximating it;
- duplicate joint/property ownership remains invalid even when two channels use different interpolation modes;
- regressions should cover translation, scale, and quaternion STEP behavior, exact key boundaries, repeated/out-of-order requests, a value immediately before/at a key, arbitrary-order parent topology, normal-aware fixed lighting, and shadow silhouettes against independent semantic references;
- a mixed LINEAR+STEP glTF fixture should import exact-equivalently to a programmatic M111 clip, including one animation from an M114 collection feeding M113 blending;
- malformed sampler mode, CUBICSPLINE, later STEP sample hierarchy overflow, and collection-level later animation failures must continue to reject before partial output escapes;
- M115 remains LINEAR+STEP semantic interpolation only. CUBICSPLINE tangents, morph weights, automatic animation controllers/state machines, masks/layers, additive/N-way composition, retargeting, IK/constraints, GLB/extensions/compression, GPU execution, and performance claims remain outside the milestone.
