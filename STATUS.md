# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 86

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view. Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestones 61–71 expand canonical asset/material semantics with bounded OBJ vertex color, MTL emission, deterministic anisotropy, glossy environment-reflection mip policies, and owned `map_Ks` / `map_Ke` / `map_Ns` material textures while preserving shared sampler, prepared ownership, direct/environment shading, inspection/fingerprint, and fail-closed validation contracts.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. Milestones 74–80 promote prepared-list execution into bounded flat-scene and CLI transactions with combined-bounds auto-fit, optional explicit perspective camera, scene material/shading overrides, explicit `opaque` / `source-alpha` / `alpha-to-coverage` policies, and `ordering mixed-transparency`. Mixed transparency executes opaque/A2C work in its depth-writing phase and source-alpha work in stable far-to-near order, with target-dependent preflight before framebuffer mutation.

Milestone 81 establishes the prepared per-draw spatial contract. `PreparedSpatialSubmission` owns the same validated `PreparedModelSubmission` snapshot plus one immutable object-space AABB/center record for each canonical `MaterialDraw`. Milestone 82 makes that per-draw plan executable through complete target preflight and canonical `draw_mesh_range` submission. Milestone 83 adds conservative prepared-draw frustum visibility: all eight prepared AABB corners are transformed to homogeneous clip space and a draw is rejected only when every corner is provably outside the same clip half-space; boundary and numerically ambiguous cases are retained.

Milestone 84 consumes conservative visibility in the real mixed-transparency offline source-alpha phase while retaining complete unfiltered-plan target preflight before mutation. Milestone 85 extends the same contract across the complete mixed-transparency transaction: opaque/A2C work is flattened in caller/canonical order, source-alpha work remains stable far-to-near, both complete plans are preflighted before clear/environment/geometry mutation, and only visibility-retained subsets execute through the existing prepared draw executor. A 4x mixed scene with off-frustum depth-writing work is regression-locked to exact resolved byte/hash and per-sample RGB/depth/stencil equivalence against manual omission, while target-invalid off-frustum work still rejects fail closed.

Milestone 86 promotes those one-off draw vectors into an address-stable `PreparedScenePlan` that owns prepared spatial submissions once, reevaluates caller-order/back-to-front planning and conservative visibility for each camera, binds each evaluation to the exact view/projection matrices that produced it, and preserves complete unfiltered-plan preflight before visible execution.

The exact integrated `main` commit is `7979bce89104773d732daee9cbac998a1db7b9c6` (Milestone 86). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence. The historical `milestone-73-prepared-spatial-metadata` branch is superseded by integrated M81. Milestone 87 is the active implementation surface on branch `milestone-87-offline-prepared-scene`; no separate parallel M87 implementation should be opened while it is active.

## Milestone 87 candidate — reusable offline prepared mixed scenes

M87 gives the M86 reusable plan a real offline mixed-transparency consumer. Scene/model/spatial ownership is prepared once, while each explicit camera render reevaluates ordering/visibility and rebinds camera-dependent environment reflection without rebuilding canonical model snapshots.

Acceptance surface:

- `PreparedOfflineMixedScene` owns one address-stable `PreparedScenePlan` plus the validated offline settings snapshot; source model assets may be destroyed after preparation while imported/shared texture lifetime remains retained by prepared ownership;
- preparation requires the same explicit per-entry mixed-transparency declaration as the one-shot transaction and maps opaque/A2C work to caller order and source-alpha work to stable back-to-front execution;
- each explicit-camera render calls `evaluate_prepared_scene_plan` for fresh view-depth ordering and conservative visibility without reconstructing mesh/material/texture/spatial state;
- a bounded prepared-draw execution override changes only the environment-reflection viewer position for the active camera; it copies render options for execution but never copies or rebuilds the owned `ModelAsset`;
- both complete unfiltered execution phases are preflighted with the same active-camera override before clear/environment/geometry mutation; visibility remains execution selection only;
- the existing one-shot `render_scene_preview(..., MixedTransparency)` delegates its mixed planning/filtering/execution to `PreparedScenePlan` rather than maintaining parallel depth-writing/source-alpha orchestration vectors;
- reusable explicit-camera output is regression-locked against one-shot mixed rendering on 4x targets for resolved byte/hash plus exact per-sample RGB/depth/stencil attachments across multiple cameras;
- camera B reflection must match one-shot camera B rendering after a prior camera A render, proving the viewer was rebound rather than frozen in the prepared snapshot;
- target-dependent constraints such as alpha-to-coverage on a 1x framebuffer remain fail-closed at render preflight;
- M87 adds no new raster/material/framebuffer path, auto-fit reusable camera policy, scene hierarchy, BVH/occlusion culling, or performance claim.

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
- Reusable offline execution may overlay camera-dependent reflection viewer state, but canonical prepared mesh/material/texture/spatial ownership remains immutable and is not rebuilt per camera.
- Offline mixed-transparency orchestration owns transparency policy classification: opaque/A2C retain caller/canonical order and source-alpha uses stable global far-to-near order. The reusable scene layer provides the lower-level execution phases without inventing transparency semantics.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into a new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 87

After M87 converges, re-read exact live `main`, open PRs/issues, active branches, and the reusable offline consumer. The next promotion should decide whether repeated-frame/camera-sequence orchestration needs a bounded reusable frame API and collect controlled measurements that distinguish preparation cost from per-camera evaluation/execution cost. Acceleration structures such as BVHs or occlusion culling remain premature until measured workloads show a justified bottleneck and the complete fail-closed transaction contract can be preserved.
