# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 54

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity/A2C, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

Milestones 44–46 make shadow ownership and sampling composable: every bounded light record can own a typed shadow resource, Hard/3x3-PCF is explicit per binding, and directional records can own bounded ordered cascade sets selected deterministically from perspective-correct world position and camera-view depth. Milestone 47 promotes texture minification onto the same path with owned mip chains, explicit nearest-level/trilinear policy, and raster-derived perspective-correct UV gradients shared by diffuse, opacity, and normal texture roles. Milestone 48 makes source texture interpretation explicit: historical/default imports remain linear, while opted-in diffuse color textures are sRGB-decoded to linear floats before mip construction and data-texture roles remain linear. Milestone 49 makes the opposite 8-bit boundary explicit: framebuffer storage and all rendering remain linear, while callers may opt into standard sRGB encoding only when resolved RGB becomes export bytes. Milestone 50 adds a data-preserving HDR output boundary with deterministic float RGB PFM, Milestone 51 closes the loop by importing bounded RGB PFM back into the same linear `Texture2D`/mipmap/shared-dispatch domain, Milestone 52 adds opt-in bounded HDR display mapping after resolve without changing linear rendering or archival HDR behavior, Milestone 53 makes imported/programmatic linear HDR textures executable as a deterministic equirectangular camera background while preserving normal geometry depth/stencil ownership, and Milestone 54 connects that background to the existing auto-fit headless preview/CLI transaction with real 1x/4x HDR-file execution.

The current main line also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatch path, and bounded headless OBJ preview/render tooling.

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
- the binary raster boundary accepts LF and CRLF scale-line endings exactly, without treating binary payload bytes that happen to equal ASCII whitespace as additional header separators;
- NaN/Inf samples, unsupported magic/scale, invalid dimensions, oversized rasters, truncated payloads, and trailing payload bytes reject deterministically before a usable texture is returned;
- every imported PFM constructs `Texture2D` with `TextureTransferFunction::Linear`; case-insensitive `.pfm` joins the same PPM/TGA extension dispatcher, while an explicit sRGB interpretation rejects instead of decoding or clamping HDR data;
- imported HDR texels immediately reuse the existing `Texture2D` mip generation, filtering, material image-dispatch, and cache ownership infrastructure; no parallel sampler, material, mipmap, or raster path is introduced;
- deterministic regressions lock M50 framebuffer→PFM→M51 texture round-trip values and row orientation, big-endian decoding, LF/CRLF raster boundaries, binary-leading whitespace bytes, linear mip arithmetic over signed/high-range texels, shared-dispatch metadata, sRGB rejection, bounds, non-finite, truncation, and trailing-data failures;
- the milestone adds no grayscale PFM, arbitrary scale/exposure semantics, tone mapping, environment lighting, ICC/wide-gamut management, PQ/HLG transfer, compression, GPU API, or performance/image-quality claim.

## Milestone 52 — explicit bounded HDR display mapping

Implemented acceptance surface:

- `DisplayMappingState` adds opt-in finite non-negative linear exposure and explicit `ToneMapOperator::Reinhard`; the historical no-argument and transfer-only RGB8/hash/PPM overloads remain separate and unchanged;
- display-bound negative resolved-linear components are explicitly mapped to zero before exposure, then each non-negative exposed component uses deterministic component-wise Reinhard `x / (1 + x)`;
- exposure multiplication is evaluated in double precision before bounded tone mapping so every finite float resolved value and finite float exposure can reach the mapper without float-product overflow;
- stage ordering is fixed as multisample resolve → resolved linear RGB → negative-to-zero display rule → exposure → Reinhard → existing `OutputTransferFunction::{Linear,Srgb}` → 8-bit clamp/rounding;
- invalid exposure/operator/transfer state and non-finite resolved input reject before display-mapped PPM creation or truncation, preserving fail-closed output behavior;
- mapped RGB8/hash/PPM are read-only views over resolved storage: invoking them does not mutate framebuffer color, legacy exports, or subsequent rendering state;
- `write_pfm()` remains independent and data-preserving; the same framebuffer produces byte-identical PFM before and after any display-mapped export;
- deterministic regressions lock negative/high-range reference values, exposure/Reinhard arithmetic, tone-map-before-sRGB ordering, mapped hash bytes, 1x/4x resolve equivalence, fail-closed destination preservation, legacy API stability, mapped PPM payloads, and PFM independence;
- the milestone makes no photographic/filmic quality, ACES conformance, automatic exposure, histogram adaptation, local tone mapping, ICC/display calibration, PQ/HLG, GPU execution, or performance/image-quality parity claim.

## Milestone 53 — bounded equirectangular HDR environment background

Implemented acceptance surface:

