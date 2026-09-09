# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains older detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 74

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI. Milestones 66–68 add bounded glossy environment-reflection mip policies and expose them through the same library/tooling path. Milestones 69–71 complete the current bounded MTL material-texture data plane with owned `map_Ks`, `map_Ke`, and `map_Ns`: shared decoded ownership, UV/sampler/gradient reuse, fail-closed direct/prepared/list validation, deterministic fingerprints/inspection, and shared direct/environment specular semantics.

Milestone 71 maps linear `map_Ns` RGB by arithmetic mean into the bounded `[1,1000]` exponent domain as `1 + mean(rgb) * 999`. A mapped exponent replaces the uniform `Ns` fallback for that fragment and is resolved once for both direct Blinn-Phong specular and `EnvironmentReflectionMipPolicy::MaterialShininess`. The milestone also closes the emissive-only direct-mesh UV preflight gap discovered during integration review.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. `draw_prepared_model_list_back_to_front` computes one finite/projectable mean view-space Z key per non-empty prepared entry, uses stable far-to-near ordering, rejects vertex-program entries whose post-program positions are not represented by the canonical key, and delegates execution once to the existing prepared-list path. It does not classify opacity, split mixed material draws, sort triangles, or claim order-independent transparency.

Milestone 74 promotes the heterogeneous prepared-list architecture into a bounded flat-scene headless transaction. `OfflineSceneEntry` borrows one asset, model transform, and render options; `render_scene_preview` validates/snapshots all entries, computes one combined finite world-space bound, applies one global auto-fit while preserving relative transforms, and delegates either input ordering or M72 stable back-to-front ordering to the canonical prepared-list executor. Optional environment background remains a scene-global pre-geometry pass; diffuse/reflection environment state is injected through the existing fixed-light path with authoritative preview-camera viewer binding. Empty scenes remain deterministic clear/environment-only renders.

The exact integrated `main` commit is `75cff152ee7441baffd9460f684ac301450a4abf` (Milestone 74); its Linux, macOS, and ASan/UBSan post-merge CI is green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence. The branch `milestone-73-prepared-spatial-metadata` remains a reserved concurrent surface and must pass normal PR/exact-head/post-merge gates before this status can call it integrated.

The branch `milestone-75-explicit-material-shading-model` is also an active concurrent work surface rooted at M74. It owns the explicit material/BRDF boundary and must not be raced by unrelated tooling work. Its branch state is not integration evidence until it passes the normal PR and exact-main gates.

## Milestone 76 candidate — bounded flat-scene CLI manifests

M76 is deliberately independent from both the reserved M73 spatial-metadata surface and the active M75 material/shading surface. It closes the tooling gap left after M74: the library can execute heterogeneous flat scenes, but the headless CLI previously accepted only one OBJ asset.

Acceptance surface:

- `tiny_renderer_render` accepts either a legacy single `.obj` input or a bounded `.trscene` flat-scene manifest and preserves the existing output/environment/sampler controls;
- the manifest starts with exact `tiny-renderer-scene-v1`, accepts optional `ordering input|back-to-front` at most once, and accepts at most 256 model entries of `model FILE.obj TX TY TZ SCALE ROTATION_Y_RADIANS`;
- model paths are sibling OBJ filenames only: absolute paths, root components, parent traversal, and subdirectories reject rather than escaping the manifest directory;
- transform numbers must parse completely and be finite, with strictly positive uniform scale; malformed directives and trailing tokens reject with deterministic manifest path/line diagnostics;
- all referenced assets and render options are loaded before pointer-stable `OfflineSceneEntry` construction, then the complete scene delegates once to M74 `render_scene_preview` rather than duplicating framing, sorting, prepared submission, or raster logic;
- input/back-to-front manifest ordering maps directly to the existing M74/M72 ordering policies;
- global texture sampler controls are applied to each entry through existing `ModelRenderOptions`; environment diffuse/reflection on a non-empty heterogeneous scene requires one shared canonical normal binding and rejects otherwise rather than guessing a layout;
- an empty manifest remains a valid clear/environment-only scene;
- `.obj` input remains on the established `render_model_preview` path;
- Linux and macOS CI render the same two-model 4x manifest twice and require byte-identical PPM output, while ASan/UBSan executes the same complete manifest path;
- unsafe sibling traversal is required to fail on every CI path and must not create an output image;
- this milestone adds no hierarchy, animation, persistent scene graph, new material/BRDF semantics, spatial metadata, alternate raster path, GPU execution, or performance/image-quality claim.

PR candidate: `#90`. Exact implementation head `8183f6525c697ed0e31d1bbafa2be79db314c539` passed Ubuntu, macOS, and ASan/UBSan build/test/sample plus the new scene-manifest integration gates before this status synchronization. Because this status update changes the exact candidate head, the final head must pass the same three gates again before integration.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Submission-order helpers may reorder already-prepared work, but they must delegate execution to the canonical prepared-list path rather than creating a second raster path.
- Offline scene orchestration owns only bounded preparation, combined framing, environment injection, and executor selection; it does not own raster/material/depth/blend semantics.
- Flat-scene manifest import owns only bounded path/transform/order parsing; it delegates asset import to the canonical OBJ/model loader and rendering to `render_scene_preview`.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 76

Do not race the reserved M73 prepared-spatial-metadata surface or the active M75 explicit-material/shading surface. If either converges and integrates first, re-audit from that exact `main` before selecting another implementation slice. Once the material/scene work streams converge, prefer a cross-layer integration that consumes those real contracts over another parser/map-name feature: for example, scene-level tooling that exposes an already-integrated explicit material mode, or verified draw-granularity transparency ordering only if integrated spatial metadata can represent the required post-transform geometry. Keep hierarchy/animation/persistent scene storage deferred until a concrete executable requirement justifies them.
