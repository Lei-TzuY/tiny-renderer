# tiny-renderer

`tiny-renderer` is a correctness-first educational CPU software rasterizer written in modern C++20 without OpenGL, Vulkan, Direct3D, SDL rendering APIs, or an existing rasterization library. Milestone 1 established the end-to-end triangle pipeline, Milestone 2 added indexed meshes, Milestone 3 generalized vertex varyings, Milestone 4 hardened fixed-point coverage, Milestone 5 added explicit interpolation semantics, Milestone 6 added deterministic in-memory texture sampling, Milestone 7 added bounded OBJ position/UV import, Milestone 8 added bounded binary PPM texture-file import, Milestone 9 added inverse-transpose normal handling plus deterministic world-space directional Lambert lighting, Milestone 10 added bounded OBJ normal import, Milestone 11 added bounded runtime material albedo, Milestone 12 added strict diffuse MTL import plus ordered OBJ material draw batches, Milestone 13 added an explicit base-color source contract, Milestone 14 added bounded owned MTL `map_Kd` diffuse textures, Milestone 15 consolidated rich assets into one canonical mesh plus ordered material draw ranges, and Milestone 16 executes those ranges directly without transient mesh-container materialization.

## Rendering pipeline

The renderer follows this path for every submitted triangle:

1. **Object space** — user-supplied or imported positions plus a fixed-capacity scalar varying payload. The convenience RGB vertex form occupies varying channels 0–2 and defaults them to smooth interpolation. Bounded OBJ `v/vt` import emits UV in channels 0–1; bounded `v/vt/vn` import emits `[u, v, nx, ny, nz]` in channels 0–4.
2. **Model / view / projection** — home-grown `Vec*` and `Mat4` types transform each position into homogeneous clip space. When directional lighting is enabled, explicitly bound object-space normal channels are transformed into world space by the model matrix's inverse-transpose 3×3 normal matrix before clipping.
3. **Homogeneous clipping** — Sutherland-Hodgman clipping against all six canonical clip planes (`-w <= x,y,z <= w`) prevents invalid/off-screen geometry from reaching rasterization. Generated vertices preserve the interpolation contract: smooth channels use the homogeneous edge parameter, noperspective channels use the corresponding projected edge parameter, and flat channels retain the submitted primitive's provoking value. Transformed normal channels remain ordinary varyings here, so normal clipping shares the same deterministic path.
4. **Perspective divide** — `(x, y, z) / w` produces normalized device coordinates.
5. **Viewport transform** — NDC is mapped to pixel coordinates with a top-left image origin.
6. **Fixed-point triangle setup/coverage** — screen-space vertices are quantized to a 1/256-pixel grid; signed 64-bit integer edge equations plus an exact top-left rule decide pixel-center ownership. Quantized zero-area triangles are discarded.
7. **Qualified barycentric interpolation** — accepted samples use the original floating screen positions. `smooth` channels use perspective-correct `varying / w` and `1 / w`, `noperspective` channels use screen-linear barycentric interpolation, and `flat` channels use the first submitted vertex as the provoking vertex.
8. **Base-color source selection** — `BaseColorSource::Auto` preserves the legacy contract by choosing a bound texture when present and RGB varyings otherwise. Explicit `VaryingColor` and `Texture` modes validate only their relevant bindings, while `ConstantWhite` contributes `(1,1,1)` without consuming RGB or UV channels. Conflicting or invalid source/binding state fails before framebuffer mutation.
9. **Texture sampling** — when the resolved source is `Texture`, normalized UVs use explicit `Clamp` or `Repeat` addressing and `Nearest` or texel-center `Bilinear` filtering. Rich material assets can own a bounded P6 texture imported from MTL `map_Kd`; the resulting `Texture2D` still enters this same sampler and raster path.
10. **Material albedo** — a validated runtime `MaterialState` multiplies the selected base color component-wise. The default white albedo `(1,1,1)` is byte-compatible with earlier milestones. Material-aware OBJ loading maps bounded MTL `Kd` values directly onto this same state; with `ConstantWhite`, `Kd` itself becomes the visible diffuse base color, while mapped materials multiply sampled `map_Kd` texels by `Kd`.
11. **Directional Lambert lighting** — when enabled, the interpolated world-space normal is renormalized per fragment. The material-modulated base color is multiplied by `ambient + diffuse * max(dot(normal, direction_to_light), 0)`, with validated bounded coefficients. Lighting-disabled rendering retains the same fragment path without the Lambert factor.
12. **Depth testing** — NDC depth is mapped to `[0, 1]` and compared against a floating-point z-buffer.
13. **Framebuffer / image output** — RGB float pixels plus depth are stored in CPU memory and emitted as binary PPM (`P6`) without a GUI.

