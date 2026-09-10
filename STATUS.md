# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains older detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 82

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view. Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestones 61–71 expand canonical asset/material semantics with bounded OBJ vertex color, MTL emission, deterministic anisotropy, glossy environment-reflection mip policies, and owned `map_Ks` / `map_Ke` / `map_Ns` material textures while preserving the shared texture sampler, prepared ownership, direct/environment shading contracts, inspection/fingerprint semantics, and fail-closed validation.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. `draw_prepared_model_list_back_to_front` computes one finite/projectable mean view-space Z key per non-empty prepared entry, uses stable far-to-near ordering, rejects vertex-program entries whose post-program positions are not represented by the canonical key, and delegates execution to the existing prepared-list path.

Milestones 74–80 promote prepared-list execution into bounded flat-scene and CLI transactions with combined-bounds auto-fit, optional explicit perspective camera, scene material/shading overrides, explicit `opaque` / `source-alpha` / `alpha-to-coverage` policies, and `ordering mixed-transparency`. M80 executes opaque/A2C entries first in caller order and source-alpha entries in stable entry-level far-to-near order. Scene ordering and target-dependent preflight occur before framebuffer clear, environment drawing, or geometry submission.

Milestone 81 establishes the first real prepared per-draw spatial contract. `PreparedSpatialSubmission` owns the same validated `PreparedModelSubmission` snapshot plus one immutable object-space AABB/center record for each canonical `MaterialDraw`. `order_prepared_model_draws_back_to_front` flattens heterogeneous prepared draws and returns a stable global far-to-near plan keyed by affine-transformed prepared centers. Equal-depth records preserve caller entry and canonical draw order. Non-finite/projective transforms, null submissions, plan-size overflow, and vertex-program geometry not represented by canonical bounds fail closed.

Milestone 82 makes that per-draw spatial plan executable. `preflight_prepared_draw_order` validates a complete selected plan before mutation and `draw_prepared_draw_order` delegates each selected record to the canonical prepared material mapping plus `draw_mesh_range`. Mixed-transparency offline rendering keeps opaque/A2C entries in its depth-writing phase while promoting source-alpha work to one global per-draw far-to-near plan across heterogeneous models. Historical input-order and entry-level back-to-front paths remain intact.

The exact integrated `main` commit is `3015009191cf7ad265b87e830982bb9567b3ca45` (Milestone 82). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence. The historical `milestone-73-prepared-spatial-metadata` branch is superseded by integrated M81. Milestone 83 is the active implementation surface on branch `milestone-83-prepared-draw-frustum-culling`; no separate parallel M83 implementation should be opened while it is active.

## Milestone 83 candidate — conservative prepared-draw frustum visibility rejection

M83 consumes the AABBs already owned by M81 and the executable draw plans added by M82. It adds a bounded visibility filter that can remove a prepared draw only when its complete prepared object-space AABB is provably outside the camera homogeneous clip volume. It does not create a second raster path and does not claim a performance improvement.

Acceptance surface:

- `filter_prepared_draw_order_to_frustum` accepts an existing ordered `PreparedDrawOrderEntry` span plus affine view and finite projection transforms and returns retained records in exact caller order;
- each selected record reuses the prepared draw's immutable AABB; no triangle/index rescan or per-draw mesh copy is introduced;
- all eight AABB corners are transformed by `projection * view * model`; a draw is rejected only when every corner lies strictly outside the same one of the six homogeneous clip half-spaces;
- relative epsilon tolerance biases clip-boundary and numerically ambiguous cases toward retention, preventing the visibility layer from claiming visibility it cannot conservatively prove absent;
- affine model/view constraints remain the same bounded spatial contract used by M81/M82, while finite perspective projection is allowed;
- null submissions, non-finite inherited planning depth, unavailable draw indices, metadata/range mismatch, non-finite transforms/results, projective model/view transforms, and vertex-program geometry outside canonical prepared bounds fail closed;
- deterministic regression coverage exercises all six clip planes, exact boundary contact, frustum-crossing bounds, large conservative bounds, stable retained order, empty plans, invalid-state rejection, and perspective execution;
- executing the retained plan is exact RGB/depth/stencil sample-equivalent to executing the unfiltered plan when removed draws are truly off-frustum, including a 4x framebuffer target;
- M83 does not add occlusion culling, triangle-level culling, BVHs/spatial trees, scene-wide automatic scheduling, vertex-program bounds, GPU APIs, or performance/image-quality claims.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list/draw-plan submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Prepared draw execution reuses `model_rasterizer` and `draw_mesh_range`; the spatial layer owns metadata/planning/visibility, not material or framebuffer semantics.
- Prepared spatial metadata is derived once from the same owned canonical model snapshot that is later submitted; schedulers consume metadata rather than rescanning triangle/index data for every sort or visibility test.
- Spatial ordering, visibility, and execution reject geometry mutations they cannot represent, including vertex-program position changes and projective model/view transforms in the bounded contract.
- Conservative visibility may retain false positives but must not reject a draw unless its prepared bound proves it cannot intersect the homogeneous clip volume.
- Offline scene orchestration owns bounded preparation, framing/camera selection, explicit execution classification, environment injection, and executor selection; it does not own raster/material/depth/blend math.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into a new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 83

After M83 converges, re-read exact live `main`, open PRs/issues, active branches, and the complete prepared-spatial/offline-render contract before selecting the next slice. If the visibility contract remains conservative under integration evidence, the next higher-value promotion should consume the filtered prepared-draw plan in a real scene execution phase before target mutation, rather than farming more geometric corner cases. Any integration must preserve historical output for retained draws and continue to make no performance claim without controlled measurement.
