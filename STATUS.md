# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains older detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 81

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view. Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestones 61–71 expand canonical asset/material semantics with bounded OBJ vertex color, MTL emission, deterministic anisotropy, glossy environment-reflection mip policies, and owned `map_Ks` / `map_Ke` / `map_Ns` material textures while preserving the shared texture sampler, prepared ownership, direct/environment shading contracts, inspection/fingerprint semantics, and fail-closed validation.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. `draw_prepared_model_list_back_to_front` computes one finite/projectable mean view-space Z key per non-empty prepared entry, uses stable far-to-near ordering, rejects vertex-program entries whose post-program positions are not represented by the canonical key, and delegates execution to the existing prepared-list path.

Milestones 74–80 promote prepared-list execution into bounded flat-scene and CLI transactions with combined-bounds auto-fit, optional explicit perspective camera, scene material/shading overrides, explicit `opaque` / `source-alpha` / `alpha-to-coverage` policies, and `ordering mixed-transparency`. M80 executes opaque/A2C entries first in caller order and source-alpha entries in stable entry-level far-to-near order. Scene ordering and target-dependent preflight occur before framebuffer clear, environment drawing, or geometry submission.

Milestone 81 establishes the first real prepared per-draw spatial contract. `PreparedSpatialSubmission` owns the same validated `PreparedModelSubmission` snapshot plus one immutable object-space AABB/center record for each canonical `MaterialDraw`. `order_prepared_model_draws_back_to_front` flattens heterogeneous prepared draws and returns a stable global far-to-near plan keyed by affine-transformed prepared centers. Equal-depth records preserve caller entry and canonical draw order. Non-finite/projective transforms, null submissions, plan-size overflow, and vertex-program geometry not represented by canonical bounds fail closed. M81 plans work but deliberately does not execute them.

The exact integrated `main` commit is `2c024da96e3e57a67228bb1b46a36b6aac572358` (Milestone 81). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence. The historical `milestone-73-prepared-spatial-metadata` branch contains no newer spatial implementation than its old base and is superseded by integrated M81.

Milestone 82 is the active implementation surface on branch `milestone-82-prepared-draw-executor`.

## Milestone 82 candidate — canonical prepared-draw execution and mixed-scene integration

M82 turns the M81 planning contract into executable draw-granularity scheduling without creating a second raster path. The selected draw executor lives beside the existing prepared-model executor so material, texture, lighting, shadow, depth/stencil/blend, alpha-test, and alpha-to-coverage state continue to flow through the same canonical `model_rasterizer` mapping and `draw_mesh_range` path.

Acceptance surface:

- `preflight_prepared_draw_order` validates every selected `PreparedDrawOrderEntry` before any selected draw may mutate the target;
- `draw_prepared_draw_order` executes the supplied plan exactly in caller order after complete plan preflight and delegates each record to the canonical prepared material/raster mapping plus selected `DrawRange` execution;
- null spatial submissions, non-finite planning depths, non-affine model transforms, unavailable draw indices, metadata/range inconsistency, and vertex-program geometry outside the M81 spatial contract fail closed;
- a later target-dependent failure such as alpha-to-coverage on a 1x framebuffer rejects the complete plan before an earlier valid draw can write color/depth/stencil;
- standalone prepared-draw execution is byte/hash-equivalent to explicit far-to-near submissions of the same selected ranges;
- `OfflineSceneOrdering::MixedTransparency` preserves opaque/A2C caller order as the depth-writing phase but promotes only its source-alpha phase to one global per-draw far-to-near plan across heterogeneous models;
- source-alpha spatial preparation moves the already-prepared model snapshots into spatial owners rather than copying a model mesh per draw;
- mixed-scene ordering plus target-dependent preflight for both depth-writing entries and source-alpha draw records completes before framebuffer clear, environment drawing, or geometry submission;
- historical `InputOrder` and entry-level `BackToFront` execution remain on their existing prepared-list paths and keep their established semantics;
- regression coverage includes a source-alpha model whose near/far material draws straddle a middle draw from another model, proving global draw ordering against an explicit far-to-near reference and distinguishing it from canonical per-model draw order;
- the slice does not sort triangles, infer transparency from materials, support vertex-program spatial bounds, add frustum culling, claim order-independent transparency, or make performance/image-quality claims.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list/draw-plan submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Prepared draw execution reuses `model_rasterizer` and `draw_mesh_range`; the spatial layer owns metadata/planning, not material or framebuffer semantics.
- Prepared spatial metadata is derived once from the same owned canonical model snapshot that is later submitted; schedulers consume metadata rather than rescanning triangle/index data for every sort.
- Spatial ordering and execution reject geometry mutations they cannot represent, including vertex-program position changes and projective model/view transforms in the bounded contract.
- Offline scene orchestration owns bounded preparation, framing/camera selection, explicit execution classification, environment injection, and executor selection; it does not own raster/material/depth/blend math.
- Flat-scene manifest import delegates asset import to the canonical OBJ/model loader and rendering to `render_scene_preview`.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 82

After M82 converges, re-read exact live `main`, open PRs/issues, active branches, and the complete prepared-spatial contract before selecting the next slice. The next architectural frontier should be functional prepared-draw visibility rejection using the already-owned AABBs only if a conservative, deterministic affine view-frustum test can be specified and regression-proven without changing visible output for retained draws or claiming a performance win. If that contract is not yet strong enough, promote another integration gap that consumes M81/M82 metadata rather than farming parser or state micro-features.
