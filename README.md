# tiny-renderer

`tiny-renderer` is a correctness-first educational CPU software rasterizer written in modern C++20 without OpenGL, Vulkan, Direct3D, SDL rendering APIs, or an existing rasterization library. Milestone 1 established the end-to-end triangle pipeline, Milestone 2 added indexed meshes, Milestone 3 generalized vertex varyings, Milestone 4 hardened fixed-point coverage, Milestone 5 added explicit interpolation semantics, Milestone 6 added deterministic in-memory texture sampling, Milestone 7 added bounded OBJ position/UV import, Milestone 8 added bounded binary PPM texture-file import, and Milestone 9 adds inverse-transpose normal handling plus deterministic world-space directional Lambert lighting through the same fragment path.

## Rendering pipeline

The renderer follows this path for every submitted triangle:

1. **Object space** — user-supplied or imported positions plus a fixed-capacity scalar varying payload. The convenience RGB vertex form occupies varying channels 0–2 and defaults them to smooth interpolation; the bounded OBJ importer emits raw OBJ UVs in smooth channels 0–1.
2. **Model / view / projection** — home-grown `Vec*` and `Mat4` types transform each position into homogeneous clip space. When directional lighting is enabled, explicitly bound object-space normal channels are transformed into world space by the model matrix's inverse-transpose 3×3 normal matrix before clipping.
3. **Homogeneous clipping** — Sutherland-Hodgman clipping against all six canonical clip planes (`-w <= x,y,z <= w`) prevents invalid/off-screen geometry from reaching rasterization. Generated vertices preserve the interpolation contract: smooth channels use the homogeneous edge parameter, noperspective channels use the corresponding projected edge parameter, and flat channels retain the submitted primitive's provoking value. Transformed normal channels remain ordinary varyings here, so normal clipping shares the same deterministic path.
4. **Perspective divide** — `(x, y, z) / w` produces normalized device coordinates.
5. **Viewport transform** — NDC is mapped to pixel coordinates with a top-left image origin.
6. **Fixed-point triangle setup/coverage** — screen-space vertices are quantized to a 1/256-pixel grid; signed 64-bit integer edge equations plus an exact top-left rule decide pixel-center ownership. Quantized zero-area triangles are discarded.
7. **Qualified barycentric interpolation** — accepted samples use the original floating screen positions. `smooth` channels use perspective-correct `varying / w` and `1 / w`, `noperspective` channels use screen-linear barycentric interpolation, and `flat` channels use the first submitted vertex as the provoking vertex.
8. **Fragment color source** — without a texture binding, three validated varying channels supply framebuffer RGB. With a texture binding, two validated varying channels supply UV coordinates to the bound `Texture2D` sampler; this uses the exact same clipping/interpolation/coverage path rather than a separate textured rasterizer.
9. **Texture sampling** — normalized UVs use explicit `Clamp` or `Repeat` addressing and `Nearest` or texel-center `Bilinear` filtering. Texture output replaces the untextured color binding for the fragment.
10. **Directional Lambert lighting** — when enabled, the interpolated world-space normal is renormalized per fragment. The base color or sampled texture is multiplied by `ambient + diffuse * max(dot(normal, direction_to_light), 0)`, with validated bounded coefficients. Lighting-disabled rendering remains byte-compatible with the earlier path.
11. **Depth testing** — NDC depth is mapped to `[0, 1]` and compared against a floating-point z-buffer.
12. **Framebuffer / image output** — RGB float pixels plus depth are stored in CPU memory and emitted as binary PPM (`P6`) without a GUI.

