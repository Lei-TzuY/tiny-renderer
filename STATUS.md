# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the earlier detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 57

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

Milestones 44–47 make shadow and minification state composable: bounded light records own typed shadow resources, hard/3x3-PCF sampling is explicit, directional lights may own deterministic cascade sets, and `Texture2D` owns mip chains plus nearest-level/trilinear sampling driven by raster-derived perspective-correct UV gradients. Milestones 48–52 make color/HDR boundaries explicit: opted-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM imports into the same linear texture/mipmap domain, and bounded Reinhard display mapping remains a read-only export view.

Milestones 53–57 promote linear HDR environment textures into an executable rendering and lighting subsystem without creating a second sampler, framebuffer, or model-shading path. M53 adds deterministic equirectangular camera backgrounds with per-sample 1x/4x rays and fail-closed precomputation; M54 connects the environment to the existing auto-fit headless OBJ preview/CLI transaction; M55 adds explicit camera-ray-footprint environment mip selection by deriving seam-aware equirectangular UV gradients and delegating LOD/filtering to the existing M47 `Texture2D::sample_grad` path; M56 adds a separate bounded diffuse environment-light contribution using deterministic hemispherical quadrature through the established fixed-light/material shading path; M57 exposes that diffuse lighting independently through the headless preview/CLI transaction while safely sharing one decoded HDR texture when background and lighting name the same image.

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

## Milestone 57 — environment-lit headless rendering

Implemented acceptance surface:

- `OfflineRenderSettings` carries optional borrowed diffuse environment-light state independently from its optional environment background state; the absent path preserves historical clear/background-only output;
- `render_model_preview` injects that state into the existing `ModelRenderOptions.fixed_lights.environment_diffuse` path rather than introducing a headless-only shader, and explicitly rejects duplicate environment-light ownership instead of silently overwriting caller state;
- `tiny_renderer_render` adds independent `--environment-light`, `--environment-light-intensity`, and `--environment-light-yaw` controls with deterministic duplicate, orphan, malformed, and missing-file rejection; background visibility does not imply lighting and lighting does not require a visible background;
- CLI environment lighting accepts the canonical normal-bearing OBJ layouts already produced by the loader: normal-only `v//vn` uses channels 0/1/2 and UV+normal `v/vt/vn` uses 2/3/4; unsupported/no-normal layouts fail closed when lighting is requested;
- when background and diffuse lighting name the same lexically normalized image path, one decoded linear HDR `Texture2D` is retained for the complete CLI transaction and borrowed by both states; different paths remain independently loaded without adding an implicit cache layer;
- the existing auto-fit model transform, preview camera, view/projection matrices, material state, fixed-light accumulator, normal transformation/mapping, raster path, 1x/4x framebuffer ownership, Reinhard+sRGB PPM export, and resolved-linear PFM export remain authoritative;
- the registered headless-environment CTest now exercises lighting-only, background-only, and combined API rendering on 1x/4x targets, including deterministic repeats and an analytic constant-environment Lambert result;
- the same CTest creates a real normal-bearing OBJ/MTL plus linear HDR PFM fixture, launches the built `tiny_renderer_render` executable, and verifies byte-for-byte repeated 1x PPM and 4x PFM output for background-only, lighting-only, and combined modes;
- malformed CLI regressions verify orphan light options, duplicate light specification, and missing light files fail before output creation;
- the milestone makes no specular IBL, reflection-probe, BRDF-LUT, PBR, automatic-exposure, photographic-quality, GPU, or performance claim.

## Next frontier — Milestone 58

Promote the environment subsystem from diffuse-only material lighting to **bounded view-dependent HDR environment reflection**. The goal is to add a real specular environment capability while deliberately stopping short of pretending to implement a full physically based IBL pipeline.

Acceptance should require:

- a first-class environment-reflection state remains independent from background visibility and diffuse environment lighting, borrows an existing validated linear HDR `Texture2D`, and reuses the M53 equirectangular direction/yaw convention and existing sampler implementation;
- reflection direction is derived deterministically from the perspective-correct world-space fragment position, finite viewer position, and the same prepared world-space normal used by fixed lighting/normal mapping; no screen-space approximation or second varying path is introduced;
- the sampled linear HDR radiance is modulated by the existing bounded material specular RGB and added through the established lighting accumulator without multiplying diffuse albedo; zero material specular preserves the current M57 output exactly;
- this first reflection slice is explicitly a perfect-mirror teaching contract: material `shininess` continues to govern local Blinn-Phong lights but does not masquerade as roughness or an unverified mip/BRDF mapping for environment reflection;
- static/shared preflight rejects invalid texture domain, sampler, intensity/yaw, viewer position, normal binding, world-position requirements, and non-finite source radiance before framebuffer mutation; MVP-only submissions remain rejected whenever the required world-space state cannot be defined correctly;
- direct model, prepared plans, 1x/4x rendering, tangent-space normal mapping, heterogeneous prepared lists, opacity/alpha stages, depth/stencil/blend, HDR PFM output, and display mapping continue through the same execution path;
- deterministic regressions cover analytic constant environments, view/normal/yaw-dependent reflection lookup, zero-specular compatibility, additive composition with diffuse environment and fixed lights, imported-PFM/programmatic-HDR equivalence, prepared/list equivalence, 1x/4x behavior, and invalid-state no-write rejection;
- the slice makes no roughness-prefilter, importance-sampling, Fresnel, BRDF-LUT, energy-conserving PBR, Monte Carlo convergence, anisotropic, photographic-quality, GPU, or performance claim. Those require separate implementation and evidence.
