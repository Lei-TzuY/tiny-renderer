# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record; this file records the currently integrated frontier and the next executable promotion. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 39

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point top-left coverage, interpolation qualifiers, depth/stencil/blend ownership, viewport/scissor, 4× MSAA, material/texture import, opacity and alpha-to-coverage, directional shadow mapping, alpha-tested cutouts, bounded fragment programs, and bounded object-space vertex programs. Milestones 36–39 extend that same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, and a fixed-capacity deterministic multi-light collection. None of these capabilities bypass the shared clipping, rasterization, interpolation, validation, or framebuffer ownership path.

Milestone 39 is integrated on `main`. The current `main` head `a77f50bfe1bd7d911921ccc0b727ed76d0403153` also includes bounded signed OBJ relative face indices, bounded polygon triangulation, and smoothing groups with deterministic generated normals. Its Linux, macOS, and ASan/UBSan CI gates pass.

## Milestone 40 — bounded point-light cubemap shadows

Current implementation PR: #50 (`milestone-40-point-shadow-cubemap`).

Implemented acceptance surface:

- `DepthCubemap` is an immutable six-face single-sample depth resource with equal non-zero face size, overflow-safe storage, finite normalized depth values, six finite face view-projection transforms, and deterministic dominant-axis face selection with X → Y → Z tie precedence.
- Every cubemap owns the exact finite world-space point-light position used for capture. Binding the resource to a different point-light position is rejected during static/shared preflight before framebuffer mutation; face selection and stored face projections can therefore never silently use different origins.
- `render_point_shadow_cubemap` renders six documented 90-degree canonical point-light views through the existing prepared-model, vertex-program, clipping, culling, alpha-test/opacity, and depth raster path. Programmed geometry is prepared once per entry and reused across the six faces.
- `PointShadowState` and `FixedLightCollection::shadowed_point_index` associate at most one owned cubemap with one point-light record for this first slice. Missing maps, negative/non-finite bias, out-of-range indices, cross-type associations, capture-origin mismatches, and MVP-only execution reject fail-closed.
- Camera shading uses the established perspective-correct world-space fragment position for point direction, attenuation, cubemap face selection, face projection, nearest depth comparison, and Blinn-Phong lighting.
- Shadow visibility modulates only the associated point light's diffuse/specular response. Ambient remains unshadowed, all other fixed lights remain independent, and caller-order accumulation is unchanged.
- Direct triangles/meshes/ranges, direct models, prepared plans, instance batches, and heterogeneous prepared lists propagate the same state. Prepared plans retain cubemap lifetime; a malformed later list entry cannot partially commit earlier color/depth/stencil ownership.
- Regression coverage locks all six axial faces and tie precedence, capture depth, near/far validation, capture-origin ownership, non-finite origins, selective two-point-light shadowing, prepared lifetime/list equivalence, invalid associations, origin mismatch before mutation, and later-invalid-list fail-closed behavior.

The corrected exact PR candidate must pass Linux, macOS, and ASan/UBSan CI again after this status/roadmap synchronization. M40 is not closed until the resulting exact `main` commit passes the same post-merge gates.

Milestone 40 deliberately makes no PCF/soft-shadow, seam filtering, multiple point-cubemap association, spotlight, distance-linear cubemap depth, GPU cubemap, automatic shadow allocation, physically based lighting, or performance-parity claim.

## Next frontier — Milestone 41

The next architectural promotion is **bounded deterministic spotlight shading**. This is the highest-value next fixed-light extension now that directional and point lighting, multi-light accumulation, and both directional/point hard-shadow resource topologies are established. The slice should add executable cone-light behavior rather than an enum/API shell.

Acceptance for the first slice should require:

- one `SpotLight` fixed-light record with finite world-space position, finite non-zero direction, shared normal binding, finite viewer position, bounded ambient/diffuse coefficients, and non-negative linear/quadratic distance attenuation;
- explicit finite inner/outer cone state with a documented deterministic angular falloff and fail-closed ordering constraints; malformed or degenerate cone definitions reject before ownership rather than being silently clamped;
- diffuse/specular spotlight contribution computed from the same perspective-correct world position and normal-mapped shading normal already used by point lights, with ambient semantics documented separately from cone/distance attenuation;
- caller-ordered accumulation with directional/point records inside the existing fixed-capacity collection, preserving fragment-program ordering and the established discard → alpha test → A2C → stencil/depth/blend/color ownership sequence;
- direct/range/model/prepared/list propagation, static/shared validation, vertex-program deformation, normal mapping, and whole-list fail-closed behavior without a parallel raster path;
- deterministic analytic regressions for inside/transition/outside cone behavior, distance attenuation composition, Blinn-Phong specular, mixed-light caller order, program ordering, prepared/list equivalence, and invalid-state rejection before writes;
- the first spotlight slice does not yet add spotlight shadow maps, cookie/projector textures, IES profiles, physically based photometry, unlimited lights, GPU execution, or performance claims.

## Deliberate later work

General/full OBJ and MTL syntax, multiple material libraries, general image formats, mipmaps, anisotropic filtering, sRGB handling, destination alpha, transparency sorting/OIT, programmable sample locations and masks, centroid interpolation, temporal antialiasing, PCF/cascaded/soft shadows, cubemap seam filtering, multiple simultaneous point-shadow resources, spotlight shadows, explicit light colors, physically based BRDFs/IBL, general bump/parallax/displacement mapping, full shader languages/derivatives/JIT, GPU acceleration, and a general scene graph remain outside the current bounded CPU teaching architecture until a higher-value executable milestone justifies them.
