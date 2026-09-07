# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 65

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI, validates unsupported combinations before output creation, and regression-locks implicit/default compatibility plus deterministic filtered rendering.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. In the current lineage, milestone-60 and milestone-62 branch names exist outside the exact integrated `main` history. Branch names must not be used as completion evidence; any future integration must pass the same PR/exact-head/post-merge gates as other milestones.

## Milestone 66 candidate — reflection environment footprint mip selection

This candidate makes mip/aniso selection executable for view-dependent environment reflection without introducing a second reflection filter or changing the legacy perfect-mirror default.

Acceptance surface:

- `EnvironmentReflectionState` gains trailing `EnvironmentReflectionMipPolicy::{BaseLevel, AngularFootprint}` plus a bounded angular footprint, preserving source-compatible default aggregate state;
- `BaseLevel` requires disabled mip filtering and a zero angular footprint, preserving M58/M59 direct environment sampling;
- `AngularFootprint` requires nearest or linear mip filtering and a finite caller-selected radius in `(0, pi/2]`;
- the actual view-dependent reflected world-space ray remains the lookup center; two deterministic tangent-basis neighbor rays define a finite teaching footprint around that center;
- center/neighbor rays reuse the established equirectangular mapping and shortest wrapped longitude delta, so footprint derivatives cannot spuriously span the `u=0/1` seam;
- the resulting gradients are delegated to the existing `Texture2D::sample_grad`, inheriting shared nearest/trilinear mip selection and bounded 1x/2x/4x anisotropy rather than duplicating filtering logic;
- existing fixed-light/reflection state propagation automatically carries the policy through direct, prepared, instance/list, and headless preview paths, while existing reflection validation rejects malformed state before framebuffer execution;
- deterministic regressions lock base-level compatibility, independent gradient/reference equivalence, seam wrapping, invalid-state rejection, direct/prepared exact-sample equivalence, headless authoritative-viewer equivalence, and prepared-model fail-closed validation;
- the implementation head `b5d46a542a87e688af58b9d7a17046d894a14d50` passed Linux, macOS, and ASan/UBSan CI before this status closure update; this documentation commit creates a new final candidate that must pass the same exact-head gates before integration;
- this is an explicitly documented finite angular footprint. It does not claim true screen-space reflection derivatives, normal-map derivative propagation, roughness filtering, hardware-API/PBR equivalence, perceptual quality, or performance improvement.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 66

After exact-head and post-merge CI close M66, the next architectural promotion should be **material-coupled glossy environment reflection**. M66 supplies a verified reflection mip footprint, but the footprint is still one reflection-light-level value shared by every material in the submission. The renderer already owns bounded material specular color and Blinn-Phong shininess, so the next valuable cross-layer slice is to make environment-reflection sharpness a material semantic rather than a global reflection-light tuning knob.

A coherent Milestone 67 should preserve the current perfect-mirror/base-level default, define one bounded deterministic mapping from material state to reflection footprint (or an equally explicit material-level reflection-blur parameter), feed that footprint through the M66 sampler instead of adding a new filter, preserve direct/prepared/headless equivalence and fail-closed validation, and prove that two materials under one environment light can resolve different deterministic reflection sharpness. It must remain a teaching-space glossy-reflection model: no microfacet BRDF, Fresnel, energy-conservation, importance-sampling, hardware-API, or PBR-conformance claim without a substantially broader implementation and evidence base.
