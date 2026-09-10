# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 84

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view. Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestones 61–71 expand canonical asset/material semantics with bounded OBJ vertex color, MTL emission, deterministic anisotropy, glossy environment-reflection mip policies, and owned `map_Ks` / `map_Ke` / `map_Ns` material textures while preserving shared sampler, prepared ownership, direct/environment shading, inspection/fingerprint, and fail-closed validation contracts.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. Milestones 74–80 promote prepared-list execution into bounded flat-scene and CLI transactions with combined-bounds auto-fit, optional explicit perspective camera, scene material/shading overrides, explicit `opaque` / `source-alpha` / `alpha-to-coverage` policies, and `ordering mixed-transparency`. Mixed transparency executes opaque/A2C work in its depth-writing phase and source-alpha work in stable far-to-near order, with target-dependent preflight before framebuffer mutation.

Milestone 81 establishes the prepared per-draw spatial contract. `PreparedSpatialSubmission` owns the same validated `PreparedModelSubmission` snapshot plus one immutable object-space AABB/center record for each canonical `MaterialDraw`. Milestone 82 makes that per-draw plan executable through complete target preflight and canonical `draw_mesh_range` submission. Milestone 83 adds conservative prepared-draw frustum visibility: all eight prepared AABB corners are transformed to homogeneous clip space and a draw is rejected only when every corner is provably outside the same clip half-space; boundary and numerically ambiguous cases are retained.

Milestone 84 consumes that conservative visibility in the real mixed-transparency offline source-alpha phase. The complete original globally ordered source-alpha plan is target-preflighted before clear/environment/geometry mutation, while only visibility-retained records are submitted. Off-frustum target-invalid records therefore still reject fail closed, and 4x source-alpha filtering is regression-locked to exact resolved byte/hash plus per-sample RGB/depth/stencil equivalence against manual omission.

The exact integrated `main` commit is `0f400bda45640bb94f1a6baa52289f2471fb0551` (Milestone 84). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence. The historical `milestone-73-prepared-spatial-metadata` branch is superseded by integrated M81. Milestone 85 is the active implementation surface on branch `milestone-85-scene-wide-depth-visibility`; no separate parallel M85 implementation should be opened while it is active.

## Milestone 85 candidate — scene-wide depth-writing visibility

M85 promotes prepared frustum visibility from the source-alpha phase to the complete mixed-transparency scene transaction. Opaque and alpha-to-coverage work now receive the same prepared per-draw spatial selection contract without changing their required caller/canonical execution order.

Acceptance surface:

- `flatten_prepared_model_draws` produces one validated `PreparedDrawOrderEntry` per canonical material draw in exact caller entry order then canonical draw order, while recording the same finite view-space center depth used by the existing sorter;
- `order_prepared_model_draws_back_to_front` is defined as stable sorting of that canonical flatten result, so spatial validation/depth calculation is not duplicated;
- mixed-transparency opaque/A2C entries are promoted to `PreparedSpatialSubmission`, flattened without sorting, conservatively filtered by `filter_prepared_draw_order_to_frustum`, and executed by the existing `draw_prepared_draw_order` path;
- source-alpha entries continue to use the global far-to-near plan and the same conservative filter/executor;
- both complete original depth-writing and source-alpha plans are target-preflighted before clear/environment/geometry mutation; visibility chooses execution subsets only and cannot bypass invalid target-dependent state;
- depth-writing visibility preserves exact caller entry and canonical material-draw order for retained records; no painter reordering is applied to opaque/A2C work;
- a 4x mixed scene containing provably off-frustum opaque and alpha-to-coverage draws is exact RGB/depth/stencil sample-equivalent and resolved byte/hash-equivalent to the same scene with those draws manually omitted;
- a target-invalid off-frustum depth-writing record still rejects the transaction before rendering, matching the existing M84 source-alpha validation-bypass regression;
- M85 adds no new raster/material/depth/blend/framebuffer path and makes no performance claim.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate, plan, filter, and submit rather than duplicating ownership semantics.
- Model/prepared/list/draw-plan submission validates complete required state before writes whenever later invalid state could otherwise partially commit earlier work.
- Prepared draw execution reuses canonical model material mapping and `draw_mesh_range`; the spatial layer owns metadata/planning/visibility, not material or framebuffer semantics.
- Prepared spatial metadata is derived once from the same owned canonical model snapshot later submitted; caller-order flattening, transparency ordering, and visibility consume that metadata instead of rescanning triangle/index data.
- Spatial ordering, visibility, and execution reject geometry mutations they cannot represent, including vertex-program position changes and projective model/view transforms in the bounded contract.
- Conservative visibility may retain false positives but must not reject a draw unless its prepared bound proves it cannot intersect the homogeneous clip volume.
- Visibility is an execution-selection optimization only; it must not weaken validation of the complete transaction being represented.
- Offline mixed-transparency orchestration owns phase classification and ordering policy: opaque/A2C retain caller/canonical order, source-alpha uses stable global far-to-near order, and both phases share the same prepared spatial filter/executor contract.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into a new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 85

After M85 converges, re-read exact live `main`, open PRs/issues, active branches, and the complete prepared-spatial/offline scene transaction. The next architectural promotion should evaluate whether scene visibility should become an explicit reusable execution-plan object that can be inspected and reused across repeated frames/cameras without duplicating prepared ownership, while preserving complete target validation and conservative correctness. Do not jump to BVHs, occlusion culling, or performance claims until a reusable plan has a clear executable consumer and controlled measurements justify acceleration-oriented work.
