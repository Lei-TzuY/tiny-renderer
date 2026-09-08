# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 69

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI, validates unsupported combinations before output creation, and regression-locks implicit/default compatibility plus deterministic filtered rendering. Milestone 66 adds a bounded angular footprint for view-dependent environment reflection, seam-aware equirectangular gradients, and reuse of the same `Texture2D::sample_grad` nearest/trilinear/anisotropic path while preserving the historical base-level perfect-mirror default. Milestone 67 adds opt-in material-coupled glossy environment reflection by resolving the existing angular footprint once per draw as `max_footprint / shininess`. Milestone 68 exposes the existing reflection mip, anisotropy, fixed-footprint, and material-shininess policies through the headless renderer without creating a CLI-only sampling path. The subsequent headless display-output control slice also integrated explicit output transfer/exposure/Reinhard controls through the existing display-mapping path. Milestone 69 then adds owned linear `map_Ks` reflectance textures through the same material cache, UV/sampler/gradient path, direct/environment specular consumers, fingerprints, and inspection surface. The current exact `main` commit before Milestone 70 is `7df5cd6f2bc56455c47d7e130ca266a8be6ece08`, and its Linux, macOS, and ASan/UBSan post-merge CI is green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. In the current lineage, milestone-60 and milestone-62 branch names exist outside the exact integrated `main` history, and an old `milestone-64-mtl-emissive-map` branch is only a stale branch point at the Milestone 63 commit rather than an implementation. Branch names must not be used as completion evidence; any future integration must pass the same PR/exact-head/post-merge gates as other milestones.

## Milestone 70 candidate — owned MTL `map_Ke` emissive textures

This candidate closes the remaining texture-driven radiance gap above the integrated bounded uniform `Ke` path. It extends the existing material data plane rather than introducing an emitter subsystem or a second shading path.

Acceptance surface:

- bounded rich MTL accepts one sibling-safe `map_Ke <filename>` per material while legacy strict `load_mtl` continues to reject mapped-asset directives;
- canonical material definitions, compatibility batches, and `MaterialDraw` own an optional emissive texture through the shared `(path, transfer-function)` cache, with PPM/TGA/PFM dispatch unchanged;
- `map_Ke` is linear data and may contain finite non-negative HDR texels above one; the role deliberately does not reuse the `[0,1]` specular/reflectance constraint;
- emissive maps reuse canonical material UV channels, sampler validation, mip chains, raster-derived gradients, trilinear filtering, and bounded 1x/2x/4x anisotropy rather than adding role-specific filtering;
- fixed shading resolves exactly one `emissive_radiance = MaterialState::emissive * sampled_map_Ke_rgb`; absent `map_Ke` resolves to the historical uniform `Ke`, and the result is added on every fixed-shading exit before the fragment program;
- direct/range/model/prepared/list paths reject invalid emissive resources or UV/sampler state before framebuffer mutation, and whole-list preflight prevents a malformed later emissive draw from partially committing earlier entries;
- prepared plans retain emissive texture lifetime independently of the source asset; repeated same-file linear material roles share decoded ownership;
- model fingerprints add emissive texture content only when that role exists, preserving map_Ke-free historical fingerprints, and inspection exposes emissive texture dimensions;
- deterministic regressions cover parser strictness, file/programmatic equivalence, shared cache ownership, HDR PFM values above one, pre-fragment-program ordering, mip/anisotropy sampler reuse, negative-resource rejection, later-list invalid-UV fail-closed behavior, prepared lifetime, fingerprint content semantics, and inspection visibility;
- the slice does not add bloom, global illumination, area-light emission, new UV transforms, automatic exposure, GPU acceleration, or performance/image-quality claims.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 70

After exact-head and post-merge CI close M70, the next material-data frontier should be a bounded owned `map_Ns` shininess texture feeding the already-shared direct Blinn-Phong exponent and material-coupled glossy environment-reflection footprint. That slice should preserve one UV/sampler/gradient path, define a deterministic scalar extraction/range mapping, and prove direct/environment consumers share one resolved per-fragment shininess value. It should not be represented as PBR roughness or microfacet support without a separate architectural promotion and evidence.
