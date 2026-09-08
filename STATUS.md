# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 68

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI, validates unsupported combinations before output creation, and regression-locks implicit/default compatibility plus deterministic filtered rendering. Milestone 66 adds a bounded angular footprint for view-dependent environment reflection, seam-aware equirectangular gradients, and reuse of the same `Texture2D::sample_grad` nearest/trilinear/anisotropic path while preserving the historical base-level perfect-mirror default. Milestone 67 adds opt-in material-coupled glossy environment reflection by resolving the existing angular footprint once per draw as `max_footprint / shininess`. Milestone 68 exposes the existing reflection mip, anisotropy, fixed-footprint, and material-shininess policies through the headless renderer without creating a CLI-only sampling path. The subsequent headless display-output control slice also integrated explicit output transfer/exposure/Reinhard controls through the existing display-mapping path. The current exact `main` commit before Milestone 69 is `fef2127918d4a48ad3fb758e97349a186c4a4f7b`, and its Linux, macOS, and ASan/UBSan post-merge CI is green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. In the current lineage, milestone-60 and milestone-62 branch names exist outside the exact integrated `main` history, and an old `milestone-64-mtl-emissive-map` branch is only a stale branch point at the Milestone 63 commit rather than an implementation. Branch names must not be used as completion evidence; any future integration must pass the same PR/exact-head/post-merge gates as other milestones.

## Milestone 69 candidate — owned MTL `map_Ks` specular textures

This candidate closes the material data-plane gap between bounded uniform `Ks`/`Ns` and the already-integrated direct/environment specular consumers. It does not introduce another lighting path.

Acceptance surface:

- the bounded rich MTL parser accepts at most one `map_Ks <filename>` per material while the legacy strict `load_mtl` path continues to reject mapped-asset directives;
- `MaterialAssetDefinition`, compatibility material batches, and canonical `MaterialDraw` propagate an optional owned specular texture through the same sibling-path-safe image loader and shared `(path, transfer-function)` cache used by the other material texture roles;
- `map_Ks` is interpreted as linear RGB reflectance data; same-file linear roles such as `map_d` and `map_Ks` share decoded ownership, while transfer-distinct roles remain separate cache entries;
- `TextureBinding` carries the specular role as a trailing field and reuses the material draw's canonical UV channels, sampler, mip chain, and raster-derived `TextureGradients` rather than introducing specular-specific coordinates or filtering;
- direct triangle/mesh/range submission and shared model/prepared/list preflight require valid UV/sampler state whenever `map_Ks` is present and reject out-of-range specular texture data before framebuffer mutation;
- fixed shading resolves one per-fragment `specular_reflectance = MaterialState::specular * sampled_map_Ks_rgb` and passes that same resolved value to both Blinn-Phong direct-light specular and environment-reflection contribution, avoiding duplicate texture sampling and divergent material interpretation;
- missing `map_Ks` resolves exactly to the existing uniform `MaterialState::specular` path, preserving historical unmapped behavior;
- prepared submissions retain `map_Ks` ownership after source-object lifetime ends; heterogeneous prepared lists preserve whole-list fail-before-write behavior when a later specular-textured draw has invalid dynamic UV state;
- the stable logical model fingerprint includes `map_Ks` content only when that role is present, preserving historical map_Ks-free fingerprints; the model asset inspector exposes the specular texture role alongside diffuse/opacity/normal roles so changed fingerprints remain explainable;
- deterministic regressions cover file import, duplicate/legacy parser behavior, shared linear cache ownership, direct-light RGB modulation, environment-reflection modulation, prepared lifetime, invalid sampler/texel state, later-list invalid-UV rejection, fingerprint semantics, and inspection visibility;
- a construction run on implementation head `caaf2f28ddfaa81174ea5c93d9204bf05ec03fc6` completed Release build, focused specular/model/prepared/environment regressions, and the full Linux test suite successfully. Later inspection/status commits create a new final candidate and therefore still require the normal exact-head Linux/macOS/ASan+UBSan PR gates before integration;
- the milestone makes no roughness/metalness, Fresnel, microfacet/PBR, independent specular-UV, new image-format, GPU, performance, or image-quality claim.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 69

After exact-head and post-merge CI close M69, the next material-data promotion should be **owned emissive texture maps (`map_Ke`) feeding the existing linear-HDR emission path**. Uniform bounded `Ke` is already integrated, while emissive variation is still draw-uniform; this leaves one materially distinct texture-driven radiance source missing from the otherwise shared material texture architecture.

A coherent Milestone 70 should add one optional linear/HDR-capable emissive texture role through the rich MTL/material-asset path and shared ownership/cache, reuse the existing material UV/sampler/mip/anisotropic-gradient machinery, and resolve one per-fragment emissive radiance that combines the bounded uniform `Ke` control with the sampled map before the fragment program. Direct/model/prepared/list submission must retain fail-closed UV/sampler/resource validation and whole-list preflight semantics. The slice should prove file/programmatic equivalence, prepared lifetime, shared cache behavior, mip/anisotropy reuse, HDR PFM emission above one when explicitly sourced from a linear HDR texture, and map-free fingerprint/output compatibility. It should not create a bloom/postprocess system, physically based area-light emission, global illumination, new UV transforms, automatic exposure changes, or a GPU/performance claim. The stale `milestone-64-mtl-emissive-map` branch is not implementation evidence and must not be treated as an active competing surface unless its live head advances independently.