Indexed meshes are deliberately a submission/assembly layer above this pipeline: triangle indices reference reusable vertices, topology and varying contracts are validated before drawing, and each assembled face then enters the same clipping/rasterization/depth path as an explicitly submitted triangle. `DrawRange` provides a bounded contiguous-triangle submission contract above the same triangle core. The rich asset layer stores one canonical `Mesh` in `ModelAsset` plus ordered `MaterialDraw` records; direct range execution now preflights the requested range against that canonical mesh and submits selected faces one at a time without constructing a temporary `Mesh` or selected triangle vector. Asset import remains above the rendering core, so the rasterizer remains unaware of OBJ, MTL, and PPM file formats.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/tiny_renderer_sample milestone1.ppm
```

The original sample scene still renders three colored triangles through a perspective camera. Two overlap at different depths to make z-buffer visibility obvious, and one crosses the left clip plane to exercise clipping. Texture sampling, file-driven OBJ/PPM import, normal import, lighting, runtime material albedo, ordered OBJ/MTL materials, Kd-only rendering, owned `map_Kd` textures, canonical model draw ranges, and direct range execution are exercised by dedicated tests without changing this baseline sample or its deterministic framebuffer contract.

## Architecture

- `include/tiny_renderer/math.hpp` — vectors, `Mat3`/`Mat4`, camera/view and perspective projection math, plus inverse-transpose model normal-matrix construction.
- `include/tiny_renderer/mesh.hpp` — vertices, fixed-capacity `VaryingPack`, per-channel `Interpolation` qualifiers, indexed triangle topology, `DrawRange`, and the reusable mesh data model.
- `include/tiny_renderer/material.hpp` — bounded runtime material state shared by the rasterizer and asset loaders.
- `include/tiny_renderer/mtl_loader.hpp`, `src/mtl_loader.cpp` — strict legacy diffuse MTL `newmtl`/`Kd` loading plus a separate richer asset parser that retains a bounded optional sibling `map_Kd` filename.
- `include/tiny_renderer/obj_loader.hpp`, `src/obj_loader.cpp` — bounded OBJ geometry parsing, source-line diagnostics, deterministic position/UV(/normal) normalization, legacy `Mesh` loading, and legacy ordered material batching.
- `src/material_asset_loader.cpp` — richer OBJ/MTL enrichment that creates one canonical `ModelAsset`, resolves optional `map_Kd` through the bounded PPM loader, deduplicates texture loads by normalized path, emits ordered `MaterialDraw` ranges, and derives the older `MaterialAssetBatch` API as a compatibility adapter.
- `include/tiny_renderer/ppm_loader.hpp`, `src/ppm_loader.cpp` — bounded binary PPM stream/file decoding with strict header/raster validation into `Texture2D`.
- `include/tiny_renderer/texture.hpp`, `src/texture.cpp` — validated in-memory RGB textures plus deterministic normalized-coordinate addressing and nearest/bilinear sampling.
- `include/tiny_renderer/framebuffer.hpp`, `src/framebuffer.cpp` — RGB/depth storage, depth writes, deterministic byte conversion and PPM output.
- `include/tiny_renderer/rasterizer.hpp`, `src/rasterizer.cpp` — explicit base-color source state, color/texture/normal bindings, bounded runtime material state, full-mesh submission, qualifier-aware clip-space interpolation, perspective divide, fixed-point subpixel coverage, qualified raster interpolation, source selection, material modulation, directional Lambert modulation, and depth testing.
- `src/rasterizer_range.cpp` — direct bounded-range preflight and selected-face submission. It preserves fail-closed range/index/state/normal-transform behavior while avoiding the Milestone 15 transient mesh-container copy; accepted faces still enter the existing `draw_triangle` core.
- `src/main.cpp` — deterministic end-to-end baseline sample scene.
- `tests/test_main.cpp` — dependency-free mathematical, rasterization, mesh, varying, and integration correctness tests.
- `tests/test_fixed_point.cpp` — fixed-point ownership, quantization-stability, and subpixel-degeneracy regressions.
- `tests/test_interpolation.cpp` — analytic smooth/noperspective/flat semantics, clipping behavior, provoking-vertex preservation, and fail-closed qualifier-layout regressions.
- `tests/test_texture.cpp` — sampler addressing/filtering, perspective-correct UV, clipping continuity, and fail-closed texture-binding regressions.
- `tests/test_obj_loader.cpp`, `tests/fixtures/textured_quad.obj`, `tests/fixtures/lit_textured_quad.obj` — OBJ pair/triple index normalization, rejection, diagnostics, legacy textured equivalence, and file-driven textured-Lambert normal-import equivalence.
- `tests/test_ppm_loader.cpp`, `tests/fixtures/checker.ppm` — P6 binary decoding, malformed/truncated/overflow rejection, and file-driven OBJ + PPM render equivalence.
- `tests/test_lighting.cpp` — inverse-transpose normal correctness, non-uniform-transform lighting, texture modulation, lit clipping equivalence, and fail-closed lighting-contract regressions.
- `tests/test_material.cpp` — default-material/source byte stability, explicit varying/texture compatibility, constant-white material/Lambert composition, clipping equivalence, and fail-closed material/source validation.
- `tests/test_material_import.cpp` plus material fixtures — strict/rich MTL parsing, ordered compatibility batches, Kd-only stability, owned `map_Kd` lifetime/deduplication, metadata/path rejection, and file-driven mixed texture/material render equivalence.
- `tests/test_model_asset.cpp` — bounded draw-range fail-closed behavior, selected-only index semantics, canonical model range coverage/order, shared texture lifetime, and byte/hash equivalence against Milestone 14 compatibility batches.
- `tests/test_direct_range.cpp` — direct range execution under non-uniform lit transforms, singular normal-matrix fail-closed behavior, and empty-range state validation.
- `.github/workflows/ci.yml` — Linux/macOS build/test plus Linux ASan/UBSan coverage.

## Implemented

### Milestone 1 — CPU triangle pipeline

- C++20 CPU-only rasterization
- vector/matrix math, model/view/projection transforms
- OpenGL-style homogeneous clip volume and six-plane clipping
- perspective projection/divide and viewport transform
- deterministic pixel-center triangle coverage with a top-left edge rule
- winding normalization and degenerate rejection
- barycentric coordinates and perspective-correct interpolation
- floating-point z-buffer visibility
- bounded framebuffer writes and PPM image output
- deterministic sample scene and framebuffer hashing
- unit/integration tests and sanitizer CI

### Milestone 2 — indexed mesh assembly

- reusable vertex arrays plus `uint32_t` indexed triangle lists
- `draw_mesh` entry points for precomposed MVP or model/view/projection transforms
- fail-closed index preflight: malformed topology is rejected before any framebuffer mutation
- indexed rendering proven byte-equivalent to the same faces submitted as explicit triangles
- shared-edge regression proving two indexed triangles form a crack-free quad under the top-left coverage rule
- analytic regression proving RGB interpolation follows the perspective-correct `attribute/w` and `1/w` equation rather than affine screen-space interpolation

### Milestone 3 — generalized varying pipeline

- fixed-capacity payload of up to eight scalar varyings per vertex, with the existing RGB constructor preserved as a convenience path
- channel-agnostic interpolation at generated homogeneous clip vertices
- channel-agnostic perspective-correct raster interpolation using `varying/w` and `1/w`
- configurable RGB binding to any three active varying channels without introducing programmable fragment shaders
- fail-closed validation for mismatched varying counts and out-of-range color bindings before framebuffer mutation
- regression proof that non-default high varying channels survive both clipping and perspective correction
- existing sample output remains byte-stable with framebuffer FNV-1a64 `0x3c053f9c4b77e41f`

### Milestone 4 — fixed-point subpixel coverage

- 8-bit subpixel quantization (`1/256` pixel) for triangle setup and pixel-center coverage
- signed 64-bit edge equations with an explicit coordinate safety bound that prevents integer overflow rather than relying on undefined behavior
- exact integer top-left edge ownership with no floating epsilon in the coverage decision
- floating barycentric/depth/varying interpolation retained after coverage so the phase changes ownership semantics rather than shading precision
- draw-order regression proving differently colored triangles sharing an exact diagonal have one deterministic owner per edge sample
- regression proving subpixel perturbations that quantize to identical coordinates have identical coverage
- quantized zero-area triangles are deterministically rejected

### Milestone 5 — interpolation qualifiers

- each active varying channel carries an explicit `Smooth`, `NoPerspective`, or `Flat` interpolation mode
- legacy RGB and initializer-list payloads remain smooth by default, preserving existing rendering behavior
- smooth channels retain perspective-correct interpolation through `varying / w` and `1 / w`
- noperspective channels interpolate linearly in post-divide screen space, including clip-generated vertices via projected edge parameters
- flat channels use the first submitted triangle vertex as a deterministic provoking vertex; its value is propagated before clipping so clipping, fan triangulation, and winding normalization cannot change it
- triangles and meshes reject qualifier-layout mismatches before framebuffer mutation
- analytic regression exercises smooth, noperspective, and flat channels simultaneously in one projected primitive
- clipping regressions prove noperspective equivalence and flat provoking-value preservation when the provoking vertex itself lies outside the clip volume

### Milestone 6 — in-memory texture sampling

- validated `Texture2D` stores finite RGB texels with explicit dimensions and deterministic indexing
- normalized UV sampling supports `Clamp` and `Repeat` independently on U/V
- filtering supports nearest and bilinear interpolation with texel-center sampling; repeat-mode bilinear filtering crosses wrap seams correctly
- optional `TextureBinding` selects the texture plus U/V varying channels and sampler state without adding a second raster path
- bound UVs inherit the existing varying interpolation semantics, so smooth UVs are perspective-correct automatically
- texture UV bindings and input UV values are preflight-validated before framebuffer writes
- sampler regressions cover all four address/filter behaviors used by the milestone
- integration regression deliberately chooses a probe where perspective-correct and affine UVs select different texels and proves the perspective-correct result wins
- clipping regression proves a textured clipped primitive is framebuffer-equivalent to the same explicitly clipped geometry
- existing untextured sample path remains the default when no texture is bound

### Milestone 7 — bounded OBJ position/UV import

- `load_obj(std::istream&)` and `load_obj_file(path)` parse OBJ data without coupling asset I/O to rasterization
- the accepted geometry subset is intentionally explicit: `v x y z`, `vt u v`, and exactly-three-corner `f v/vt v/vt v/vt` records
- comments and non-geometric `o`, `g`, `s`, `usemtl`, and `mtllib` metadata are tolerated by the legacy geometry-only loader
- only positive absolute 1-based indices are accepted; zero, relative indices, missing UVs, out-of-range references, malformed finite numbers, quads, ngons, and other unsupported directives fail closed with `ObjParseError`
- parse errors preserve the failing source line number for deterministic diagnostics
- independent OBJ position/UV references are normalized into the renderer's unified vertex representation using `(position index, UV index)` as the identity; the same pair reuses a vertex while one position paired with two UVs is split
- unified vertices and triangle indices are emitted in deterministic first-seen face/corner order
- imported UVs occupy smooth varying channels 0 and 1 exactly as stored in the OBJ; the importer performs no implicit V flip
- an in-repo fixture deliberately uses independent indices and a UV seam, then proves the imported textured mesh renders byte-identically to an equivalent programmatic `Mesh`

### Milestone 8 — bounded binary PPM texture import

- `load_ppm(std::istream&)` and `load_ppm_file(path)` decode texture files without coupling image I/O to the sampler or rasterizer
- the accepted subset is binary P6 with positive dimensions and `maxval` exactly 255; ASCII P3 and wider sample depths are deliberately rejected
- header token parsing accepts ASCII whitespace and `#` comments before tokens, then requires exactly one ASCII whitespace byte between maxval and the binary raster so payload bytes are never heuristically skipped
- RGB bytes map directly to float texels by division by 255 with row order preserved exactly as stored
- width/height, pixel-count, and RGB-byte-count arithmetic are checked before allocation; a 64 MiB raster safety bound prevents unbounded file-driven allocation in this educational decoder
- truncated rasters and any trailing bytes are rejected, making the accepted file representation deterministic rather than silently accepting concatenated data
- memory-stream regressions exercise arbitrary binary values including `0x00` and `0xff`, plus malformed magic, dimensions, maxval, overflow, truncation, and trailing-data cases
- the in-repo printable-byte P6 fixture combines with the Milestone 7 OBJ fixture to prove a fully file-driven textured render is byte-identical to the equivalent programmatic mesh and texels

