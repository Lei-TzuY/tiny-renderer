# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record; this file records the integrated frontier, the current integration candidate, and the next executable promotion. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 41

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point top-left coverage, interpolation qualifiers, depth/stencil/blend ownership, viewport/scissor, 4× MSAA, material/texture import, opacity and alpha-to-coverage, directional shadow mapping, alpha-tested cutouts, bounded fragment programs, and bounded object-space vertex programs. Milestones 36–41 extend that same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, a fixed-capacity deterministic multi-light collection, bounded point-light cubemap shadows, and bounded spotlight shading. None of these capabilities bypass the shared clipping, rasterization, interpolation, validation, or framebuffer ownership path.

Milestone 41 is integrated on `main` with post-merge Linux, macOS, and ASan/UBSan gates passing. The current main line also includes bounded signed OBJ relative face indices, bounded polygon triangulation, smoothing groups with deterministic generated normals, and deterministic ModelAsset inspection/fingerprint tooling.

## Milestone 42 — bounded deterministic spotlight shadow mapping

The current integration candidate extends the existing fixed-light and depth-shadow architecture with one type-specific spotlight shadow association rather than creating a parallel geometry or framebuffer path.

Implemented acceptance surface:

- `SpotShadowMap` owns immutable single-sample depth data together with the exact finite capture position, normalized capture direction, outer-cone cosine, and finite light view-projection transform.
- `render_spot_shadow_map` derives a square perspective frustum from explicit near/far planes and the spotlight outer cone, then reuses prepared geometry, object-space vertex programs, homogeneous clipping, culling, opacity/alpha-test handling, and the existing depth raster path.
- `FixedLightCollection` carries one optional spotlight-shadow association and shared owned resource lifetime. Missing maps, negative/non-finite bias, out-of-range or cross-type associations, and capture position/direction/cone mismatches reject fail-closed before framebuffer mutation.
- Camera shading projects the established perspective-correct world position through the captured spotlight transform. Visibility modulates only the associated spotlight diffuse/specular contribution; spotlight ambient and every unrelated light remain independent, and caller-order accumulation is unchanged.
- Direct/range/model/prepared/instance/list submission propagates the same state; prepared plans retain the map lifetime and heterogeneous lists preserve whole-list preflight before writes.
- Regression coverage locks capture depth and identity, occluded/unoccluded shading, selective mixed-spotlight shadowing, prepared lifetime/list byte+hash equivalence, invalid association/resource/capture state, alpha-tested cutout casters, vertex-program deformation before capture clipping/rasterization, malformed-later-entry no-write behavior, and disabled-state compatibility.
- The candidate deliberately does not add PCF/soft shadows, cookies, IES profiles, multiple simultaneous spotlight-shadow resources, cascades, GPU shadow APIs, physically based lighting, or performance claims.

PR #54 is the integration vehicle. Its implementation head must remain green on Ubuntu, macOS, and ASan/UBSan; after integration the exact new `main` commit must pass the same gates before Milestone 42 is considered closed.

## Next frontier — Milestone 43

The next architectural promotion is **explicit deterministic RGB light color** across the existing directional, point, and spotlight pipeline. The fixed-light system currently accumulates scalar ambient/diffuse intensity against implicitly white light, so mixed-light scenes cannot express colored illumination even though the renderer already owns deterministic multi-light accumulation, material albedo/specular, normal mapping, shadows, fragment programs, and unclamped float framebuffer storage.

Acceptance for the first RGB-light slice should require:

- directional, point, and spotlight records expose a finite bounded RGB light color with opaque-white `{1,1,1}` defaults so every established scene remains byte/hash compatible when the new state is unused;
- one documented component-wise rule tints ambient, Lambert diffuse, and Blinn-Phong specular contributions before deterministic caller-order accumulation, without moving material/shadow/fragment-program ownership into a second shading path;
- point/spot attenuation and cone falloff remain scalar geometric factors, while directional/point/spot shadow visibility continues to modulate only the associated direct contribution and preserves colored ambient semantics;
- legacy singular directional/point-light submission and `FixedLightCollection` use the same color semantics, validation, normal binding, world-position requirements, and fail-closed model/prepared/list propagation;
- malformed non-finite/out-of-range color state rejects before framebuffer mutation, including a malformed later prepared-list entry;
- deterministic regressions cover white-default compatibility, primary/secondary color composition, mixed differently colored light types, tinted specular, shadowed colored direct light with unaffected ambient/unrelated lights, fragment-program ordering, direct/range/model equivalence, and prepared-list byte/hash equivalence;
- this slice makes no spectral, color-temperature, sRGB, exposure/tonemapping, HDR photometry, physically based BRDF, or performance claim.

## Deliberate later work

General/full OBJ and MTL syntax, multiple material libraries, general image formats, mipmaps, anisotropic filtering, sRGB handling, destination alpha, transparency sorting/OIT, programmable sample locations and masks, centroid interpolation, temporal antialiasing, PCF/cascaded/soft shadows, cubemap seam filtering, multiple simultaneous point/spot shadow resources, cookie/IES lighting, spectral/color-temperature lighting, physically based BRDFs/IBL, general bump/parallax/displacement mapping, full shader languages/derivatives/JIT, GPU acceleration, and a general scene graph remain outside the current bounded CPU teaching architecture until a higher-value executable milestone justifies them.
