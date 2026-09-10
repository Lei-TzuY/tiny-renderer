# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains older detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 80

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI. Milestones 66–68 add bounded glossy environment-reflection mip policies and expose them through the same library/tooling path. Milestones 69–71 complete the current bounded MTL material-texture data plane with owned `map_Ks`, `map_Ke`, and `map_Ns`: shared decoded ownership, UV/sampler/gradient reuse, fail-closed direct/prepared/list validation, deterministic fingerprints/inspection, and shared direct/environment specular semantics.

Milestone 71 maps linear `map_Ns` RGB by arithmetic mean into the bounded `[1,1000]` exponent domain as `1 + mean(rgb) * 999`. A mapped exponent replaces the uniform `Ns` fallback for that fragment and is resolved once for both direct Blinn-Phong specular and `EnvironmentReflectionMipPolicy::MaterialShininess`. The milestone also closes the emissive-only direct-mesh UV preflight gap discovered during integration review.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. `draw_prepared_model_list_back_to_front` computes one finite/projectable mean view-space Z key per non-empty prepared entry, uses stable far-to-near ordering, rejects vertex-program entries whose post-program positions are not represented by the canonical key, and delegates execution to the existing prepared-list path. It does not classify opacity, split mixed material draws, sort triangles, or claim order-independent transparency.

Milestone 74 promotes the heterogeneous prepared-list architecture into a bounded flat-scene headless transaction. `OfflineSceneEntry` borrows one asset, model transform, and render options; `render_scene_preview` validates/snapshots entries, computes combined finite world-space bounds, applies one global auto-fit while preserving relative transforms, and delegates to canonical prepared-list execution. Optional environment background remains a scene-global pre-geometry pass; diffuse/reflection environment state is injected through the existing fixed-light path with authoritative preview-camera viewer binding. Empty scenes remain deterministic clear/environment-only renders.

Milestone 76 promotes that flat-scene library transaction into the real headless CLI. `tiny_renderer_render` accepts either legacy `.obj` input or one bounded `.trscene` manifest. The manifest starts with exact `tiny-renderer-scene-v1`, accepts bounded ordering and at most 256 sibling-only OBJ model records with finite transform state, rejects traversal/subdirectories/absolute references, loads all referenced assets before pointer-stable scene construction, and delegates the scene to `render_scene_preview`.

Milestone 77 adds one bounded optional perspective-camera directive to the same flat-scene manifest. Explicit eye/target/up/FOV/near/far state is fail-closed validated, preserves caller world-space composition instead of applying auto-fit, and drives geometry, environment-background rays, and reflection viewer position from one authoritative camera. Omitting the directive preserves combined-bounds auto-fit.

Milestone 75 integrates explicit `MaterialShadingModel::{BlinnPhong,Lambert}` semantics with Blinn-Phong as compatibility default. Bounded MTL `illum 1` imports Lambert and `illum 2` imports Blinn-Phong; Lambert keeps diffuse/emissive/environment-diffuse contributions but suppresses direct and environment specular semantics. Milestone 78 consumes that contract through `.trscene`: one optional model shading override selects `inherit`, `lambert`, or `blinn-phong` on scene-owned asset snapshots before prepared submission.

Milestone 79 promotes existing material opacity, source-alpha blending, depth-write state, and deterministic alpha-to-coverage into the real `.trscene` workflow. A model record accepts one optional `opaque`, `source-alpha`, or `alpha-to-coverage` policy after its shading token. `source-alpha` maps to the existing source-alpha RGB blend factors with depth writes disabled; `alpha-to-coverage` maps to the existing 4x coverage state with blending disabled and depth writes enabled. Material `d`/`map_d` remains the only fragment-opacity source. Invalid tokens and A2C on 1x fail before output creation, and deterministic CLI coverage executes under Linux, macOS, and sanitizers without a scene-specific raster path.

Milestone 80 turns those per-entry transparency policies into one bounded mixed scene transaction. `.trscene ordering mixed-transparency` keeps explicitly opaque and alpha-to-coverage entries in caller order as the depth-writing phase, then executes explicitly source-alpha entries using the established stable far-to-near prepared-entry ordering. `preflight_prepared_model_list` shares the real vertex-program preparation plus per-draw dynamic validation path, and scene ordering plus complete target-dependent preflight occurs before framebuffer clear, environment drawing, or geometry submission. Mixed execution still delegates to canonical prepared-list rendering and makes no triangle-sorting or order-independent-transparency claim.

