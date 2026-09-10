# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 83

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view. Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestones 61–71 expand canonical asset/material semantics with bounded OBJ vertex color, MTL emission, deterministic anisotropy, glossy environment-reflection mip policies, and owned `map_Ks` / `map_Ke` / `map_Ns` material textures while preserving shared sampler, prepared ownership, direct/environment shading, inspection/fingerprint, and fail-closed validation contracts.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. Milestones 74–80 promote prepared-list execution into bounded flat-scene and CLI transactions with combined-bounds auto-fit, optional explicit perspective camera, scene material/shading overrides, explicit `opaque` / `source-alpha` / `alpha-to-coverage` policies, and `ordering mixed-transparency`. Mixed transparency executes opaque/A2C work in its depth-writing phase and source-alpha work in stable far-to-near order, with target-dependent preflight before framebuffer mutation.

Milestone 81 establishes the prepared per-draw spatial contract. `PreparedSpatialSubmission` owns the same validated `PreparedModelSubmission` snapshot plus one immutable object-space AABB/center record for each canonical `MaterialDraw`. `order_prepared_model_draws_back_to_front` flattens heterogeneous prepared draws into a stable global far-to-near plan keyed by affine-transformed prepared centers. Equal-depth records preserve caller entry and canonical draw order.

Milestone 82 makes that per-draw spatial plan executable. `preflight_prepared_draw_order` validates a complete selected plan before mutation and `draw_prepared_draw_order` delegates each selected record through canonical prepared material mapping and `draw_mesh_range`. Mixed-transparency offline rendering therefore uses one global per-draw source-alpha order across heterogeneous models rather than a parallel raster path.

Milestone 83 adds conservative prepared-draw frustum visibility rejection. `filter_prepared_draw_order_to_frustum` consumes the immutable AABBs already owned by prepared spatial submissions, transforms all eight corners into homogeneous clip space, and rejects a draw only when every corner is provably outside the same clip half-space. Boundary and numerically ambiguous cases are retained. Affine model/view constraints, finite projection validation, canonical range/metadata matching, and rejection of unrepresented vertex-program geometry remain fail closed. Filtered and unfiltered execution are regression-locked to exact attachment equivalence whenever removed draws are truly off-frustum.

The exact integrated `main` commit is `e8b94b62438a9c01a3d762ef98d5300f40aa6079` (Milestone 83). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence. The historical `milestone-73-prepared-spatial-metadata` branch is superseded by integrated M81. Milestone 84 is the active implementation surface on branch `milestone-84-offline-source-alpha-visibility`; no separate parallel M84 implementation should be opened while it is active.

## Milestone 84 candidate — offline source-alpha visibility execution

M84 consumes M83 visibility in the real mixed-transparency offline scene transaction instead of leaving frustum filtering as a library-only planning primitive. It deliberately limits the first scene integration to the source-alpha per-draw phase, where M82 already owns a global prepared draw plan.

Acceptance surface:

- `OfflineSceneOrdering::MixedTransparency` builds its global source-alpha prepared draw order exactly as before, then applies `filter_prepared_draw_order_to_frustum` using the resolved scene view/projection before framebuffer construction or any target mutation;
- only the visibility-retained source-alpha records reach `draw_prepared_draw_order`; opaque/A2C execution and historical input-order/back-to-front paths remain unchanged in this slice;
- visibility filtering preserves the exact stable draw order of retained records and does not create a second material, raster, depth, blend, or framebuffer path;
- target-dependent transactional validation still covers the complete original source-alpha plan before clear/environment/geometry mutation, including records later omitted from raster execution, so frustum culling cannot become a validation bypass;
- a source-alpha record with a target-invalid viewport still rejects the scene even when its prepared AABB is provably outside the camera frustum;
- a 4x mixed-transparency scene containing a provably off-frustum source-alpha draw is exact RGB/depth/stencil sample-equivalent and resolved byte/hash-equivalent to the same scene with that draw manually omitted;
- existing conservative M83 visibility regressions continue to cover all clip planes, boundary contact, frustum crossing, perspective execution, invalid spatial state, vertex-program rejection, and empty plans;
- M84 makes no performance claim and adds no occlusion culling, triangle-level culling, BVH/spatial tree, general scene graph, GPU API, or replacement raster path.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate, plan, filter, and submit rather than duplicating ownership semantics.
- Model/prepared/list/draw-plan submission validates complete required state before writes whenever later invalid state could otherwise partially commit earlier work.
- Prepared draw execution reuses canonical model material mapping and `draw_mesh_range`; the spatial layer owns metadata/planning/visibility, not material or framebuffer semantics.
- Prepared spatial metadata is derived once from the same owned canonical model snapshot later submitted; ordering and visibility consume that metadata instead of rescanning triangle/index data.
- Spatial ordering, visibility, and execution reject geometry mutations they cannot represent, including vertex-program position changes and projective model/view transforms in the bounded contract.
- Conservative visibility may retain false positives but must not reject a draw unless its prepared bound proves it cannot intersect the homogeneous clip volume.
- Visibility is an execution-selection optimization only; it must not weaken validation of the transaction being represented.
- Offline scene orchestration owns bounded preparation, framing/camera selection, explicit execution classification, visibility selection, environment injection, and executor selection; it does not own raster/material/depth/blend math.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into a new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 84

After M84 converges, re-read exact live `main`, open PRs/issues, active branches, and the prepared-spatial/offline-render transaction. The next architectural promotion should evaluate making conservative visibility a scene-wide prepared execution-selection phase for depth-writing opaque/A2C work as well as source-alpha work, while preserving complete preflight-before-mutation semantics and avoiding duplicated spatial ownership. Do not add BVHs, occlusion culling, or performance claims until the flat prepared scene can consume one coherent conservative visibility contract and controlled evidence justifies further acceleration work.
