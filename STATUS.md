# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record; this file records the currently integrated frontier and the next executable promotion. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Completed architecture through Milestone 38

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point top-left coverage, interpolation qualifiers, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadow mapping, alpha-tested cutouts, bounded fragment programs, and bounded object-space vertex programs. All later capabilities continue to route through the same clipping, rasterization, interpolation, shading, and framebuffer ownership path rather than introducing parallel renderers.

### Milestone 36 — bounded tangent-space normal mapping

- Material draws may own one optional normal-map texture imported through bounded rich-MTL `map_Bump`, sharing the existing UV/sampler and shared texture lifetime machinery.
- Tangent frames derive from canonical object-space triangle positions, UVs, and geometric normals; required degenerate or unstable bases reject before framebuffer mutation.
- Tangent/bitangent vectors use the model linear transform while geometric normals retain inverse-transpose normal transformation, including non-uniform scale.
- Normal-map shading remains integrated with direct/range/model/prepared/list execution, fragment programs, alpha stages, depth/stencil/blend ownership, and directional shadows.

Integrated on `main` as `e40276798506274d84fb293979184ed4fb40015d`; exact-main Linux, macOS, and ASan/UBSan CI passed.

### Milestone 37 — view-dependent Blinn-Phong specular lighting

- `MaterialState` carries bounded specular RGB and shininess with strict bounded MTL `Ks` / `Ns` import.
- Directional fixed lighting has a finite world-space viewer position and perspective-correct world-space fragment position is carried only when required.
- Fixed shading combines ambient/diffuse Lambert response with a Blinn-Phong half-vector specular term, using the same normal-mapped shading normal and directional shadow visibility.
- Bounded vertex programs deform geometry before world-space lighting positions are derived; optional fragment programs still observe one completed fixed-shading result before discard/alpha/A2C/framebuffer ownership.

Integrated on `main` as `12a75e631a1146b516ee1f3e8ed536572fd2c796`; exact-main Linux, macOS, and ASan/UBSan CI passed.

### Milestone 38 — bounded point-light shading

- `PointLight` adds finite world-space position/viewer state, bounded ambient/diffuse coefficients, and finite non-negative linear/quadratic attenuation.
- Diffuse/specular attenuation is exactly `1 / (1 + linear*d + quadratic*d^2)` while ambient remains deliberately unattenuated.
- Point lighting uses the established perspective-correct world-space fragment position, Blinn-Phong material state, tangent-space normal maps, shared preflight, and direct/range/model/prepared/list execution path.
- MVP-only fixed lighting rejects before ownership; malformed later prepared-list transforms fail the complete batch before earlier writes.
- The M38 first slice intentionally kept legacy directional and point lights mutually exclusive and did not add point-light shadows.

Integrated on `main` as `37eb47fa2d65e8ead8671831605dc47aab56bc9c`; exact-main Linux, macOS, and ASan/UBSan CI passed.

`main` then integrated bounded signed Wavefront OBJ relative face indices as `81875314bd6cf76a19251e1a7da4d3e0a4b33ba7`, preserving the same canonical legacy/rich OBJ parser and deterministic rejection of zero/out-of-range references.

## Milestone 39 — bounded deterministic multi-light accumulation

Current implementation PR: #47 (`milestone-39-bounded-multi-light`).

Implemented acceptance surface:

