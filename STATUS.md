# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 67

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI, validates unsupported combinations before output creation, and regression-locks implicit/default compatibility plus deterministic filtered rendering. Milestone 66 adds a bounded angular footprint for view-dependent environment reflection, seam-aware equirectangular gradients, and reuse of the same `Texture2D::sample_grad` nearest/trilinear/anisotropic path while preserving the historical base-level perfect-mirror default. Milestone 67 adds opt-in material-coupled glossy environment reflection by resolving the existing angular footprint once per draw as `max_footprint / shininess`; the exact merged `main` commit `9bfb75a1fe376afda90b92dd1dfe2922e8d5f355` passed Linux, macOS, and ASan/UBSan post-merge CI.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. In the current lineage, milestone-60 and milestone-62 branch names exist outside the exact integrated `main` history. Branch names must not be used as completion evidence; any future integration must pass the same PR/exact-head/post-merge gates as other milestones.

## Milestone 68 candidate — headless reflection filtering controls

This candidate exposes the already-integrated M66/M67 environment-reflection filtering policies through `tiny_renderer_render` without adding a CLI-only sampler or rendering path.

Acceptance surface:

- historical reflection path/intensity/yaw-only invocation remains base-level by default and is byte-identical to explicitly selecting base mip filtering with anisotropy 1;
- `--environment-reflection-mip base|nearest|linear` configures the existing reflection sampler mip mode;
- `--environment-reflection-anisotropy 1|2|4` configures the existing bounded anisotropy policy and inherits the shared sampler rule that anisotropy above 1 requires mip filtering;
- `--environment-reflection-footprint RADIANS` selects the existing fixed `EnvironmentReflectionMipPolicy::AngularFootprint` path when paired with filtered mip state;
- `--environment-reflection-material-shininess` selects the existing M67 `MaterialShininess` policy and treats the supplied footprint as the bounded maximum footprint resolved per draw from `MaterialState::shininess`;
- incomplete, orphaned, duplicated, conflicting, unsupported, or out-of-range reflection filtering controls reject before output-file creation through the existing parser, sampler, and reflection validators;
- deterministic 4x PFM regressions compare fixed-footprint and material-coupled CLI output byte-for-byte with the same direct `OfflineRenderSettings` / `EnvironmentReflectionState`, while an Ns=4 fixture proves the two policies are observably distinct;
- existing headless reflection, combined environment, generated-normal, zero-specular, and invalid-layout regressions remain on the same integration path;
- implementation head `c4da2a3ef1087269089fded4952d2092178329db` passed Linux, macOS, and ASan/UBSan CI including the complete suite before this status closure update; this documentation commit creates a new final candidate and therefore must pass the same exact-head gates before integration;
- the milestone adds no sampler, automatic quality heuristic, material rewrite, microfacet/PBR semantics, GPU behavior, image-quality claim, or performance claim.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 68

After exact-head and post-merge CI close M68, the next architectural promotion should be **owned MTL `map_Ks` specular textures**. The renderer already has bounded uniform `Ks`/`Ns`, material-texture ownership for diffuse/opacity/normal roles, Blinn-Phong direct-light specular, and view-dependent environment reflection, but specular reflectance is still uniform per draw. That leaves a cross-layer material data-plane gap rather than a frontend gap.

A coherent Milestone 69 should add one optional specular texture role through the existing rich MTL/material-asset path and shared texture ownership/cache. `map_Ks` should reuse the material draw's canonical UV channels, sampler, transfer-function interpretation, mip chain, and raster-derived gradients; missing `map_Ks` must preserve current uniform-`Ks` behavior exactly. The sampled linear RGB should modulate the existing bounded `MaterialState::specular` before both Blinn-Phong direct-light specular and environment-reflection contribution, so both consumers share one resolved per-fragment specular reflectance rather than duplicating texture sampling. Direct/range/model/prepared/list submission must retain fail-closed UV/sampler validation and owned lifetime semantics, including whole-list rejection before writes when later state is invalid. File-driven and programmatic regressions should prove imported/programmatic equivalence, shared decoded-texture deduplication, mip/anisotropy behavior, direct/prepared/list equivalence, and zero/unmapped compatibility. The slice should not add roughness/metalness workflows, Fresnel, microfacet BRDFs, energy conservation claims, new image formats, independent specular UV transforms, GPU execution, or performance/image-quality claims.