### Milestone 9 — inverse-transpose normals and directional Lambert lighting

- reusable `Mat3` math derives the world-space normal transform as the inverse-transpose of the model transform's upper-left 3×3
- singular or non-finite normal transforms are rejected before framebuffer mutation rather than falling back to an incorrect model-times-normal approximation
- opt-in `NormalBinding` selects three existing varying channels; all three channels must exist and share one interpolation qualifier
- vertex normal inputs must be finite and non-zero; transformed normals remain linear varyings through clipping and raster interpolation, then are renormalized per fragment
- `DirectionalLight` uses a validated world-space direction toward the light plus non-negative ambient/diffuse coefficients whose sum is bounded to at most one
- Lambert intensity is `ambient + diffuse * max(dot(normal, direction_to_light), 0)` and modulates the existing varying-color or sampled-texture fragment result
- interpolated near-zero normals fall back deterministically to ambient-only shading instead of throwing from inside rasterization after partial framebuffer mutation
- lighting requires the model/view/projection overload because a precomposed MVP cannot recover the model normal transform; MVP-only lighting calls fail closed
- mesh lighting validates and transforms all vertex normals before the first triangle write, preserving fail-closed behavior across multi-triangle submissions
- regressions prove inverse-transpose correctness under non-uniform scaling, transformed tangent orthogonality, textured Lambert composition, lit clipping equivalence, and fail-closed invalid binding/MVP/singular-model cases
- lighting is disabled by default, so the earlier baseline sample and unlit rendering contract remain unchanged

