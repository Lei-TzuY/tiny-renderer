# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 66

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI, validates unsupported combinations before output creation, and regression-locks implicit/default compatibility plus deterministic filtered rendering. Milestone 66 adds a bounded angular footprint for view-dependent environment reflection, seam-aware equirectangular gradients, and reuse of the same `Texture2D::sample_grad` nearest/trilinear/anisotropic path while preserving the historical base-level perfect-mirror default.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. In the current lineage, milestone-60 and milestone-62 branch names exist outside the exact integrated `main` history. Branch names must not be used as completion evidence; any future integration must pass the same PR/exact-head/post-merge gates as other milestones.

## Milestone 67 candidate — material-coupled glossy environment reflection

This candidate promotes M66's reflection footprint from one light-level constant into an opt-in material semantic while retaining exactly one environment-reflection filtering implementation.

Acceptance surface:

- `EnvironmentReflectionMipPolicy::MaterialShininess` is opt-in; `BaseLevel` remains the source-compatible default and fixed `AngularFootprint` keeps its existing behavior;
- under the material-coupled policy, `angular_footprint_radians` is the maximum footprint at material shininess 1 and each material deterministically resolves `effective_footprint = max_footprint / shininess` using the existing bounded `MaterialState::shininess` domain `[1, 1000]`;
- each draw resolves material-coupled state once when its `Rasterizer` is constructed, then executes through the existing M66 `AngularFootprint` path and `Texture2D::sample_grad`; no second glossy sampler or fragment filter is introduced;
- material-independent reflection sampling explicitly rejects unresolved material-coupled state, and malformed mip/filter/footprint state still fails closed through existing reflection validation;
- two material draws sharing one environment reflection can resolve observably different deterministic reflection sharpness solely from their shininess values;
- direct and prepared 4x submissions remain exact-sample equivalent, and headless reflection remains exact-sample equivalent to explicit model options using the authoritative preview viewer;
- independent regressions compare shininess 1, 4, and 1000 against the documented `max_angle / Ns` gradients fed to the existing texture gradient sampler, and reject zero/out-of-range/non-finite shininess at the material-aware helper boundary;
- implementation head `ab4eca334bfb3b263453676e01b9ca03823e33cb` passed Linux, macOS, and ASan/UBSan CI including the complete test suite and sample before this status closure update; this documentation commit creates a new final candidate and therefore must pass the same exact-head gates before integration;
- this remains a bounded teaching-space glossy-reflection mapping. It does not claim a microfacet BRDF, Fresnel behavior, energy conservation, roughness calibration, importance sampling, hardware-API/PBR equivalence, perceptual quality, or performance improvement.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 67

After exact-head and post-merge CI close M67, the next architectural promotion should be **headless reflection filtering controls**. M66 and M67 are executable through the library/headless API, but `tiny_renderer_render` currently exposes environment-reflection path/intensity/yaw without a way to select the verified reflection mip footprint, anisotropy, or material-coupled glossy policy from the command line.

A coherent Milestone 68 should expose bounded CLI controls that configure the existing `EnvironmentReflectionState` rather than introducing CLI-only rendering behavior. It should preserve base-level reflection when no new flags are supplied; support the existing nearest/trilinear mip policy and bounded 1x/2x/4x anisotropy where valid; allow an explicit fixed angular footprint or the M67 material-shininess policy with a bounded maximum footprint; reject incomplete/conflicting/unsupported combinations before output-file creation; and prove deterministic CLI output plus byte/sample equivalence to the same library settings. It should not add a new sampler, automatic quality heuristics, material rewriting, PBR semantics, or performance/image-quality claims.
