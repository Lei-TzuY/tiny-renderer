# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains older detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 77 plus Milestone 75

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI. Milestones 66–68 add bounded glossy environment-reflection mip policies and expose them through the same library/tooling path. Milestones 69–71 complete the current bounded MTL material-texture data plane with owned `map_Ks`, `map_Ke`, and `map_Ns`: shared decoded ownership, UV/sampler/gradient reuse, fail-closed direct/prepared/list validation, deterministic fingerprints/inspection, and shared direct/environment specular semantics.

Milestone 71 maps linear `map_Ns` RGB by arithmetic mean into the bounded `[1,1000]` exponent domain as `1 + mean(rgb) * 999`. A mapped exponent replaces the uniform `Ns` fallback for that fragment and is resolved once for both direct Blinn-Phong specular and `EnvironmentReflectionMipPolicy::MaterialShininess`. The milestone also closes the emissive-only direct-mesh UV preflight gap discovered during integration review.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. `draw_prepared_model_list_back_to_front` computes one finite/projectable mean view-space Z key per non-empty prepared entry, uses stable far-to-near ordering, rejects vertex-program entries whose post-program positions are not represented by the canonical key, and delegates execution once to the existing prepared-list path. It does not classify opacity, split mixed material draws, sort triangles, or claim order-independent transparency.

Milestone 74 promotes the heterogeneous prepared-list architecture into a bounded flat-scene headless transaction. `OfflineSceneEntry` borrows one asset, model transform, and render options; `render_scene_preview` validates/snapshots all entries, computes one combined finite world-space bound, applies one global auto-fit while preserving relative transforms, and delegates either input ordering or M72 stable back-to-front ordering to the canonical prepared-list executor. Optional environment background remains a scene-global pre-geometry pass; diffuse/reflection environment state is injected through the existing fixed-light path with authoritative preview-camera viewer binding. Empty scenes remain deterministic clear/environment-only renders.

Milestone 76 promotes that flat-scene library transaction into the real headless CLI. `tiny_renderer_render` accepts either one legacy `.obj` input or one bounded `.trscene` manifest. The manifest starts with exact `tiny-renderer-scene-v1`, accepts optional `ordering input|back-to-front`, accepts at most 256 sibling-only OBJ model records with finite translation/uniform-scale/Y-rotation state, rejects traversal/subdirectories/absolute references, loads all referenced assets before pointer-stable scene construction, and delegates the complete scene exactly once to `render_scene_preview`. Global texture sampler and environment controls remain shared with the existing renderer.

Milestone 77 adds one bounded optional perspective-camera directive to the same flat-scene manifest. Explicit eye/target/up/FOV/near/far state is fail-closed validated, preserves caller world-space composition instead of applying auto-fit, and drives geometry, environment-background rays, and reflection viewer position from one authoritative camera. Omitting the directive preserves the historical combined-bounds auto-fit path. Explicit-camera scenes still delegate exactly once to the canonical prepared-list executor; no camera-specific raster path exists.

Milestone 75 is now integrated on top of the M77 scene/tooling baseline. `MaterialState` has an explicit trailing `MaterialShadingModel::{BlinnPhong,Lambert}` contract with Blinn-Phong as the compatibility default. Bounded MTL `illum 1` imports Lambert and `illum 2` imports Blinn-Phong; Lambert keeps diffuse/emissive/environment-diffuse contributions but suppresses direct and environment specular semantics. Material-aware validation derives normal/world-position requirements from contributions that can execute, inspection exposes the semantic model, and non-default model state participates in versioned fingerprint semantics without changing historical all-Blinn-Phong fingerprints.

The exact integrated `main` commit is `29eb5d246a220999ab52692b771680ce0ae05843`. Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence.

The branch `milestone-73-prepared-spatial-metadata` remains a reserved but currently stale surface. Its live head is still the M72 commit `f4d63c1c4fbbf285875a54e5fb2da84eb8980bc1`; current `main` is strictly ahead of it and no M73 spatial-metadata implementation may be assumed.

The former `milestone-75-explicit-material-shading-model` candidate has been integrated through PR #93. The next active implementation surface is Milestone 78 below.

## Milestone 78 candidate — flat-scene per-entry material shading override

M78 consumes the integrated M75 material contract and M76/M77 flat-scene tooling in one bounded cross-layer slice. It lets a scene record select the fixed-function material model for that scene-owned model snapshot without changing the source OBJ/MTL asset or adding a scene-specific renderer.

Acceptance surface:

- the existing `model FILE.obj TX TY TZ SCALE ROTATION_Y_RADIANS` manifest record remains valid and preserves inherited MTL material models byte-for-byte;
- one optional trailing token accepts exactly `inherit`, `lambert`, or `blinn-phong`; omitted and explicit `inherit` both preserve imported material models;
- a Lambert or Blinn-Phong override applies to every canonical `MaterialDraw` in the scene-owned loaded `ModelAsset` snapshot before normal preview-option derivation and before prepared-model construction;
- source OBJ/MTL files and any independently loaded `ModelAsset` remain unchanged; the override is instance/tooling configuration, not asset mutation or a new material representation;
- execution remains `load_obj_model_asset_file` → existing `MaterialState` → `render_scene_preview` → `prepare_model_asset` → canonical prepared-list/raster path;
- deterministic CLI evidence uses one MTL `illum 1` Lambert asset plus the existing HDR environment-reflection path: inherited and explicit Lambert renders must be byte-identical, while explicit Blinn-Phong must produce a different reflection result;
- malformed/unsupported shading tokens fail before output creation, and the same positive/negative paths execute under ASan/UBSan;
- no general MTL illumination-model support, PBR/BRDF expansion, hierarchy, persistent scene graph, per-draw manifest override, or performance/image-quality claim is introduced.

Candidate branch: `milestone-78-scene-material-shading-override`. Branch commits are not integration evidence until the exact final PR head and post-merge `main` pass Ubuntu, macOS, and ASan/UBSan gates.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Submission-order helpers may reorder already-prepared work, but they must delegate execution to the canonical prepared-list path rather than creating a second raster path.
- Offline scene orchestration owns only bounded preparation, framing/camera selection, environment injection, and executor selection; it does not own raster/material/depth/blend semantics.
- Flat-scene manifest import owns bounded path/transform/order/camera/material-mode parsing; it delegates asset import to the canonical OBJ/model loader and rendering to `render_scene_preview`.
- Tooling-level material overrides configure the existing `MaterialState` on scene-owned asset snapshots; they do not create a parallel shading implementation.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 78

After M78 converges, re-read exact live `main`, open PRs, and the reserved M73 branch before selecting the next slice. If M73 becomes a real integrated spatial contract, promote directly to draw-granularity transparency ordering or another consumer of those bounds. If it remains stale, prefer another cross-layer scene/runtime integration that configures already-existing verified render state over hierarchy, persistent scene storage, broad PBR, or parser-only expansion.