### Milestone 10 — bounded OBJ normal import

- accepts finite, non-zero `vn x y z` records while preserving the existing `v` and two-component `vt` subset
- accepts exactly-three-corner `v/vt/vn` faces in addition to the legacy triangle `v/vt` form
- every corner of a face must use one index layout, and all faces in one loaded mesh must consistently use either `v/vt` or `v/vt/vn`; mixed schemas fail closed before a `Mesh` is returned
- only positive absolute 1-based position, texture-coordinate, and normal indices are accepted; `v//vn`, zero/relative indices, missing attributes, and out-of-range references remain rejected
- normal-bearing unified vertices use `(position index, UV index, normal index)` identity, preserving deterministic first-seen reuse/splitting across UV and normal seams
- normal-bearing imports emit five smooth varying channels `[u, v, nx, ny, nz]`, directly compatible with `TextureBinding{...,0,1,...}` and `NormalBinding{2,3,4}`
- legacy `v/vt` imports remain two-channel UV payloads and retain Milestone 7 byte/hash-equivalent rendering behavior
- regressions cover triple-index normalization, malformed/zero/out-of-range/relative normals, missing UVs, mixed corner/mesh layouts, and unsupported directives
- an in-repo independent-index `v/vt/vn` fixture plus the existing P6 fixture drives the real textured Lambert path and is framebuffer byte/hash-identical to the equivalent programmatic normal-bearing mesh

