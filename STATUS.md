# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 64

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. In the current lineage, milestone-60 and milestone-62 branch names exist outside the exact integrated `main` history. Live comparison shows the milestone-60 reflection-mip branch has no commits ahead of M64 main, while milestone-62 contains separate OBJ parser work. Branch names must not be used as completion evidence; any future integration must pass the same PR/exact-head/post-merge gates as other milestones.

## Milestone 65 candidate — headless model texture sampler control

This candidate turns the M64 shared material sampler into an executable headless workflow capability rather than adding another filtering implementation.

Acceptance surface:

- `tiny_renderer_render` accepts `--texture-mip base|nearest|linear` and writes the selected policy directly into the existing `ModelRenderOptions::sampler.mip_filter`;
- `tiny_renderer_render` accepts `--texture-anisotropy 1|2|4` and writes the selected bound directly into the existing `SamplerState::max_anisotropy`;
- omitted flags preserve the historical `base` / anisotropy `1` defaults and therefore the existing headless render path;
- the CLI invokes the shared `validate_sampler_state` before rendering, so unsupported anisotropy levels and anisotropy greater than one with disabled mip filtering fail before output-file creation;
- diffuse, opacity, and normal material texture roles continue to share the same UV-gradient, sampler, mip-chain, transfer-function, and ownership machinery; M65 adds no role-specific sampler path;
- integration regression launches the built `tiny_renderer_render` executable against a generated OBJ/MTL/PPM asset, proving implicit defaults and explicit `base`/`1` are byte-identical, `linear`/`4` is deterministic across repeated invocations, and invalid combinations leave no output file;
- the initial implementation head passed Linux, macOS, and ASan/UBSan CI before this status closure update; the final candidate and eventual merge still require the same exact-head and post-merge gates;
- no performance, hardware-API equivalence, perceptual-quality, or colorimetric claim is made.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Texture roles share `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 65

After exact-head and post-merge CI close M65, the next architectural promotion should move beyond CLI flag expansion. The highest-value current gap is **reflection-environment ray-footprint mip selection**: perfect-mirror environment reflection exists, material texture gradients/mips/anisotropy exist, and background ray-footprint mip selection exists, but reflection sampling still needs a verified footprint policy tied to the camera/view-dependent reflection direction.

A coherent next slice should require deterministic reflection-direction derivatives or another explicitly documented finite footprint, reuse the existing environment texture mip chain and sampler rather than inventing a reflection-only filter, preserve legacy base-level reflection by default, reject malformed state before framebuffer mutation, and prove direct/prepared/headless equivalence. The stale `milestone-60-reflection-ray-footprint-mips` branch is currently ahead by zero commits relative to M64 main, so it is not active competing implementation work; recheck that live fact before starting the promoted slice.
