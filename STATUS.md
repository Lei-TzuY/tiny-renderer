# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the earlier detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 56

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

Milestones 44–47 make shadow and minification state composable: bounded light records own typed shadow resources, hard/3x3-PCF sampling is explicit, directional lights may own deterministic cascade sets, and `Texture2D` owns mip chains plus nearest-level/trilinear sampling driven by raster-derived perspective-correct UV gradients. Milestones 48–52 make color/HDR boundaries explicit: opted-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM imports into the same linear texture/mipmap domain, and bounded Reinhard display mapping remains a read-only export view.

Milestones 53–56 promote linear HDR environment textures into an executable rendering and lighting subsystem without creating a second sampler or framebuffer path. M53 adds deterministic equirectangular camera backgrounds with per-sample 1x/4x rays and fail-closed precomputation; M54 connects the environment to the existing auto-fit headless OBJ preview/CLI transaction; M55 adds explicit camera-ray-footprint environment mip selection by deriving seam-aware equirectangular UV gradients and delegating LOD/filtering to the existing M47 `Texture2D::sample_grad` path; M56 adds a separate bounded diffuse environment-light contribution using deterministic hemispherical quadrature through the established fixed-light/material shading path.

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

## Milestone 56 — bounded diffuse environment lighting

Implemented acceptance surface:

- `EnvironmentDiffuseState` is distinct from camera-background visibility and borrows one validated linear HDR `Texture2D` with bounded intensity/yaw plus the established sampler state;
- diffuse irradiance uses a fixed deterministic 16-direction cosine-weighted hemisphere quadrature with documented order and equal `pi/N` irradiance weights; a constant environment therefore integrates to `pi * radiance` and its Lambert factor returns the scaled radiance exactly within numeric tolerance;
- equirectangular sampling reuses the M53 world-direction mapping and seam convention rather than introducing a second environment coordinate system;
- the contribution multiplies the existing linear material base RGB and is added through the established fixed-light accumulator, composing deterministically with directional/point/spot lighting while remaining independent from background drawing;
- environment diffuse lighting shares the active normal binding and therefore reuses inverse-transpose normal transformation, perspective-correct interpolation, tangent-space normal mapping, and the existing fixed-light normal preparation path;
- static/shared preflight validates linear texture domain, sampler state, disabled mip filtering for this first lighting slice, finite bounded intensity/yaw, finite non-negative source radiance, light-capacity/type state, and normal-binding compatibility before framebuffer ownership;
- direct model, prepared submission, 1x/4x rasterization, and heterogeneous prepared-list execution retain the same material/raster/depth/stencil/alpha paths; prepared state preserves the borrowed environment reference contract without duplicating the texture;
- deterministic regressions cover analytic constant environments, high-contrast directional environments and yaw, repeated integration, environment-only 1x/4x rendering, additive composition with fixed directional light, imported-PFM versus programmatic HDR equivalence, prepared/list equivalence, and invalid radiance/texture-domain no-write rejection;
- the registered CTest target executes as part of the normal Linux/macOS/sanitizer test suite rather than existing as an unexecuted test source;
- the milestone makes no specular IBL, importance-sampling, prefiltered reflection-probe, BRDF-LUT, energy-conserving PBR, Monte Carlo convergence, anisotropic, GPU, or performance-parity claim.

## Next frontier — Milestone 57

Promote M56 from a library-level lighting capability into **environment-lit headless rendering** so a real OBJ + HDR environment can exercise background visibility and diffuse environment lighting independently through the existing offline render transaction and CLI.

Acceptance should require:

- `OfflineRenderSettings` can carry diffuse environment-light state independently from its optional environment background state, preserving the historical clear/background-only path when lighting is absent;
- the headless CLI exposes bounded explicit environment-light controls with deterministic duplicate, malformed, orphan, and missing-file rejection; enabling background must not implicitly enable lighting and enabling lighting must not require a visible background;
- when background and diffuse lighting intentionally reference the same image/configuration, the CLI transaction owns one decoded linear HDR texture for the full render and safely shares that lifetime rather than reopening or duplicating an implicit cache path;
- the same auto-fit model/view/projection and existing normal/material/fixed-light execution path are used; no scene graph or second shading path is introduced;
- configuration, environment loading, model loading, environment-light validation, and render preflight complete before output creation or framebuffer mutation where the existing transaction permits it;
- real OBJ + HDR PFM regressions cover lighting-only, background-only, combined background+lighting, 1x/4x, deterministic repeated PPM/PFM output, and malformed-state fail-closed behavior;
- output display mapping and HDR serialization retain M49–M52 semantics and the slice makes no PBR/specular-IBL, automatic exposure, photographic-quality, GPU, or performance claim.
