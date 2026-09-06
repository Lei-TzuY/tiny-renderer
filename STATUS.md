# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record; this file records the currently integrated frontier and the next executable promotion. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Completed architecture through Milestone 35

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point top-left coverage, interpolation qualifiers, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadow mapping, alpha-tested cutouts, bounded fragment programs, and bounded object-space vertex programs. All later capabilities continue to route through the same clipping, rasterization, interpolation, and framebuffer ownership path rather than introducing parallel renderers.

## Milestone 36 — bounded tangent-space normal mapping

- Material draws may own one optional normal-map texture imported through the bounded rich-MTL `map_Bump` path, sharing the existing UV/sampler and shared texture lifetime machinery.
- Tangent frames derive from canonical object-space triangle positions, UVs, and geometric normals. Required degenerate or numerically unstable geometry/UV bases reject before framebuffer mutation.
- Tangent and bitangent vectors use the model linear transform while geometric normals retain the inverse-transpose normal transform, including non-uniform-scale coverage.
- Normal-map RGB is decoded into a bounded tangent-space normal, normalized safely, transformed into the existing fixed-light space, and consumed by the established lighting/shadow path.
- Direct triangles/meshes/ranges, model submission, prepared instances, heterogeneous lists, fragment programs, alpha testing, A2C, and framebuffer ownership keep their established ordering and fail-closed behavior.

Integrated on `main` as `e40276798506274d84fb293979184ed4fb40015d`; exact-main Linux, macOS, and ASan/UBSan CI passed.

## Milestone 37 — view-dependent Blinn-Phong specular lighting

- `MaterialState` carries bounded specular RGB and shininess, with strict bounded MTL `Ks` / `Ns` import.
- Directional fixed lighting has an explicit finite world-space viewer position.
- The camera raster path carries perspective-correct world-space fragment position only when required for view-dependent fixed lighting.
- Fixed lighting combines material base RGB with ambient/diffuse Lambert response and a Blinn-Phong half-vector specular term; directional shadow visibility applies to the directional lit contribution rather than to ambient.
- Tangent-space normal mapping feeds the same shading normal used by diffuse and specular terms, and M35 vertex programs deform geometry before world-space fragment positions are derived.
- Optional fragment programs still observe one completed fixed-shading RGB/opacity result before discard, alpha test, A2C, and framebuffer ownership.

Integrated on `main` as `12a75e631a1146b516ee1f3e8ed536572fd2c796`; exact-main Linux, macOS, and ASan/UBSan CI passed.

## Milestone 38 — bounded point-light shading

Current implementation PR: #45 (`milestone-38-bounded-point-light`).

Acceptance and implemented behavior:

- `PointLight` is explicit raster/model state with a finite world-space position, finite viewer position, bounded ambient/diffuse coefficients, and finite non-negative linear/quadratic attenuation.
- Diffuse/specular attenuation is exactly `1 / (1 + linear*d + quadratic*d^2)`; ambient is deliberately unattenuated.
- This first slice keeps directional and point fixed lights mutually exclusive rather than silently inventing multi-light accumulation semantics.
- Point lighting requires separate model/view/projection transforms. MVP-only direct/model/prepared execution rejects before RGB, depth, or stencil ownership.
- Per-sample point direction and distance use the existing perspective-correct world-space fragment position after any bounded object-space vertex program.
- Material Blinn-Phong specular and tangent-space normal maps feed the same fixed-light path for point lighting.
- Existing directional shadow state remains directional-only; point-only lighting rejects that binding rather than misapplying a 2D directional depth map.
- Direct triangle/mesh/range execution, direct models, prepared instances, and heterogeneous prepared lists all propagate point-light state through shared preflight and the existing raster path.
- Whole-list preflight rejects a malformed later world transform before an earlier valid entry can mutate the framebuffer.
- Regression coverage includes analytic attenuation, front/behind Lambert response, viewer-dependent specular, normal-map interaction, invalid fixed-light combinations, MVP-only fail-closed behavior, and later-entry whole-list rejection.

The implementation head `d3da375e1725f5d92eb91c44f2a4b71b59fd0cb1` passed Linux, macOS, and ASan/UBSan PR CI before this status update. The final PR head must pass those gates again before integration.

Milestone 38 deliberately does not claim multiple simultaneous lights, point-light shadow maps/cubemaps, physically based falloff, light colors, spotlights, clustered/deferred lighting, or performance/API conformance.

## Next frontier — Milestone 39

The next architectural promotion is **bounded deterministic multi-light accumulation**. The purpose is to remove the current directional-versus-point mutual exclusion by introducing one explicit, validated fixed-light collection while preserving the single raster/shading/ownership path.

Acceptance for the first slice:

- expose a small documented fixed-capacity light collection rather than an unbounded scene graph or per-sample allocation path;
- preserve existing single-directional and single-point rendering byte/hash-equivalently when represented through the new collection;
- validate every light record and every cross-resource relationship before framebuffer mutation, including finite positions/viewers, bounded coefficients, non-negative attenuation, normal bindings, and supported shadow association;
- compute/interpolate world position and the final geometric/normal-mapped shading normal once per sample, then accumulate individual light contributions in deterministic caller order;
- define ambient, Lambert diffuse, and Blinn-Phong specular accumulation explicitly without silently clamping float framebuffer RGB; existing RGB8 export remains the bounded display/output boundary;
- allow the existing directional shadow map to modulate only its associated directional light contribution while point-light contributions remain unshadowed in this slice;
- run the optional fragment program exactly once on the completed accumulated fixed-light RGB/opacity result, followed by the established discard -> alpha test -> alpha-to-coverage -> stencil/depth/blend/color ownership sequence;
- propagate the light collection through direct triangles/meshes/ranges, direct models, prepared instances, and heterogeneous prepared lists with whole-batch fail-closed preflight;
- add deterministic regressions for one-light backward compatibility, two-point analytic accumulation, directional-plus-point accumulation with directional shadowing, normal-map/specular interaction, M35 vertex deformation, prepared/list equivalence, and a malformed later light/list entry rejecting before writes;
- make no claim of physically based energy conservation, unlimited lights, spotlights, point-light cubemap shadows, clustered/deferred lighting, GPU execution, or performance improvement.

## Deliberate later work

General/full OBJ and MTL syntax, polygon triangulation, relative OBJ indices, smoothing/generated normals, multiple material libraries, general image formats, mipmaps, anisotropic filtering, sRGB handling, destination alpha, transparency sorting/OIT, programmable sample locations and masks, centroid interpolation, temporal antialiasing, percentage-closer/cascaded/soft shadows, point-light cubemap shadows, spotlights, explicit light colors, physically based BRDFs/IBL, general bump/parallax/displacement mapping, full shader languages/derivatives/JIT, GPU acceleration, and a general scene graph remain outside the current bounded CPU teaching architecture until a higher-value executable milestone justifies them.