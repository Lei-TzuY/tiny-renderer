# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 55

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

Milestones 44–47 make shadow and minification state composable: bounded light records own typed shadow resources, hard/3x3-PCF sampling is explicit, directional lights may own deterministic cascade sets, and `Texture2D` owns mip chains plus nearest-level/trilinear sampling driven by raster-derived perspective-correct UV gradients. Milestones 48–52 make color/HDR boundaries explicit: opted-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM imports into the same linear texture/mipmap domain, and bounded Reinhard display mapping remains a read-only export view.

Milestones 53–55 promote linear HDR environment textures into an executable rendering subsystem without creating a second sampler or framebuffer path. M53 adds deterministic equirectangular camera backgrounds with per-sample 1x/4x rays and fail-closed precomputation; M54 connects the environment to the existing auto-fit headless OBJ preview/CLI transaction; M55 adds explicit camera-ray-footprint environment mip selection by deriving seam-aware equirectangular UV gradients and delegating LOD/filtering to the existing M47 `Texture2D::sample_grad` path.

The current main line also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

## Milestone 53 — bounded equirectangular HDR environment background

Implemented acceptance surface:

- `EnvironmentBackgroundState` borrows one existing linear `Texture2D` and carries a validated sampler, finite bounded intensity, and finite bounded yaw without introducing a second decoder, cache, mip chain, or framebuffer path;
- `PerspectiveCameraState` reconstructs world-space background rays from explicit finite eye/target/up/FOV/aspect state while geometry remains on the existing model/view/projection path;
- equirectangular mapping uses one documented convention: +Y north pole, -Y south pole, -Z at `u=0.5`, +X increasing longitude, +Z owning the canonical `u=0` seam, deterministic longitude at exact poles, and yaw around +Y;
- the exact +Z seam is canonicalized before `atan2` so signed-zero/libm differences cannot move seam ownership between `u=0` and `u=1` on supported platforms;
- 1x and 4x background rays use the renderer's established center/quarter-offset sample positions;
- rays, samples, and scaled HDR radiance are validated into temporary storage before the first framebuffer mutation;
- commit reuses `Framebuffer::test_and_write_sample` with replacement RGB, always-pass depth comparison, depth writes disabled, and stencil disabled, so later geometry occludes the background through unchanged depth/stencil ownership;
- deterministic regressions cover cardinal directions, seam/poles/yaw, per-sample reconstruction, PFM-imported/programmatic HDR equivalence, depth/stencil preservation, geometry occlusion, display mapping, and fail-closed invalid/overflow state.

## Milestone 54 — environment-enabled headless rendering

Implemented acceptance surface:

- `OfflineRenderSettings` carries a trailing optional borrowed environment state while the absent path preserves historical clear-color preview behavior;
- one auto-fit perspective camera definition is reused exactly for environment rays and geometry view/projection matrices;
- `tiny_renderer_render` preserves the historical positional form and adds bounded `--environment`, `--environment-intensity`, and `--environment-yaw` options with deterministic duplicate/unknown/malformed/orphan rejection;
- environment images live for the complete CLI transaction and load through the existing linear PPM/TGA/PFM dispatcher;
- output extension, CLI structure, environment loading/validation, model loading, and rendering all finish before PPM/PFM output creation;
- PPM remains default Reinhard display mapping followed by sRGB encoding, while PFM remains resolved-linear HDR serialization;
- real OBJ + HDR PFM 1x/4x CLI executions are repeated byte-for-byte in regression coverage.

## Milestone 55 — deterministic environment ray-footprint mip selection

Implemented acceptance surface:

- `EnvironmentMipPolicy::{BaseLevel,RayFootprint}` makes environment minification explicit. `BaseLevel` is the default and preserves M53/M54 behavior by requiring disabled mip filtering;
- `RayFootprint` requires the existing nearest-level or trilinear mip policy and does not add a second sampler, mip chain, cache, or LOD implementation;
- every 1x/4x environment sample first reconstructs its established perspective camera ray and equirectangular UV; gradients are derived from neighboring pixels using the same sample index;
- horizontal longitude derivatives use the shortest wrapped `u` distance across the canonical `u=0/1` seam, preventing a seam crossing from being interpreted as a near-full-texture footprint;
- boundary pixels use deterministic backward differences where a forward neighbor does not exist; one-pixel axes produce zero derivative on that axis;
- the resulting `TextureGradients` are delegated directly to M47 `Texture2D::sample_grad`, retaining its texel-space footprint calculation, non-negative LOD rule, available-level clamping, nearest-level selection, and trilinear interpolation;
- all rays, UVs, gradients, mip samples, and intensity-scaled radiance are computed before the first framebuffer write, preserving the environment transaction's fail-closed semantics;
- `tiny_renderer_render` adds `--environment-mip base|nearest|linear`; omitted/base remains base-level, while nearest/linear select ray-footprint LOD with the corresponding existing mip filter;
- regressions lock exact base-level per-sample compatibility, actual minification through an independently derived `sample_grad` reference, seam-wrapped derivatives, deterministic 1x/4x sample storage and resolve, invalid-policy no-mutation behavior, and repeated real OBJ + HDR PFM headless CLI output;
- the milestone makes no anisotropic/EWA, photographic image-quality, performance, GPU, or colorimetric parity claim.

## Next frontier — Milestone 56

Promote the environment texture from a camera-visible background into **bounded diffuse environment lighting** while keeping the renderer's existing linear-HDR, normal, material, and multi-light ownership explicit. The objective is not to label a single normal-direction lookup as physically based lighting; the slice should own a deterministic bounded hemispherical integration contract before making a diffuse-irradiance claim.

Acceptance should require:

- a distinct optional environment-light state borrows or owns a validated linear HDR environment without coupling background visibility to lighting enablement;
- diffuse irradiance for a surface normal is computed by a documented deterministic bounded hemisphere quadrature/integration rule with fixed sample count/order/weights, finite non-negative radiance checks, and deterministic equirectangular seam handling;
- integration output feeds the existing linear RGB material-lighting path as a diffuse contribution and composes deterministically with current directional/point/spot lights rather than bypassing the established fragment/light accumulator;
- normal transformation/interpolation remains on the existing inverse-transpose and perspective-correct paths; invalid normals, environment state, quadrature state, or radiance fail before framebuffer mutation through existing model/prepared/list preflight where applicable;
- imported PFM and equivalent programmatic HDR environments produce equivalent irradiance/render results, and background-off + lighting-on as well as background-on + lighting-off are independently executable;
- 1x/4x, prepared model, instance batch, heterogeneous list, clipping, depth/stencil, alpha, and display/HDR output semantics remain on their existing paths;
- regressions include analytic constant-environment cases, directional/high-contrast environment cases, rotation/yaw behavior, deterministic repeated execution, and fail-closed malformed/non-finite state;
- the first slice does not claim specular IBL, importance sampling, prefiltered reflection probes, BRDF LUTs, energy-conserving PBR conformance, Monte Carlo convergence guarantees, anisotropic filtering, GPU execution, or performance parity.
