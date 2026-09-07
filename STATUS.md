# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 47

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity/A2C, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

Milestones 44–46 make shadow ownership and sampling composable: every bounded light record can own a typed shadow resource, Hard/3x3-PCF is explicit per binding, and directional records can own bounded ordered cascade sets selected deterministically from perspective-correct world position and camera-view depth. Milestone 47 promotes texture minification onto the same path with owned mip chains, explicit nearest-level/trilinear policy, and raster-derived perspective-correct UV gradients shared by diffuse, opacity, and normal texture roles.

The current main line also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, and bounded PPM/TGA texture import through the shared image dispatch path.

## Milestone 47 — deterministic mipmapped minification

Implemented acceptance surface:

- `Texture2D` owns a deterministic complete mip chain while default `MipFilterMode::Disabled` preserves the established level-zero nearest/bilinear sampling path;
- odd mip extents use deterministic ceil-half dimensions and each generated texel averages only the existing parent footprint with finite/overflow-safe storage validation;
- explicit nearest-level and trilinear policies are bounded and validated, including framebuffer-independent prepared-model rejection of unknown mip policy;
- rasterization derives perspective-correct screen-space UV gradients from the existing barycentric/interpolation terms and selects isotropic LOD from the maximum texel-space derivative footprint;
- diffuse, opacity, and tangent-space normal textures consume the same validated raster gradient footprint rather than maintaining separate coordinate/derivative paths;
- clipping, viewport/scissor, 1x/4x sampling, alpha test/A2C, fixed lighting/shadows, fragment programs, direct/model/prepared/list submission, and imported texture ownership remain on the established raster path;
- deterministic regressions cover mip contents including odd extents, explicit/gradient LOD, trilinear interpolation, legacy disabled behavior, raster-derived minification across diffuse/opacity/normal roles, and prepared invalid-policy rejection;
- the milestone makes no anisotropic, sRGB/colorimetric, shader-derivative, GPU, performance, or image-quality claim.

## Next frontier — Milestone 48

Promote texture assets to **explicit transfer-function interpretation with opt-in sRGB diffuse decoding and linear data-texture semantics**. Preserve the historical linear-float/default import path exactly. The first slice should decode opted-in sRGB color textures into linear values before mip generation/filtering/lighting, keep opacity and normal maps explicitly linear data, and make texture-cache identity include interpretation so one file used under different material roles cannot silently share incompatible decoded texels. This is input texture semantics only: no ICC/profile system, HDR transfer functions, output/display transform, spectral claim, GPU path, or performance claim belongs in the slice.
