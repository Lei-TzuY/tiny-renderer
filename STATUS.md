# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Architecture frontier: Milestone 104 bounded programmatic independent-time blend schedule

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

## Promotion after Milestone 104

Independent source-time scheduling makes two-clip cross-fades expressive at the batch level. The next architectural limitation is that every graph node still receives the same per-sample blend weight. Milestone 105 should establish a **bounded programmatic per-node blend mask** layered over M104.

A Milestone 105 slice should require:

- one immutable validated mask with exactly one finite weight in `[0,1]` per graph node plus one finite camera weight in `[0,1]`;
- one M104 schedule record still supplies the per-output global blend weight. Effective camera/node weights are bounded products of global weight and the corresponding mask weight;
- an all-ones mask must be exact-equivalent to M104, while a zero node mask must preserve that node's left-source local transform exactly even when other nodes cross-fade;
- mask application occurs only on complete M102 graph-local frames and strictly before M99 composition. A masked transform-only pivot must influence all descendants through ordinary graph composition rather than any world-space patch;
- mask node count must match the immutable graph and both sampled source frames before blending. Non-finite/out-of-range mask weights, ownership mismatch, invalid source times, or invalid schedule weights reject the whole batch before M99/M92;
- a regression should blend one transform-only pivot while pinning one render-bound child to the left clip, proving independently controllable local graph regions and exact comparison against a manually materialized masked-local M99 reference;
- later masked local/composed overflow must reject the complete requested batch before any earlier frame executes;
- M105 remains programmatic and two-source only. It adds no file syntax, additive blending, N-way layer stacks, skeletal skinning, easing/looping/extrapolation, topology animation, parallel execution, alternate raster path, or performance claim.
