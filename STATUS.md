# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 63

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. In the current lineage, the repository contains milestone-60 and milestone-62 branches that are not part of the exact integrated `main` history represented above. Their branch names must not be used as completion evidence; any future integration must pass the same PR/exact-head/post-merge gates as other milestones.

## Milestone 64 — bounded anisotropic texture filtering

This branch promotes the M47 gradient sampler from isotropic mip selection to a bounded deterministic anisotropic option without adding a second sampler path.

Acceptance surface:

- `SamplerState::max_anisotropy` is a trailing source-compatible field and accepts exactly `1`, `2`, or `4`;
- `1` preserves the historical `sample_grad` max-footprint LOD path exactly;
- anisotropy greater than one requires an enabled mip policy and is rejected during existing sampler validation otherwise;
- the texture-space 2x2 gradient footprint is reduced analytically to deterministic major/minor principal axes;
- the minor axis selects the existing nearest-level or trilinear mip path while a bounded number of centered taps integrate along the major axis;
- every tap reuses existing clamp/repeat addressing and existing level filtering; no new mip chain, texture cache, derivative path, or color-space rule is introduced;
- extreme finite gradients are bounded before floating-to-integer tap conversion, avoiding out-of-range conversion behavior;
- direct `Texture2D::sample_grad` regressions lock 1x compatibility, 2x/4x tap behavior, derivative-column invariance, repeat-seam behavior, and invalid-state rejection;
- raster integration regression proves perspective/raster-derived UV gradients carry the anisotropy state into ordinary model texture sampling, with deterministic repeated output;
- prepared model construction inherits the same fail-closed sampler validation;
- no performance, hardware-API equivalence, perceptual-quality, or colorimetric claim is made without controlled evidence.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Texture roles share `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 64

After exact-head and post-merge CI close M64, the next work should be selected from live repository state rather than milestone-number assumption. Priority should go to a cross-layer consumer of the shared sampler capability (for example a bounded headless/material/environment anisotropy control) or to another independent architecture gap if active milestone-60/milestone-62 work occupies those surfaces. Do not duplicate or overwrite concurrent OBJ/parser work.
