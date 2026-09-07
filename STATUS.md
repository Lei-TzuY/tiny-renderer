# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 49

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity/A2C, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

Milestones 44–46 make shadow ownership and sampling composable: every bounded light record can own a typed shadow resource, Hard/3x3-PCF is explicit per binding, and directional records can own bounded ordered cascade sets selected deterministically from perspective-correct world position and camera-view depth. Milestone 47 promotes texture minification onto the same path with owned mip chains, explicit nearest-level/trilinear policy, and raster-derived perspective-correct UV gradients shared by diffuse, opacity, and normal texture roles. Milestone 48 makes source texture interpretation explicit: historical/default imports remain linear, while opted-in diffuse color textures are sRGB-decoded to linear floats before mip construction and data-texture roles remain linear. Milestone 49 makes the opposite boundary explicit: framebuffer storage and all rendering remain linear, while callers may opt into standard sRGB encoding only when resolved RGB becomes 8-bit export bytes.

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

## Milestone 49 — explicit output transfer semantics

Implemented acceptance surface:

- `OutputTransferFunction::{Linear,Srgb}` makes the 8-bit export transform explicit while the historical no-argument `rgb8()`, `fnv1a64()`, and `write_ppm()` paths remain the default linear clamp-and-quantize behavior;
- opt-in sRGB export applies the standard piecewise linear-to-sRGB equation only after resolved framebuffer RGB is available, so rasterization, lighting, blending, alpha handling, stencil/depth ownership, and multisample storage/resolve remain linear;
- explicit `Linear` export delegates to the legacy byte/hash/PPM paths and is regression-locked byte-identically, including the established deterministic sample-scene hash;
- 4x MSAA resolves linear sample colors before transfer encoding; a 4x target and 1x target with equal resolved linear RGB therefore produce identical sRGB export bytes;
- finite resolved channels are clamped to `[0,1]` only at the existing 8-bit boundary before standard sRGB encoding and rounding; unknown transfer state and non-finite sRGB export input reject deterministically;
- sRGB PPM export validates and encodes the complete payload before creating or truncating the destination file, so invalid resolved values fail closed without leaving a partial output;
- deterministic regressions lock low-segment/nonlinear transfer values, clamp/rounding, legacy-vs-explicit-Linear compatibility, separate linear/sRGB hashes, resolve-before-encode semantics, exact PPM payload bytes, invalid enum rejection, and non-finite fail-closed output behavior;
- the milestone adds no gamma-space framebuffer rewrite, tone mapper, HDR file format, ICC/display profile system, destination alpha, GPU path, or performance/colorimetric conformance claim beyond the documented standard sRGB transfer equation.

## Next frontier — Milestone 50

Promote the float framebuffer to **deterministic linear HDR/PFM export**. The renderer can already accumulate finite linear RGB values above `1.0`, but every current file/byte export clamps to the 8-bit boundary. The next slice should preserve resolved linear floating-point data instead of introducing a tone mapper first.

Acceptance should require:

- a bounded PFM-style RGB export writes the resolved linear framebuffer directly as finite 32-bit floating-point values, preserving negative and greater-than-one values rather than applying `OutputTransferFunction`, clamping, or tone mapping;
- the file representation uses one explicitly documented byte order and row order so exact payload bytes are deterministic across Linux and macOS rather than depending on host endianness or an ambiguous image-origin convention;
- all resolved pixels are validated before file creation/truncation, so NaN/Inf input fails closed without leaving a partial HDR file;
- 1x and 4x framebuffers with equal resolved linear colors produce identical float payloads, proving export occurs after the existing deterministic linear resolve;
- regressions lock the PFM header, scale/endian marker, row ordering, exact float payload bits for representative negative/zero/fractional/>1 values, and round-trip interpretation with a small test reader;
- the existing 8-bit linear/sRGB exports and hashes remain unchanged and independent from the new float export path;
- the slice does not add tone mapping, exposure controls, ICC/wide-gamut management, PQ/HLG or other HDR transfer functions, compression, GPU APIs, or performance/image-quality claims.