### Milestone 11 — bounded runtime material albedo

- `MaterialState` adds a constant RGB albedo to the existing rasterizer state without introducing a parallel material renderer
- the default white albedo `(1,1,1)` preserves previous rendering behavior byte-for-byte and keeps existing constructor call sites source-compatible by appending the new state after `DirectionalLight`
- each albedo component must be finite and within `[0,1]`; invalid state is rejected at draw entry before any framebuffer mutation
- the unified fragment order is varying/texture base source → component-wise albedo modulation → optional Lambert intensity → depth-tested framebuffer write
- untextured varying color and sampled texture color therefore share identical material semantics
- regressions analytically verify untextured modulation and texture × albedo × Lambert composition
- clipping regression proves material-modulated automatic clipping is byte-identical to equivalent explicitly clipped geometry
- NaN, negative, and above-one albedos are fail-closed, while explicit white material is byte/hash-identical to the implicit default path
- all earlier sample/hash, texture, lighting, OBJ/PPM asset, fixed-point, interpolation, and sanitizer coverage remains enabled

### Milestone 12 — bounded diffuse MTL material import

- `MaterialState` is factored into a reusable material header so runtime shading and asset import share exactly one albedo representation
- `load_mtl(std::istream&)` and `load_mtl_file(path)` implement a deliberately strict MTL subset containing only single-token `newmtl` names and exactly one `Kd r g b` per material
- every `Kd` component must be finite and within `[0,1]`; missing/duplicate `Kd`, duplicate names, malformed values, and unsupported MTL directives fail closed with `MtlParseError`
- legacy `load_obj` / `load_obj_file -> Mesh` behavior is retained for geometry-only callers
- `load_obj_material_batches_file(path)` promotes material metadata into executable `MaterialBatch` values without duplicating OBJ geometry parsing
- material-aware loading accepts exactly one `mtllib` naming a sibling file, requires an active `usemtl` for material-bound faces, rejects unknown material names, and rejects parent/nested material-library paths
- contiguous material runs preserve submitted face order: an A→B→A sequence remains three batches and is never regrouped by material name
- each batch carries canonical normalized mesh geometry, the material name, and the exact runtime `MaterialState` decoded from MTL `Kd`
- OBJ geometry and material metadata passes must agree on face count; disagreement fails rather than silently associating the wrong material with geometry
- an OBJ with no material library becomes one default-white batch, preserving legacy behavior and triangle order
- integration regression loads OBJ + MTL + PPM + explicit normals, renders ordered material batches through existing texture/albedo/Lambert stages, and proves byte/hash equality with equivalent programmatic material submissions
- regressions also cover unknown `usemtl`, `usemtl` without `mtllib`, bounded sibling-library path enforcement, duplicate/missing MTL data, and unsupported material directives