The exact integrated `main` commit is `5bed6d662e89990fef47a45be7a0e44e8230ac79` (Milestone 80). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence.

The branch `milestone-73-prepared-spatial-metadata` remains at `f4d63c1c4fbbf285875a54e5fb2da84eb8980bc1`, which is exactly the historical Milestone 72 integration commit. It contains no prepared-spatial implementation and is therefore only a stale reservation, not code to revive or completion evidence. Milestone 81 supersedes that unimplemented intent from current M80 main rather than rebasing the repository back onto the stale surface.

Milestone 81 is the active implementation surface on branch `milestone-81-prepared-draw-spatial-ordering`.

## Milestone 81 candidate — prepared per-draw spatial metadata and ordering plan

M81 establishes the first real prepared-spatial contract at material-draw granularity. The purpose is to stop future draw-level schedulers from rescanning triangle/index data every time they need a spatial key. This slice produces executable deterministic planning output; it deliberately does not yet add a second draw executor or duplicate model-to-raster state mapping.

Acceptance surface:

- `PreparedSpatialSubmission` owns an existing `PreparedModelSubmission` plus one immutable `PreparedDrawSpatialMetadata` record for every canonical `MaterialDraw`, without copying the mesh per draw;
- each metadata record preserves the exact `DrawRange` and computes a finite object-space AABB plus midpoint from only the indexed triangle corners that can actually contribute fragments for that draw;
- spatial preparation rejects non-finite referenced positions deterministically while preserving the underlying prepared model/resource lifetime semantics;
- `order_prepared_model_draws_back_to_front` accepts a span of heterogeneous `PreparedSpatialListEntry` values, flattens all material draws across all entries, and returns one stable far-to-near plan keyed by transformed prepared AABB centers;
- equal-depth records preserve caller entry order and canonical material-draw order through stable sorting;
- ordering accepts only finite affine model/view transforms for this first spatial contract, rejecting projective model/view state instead of pretending an object-space AABB remains affine under arbitrary projective transforms;
- vertex-program submissions are rejected because post-program geometry is not represented by the canonical prepared bounds;
- null prepared entries, non-finite transformed centers, and plan-size overflow fail closed before a plan is returned;
- empty prepared models contribute no draw records and produce deterministic empty plans;
- regression coverage locks exact AABB/center values, ownership independence from source-asset mutation, single-model multi-draw sorting, equal-depth stability, cross-model transform sorting, invalid spatial state rejection, and empty-plan semantics;
- this milestone does not execute the plan, sort individual triangles, infer transparency, add frustum culling, claim conservative projected bounds, introduce a scene graph, or make a performance claim.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Submission-order helpers may reorder already-prepared work, but execution still delegates to the canonical prepared-list/range path rather than creating a second raster path.
- Prepared spatial metadata is derived once from the same owned canonical model snapshot that will later be submitted; schedulers consume metadata rather than independently rescanning triangle/index data.
- Spatial ordering rejects geometry mutations it cannot represent, including vertex-program position changes and projective model/view transforms in the first bounded contract.
- Offline scene orchestration owns bounded preparation, framing/camera selection, explicit execution classification, environment injection, and executor selection; it does not own raster/material/depth/blend math.
- Flat-scene manifest import owns bounded path/transform/order/camera/material-mode/transparency-mode parsing; it delegates asset import to the canonical OBJ/model loader and rendering to `render_scene_preview`.
- Tooling-level material overrides configure existing `MaterialState`; tooling-level transparency declarations configure existing `ModelRenderOptions` blend/depth/alpha-to-coverage state. Neither creates parallel shading or ownership semantics.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 81

After M81 converges, re-read exact live `main`, open PRs/issues, and active branches before extending the surface. The next high-value slice is a canonical prepared-draw executor that consumes `PreparedDrawOrderEntry` without rebuilding model/material/raster state in a parallel implementation, followed by promotion of the M80 source-alpha phase to consume the same draw plan. That integration must preflight every selected draw before the first framebuffer/environment mutation, preserve historical entry-level `BackToFront` behavior, and prove mixed-scene output against an explicit far-to-near draw-range reference. Until that executor is real, M81 is a spatial planning contract only: do not claim draw-level rendering, culling acceleration, order-independent transparency, or performance improvement.
