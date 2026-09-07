# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 48

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity/A2C, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

Milestones 44–46 make shadow ownership and sampling composable: every bounded light record can own a typed shadow resource, Hard/3x3-PCF is explicit per binding, and directional records can own bounded ordered cascade sets selected deterministically from perspective-correct world position and camera-view depth. Milestone 47 promotes texture minification onto the same path with owned mip chains, explicit nearest-level/trilinear policy, and raster-derived perspective-correct UV gradients shared by diffuse, opacity, and normal texture roles. Milestone 48 makes source texture interpretation explicit: historical/default imports remain linear, while opted-in diffuse color textures are sRGB-decoded to linear floats before mip construction and data-texture roles remain linear.

The current main line also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, and bounded PPM/TGA texture import through the shared image dispatch path.

## Milestone 48 — explicit texture transfer semantics

Implemented acceptance surface:

- `TextureTransferFunction::{Linear,Srgb}` explicitly records how source RGB samples enter the renderer's linear-float texture domain, while `Linear` remains the default and preserves historical constructor/import values;
- opted-in sRGB source texels use the standard bounded piecewise sRGB decode and are converted to linear RGB before the base level enters mip generation, so all later mip filtering, raster sampling, lighting, opacity-independent color work, and prepared/model submission consume linear texels;
- PPM, TGA, and the shared image-dispatch path propagate transfer interpretation without creating a second decoder or sampler path;
- `ModelAssetLoadOptions::diffuse_transfer` is intentionally the only caller-selectable material-role interpretation in this slice; opacity and normal maps are always imported as explicit linear data textures;
- decoded texture-cache identity includes both normalized path and transfer interpretation, preserving legacy same-file all-linear deduplication while preventing an sRGB diffuse role from aliasing a differently decoded linear data role; opacity and normal roles using the same file still share one linear resource;
- unknown transfer enums and non-normalized sRGB source texels reject deterministically rather than silently falling back to linear behavior;
- deterministic regressions lock known sRGB values, decode-before-mip averaging, default PPM/TGA byte semantics, shared image dispatch, and file-driven cross-role ownership under both default-linear and sRGB-diffuse import;
- the milestone adds no ICC/profile system, HDR transfer functions, output/display transform, spectral/colorimetric claim beyond the standard sRGB transfer equation, GPU path, anisotropic filtering, or performance claim.

## Next frontier — Milestone 49

Promote the now-linear internal color pipeline to **explicit output transfer semantics at the export boundary**. Keep framebuffer sample storage, blending, resolve, lighting, and all raster ownership linear; preserve the historical `rgb8()` / PPM linear clamp-and-quantize result as the default; add an opt-in sRGB encode mode only when converting resolved linear framebuffer RGB to 8-bit/export bytes. Acceptance should lock the standard piecewise linear-to-sRGB equation, clamp/finite behavior at the existing byte boundary, deterministic 1x/4x equivalence, unchanged default hashes, and explicit rejection of unknown output-transfer state. This remains a bounded export transform, not an ICC/display-profile system, tone mapper, HDR pipeline, gamma-correct blending rewrite, GPU path, or performance claim.