### Milestone 13 — explicit base-color source state

- `BaseColorSource` makes the fragment source an explicit renderer state with `Auto`, `VaryingColor`, `Texture`, and `ConstantWhite` modes
- `Auto` remains the default and preserves the complete earlier source-selection contract: bound texture first, otherwise RGB varyings
- explicit `VaryingColor` and `Texture` modes validate only the bindings they consume, while source/binding conflicts are rejected before framebuffer mutation
- `Texture` requires an actual bound `Texture2D`; `VaryingColor` and `ConstantWhite` reject a simultaneously bound texture so ignored state cannot silently change intent
- `ConstantWhite` supplies `(1,1,1)` before material modulation and consumes neither RGB nor UV channels, allowing UV/normal-only imported meshes to use MTL `Kd` as their real diffuse color
- material modulation, clipping, fixed-point coverage, interpolation, Lambert lighting, depth testing, and framebuffer output remain one shared path after source selection
- explicit varying and texture modes are regression-proven byte-identical to their corresponding legacy automatic paths
- constant-white × albedo × Lambert is verified analytically, and clipped constant-white geometry is byte-identical to equivalent explicitly clipped triangles
- invalid source enum values and invalid source/binding combinations are fail-closed before framebuffer writes
- the real OBJ/MTL normal-bearing fixture renders imported warm/cool `Kd` values without loading any texture, with byte/hash equality to equivalent programmatic material batches

### Milestone 14 — owned MTL diffuse texture assets

