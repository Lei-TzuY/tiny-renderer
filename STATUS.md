# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains older detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 76

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI. Milestones 66–68 add bounded glossy environment-reflection mip policies and expose them through the same library/tooling path. Milestones 69–71 complete the current bounded MTL material-texture data plane with owned `map_Ks`, `map_Ke`, and `map_Ns`: shared decoded ownership, UV/sampler/gradient reuse, fail-closed direct/prepared/list validation, deterministic fingerprints/inspection, and shared direct/environment specular semantics.

Milestone 71 maps linear `map_Ns` RGB by arithmetic mean into the bounded `[1,1000]` exponent domain as `1 + mean(rgb) * 999`. A mapped exponent replaces the uniform `Ns` fallback for that fragment and is resolved once for both direct Blinn-Phong specular and `EnvironmentReflectionMipPolicy::MaterialShininess`. The milestone also closes the emissive-only direct-mesh UV preflight gap discovered during integration review.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. `draw_prepared_model_list_back_to_front` computes one finite/projectable mean view-space Z key per non-empty prepared entry, uses stable far-to-near ordering, rejects vertex-program entries whose post-program positions are not represented by the canonical key, and delegates execution once to the existing prepared-list path. It does not classify opacity, split mixed material draws, sort triangles, or claim order-independent transparency.

Milestone 74 promotes the heterogeneous prepared-list architecture into a bounded flat-scene headless transaction. `OfflineSceneEntry` borrows one asset, model transform, and render options; `render_scene_preview` validates/snapshots all entries, computes one combined finite world-space bound, applies one global auto-fit while preserving relative transforms, and delegates either input ordering or M72 stable back-to-front ordering to the canonical prepared-list executor. Optional environment background remains a scene-global pre-geometry pass; diffuse/reflection environment state is injected through the existing fixed-light path with authoritative preview-camera viewer binding. Empty scenes remain deterministic clear/environment-only renders.

Milestone 76 promotes that flat-scene library transaction into the real headless CLI. `tiny_renderer_render` accepts either one legacy `.obj` input or one bounded `.trscene` manifest. The manifest starts with exact `tiny-renderer-scene-v1`, accepts optional `ordering input|back-to-front`, accepts at most 256 sibling-only OBJ model records with finite translation/uniform-scale/Y-rotation state, rejects traversal/subdirectories/absolute references, loads all referenced assets before pointer-stable scene construction, and delegates the complete scene exactly once to `render_scene_preview`. Global texture sampler and environment controls remain shared with the existing renderer. Linux/macOS CI render the same 4x scene manifest twice byte-identically, ASan/UBSan executes the complete manifest path, and unsafe traversal must fail without creating output.

The exact integrated `main` commit is `34622537a87492bb189095351da9043d0ac6135b` (Milestone 76). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ/flat-scene preview/render tooling.

### Milestone-number and concurrency note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence.

The branch `milestone-73-prepared-spatial-metadata` remains a reserved concurrent surface. Its current head still corresponds to the earlier M72 line rather than an integrated M73 implementation, so no spatial-metadata contract may be assumed until that work passes normal PR/exact-head/post-merge gates.

The branch `milestone-75-explicit-material-shading-model` is an active concurrent material/BRDF surface rooted before M76 and must not be raced by unrelated scene/tooling work. It is currently divergent from `main` and still contains construction-state commits/files; it is not integration evidence until it is cleaned, synchronized, fully tested, reviewable, and integrated through the normal gates.

## Milestone 77 candidate — explicit flat-scene perspective camera

M77 is deliberately confined to the M74/M76 flat-scene orchestration and CLI surface. It does not modify the reserved M73 spatial-metadata contract or the active M75 material/BRDF boundary. The executable gap is that flat-scene transforms are currently always recentered/rescaled through one fixed +Z auto-fit camera, so a manifest cannot preserve caller world-space composition while choosing a concrete viewpoint.

Acceptance surface:

- `.trscene` accepts at most one optional `camera EX EY EZ TX TY TZ UX UY UZ VFOV_RADIANS NEAR FAR` directive while preserving the existing v1 header, ordering directive, model records, path confinement, and deterministic line diagnostics;
- eye/target/up vectors and projection scalars must be finite; eye-target separation and up magnitude must be non-zero; up must not be parallel to the viewing direction; vertical FOV must lie in `(0, pi)`; near must be positive and far must be strictly greater than near;
- no camera directive preserves the historical M74/M76 combined-bounds auto-fit behavior and default output path;
- an explicit camera keeps every scene entry in caller world space, injects identity global fit, builds view/projection from the supplied camera, and still delegates exactly once to the canonical prepared-list or painter-order executor;
- environment background ray reconstruction uses the exact same active eye/target/up/FOV/aspect as geometry, and environment reflection uses the same active eye as viewer position;
- empty explicit-camera scenes remain valid clear/environment-only renders;
- CLI output identifies `camera=explicit|auto-fit` without changing raster/material semantics;
- Linux and macOS CI render the same explicit-camera 4x manifest twice byte-identically, require that its result differs from the legacy auto-fit view, and reject a degenerate camera without creating output; ASan/UBSan executes the same explicit-camera and rejection paths;
- the slice adds no hierarchy, animation, persistent scene graph, spatial-metadata contract, new material/BRDF semantics, alternate raster path, GPU execution, or performance/image-quality claim.

Candidate branch: `milestone-77-explicit-scene-camera`. Its exact final head must pass Ubuntu, macOS, and ASan/UBSan gates before integration; branch commits alone are not completion evidence.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Submission-order helpers may reorder already-prepared work, but they must delegate execution to the canonical prepared-list path rather than creating a second raster path.
- Offline scene orchestration owns only bounded preparation, framing/camera selection, environment injection, and executor selection; it does not own raster/material/depth/blend semantics.
- Flat-scene manifest import owns only bounded path/transform/order/camera parsing; it delegates asset import to the canonical OBJ/model loader and rendering to `render_scene_preview`.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 77

Do not race the reserved M73 prepared-spatial-metadata surface or active M75 material/shading surface. After M77 converges, re-read exact live `main` plus those branches before choosing the next implementation slice. Once the material/scene/spatial work streams expose real integrated contracts, prefer a cross-layer integration that consumes them over another parser or map-name feature: for example scene-level tooling for an integrated explicit material mode, or verified draw-granularity transparency ordering only if integrated spatial metadata can represent the required post-transform geometry. Keep hierarchy, animation, persistent scene storage, and broader camera/scene formats deferred until a concrete executable requirement justifies them.
