# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains older detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 79

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

The exact integrated `main` commit is `6f405aff1a8b3ff2391724dd1081ab4b27e42423` (Milestone 79). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence.

The branch `milestone-73-prepared-spatial-metadata` remains a reserved but stale surface at `f4d63c1c4fbbf285875a54e5fb2da84eb8980bc1`. It is not completion evidence and this milestone does not compete with or assume its spatial-metadata contract.

Milestone 80 is the active implementation surface on branch `milestone-80-mixed-scene-transparency`.

## Milestone 80 candidate — mixed opaque/transparent scene transaction

M80 turns the M79 per-entry transparency policy into one bounded heterogeneous scene execution contract rather than forcing callers to choose a single ordering policy for all entries.

Acceptance surface:

- `.trscene` accepts `ordering mixed-transparency` in addition to historical `input` and `back-to-front`;
- `OfflineSceneEntry` carries an optional explicit transparency declaration; absence preserves caller-owned `ModelRenderOptions` for legacy input/back-to-front library use, while mixed ordering requires a declaration for every entry instead of inferring transparency from material data or arbitrary blend state;
- a present declaration is applied to the scene-owned render-option snapshot in `render_scene_preview`, keeping classification and the M79 policy mapping coupled;
- mixed execution preserves `opaque` and `alpha-to-coverage` entries in deterministic caller order as the depth-writing phase, then executes only explicitly `source-alpha` entries in the established M72 stable far-to-near entry order;
- the M72 sort-key calculation is reusable independently from execution, and `draw_prepared_model_list_back_to_front` delegates to that single ordering implementation rather than maintaining a second sorter;
- `preflight_prepared_model_list` exposes the existing vertex-program preparation plus per-draw dynamic preflight without submitting fragments; `draw_prepared_model_list` shares the same internal helper;
- ordering validation and complete target-dependent prepared-list preflight occur before `Framebuffer::clear`, environment-background drawing, or geometry submission, so a later invalid mixed entry cannot partially commit an earlier phase;
- the stronger pre-write ordering/preflight sequence also applies to historical `BackToFront`, closing the prior gap where environment pixels could be written before a sort-key rejection;
- programmatic regression proves mixed output is byte/hash-equivalent to explicit opaque-first + far-to-near source-alpha submission and observably differs from near-first non-commutative blending;
- prepared-list regression proves a later A2C entry on a 1x target fails with existing framebuffer color/depth untouched;
- a file-driven mixed manifest renders deterministically on Linux/macOS and under ASan/UBSan;
- the slice does not infer opacity, split material draws, sort individual triangles, add destination alpha, claim order-independent transparency, add a scene graph, or make performance/image-quality claims.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Submission-order helpers may reorder already-prepared work, but execution still delegates to the canonical prepared-list path rather than creating a second raster path.
- Offline scene orchestration owns bounded preparation, framing/camera selection, explicit execution classification, environment injection, and executor selection; it does not own raster/material/depth/blend math.
- Flat-scene manifest import owns bounded path/transform/order/camera/material-mode/transparency-mode parsing; it delegates asset import to the canonical OBJ/model loader and rendering to `render_scene_preview`.
- Tooling-level material overrides configure existing `MaterialState`; tooling-level transparency declarations configure existing `ModelRenderOptions` blend/depth/alpha-to-coverage state. Neither creates parallel shading or ownership semantics.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 80

After M80 converges, re-read exact live `main`, open PRs/issues, and `milestone-73-prepared-spatial-metadata` before selecting the next slice. The next architectural question is no longer another transparency token: it is whether the renderer has a verified spatial-bound contract strong enough to promote entry-level scheduling into draw-granularity visibility/ordering without duplicating transforms or sorting triangles. If the stale M73 surface remains unowned, first audit whether to revive/supersede that branch rather than creating a competing spatial-metadata implementation. Until such metadata is real, keep M72/M80 ordering explicitly entry-level and do not claim draw sorting, culling acceleration, OIT, or performance wins.
