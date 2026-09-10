# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains older detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 78

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI. Milestones 66–68 add bounded glossy environment-reflection mip policies and expose them through the same library/tooling path. Milestones 69–71 complete the current bounded MTL material-texture data plane with owned `map_Ks`, `map_Ke`, and `map_Ns`: shared decoded ownership, UV/sampler/gradient reuse, fail-closed direct/prepared/list validation, deterministic fingerprints/inspection, and shared direct/environment specular semantics.

Milestone 71 maps linear `map_Ns` RGB by arithmetic mean into the bounded `[1,1000]` exponent domain as `1 + mean(rgb) * 999`. A mapped exponent replaces the uniform `Ns` fallback for that fragment and is resolved once for both direct Blinn-Phong specular and `EnvironmentReflectionMipPolicy::MaterialShininess`. The milestone also closes the emissive-only direct-mesh UV preflight gap discovered during integration review.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. `draw_prepared_model_list_back_to_front` computes one finite/projectable mean view-space Z key per non-empty prepared entry, uses stable far-to-near ordering, rejects vertex-program entries whose post-program positions are not represented by the canonical key, and delegates execution once to the existing prepared-list path. It does not classify opacity, split mixed material draws, sort triangles, or claim order-independent transparency.

Milestone 74 promotes the heterogeneous prepared-list architecture into a bounded flat-scene headless transaction. `OfflineSceneEntry` borrows one asset, model transform, and render options; `render_scene_preview` validates/snapshots all entries, computes one combined finite world-space bound, applies one global auto-fit while preserving relative transforms, and delegates either input ordering or M72 stable back-to-front ordering to the canonical prepared-list executor. Optional environment background remains a scene-global pre-geometry pass; diffuse/reflection environment state is injected through the existing fixed-light path with authoritative preview-camera viewer binding. Empty scenes remain deterministic clear/environment-only renders.

Milestone 76 promotes that flat-scene library transaction into the real headless CLI. `tiny_renderer_render` accepts either one legacy `.obj` input or one bounded `.trscene` manifest. The manifest starts with exact `tiny-renderer-scene-v1`, accepts optional `ordering input|back-to-front`, accepts at most 256 sibling-only OBJ model records with finite translation/uniform-scale/Y-rotation state, rejects traversal/subdirectories/absolute references, loads all referenced assets before pointer-stable scene construction, and delegates the complete scene exactly once to `render_scene_preview`. Global texture sampler and environment controls remain shared with the existing renderer.

Milestone 77 adds one bounded optional perspective-camera directive to the same flat-scene manifest. Explicit eye/target/up/FOV/near/far state is fail-closed validated, preserves caller world-space composition instead of applying auto-fit, and drives geometry, environment-background rays, and reflection viewer position from one authoritative camera. Omitting the directive preserves the historical combined-bounds auto-fit path. Explicit-camera scenes still delegate exactly once to the canonical prepared-list executor; no camera-specific raster path exists.

Milestone 75 is integrated on top of the M77 scene/tooling baseline. `MaterialState` has an explicit trailing `MaterialShadingModel::{BlinnPhong,Lambert}` contract with Blinn-Phong as the compatibility default. Bounded MTL `illum 1` imports Lambert and `illum 2` imports Blinn-Phong; Lambert keeps diffuse/emissive/environment-diffuse contributions but suppresses direct and environment specular semantics. Material-aware validation derives normal/world-position requirements from contributions that can execute, inspection exposes the semantic model, and non-default model state participates in versioned fingerprint semantics without changing historical all-Blinn-Phong fingerprints.

Milestone 78 consumes that material contract through the flat-scene tooling path. A model record may optionally select `inherit`, `lambert`, or `blinn-phong`; the override is applied to every canonical `MaterialDraw` in the scene-owned loaded `ModelAsset` snapshot before preview-option derivation and prepared submission. Source OBJ/MTL assets remain unchanged, inherited and explicit Lambert execution are byte-identical in the HDR-reflection regression, explicit Blinn-Phong changes the verified reflection result, invalid shading tokens fail before output creation, and execution still delegates through `render_scene_preview` and the canonical prepared-list/raster path.