- `FixedLightCollection` is a fixed-capacity, caller-ordered collection of at most four directional/point records; a non-empty collection is mutually exclusive with the source-compatible legacy single-light state.
- Every active collection record is validated before framebuffer mutation. Selected payload state, finite direction/position/viewer values, bounded ambient/diffuse coefficients, non-negative point attenuation, supported light types, collection capacity, and one shared normal binding are fail-closed invariants.
- World position and the geometric/normal-mapped shading normal are derived once per sample, then ambient + Lambert diffuse + Blinn-Phong specular contributions accumulate in exact caller order without silently clamping float framebuffer RGB.
- A single existing directional shadow resource may be associated with exactly one directional record in the collection; point records remain unshadowed in this slice and invalid/missing associations reject before execution.
- The optional fragment program runs exactly once after complete fixed-light accumulation, followed by the established discard -> alpha test -> alpha-to-coverage -> stencil/depth/blend/color ownership sequence.
- Bounded object-space vertex programs execute before lighting world positions, so deformed geometry feeds point attenuation and specular evaluation consistently.
- Direct triangles/meshes/ranges, direct models, prepared instances, and heterogeneous prepared lists propagate the collection through shared validation and the same raster path. A malformed later prepared-list transform rejects the whole list before earlier color/depth/stencil mutation.
- Regression coverage proves one-record collection equivalence to legacy directional and point paths for both diffuse-only and non-zero Blinn-Phong specular materials, analytic two-point accumulation, directional-only shadow modulation in a mixed collection, fragment/vertex program ordering, prepared-list equivalence, and fail-closed validation.

The pre-hardening head `e225169af5a233ae791afa198ecbe4325d4cc2f6` passed Linux, macOS, and ASan/UBSan PR CI. The final exact PR head after compatibility/status hardening must pass those gates again before integration; M39 is not considered closed until the resulting exact `main` commit passes the same post-merge gates.

Milestone 39 deliberately makes no PBR, energy-conservation, unlimited-light, spotlight, point-shadow, clustered/deferred, GPU, or performance claim.

## Next frontier — Milestone 40

The next architectural promotion is **bounded deterministic point-light cubemap shadow mapping**. This is higher-value than adding another light enum or cosmetic state because it introduces a new depth resource topology, six-view capture, light/resource association, and per-sample visibility integration while exercising the M39 collection and existing fail-closed ownership architecture end to end.

Acceptance for the first slice should require:

- one immutable bounded depth-cubemap resource with six equally sized faces, non-zero/overflow-safe storage, finite normalized depth values, deterministic face addressing, and no hidden allocation during sampling;
- a deterministic helper that renders the same prepared/model geometry into the six canonical point-light view directions using one documented 90-degree projection convention and the existing depth-only raster path rather than a second rasterizer;
- explicit association of at most one cubemap shadow resource with one point-light record in the bounded M39 collection for the first slice; directional shadow association continues to use the existing 2D depth map and unsupported cross-type bindings reject before writes;
- point-shadow comparison performed from the same perspective-correct world-space fragment position already used for point direction/attenuation, with one finite non-negative bias rule and deterministic cubemap face/UV selection;
- only the associated point light's diffuse/specular contribution is visibility-modulated; ambient remains unshadowed, other point/directional records remain independent, and caller-order accumulation is unchanged;
- bounded vertex-program deformation, normal mapping, Blinn-Phong specular, fragment programs, alpha test/A2C, direct/range/model/prepared/list execution, and whole-batch preflight retain their established ordering and failure semantics;
- deterministic regressions cover six face directions/seams under the documented addressing rule, imported/programmatic geometry capture equivalence, lit/occluded analytic scenes, attenuation plus cubemap visibility, prepared/list propagation, malformed cubemap/resource association rejection, and a later invalid list entry leaving earlier framebuffer ownership untouched;
- the slice does not claim omnidirectional PCF/soft shadows, filtering across cubemap seams, multiple point-shadow maps, spotlights, physically based lighting, GPU cubemap APIs, or performance parity.

## Deliberate later work

General/full OBJ and MTL syntax, polygon triangulation, smoothing/generated normals, multiple material libraries, general image formats, mipmaps, anisotropic filtering, sRGB handling, destination alpha, transparency sorting/OIT, programmable sample locations and masks, centroid interpolation, temporal antialiasing, percentage-closer/cascaded/soft shadows, multiple simultaneous point-shadow resources, spotlights, explicit light colors, physically based BRDFs/IBL, general bump/parallax/displacement mapping, full shader languages/derivatives/JIT, GPU acceleration, and a general scene graph remain outside the current bounded CPU teaching architecture until a higher-value executable milestone justifies them.
