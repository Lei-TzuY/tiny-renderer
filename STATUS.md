# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 51

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity/A2C, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

Milestones 44–46 make shadow ownership and sampling composable: every bounded light record can own a typed shadow resource, Hard/3x3-PCF is explicit per binding, and directional records can own bounded ordered cascade sets selected deterministically from perspective-correct world position and camera-view depth. Milestone 47 promotes texture minification onto the same path with owned mip chains, explicit nearest-level/trilinear policy, and raster-derived perspective-correct UV gradients shared by diffuse, opacity, and normal texture roles. Milestone 48 makes source texture interpretation explicit: historical/default imports remain linear, while opted-in diffuse color textures are sRGB-decoded to linear floats before mip construction and data-texture roles remain linear. Milestone 49 makes the opposite 8-bit boundary explicit: framebuffer storage and all rendering remain linear, while callers may opt into standard sRGB encoding only when resolved RGB becomes export bytes. Milestone 50 adds a data-preserving HDR output boundary with deterministic float RGB PFM, and Milestone 51 closes the loop by importing bounded RGB PFM back into the same linear `Texture2D`/mipmap/shared-dispatch domain.

The current main line also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, and bounded PPM/TGA/PFM texture import through the shared image dispatch path.

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

## Milestone 51 — bounded linear PFM texture import

Implemented acceptance surface:

- `load_pfm` / `load_pfm_file` accept bounded RGB `PF` images with positive non-zero dimensions and a strict 64 MiB decoded raster ceiling;
- the first interoperable endian slice accepts exactly unit-magnitude scale markers: `-1.0` means little-endian and `1.0` means big-endian 32-bit IEEE-754 RGB payloads; arbitrary scale magnitude is rejected rather than silently interpreted as exposure;
- PFM bottom-to-top file rows are converted into the renderer's established top-to-bottom texture texel order while every finite negative/fractional/>1 RGB float is preserved numerically;
- NaN/Inf samples, unsupported magic/scale, invalid dimensions, oversized rasters, truncated payloads, and trailing payload bytes reject deterministically before a usable texture is returned;
- every imported PFM constructs `Texture2D` with `TextureTransferFunction::Linear`; case-insensitive `.pfm` joins the same PPM/TGA extension dispatcher, while an explicit sRGB interpretation rejects instead of decoding or clamping HDR data;
- imported HDR texels immediately reuse the existing `Texture2D` mip generation, filtering, material image-dispatch, and cache ownership infrastructure; no parallel sampler, material, mipmap, or raster path is introduced;
- deterministic regressions lock M50 framebuffer→PFM→M51 texture round-trip values and row orientation, big-endian decoding, linear mip arithmetic over signed/high-range texels, shared-dispatch metadata, sRGB rejection, bounds, non-finite, truncation, and trailing-data failures;
- the milestone adds no grayscale PFM, arbitrary scale/exposure semantics, tone mapping, environment lighting, ICC/wide-gamut management, PQ/HLG transfer, compression, GPU API, or performance/image-quality claim.

## Next frontier — Milestone 52

Promote the now-complete linear HDR I/O loop into an **explicit bounded HDR display-mapping stage at the resolved output boundary**. M49 already separates output transfer encoding from the linear framebuffer, while M50/M51 preserve HDR data losslessly; M52 should add an opt-in display mapping without changing any rendering/storage defaults.

Acceptance should require:

- a first-class output mapping state with finite non-negative exposure and an explicit bounded tone-map operator, while all existing no-argument and transfer-only RGB8/hash/PPM exports remain byte/hash-identical by default;
- the first operator is a documented deterministic component-wise Reinhard mapping applied to non-negative exposed linear RGB, with negative display-bound values handled by one explicit rule rather than accidental clamp ordering;
- stage ordering is fixed as resolved linear framebuffer → explicit exposure → tone mapping → existing `OutputTransferFunction::{Linear,Srgb}` → existing 8-bit clamp/rounding; multisample resolve therefore always occurs before display mapping;
- invalid exposure/operator state and non-finite resolved input reject before destination file creation/truncation, preserving the established fail-closed output contract;
- `write_pfm()` remains a data-preserving linear HDR path and is never affected by display-mapping state, so callers can independently choose archival/interchange HDR output or display-referred 8-bit output;
- deterministic regressions lock default compatibility, exposure math, Reinhard reference values, negative/high-range handling, tone-map-before-sRGB ordering, 1x/4x resolve equivalence, hash/PPM bytes, fail-closed invalid state, and PFM independence;
- the slice does not claim photographic/filmic quality, ACES conformance, automatic exposure, histogram adaptation, local tone mapping, ICC/display calibration, PQ/HLG, GPU execution, or performance/image-quality parity.