- `EnvironmentBackgroundState` borrows one existing linear `Texture2D` and carries an explicitly validated sampler, finite bounded intensity, and finite bounded yaw without creating a second decoder, cache, mip chain, or framebuffer path;
- `PerspectiveCameraState` reconstructs world-space background rays from explicit finite eye/target/up/FOV/aspect state while keeping geometry submission on the existing model/view/projection path;
- equirectangular mapping has one documented convention: +Y is the north pole, -Y the south, -Z maps to `u=0.5`, +X increases longitude, +Z owns the canonical `u=0` seam, exact poles use deterministic longitude zero, and yaw rotates around +Y;
- the exact +Z seam is canonicalized before `atan2` so signed-zero/libm differences cannot move seam ownership between `u=0` and `u=1` on supported platforms;
- 1x and 4x background rays use the renderer's established center/quarter-offset sample positions and sample the same linear `Texture2D` path; mip filtering is deliberately disabled until a later capability owns ray differentials/LOD semantics;
- every ray, texture sample, and intensity-scaled HDR radiance value is computed and validated into temporary storage before the first framebuffer mutation, including finite/non-negative radiance and float-range overflow checks;
- the commit phase reuses `Framebuffer::test_and_write_sample` with replacement RGB, always-pass depth comparison, depth writes disabled, and stencil disabled, so the background preserves depth/stencil and normal geometry submitted afterward occludes it through the unchanged raster/depth path;
- deterministic regressions lock cardinal directions/seam/poles/yaw, per-sample 1x/4x reconstruction and resolve, PFM-imported/programmatic HDR equivalence, depth/stencil preservation, background→geometry occlusion→M52 display mapping while retaining linear HDR storage, and fail-closed invalid/overflow state;
- the milestone adds no diffuse/specular image-based lighting, importance sampling, prefiltered cubemaps, BRDF LUTs, automatic exposure, ray-differential mip selection, GPU path, scene graph, or performance/image-quality claim.

## Milestone 54 — environment-enabled headless rendering

Implemented acceptance surface:

- `OfflineRenderSettings` carries one trailing optional borrowed `EnvironmentBackgroundState`; the default/absent environment path remains the historical clear-color preview behavior;
- the preview constructs one fixed eye/target/up/FOV/aspect camera definition and reuses those exact values for both M53 sample-ray reconstruction and the geometry view/projection matrices before the existing auto-fit model draw;
- `tiny_renderer_render` preserves the historical `INPUT.obj OUTPUT.(ppm|pfm) [WIDTH HEIGHT [SAMPLES]]` positional form and adds bounded `--environment`, `--environment-intensity`, and `--environment-yaw` options with deterministic duplicate/unknown/malformed/orphan-option rejection;
- environment images are owned only for the CLI render transaction and load through the existing linear `load_texture_image_file` PPM/TGA/PFM dispatcher; no decoder, mip chain, cache, or environment texture ownership subsystem is duplicated;
- output extension, CLI structure, environment loading/state, model loading, and the complete headless render finish before any PPM/PFM write begins, so invalid environment requests cannot produce a partially rendered output artifact;
- PPM output remains M52 default Reinhard display mapping followed by M49 sRGB encoding, while PFM remains M50 resolved-linear HDR serialization without display mapping;
- a real 2x1 little-endian PFM fixture with values greater than one drives real CLI OBJ+HDR-environment renders; 1x PPM and 4x PFM are each rendered twice and compared byte-for-byte for deterministic execution;
- focused library regressions lock no-environment compatibility, exact preview-camera environment rays, uncovered per-sample HDR radiance, unchanged geometry depth/stencil ownership, 1x/4x determinism, and invalid environment rejection;
- the milestone adds no image-based lighting, artistic camera/environment fitting, new image formats, automatic exposure, scene graph, GUI renderer, GPU path, or performance/image-quality claim.

## Next frontier — Milestone 55

Promote the environment path from forced base-level sampling into **deterministic camera-ray-footprint mip LOD selection**. M47 already owns mip chains and trilinear sampling for material textures, but M53/M54 intentionally disable environment mip filtering because no ray-footprint/LOD contract exists; high-resolution equirectangular backgrounds therefore cannot reuse the renderer's existing minification architecture safely.

Acceptance should require:

- an explicit environment mip policy keeps the current base-level behavior as the default/compatibility mode and adds an opt-in ray-footprint mode without creating a second texture sampler;
- each 1x/4x environment sample derives a finite screen-space ray footprint from the same `PerspectiveCameraState` and established sample layout used by M53/M54, then converts neighboring directions into seam-aware equirectangular UV derivatives;
- longitude derivatives use the shortest wrapped `u` distance across the `u=0/1` seam so the canonical +Z seam cannot spuriously request the coarsest mip;
- the resulting bounded LOD uses the existing `Texture2D` nearest-level/trilinear mip semantics from M47, with deterministic clamping to available levels and no anisotropic or EWA quality claim;
- preflight computes/validates all directions, derivatives, LODs, and sampled/scaled radiance before framebuffer mutation, preserving the M53 fail-closed transaction;
- regressions lock base-level byte/hash compatibility, analytic footprint/LOD cases, seam and pole behavior, 1x/4x determinism, imported PFM mip usage, headless CLI propagation, and depth/stencil preservation;
- the slice does not add diffuse/specular image-based lighting, importance sampling, prefiltered reflection probes, BRDF LUTs, anisotropic filtering, automatic exposure, GPU execution, or performance/image-quality parity claims.