Indexed meshes are deliberately a submission/assembly layer above this pipeline: triangle indices reference reusable vertices, topology and varying contracts are validated before drawing, and each assembled face then enters the same clipping/rasterization/depth path as an explicitly submitted triangle. Asset import stays above the rendering core: the OBJ loader converts a bounded geometry subset into ordinary `Mesh`, while the PPM loader converts a bounded image subset into ordinary `Texture2D`. The rasterizer therefore remains unaware of file formats.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/tiny_renderer_sample milestone1.ppm
```

The original sample scene still renders three colored triangles through a perspective camera. Two overlap at different depths to make z-buffer visibility obvious, and one crosses the left clip plane to exercise clipping. Texture sampling, file-driven OBJ/PPM import, and lighting are exercised by dedicated tests without changing this baseline sample or its deterministic framebuffer contract.

## Architecture

- `include/tiny_renderer/math.hpp` — vectors, `Mat3`/`Mat4`, camera/view and perspective projection math, plus inverse-transpose model normal-matrix construction.
- `include/tiny_renderer/mesh.hpp` — vertices, fixed-capacity `VaryingPack`, per-channel `Interpolation` qualifiers, indexed triangle topology, and the reusable mesh data model.
- `include/tiny_renderer/obj_loader.hpp`, `src/obj_loader.cpp` — bounded OBJ stream/file parsing, source-line diagnostics, and deterministic position/UV pair normalization into `Mesh`.
- `include/tiny_renderer/ppm_loader.hpp`, `src/ppm_loader.cpp` — bounded binary PPM stream/file decoding with strict header/raster validation into `Texture2D`.
- `include/tiny_renderer/texture.hpp`, `src/texture.cpp` — validated in-memory RGB textures plus deterministic normalized-coordinate addressing and nearest/bilinear sampling.
- `include/tiny_renderer/framebuffer.hpp`, `src/framebuffer.cpp` — RGB/depth storage, depth writes, deterministic byte conversion and PPM output.
- `include/tiny_renderer/rasterizer.hpp`, `src/rasterizer.cpp` — color/texture/normal bindings, mesh preflight/assembly, qualifier-aware clip-space interpolation, perspective divide, fixed-point subpixel coverage, qualified raster interpolation, fragment color selection, directional Lambert modulation, and depth testing.
- `src/main.cpp` — deterministic end-to-end baseline sample scene.
- `tests/test_main.cpp` — dependency-free mathematical, rasterization, mesh, varying, and integration correctness tests.
- `tests/test_fixed_point.cpp` — fixed-point ownership, quantization-stability, and subpixel-degeneracy regressions.
- `tests/test_interpolation.cpp` — analytic smooth/noperspective/flat semantics, clipping behavior, provoking-vertex preservation, and fail-closed qualifier-layout regressions.
- `tests/test_texture.cpp` — sampler addressing/filtering, perspective-correct UV, clipping continuity, and fail-closed texture-binding regressions.
- `tests/test_obj_loader.cpp`, `tests/fixtures/textured_quad.obj` — OBJ syntax/index normalization, rejection, diagnostics, and textured-render equivalence regressions.
- `tests/test_ppm_loader.cpp`, `tests/fixtures/checker.ppm` — P6 binary decoding, malformed/truncated/overflow rejection, and file-driven OBJ + PPM render equivalence.
- `tests/test_lighting.cpp` — inverse-transpose normal correctness, non-uniform-transform lighting, texture modulation, lit clipping equivalence, and fail-closed lighting-contract regressions.
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
- comments and non-geometric `o`, `g`, `s`, `usemtl`, and `mtllib` metadata are tolerated; material libraries are not loaded or interpreted
- only positive absolute 1-based indices are accepted; zero, relative indices, missing UVs, out-of-range references, malformed finite numbers, normals, `v/vt/vn`, quads, ngons, and other unsupported directives fail closed with `ObjParseError`
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

## Intentionally not implemented yet

The project does **not** yet include PNG/JPEG or general Netpbm input, general/full OBJ support, OBJ normal import, material interpretation, specular/Phong or physically based lighting, shadows, normal maps, ray tracing, GPU acceleration, anti-aliasing, a scene graph, programmable shaders, or a windowing/GUI layer.

## Numerical and graphics limitations

- Homogeneous clipping and attribute interpolation still use single-precision floating point, so geometry extremely close to clip planes remains subject to float precision.
- The varying payload is deliberately fixed at eight scalar channels for deterministic storage and simple teaching value; there is no dynamic shader interface or semantic type system.
- `flat` currently uses the first submitted vertex as the fixed provoking-vertex convention; this is deliberate and not configurable yet.
- Textures use linear RGB float arrays supplied by the caller or decoded byte-for-byte from the bounded P6 loader; there are no mipmaps, anisotropic filtering, sRGB conversion, compressed formats, or general image decoders.
- The PPM loader accepts only P6/maxval-255 images, requires exactly one ASCII whitespace raster separator, rejects trailing bytes, and caps raster payloads at 64 MiB. These are deliberate bounded-decoder semantics rather than a claim of full Netpbm conformance.
- Texture bindings are non-owning and the bound `Texture2D` must outlive rasterizer draw calls.
- OBJ import remains deliberately bounded to positions, 2-D texture coordinates, positive absolute indices, and triangle `v/vt` faces. It does not yet import normals, triangulate polygons, resolve relative indices, or evaluate material libraries.
- OBJ texture coordinates are preserved verbatim; because this renderer's in-memory texture rows use a top-origin convention, callers remain responsible for any asset-specific V-coordinate convention conversion.
- Directional lighting is intentionally a bounded diffuse model: one world-space light direction, no attenuation, no specular term, no multiple lights, no materials, no normal maps, and no shadowing.
- Lighting-enabled calls require separate model/view/projection matrices so normal transformation remains correct; precomposed-MVP lighting is deliberately unsupported.
- Fixed-point coverage quantizes screen-space positions to 1/256 pixel. Geometry smaller than the quantized grid can collapse to zero area by design.
- Raster targets are rejected if their dimensions would make 64-bit fixed-point edge arithmetic unsafe; this is an explicit fail-closed numerical bound.
- Depth uses the conventional finite OpenGL-style projection and a strict `<` comparison; there is no configurable depth function, reversed-Z, polygon offset, or depth precision analysis yet.
- No back-face culling is performed; both orientations are accepted and normalized for rasterization.
- Mesh topology is triangle-list only with 32-bit unified indices; there are no strips, adjacency data, vertex-cache optimization, or general-purpose scene/model containers.

## Next milestone

The highest-value next architectural frontier is **bounded OBJ normal import**. Extend the existing strict OBJ layer to accept finite `vn` records and triangle `v/vt/vn` faces, normalize independent position/UV/normal index triples into unified vertices, and emit UV plus normal varyings that bind directly to the already-verified textured Lambert path. Acceptance should prove deterministic triple-index splitting/reuse, malformed/out-of-range normal rejection, and a file-driven `OBJ(v/vt/vn) + PPM + lighting` render that is byte-identical to the equivalent programmatic mesh/texture/normals. Polygon triangulation, relative indices, materials, specular lighting, and general OBJ conformance should remain out of scope until that normal-bearing asset path is proven.
