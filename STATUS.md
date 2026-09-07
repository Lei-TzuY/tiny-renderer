# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 50

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity/A2C, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

Milestones 44–46 make shadow ownership and sampling composable: every bounded light record can own a typed shadow resource, Hard/3x3-PCF is explicit per binding, and directional records can own bounded ordered cascade sets selected deterministically from perspective-correct world position and camera-view depth. Milestone 47 promotes texture minification onto the same path with owned mip chains, explicit nearest-level/trilinear policy, and raster-derived perspective-correct UV gradients shared by diffuse, opacity, and normal texture roles. Milestone 48 makes source texture interpretation explicit: historical/default imports remain linear, while opted-in diffuse color textures are sRGB-decoded to linear floats before mip construction and data-texture roles remain linear. Milestone 49 makes the opposite 8-bit boundary explicit: framebuffer storage and all rendering remain linear, while callers may opt into standard sRGB encoding only when resolved RGB becomes export bytes. Milestone 50 adds a separate data-preserving HDR boundary: resolved linear framebuffer RGB can be exported directly as deterministic 32-bit-float RGB PFM without transfer encoding, clamping, exposure, or tone mapping.

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

## Milestone 50 — deterministic linear HDR/PFM export

Implemented acceptance surface:

- `Framebuffer::write_pfm(path)` exports the resolved linear framebuffer directly as RGB PFM (`PF`) 32-bit floating-point samples without using `OutputTransferFunction`, clamping, exposure, or tone mapping;
- the canonical file representation is explicit and cross-platform deterministic: header `PF`, negative scale marker `-1.0` declaring little-endian data, bottom-to-top row order, left-to-right pixels, and RGB channel order;
- every channel is serialized from its exact IEEE-754 32-bit float bit pattern into explicit little-endian bytes rather than writing host-native float memory;
- finite negative, zero, fractional, one, and greater-than-one resolved values are preserved bit-for-bit at the export boundary;
- the complete resolved framebuffer is validated for finite RGB before the destination file is created or truncated, so NaN/Inf input fails closed and preserves an existing destination;
- 4x MSAA reuses the existing deterministic linear resolve: a 4x target and 1x target with equal resolved signed/high-range RGB produce byte-identical PFM output;
- deterministic regressions lock the exact header, endian marker, row order, representative IEEE-754 payload bits, a small little-endian round-trip reader, fail-closed NaN/Inf behavior, and independence from all existing linear/sRGB 8-bit bytes and hashes;
- the milestone adds no tone mapping, exposure control, ICC/wide-gamut management, PQ/HLG or other HDR transfer function, compression, HDR texture import, GPU path, or performance/image-quality claim.

## Next frontier — Milestone 51

Promote the new HDR boundary into a **bounded linear PFM texture import + shared image-dispatch path**. The renderer's existing `Texture2D` linear source domain already accepts finite negative and greater-than-one values and builds mip levels in linear float, so M51 can add HDR round-trip/interoperability without changing the sampler or raster core.

Acceptance should require:

- a bounded RGB PFM loader accepts the canonical `PF` 32-bit-float layout, positive non-zero dimensions, a supported explicit endian marker, and a strictly bounded raster payload with deterministic malformed/truncated/trailing-data diagnostics;
- imported rows are converted from PFM bottom-to-top file order into the renderer's existing top-to-bottom texture texel order, while every RGB float remains finite and numerically unchanged;
- the loader explicitly produces `TextureTransferFunction::Linear` data and never applies sRGB decoding, normalization, clamping, exposure, or tone mapping; attempts to route a PFM through shared image dispatch with an incompatible sRGB interpretation reject rather than silently reinterpret HDR data;
- `.pfm` joins the existing PPM/TGA extension dispatcher without creating a second texture cache, sampler, material, or mipmap path, so diffuse/opacity/normal ownership continues through existing `Texture2D` and model-asset infrastructure;
- a deterministic framebuffer → M50 PFM → M51 texture round trip preserves representative negative/fractional/>1 texels and verifies mip generation remains the existing linear arithmetic over HDR values;
- regressions lock byte order, row order, exact float payload interpretation, safety bounds, non-finite rejection, truncated/trailing payload rejection, shared-dispatch behavior, and material/cache integration where applicable;
- the slice does not add grayscale PFM, arbitrary HDR transfer functions, tone mapping, exposure controls, environment lighting, compression, GPU APIs, or performance/image-quality claims.