The exact integrated `main` commit is `3de2447cf2f9ce7050b3e6922c24bad6a4b2f60d` (Milestone 78). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence.

The branch `milestone-73-prepared-spatial-metadata` remains a reserved but stale surface. Its live head is still the M72 commit `f4d63c1c4fbbf285875a54e5fb2da84eb8980bc1`; current `main` is strictly ahead of it and no M73 spatial-metadata contract may be assumed.

Milestone 79 is the only active implementation surface at this status update: branch `milestone-79-scene-transparency-modes`, PR #95.

## Milestone 79 candidate — flat-scene transparency execution modes

M79 promotes already-verified material opacity, source-alpha blending, depth-write state, and alpha-to-coverage into the real `.trscene` workflow without adding scene-specific blend, opacity, framebuffer, or raster semantics.

Acceptance surface:

- the established `model FILE.obj TX TY TZ SCALE ROTATION_Y_RADIANS [SHADING_MODE]` record remains valid and preserves historical opaque execution;
- one optional transparency token after the shading token accepts exactly `opaque`, `source-alpha`, or `alpha-to-coverage`; omission is `opaque`, and callers that want only transparency spell the shading token as `inherit`;
- `source-alpha` maps to the existing `BlendFactor::SourceAlpha` / `OneMinusSourceAlpha` RGB blend equation with depth writes disabled while preserving the renderer's normal depth comparison path;
- `alpha-to-coverage` maps to the existing deterministic alpha-to-coverage state with blending disabled and depth writes enabled, inheriting canonical 4x-target fail-closed validation;
- imported material `d` / `map_d` remains the only fragment-opacity source; the scene format does not duplicate or override material opacity data;
- execution remains manifest parse → canonical OBJ/MTL asset load → scene-owned render-option configuration → `render_scene_preview` → prepared-model/list preflight → the single raster/framebuffer ownership path;
- 4x CLI regressions render `opaque`, `source-alpha`, and `alpha-to-coverage` deterministically from the existing file-driven opacity-map asset; both non-opaque policies must produce observable output differences from opaque rendering;
- unknown transparency modes and alpha-to-coverage on a single-sample target fail before output creation, and sanitizer CI executes the same positive and negative paths;
- the slice does not add automatic opacity classification, destination alpha, triangle sorting, order-independent transparency, hierarchy, persistent scene storage, PBR, or performance/image-quality claims.

Candidate branch: `milestone-79-scene-transparency-modes`; PR #95. Its exact implementation head `d170daf3853bef77a8da007caedd96bf994dbe20` passed Ubuntu, macOS, and ASan/UBSan before this status-only closure commit. The final PR head must pass the same gates again before integration.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Submission-order helpers may reorder already-prepared work, but they must delegate execution to the canonical prepared-list path rather than creating a second raster path.
- Offline scene orchestration owns only bounded preparation, framing/camera selection, environment injection, and executor selection; it does not own raster/material/depth/blend semantics.
- Flat-scene manifest import owns bounded path/transform/order/camera/material-mode/transparency-mode parsing; it delegates asset import to the canonical OBJ/model loader and rendering to `render_scene_preview`.
- Tooling-level material overrides configure existing `MaterialState` on scene-owned asset snapshots; tooling-level transparency modes configure existing `ModelRenderOptions` blend/depth/alpha-to-coverage state. Neither creates a parallel shading or ownership implementation.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 79

After M79 converges, re-read exact live `main`, open PRs/issues, and the stale M73 branch before selecting the next slice. If M73 remains stale, the strongest immediate consumer is a bounded mixed opaque/transparent scene submission mode: preserve opaque and alpha-to-coverage entries in deterministic depth-writing execution, then submit explicitly `source-alpha` entries through the established M72 stable back-to-front helper, while preflighting the complete mixed transaction before the first framebuffer mutation. Do not infer transparency from arbitrary material data, split material draws, sort triangles, or claim order-independent transparency. If a concurrent spatial-metadata implementation has become real by then, re-evaluate draw-granularity ordering against that contract instead of competing with it.