- legacy `load_mtl` / `load_mtl_file` remain strict `newmtl` + `Kd` APIs and still reject `map_Kd` instead of silently losing texture metadata
- `MaterialAssetDefinition` and `load_mtl_assets` / `load_mtl_assets_file` provide a separate richer path that retains an optional `map_Kd`
- rich `map_Kd` is deliberately bounded to exactly one simple sibling filename with no options; parent paths, nested paths, duplicates, malformed records, and unsupported directives fail closed
- `MaterialAssetBatch` owns an optional diffuse texture through `shared_ptr<const Texture2D>`, so batch copies/moves cannot leave a dangling texture binding
- `load_obj_material_asset_batches_file` keeps geometry normalization on the canonical OBJ loader, preserves contiguous A→B→A material order, loads maps through the existing bounded P6 decoder, and deduplicates repeated texture paths
- mapped materials select the existing texture base-color path and still multiply sampled texels by imported `Kd`; materials without maps select `ConstantWhite`, preserving Kd-only semantics
- regression proves repeated mapped batches share one decoded texture object and that a surviving batch copy remains sampleable after the original returned batch vector is destroyed
- a mixed file-driven OBJ + MTL `map_Kd` + PPM + explicit normals + Lambert render is byte/hash-identical to equivalent programmatic mixed texture/Kd-only submissions
- the richer loader applied to the existing Kd-only fixture produces no synthetic textures and remains byte/hash-identical to Milestone 13 output
- missing texture files are rejected during asset loading before any rendering submission exists

### Milestone 15 — canonical model assets and ordered draw ranges

- `DrawRange{first_triangle, triangle_count}` gives contiguous indexed-triangle subsets an explicit bounded submission contract
- `ModelAsset` owns one canonical normalized `Mesh`; `MaterialDraw` records carry only an ordered `DrawRange`, material name/state, and optional shared diffuse texture ownership
- `load_obj_model_asset_file` preserves contiguous A→B→A material order as separate ranges over the canonical triangle sequence, deduplicates mapped texture ownership, and emits no synthetic texture for Kd-only/default draws
- the older `load_obj_material_asset_batches_file` remains source-compatible but is derived from `ModelAsset` as a compatibility adapter rather than the primary rich stored representation
- range rendering is regression-proven byte/hash-identical to explicit triangle-subset submission and to Milestone 14 compatibility batches for Kd-only and mixed `map_Kd` assets
- generated ranges are verified contiguous, non-empty, ordered, and collectively cover the canonical triangle list exactly once
- retained texture ownership remains sampleable after the originating `ModelAsset` object is destroyed

### Milestone 16 — direct draw-range execution

- `draw_mesh_range` no longer constructs a transient `Mesh`, copied vertex vector, or selected triangle vector before rendering
- range bounds are checked first, then every selected triangle index is preflighted before any framebuffer mutation; invalid indices outside the selected range remain irrelevant
- varying layout, source bindings, texture-coordinate bounds, normal binding/value state, material/light state, and raster-target safety are preflighted before selected faces are submitted
- lighting ranges additionally compute the inverse-transpose normal matrix and validate transformed-normal stability before writes, preserving the copy-backed Milestone 15 fail-closed contract
- selected indexed faces are assembled only as small stack `Triangle` values and then enter the existing `draw_triangle` path; clipping, interpolation, fixed-point coverage, shading, depth, and framebuffer code remain single-sourced
- non-uniform-scale lit range rendering is byte/hash-identical to the equivalent explicit submesh
- singular normal transforms and invalid empty-range renderer state fail before framebuffer mutation
- existing canonical `ModelAsset` regressions prove Kd-only and mixed `map_Kd` draw records remain byte/hash stable on the direct path
- this is an allocation/copy-structure change only; no wall-clock performance improvement is claimed without controlled benchmarking

## Intentionally not implemented yet

The project does **not** yet include PNG/JPEG or general Netpbm input, general/full OBJ/MTL support, MTL texture-map options, specular/Phong or physically based lighting, shadows, normal maps, ray tracing, GPU acceleration, anti-aliasing, a scene graph, programmable shaders, or a windowing/GUI layer.

## Numerical and graphics limitations

