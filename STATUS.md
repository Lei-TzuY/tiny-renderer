# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record; this file records the integrated frontier, the current integration candidate, and the next executable promotion. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 40

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point top-left coverage, interpolation qualifiers, depth/stencil/blend ownership, viewport/scissor, 4× MSAA, material/texture import, opacity and alpha-to-coverage, directional shadow mapping, alpha-tested cutouts, bounded fragment programs, and bounded object-space vertex programs. Milestones 36–40 extend that same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, a fixed-capacity deterministic multi-light collection, and bounded point-light cubemap shadows. None of these capabilities bypass the shared clipping, rasterization, interpolation, validation, or framebuffer ownership path.

Milestone 40 is integrated on `main` with post-merge Linux, macOS, and ASan/UBSan gates passing. The current main line also includes bounded signed OBJ relative face indices, bounded polygon triangulation, smoothing groups with deterministic generated normals, and deterministic ModelAsset inspection/fingerprint tooling.

## Milestone 41 — bounded deterministic spotlight shading

The current integration candidate adds a real cone-light record to the existing fixed-capacity light collection rather than creating a parallel lighting or raster path.

Implemented acceptance surface:

- `SpotLight` owns finite world-space position and viewer position, a finite non-zero outward cone direction, shared normal binding, bounded ambient/diffuse coefficients, non-negative finite linear/quadratic distance attenuation, and finite inner/outer cone cosines with `-1 <= outer < inner <= 1`.
- Spotlight direction is normalized after fail-closed validation. Cone falloff is deterministic in cosine space: full at or above the inner cosine, zero at or below the outer cosine, and linear between them.
- Ambient remains independent of distance/cone attenuation. Direct Lambert diffuse and Blinn-Phong specular share the same distance × cone factor and the existing perspective-correct world-space fragment position and normal-mapped shading normal.
- Directional, point, and spot records accumulate in caller order inside the existing `FixedLightCollection`; fragment programs still run after complete fixed-light shading and before discard/alpha/A2C/framebuffer ownership.
- Payload exclusivity, unknown light types, malformed cone/direction/attenuation/finite state, shared normal-binding mismatches, and point/directional shadow cross-type associations reject before framebuffer mutation.
- Direct triangles/meshes/ranges, direct models, prepared plans, instance batches, and heterogeneous prepared lists propagate the same state and preserve whole-list fail-closed behavior.
- Regression coverage locks inside/transition/outside cone results, distance attenuation, Blinn-Phong composition, mixed directional+point+spot accumulation, fragment-program ordering, direct-range/model equivalence, invalid-state rejection, prepared-list equivalence, and malformed-later-entry no-write behavior.

Milestone 41 deliberately does not add spotlight shadow maps, cookie/projector textures, IES profiles, physically based photometry, unlimited lights, GPU execution, or performance claims.

## Next frontier — Milestone 42

The next architectural promotion is **bounded deterministic spotlight shadow mapping**. This is the highest-value cross-layer integration after spotlight shading because the renderer already owns a verified 2D directional depth resource/capture path, point-light cubemap shadow ownership, perspective-correct world positions, and type-specific fixed-light shadow associations.

Acceptance for the first spotlight-shadow slice should require:

- one immutable owned 2D spotlight depth resource carrying the exact finite spotlight capture position, normalized capture direction, finite projection transform, and deterministic single-sample depth data;
- a capture helper that derives a bounded perspective frustum from explicit near/far planes and the spotlight outer cone, then renders through the existing prepared geometry, vertex-program, clipping, culling, alpha-test/opacity, and depth raster path rather than a shadow-only geometry implementation;
- one type-specific spotlight shadow association in `FixedLightCollection`, with missing resource, invalid bias, out-of-range/cross-type association, capture position/direction/cone mismatch, malformed projection state, and MVP-only execution rejected before framebuffer mutation;
- camera shading that samples from the established perspective-correct world position and modulates only the associated spotlight diffuse/specular response; ambient and unrelated lights remain independent and caller-order accumulation is unchanged;
- prepared ownership/lifetime, direct/range/model/instance/list propagation, vertex deformation, alpha-tested cutout capture, and whole-list fail-closed behavior on the same shared validation path;
- deterministic regressions for capture depth, inside/outside cone projection, occluded/unoccluded fragments, selective mixed-light shadowing, resource identity mismatch, invalid state before writes, prepared lifetime/list equivalence, and compatibility when spotlight shadows are disabled;
- no PCF/soft shadows, cookies, IES profiles, multiple spotlight shadow resources, cascades, GPU shadow APIs, physically based lighting, or performance claims in this first slice.

## Deliberate later work

General/full OBJ and MTL syntax, multiple material libraries, general image formats, mipmaps, anisotropic filtering, sRGB handling, destination alpha, transparency sorting/OIT, programmable sample locations and masks, centroid interpolation, temporal antialiasing, PCF/cascaded/soft shadows, cubemap seam filtering, multiple simultaneous point/spot shadow resources, cookie/IES lighting, explicit light colors, physically based BRDFs/IBL, general bump/parallax/displacement mapping, full shader languages/derivatives/JIT, GPU acceleration, and a general scene graph remain outside the current bounded CPU teaching architecture until a higher-value executable milestone justifies them.
