# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 70

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI, validates unsupported combinations before output creation, and regression-locks implicit/default compatibility plus deterministic filtered rendering. Milestone 66 adds a bounded angular footprint for view-dependent environment reflection, seam-aware equirectangular gradients, and reuse of the same `Texture2D::sample_grad` nearest/trilinear/anisotropic path while preserving the historical base-level perfect-mirror default. Milestone 67 adds opt-in material-coupled glossy environment reflection by resolving the existing angular footprint once per draw as `max_footprint / shininess`. Milestone 68 exposes the existing reflection mip, anisotropy, fixed-footprint, and material-shininess policies through the headless renderer without creating a CLI-only sampling path. The subsequent headless display-output control slice also integrated explicit output transfer/exposure/Reinhard controls through the existing display-mapping path. Milestone 69 adds owned linear `map_Ks` reflectance textures through the same material cache, UV/sampler/gradient path, direct/environment specular consumers, fingerprints, and inspection surface. Milestone 70 adds owned linear/HDR `map_Ke` emissive textures through that same material data plane, including finite non-negative resource validation, shared cache ownership, mip/anisotropic gradient sampling, fingerprints, inspection, and fail-closed direct/prepared/list propagation.

The exact integrated `main` commit is `c902ac7531f81a4f28854e0a6bdce9da4cdc3394`; its Linux, macOS, and ASan/UBSan post-merge CI is green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. In the current lineage, milestone-60 and milestone-62 branch names exist outside the exact integrated `main` history, and an old `milestone-64-mtl-emissive-map` branch is only a stale branch point at the Milestone 63 commit rather than an implementation. Branch names must not be used as completion evidence; any future integration must pass the same PR/exact-head/post-merge gates as other milestones.

## Milestone 71 candidate — owned MTL `map_Ns` shininess textures

This candidate closes the mapped scalar exponent gap above the existing bounded uniform MTL `Ns` path. It extends the established material texture data plane rather than introducing roughness/PBR semantics or a second specular path.

Acceptance surface:

- bounded rich MTL accepts one sibling-safe `map_Ns <filename>` per material while legacy strict `load_mtl` continues to reject mapped-asset directives;
- canonical material definitions, compatibility batches, and `MaterialDraw` own an optional shininess texture through the shared `(path, transfer-function)` cache; `map_Ns` is interpreted as linear data;
- shininess textures must contain finite `[0,1]` texels and reuse the canonical material UV channels, sampler validation, mip chain, raster-derived gradients, trilinear filtering, and bounded 1x/2x/4x anisotropy;
- one documented scalar rule resolves sampled linear RGB by arithmetic mean and maps `[0,1]` monotonically into the existing bounded `[1,1000]` MTL shininess domain as `1 + mean(rgb) * 999`;
- a present `map_Ns` replaces the uniform `Ns` fallback for that fragment rather than multiplying it, while an absent map preserves the historical uniform exponent path;
- the resolved per-fragment shininess value is computed once and shared by direct Blinn-Phong specular records and `EnvironmentReflectionMipPolicy::MaterialShininess`, so direct and environment consumers cannot drift onto independent exponent semantics;
- direct/range/model/prepared/list paths reject invalid shininess resources, UV bindings, sampler state, or non-finite UV values before framebuffer mutation, including whole-list rejection when a malformed later entry would otherwise partially commit earlier draws;
- prepared plans retain shininess texture lifetime independently of the source asset, same-file linear material roles share decoded ownership, fingerprints include shininess texture semantic content only when present, and inspection exposes the owned dimensions;
- deterministic regressions cover parser strictness/path rejection, cache sharing, fingerprint/inspection semantics, scalar mapping, direct-specular equivalence, environment-material-shininess equivalence, invalid resource rejection, prepared lifetime, and later-list fail-closed UV behavior;
- the slice does not claim PBR roughness, microfacet equivalence, energy conservation, new UV transforms, new image formats, GPU acceleration, or performance/image-quality parity.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 71

After exact-head and post-merge CI close M71, stop farming additional MTL map names by default. Perform a rendering-architecture audit across material semantics, transparency submission, fixed lighting, environment lighting, and tooling, then promote to the highest-value executable subsystem frontier. A PBR/microfacet or roughness model must be introduced only as an explicit new material/BRDF architecture with deterministic semantics and evidence; it must not be implied by `map_Ns`.