- Homogeneous clipping and attribute interpolation still use single-precision floating point, so geometry extremely close to clip planes remains subject to float precision.
- The varying payload is deliberately fixed at eight scalar channels for deterministic storage and simple teaching value; there is no dynamic shader interface or semantic type system.
- `flat` currently uses the first submitted vertex as the fixed provoking-vertex convention; this is deliberate and not configurable yet.
- Textures use linear RGB float arrays supplied by the caller or decoded byte-for-byte from the bounded P6 loader; there are no mipmaps, anisotropic filtering, sRGB conversion, compressed formats, or general image decoders.
- The PPM loader accepts only P6/maxval-255 images, requires exactly one ASCII whitespace raster separator, rejects trailing bytes, and caps raster payloads at 64 MiB. These are deliberate bounded-decoder semantics rather than a claim of full Netpbm conformance.
- Direct `TextureBinding` remains non-owning and the bound `Texture2D` must outlive rasterizer draw calls. Rich material/model assets avoid dangling imported `map_Kd` references by retaining shared immutable texture ownership.
- OBJ import remains deliberately bounded to positions, 2-D texture coordinates, optional explicit normals, positive absolute indices, and triangle faces. A loaded mesh must consistently use either `v/vt` or `v/vt/vn`; `v//vn`, mixed layouts, polygon triangulation, relative indices, generated normals, and smoothing-group semantics are not supported.
- The material-aware OBJ loaders accept exactly one simple sibling `mtllib` filename. Nested paths, multiple libraries, late libraries, unknown `usemtl` names, and faces without an active material after a library is declared are rejected.
- Legacy MTL import is limited to `newmtl` plus diffuse `Kd`; the richer asset API additionally accepts one simple sibling `map_Kd`. Material names are single tokens; map options, opacity, specular/emissive terms, illumination models, and general MTL syntax are not supported.
- `ModelAsset` is the canonical rich stored representation, but the legacy `MaterialBatch` / `MaterialAssetBatch` compatibility APIs still materialize full mesh copies by design.
- Direct range execution avoids transient mesh-container copies but still assembles each selected indexed face into a small stack `Triangle`; no zero-copy vertex-reference API or measured performance claim exists yet.
- Range preflight currently mirrors the core draw-state validation contract in `rasterizer_range.cpp`. The duplication is intentional for this bounded slice and should be centralized before broadening renderer state further.
- Rich model loading still performs canonical geometry parsing followed by a separate bounded material-metadata scan of the OBJ file; those two passes are cross-checked by face count but are not yet one parser result.
- OBJ texture coordinates and normals are preserved verbatim; the importer does not flip V, normalize normal magnitudes, generate normals, or infer smoothing.
- Runtime material state is one constant bounded RGB albedo per `Rasterizer`; model draws expose owned texture/material state but the caller still constructs the corresponding `TextureBinding` / `BaseColorSource` for each draw.
- Directional lighting is intentionally a bounded diffuse model: one world-space light direction, no attenuation, no specular term, no multiple lights, no normal maps, and no shadowing.
- Lighting-enabled calls require separate model/view/projection matrices so normal transformation remains correct; precomposed-MVP lighting is deliberately unsupported.
- Fixed-point coverage quantizes screen-space positions to 1/256 pixel. Geometry smaller than the quantized grid can collapse to zero area by design.
- Raster targets are rejected if their dimensions would make 64-bit fixed-point edge arithmetic unsafe; this is an explicit fail-closed numerical bound.
- Depth uses the conventional finite OpenGL-style projection and a strict `<` comparison; there is no configurable depth function, reversed-Z, polygon offset, or depth precision analysis yet.
- No back-face culling is performed; both orientations are accepted and normalized for rasterization.
- Mesh topology is triangle-list only with 32-bit unified indices; there are no strips, adjacency data, vertex-cache optimization, or general-purpose scene/model containers beyond the bounded `ModelAsset` draw list.

## Next milestone

The highest-value next architectural frontier is a **single canonical OBJ parse result for geometry plus material metadata**. Rich model loading currently parses normalized geometry once and then reopens the OBJ for a second bounded `mtllib`/`usemtl`/face-material scan, with face counts cross-checked afterward. Replace those two passes with one parser result that emits the canonical `Mesh` and ordered per-face material association from the same accepted face records, while keeping legacy `load_obj` behavior source-compatible. Acceptance should prove legacy geometry byte/hash stability, A→B→A draw-range preservation, identical Kd-only and `map_Kd` `ModelAsset` rendering, deterministic source-line diagnostics for material metadata errors, and no disagreement state that requires a post-hoc face-count check. General OBJ syntax, multiple material libraries, relative indices, polygon triangulation, generated normals, smoothing groups, and broader MTL support remain later phases.
