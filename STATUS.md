# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 85

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view. Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestones 61–71 expand canonical asset/material semantics with bounded OBJ vertex color, MTL emission, deterministic anisotropy, glossy environment-reflection mip policies, and owned `map_Ks` / `map_Ke` / `map_Ns` material textures while preserving shared sampler, prepared ownership, direct/environment shading, inspection/fingerprint, and fail-closed validation contracts.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. Milestones 74–80 promote prepared-list execution into bounded flat-scene and CLI transactions with combined-bounds auto-fit, optional explicit perspective camera, scene material/shading overrides, explicit `opaque` / `source-alpha` / `alpha-to-coverage` policies, and `ordering mixed-transparency`. Mixed transparency executes opaque/A2C work in its depth-writing phase and source-alpha work in stable far-to-near order, with target-dependent preflight before framebuffer mutation.

Milestone 81 establishes the prepared per-draw spatial contract. `PreparedSpatialSubmission` owns the same validated `PreparedModelSubmission` snapshot plus one immutable object-space AABB/center record for each canonical `MaterialDraw`. Milestone 82 makes that per-draw plan executable through complete target preflight and canonical `draw_mesh_range` submission. Milestone 83 adds conservative prepared-draw frustum visibility: all eight prepared AABB corners are transformed to homogeneous clip space and a draw is rejected only when every corner is provably outside the same clip half-space; boundary and numerically ambiguous cases are retained.

Milestone 84 consumes conservative visibility in the real mixed-transparency offline source-alpha phase while retaining complete unfiltered-plan target preflight before mutation. Milestone 85 extends the same contract across the complete mixed-transparency transaction: opaque/A2C work is flattened in caller/canonical order, source-alpha work remains stable far-to-near, both complete plans are preflighted before clear/environment/geometry mutation, and only visibility-retained subsets execute through the existing prepared draw executor. A 4x mixed scene with off-frustum depth-writing work is regression-locked to exact resolved byte/hash and per-sample RGB/depth/stencil equivalence against manual omission, while target-invalid off-frustum work still rejects fail closed.

The exact integrated `main` commit is `918c412f37f97bcf2a9822e0264783417f1fca21` (Milestone 85). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence. The historical `milestone-73-prepared-spatial-metadata` branch is superseded by integrated M81. Milestone 86 is the active implementation surface on branch `milestone-86-prepared-scene-plan`; no separate parallel M86 implementation should be opened while it is active.

## Milestone 86 candidate — reusable prepared scene plan

M86 promotes per-draw spatial planning from one-off orchestration vectors into an explicit reusable scene ownership/evaluation layer. The plan owns prepared spatial submissions once; each camera evaluation recomputes ordering and conservative visibility without rebuilding or copying the owned canonical model snapshots.

Acceptance surface:

- `PreparedScenePlan` owns a bounded caller-supplied sequence of `PreparedSpatialSubmission` snapshots plus model transforms and an explicit `CallerOrder` or `BackToFront` execution phase;
- a plan is intentionally address-stable because camera evaluations contain references into its owned submissions; copy/move after construction is rejected by the type contract rather than leaving dangling planning records possible;
- `evaluate_prepared_scene_plan` recomputes caller-order flattening, stable far-to-near ordering, and conservative frustum filtering from the existing prepared-spatial helpers for each supplied view/projection pair;
- `PreparedSceneEvaluation` retains the exact view/projection used to select visibility, so execution cannot accidentally pair a camera-A visibility subset with camera-B transforms;
- `draw_prepared_scene_evaluation` first preflights both complete unfiltered phase plans against the target and only then executes visible caller-order followed by visible back-to-front records through the existing `draw_prepared_draw_order` path;
- the same owned scene plan can be evaluated for distinct affine camera views without rebuilding prepared model/spatial ownership, and different cameras can deterministically select different visible subsets;
- combined prepared-scene execution is exact resolved byte/hash and per-sample RGB/depth/stencil equivalent to explicit execution of the same evaluated phase plans on a 4x target;
- target-invalid work remains part of complete preflight even when conservative visibility removes it from the execution subset, preserving zero-write fail-closed behavior;
- M86 does not claim automatic rebinding of camera-dependent shading state. In particular, an environment-reflection viewer position already captured inside `ModelRenderOptions` is not silently rewritten when the scene plan is evaluated for another camera;
- M86 adds no new raster/material/depth/blend/framebuffer path, no scene hierarchy, no BVH/occlusion culling, and no performance claim.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate, plan, filter, and submit rather than duplicating ownership semantics.
- Model/prepared/list/draw-plan/scene-evaluation submission validates complete required state before writes whenever later invalid state could otherwise partially commit earlier work.
- Prepared draw execution reuses canonical model material mapping and `draw_mesh_range`; spatial and scene-plan layers own metadata/planning/visibility, not material or framebuffer semantics.
- Prepared spatial metadata is derived once from the same owned canonical model snapshot later submitted; caller-order flattening, transparency ordering, visibility, and reusable scene evaluation consume that metadata instead of rescanning triangle/index data.
- Spatial ordering, visibility, and execution reject geometry mutations they cannot represent, including vertex-program position changes and projective model/view transforms in the bounded contract.
- Conservative visibility may retain false positives but must not reject a draw unless its prepared bound proves it cannot intersect the homogeneous clip volume.
- Visibility is an execution-selection optimization only; it must not weaken validation of the complete transaction being represented.
- Camera-dependent visibility state is bound to the exact matrices that produced it; reusable ownership does not imply reusable camera-derived subsets.
- Offline mixed-transparency orchestration owns transparency policy classification: opaque/A2C retain caller/canonical order and source-alpha uses stable global far-to-near order. The reusable scene layer provides the lower-level execution phases without inventing transparency semantics.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into a new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 86

After M86 converges, re-read exact live `main`, open PRs/issues, active branches, and the offline scene transaction. The next architectural promotion should evaluate consuming `PreparedScenePlan` directly from offline mixed-transparency rendering so scene ownership survives repeated renders while camera-specific settings are rebound correctly. Environment reflection is the key correctness constraint: viewer position must follow the active camera rather than being frozen in an old prepared snapshot. Do not jump to BVHs, occlusion culling, hierarchy, or performance claims until reusable offline consumption is integrated and measured under a controlled workload.
