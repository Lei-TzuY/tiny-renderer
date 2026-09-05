# tiny-renderer

`tiny-renderer` is a correctness-first educational CPU software rasterizer written in modern C++20 without OpenGL, Vulkan, Direct3D, SDL rendering APIs, or an existing rasterization library. Milestone 1 established the end-to-end triangle pipeline, Milestone 2 added indexed meshes, Milestone 3 generalized vertex varyings, Milestone 4 hardened fixed-point coverage, Milestone 5 added explicit interpolation semantics, Milestone 6 added deterministic in-memory texture sampling, and Milestone 7 adds a bounded OBJ position/UV/triangle import layer that feeds the same indexed textured pipeline.

## Rendering pipeline

The renderer follows this path for every submitted triangle:

1. **Object space** — user-supplied or imported positions plus a fixed-capacity scalar varying payload. The convenience RGB vertex form occupies varying channels 0–2 and defaults them to smooth interpolation; the bounded OBJ importer emits raw OBJ UVs in smooth channels 0–1.
2. **Model / view / projection** — home-grown `Vec*` and `Mat4` types transform each position into homogeneous clip space.
3. **Homogeneous clipping** — Sutherland-Hodgman clipping against all six canonical clip planes (`-w <= x,y,z <= w`) prevents invalid/off-screen geometry from reaching rasterization. Generated vertices preserve the interpolation contract: smooth channels use the homogeneous edge parameter, noperspective channels use the corresponding projected edge parameter, and flat channels retain the submitted primitive's provoking value.
4. **Perspective divide** — `(x, y, z) / w` produces normalized device coordinates.
5. **Viewport transform** — NDC is mapped to pixel coordinates with a top-left image origin.
6. **Fixed-point triangle setup/coverage** — screen-space vertices are quantized to a 1/256-pixel grid; signed 64-bit integer edge equations plus an exact top-left rule decide pixel-center ownership. Quantized zero-area triangles are discarded.
7. **Qualified barycentric interpolation** — accepted samples use the original floating screen positions. `smooth` channels use perspective-correct `varying / w` and `1 / w`, `noperspective` channels use screen-linear barycentric interpolation, and `flat` channels use the first submitted vertex as the provoking vertex.
8. **Fragment color source** — without a texture binding, three validated varying channels supply framebuffer RGB. With a texture binding, two validated varying channels supply UV coordinates to the bound `Texture2D` sampler; this uses the exact same clipping/interpolation/coverage path rather than a separate textured rasterizer.
9. **Texture sampling** — normalized UVs use explicit `Clamp` or `Repeat` addressing and `Nearest` or texel-center `Bilinear` filtering. Texture output replaces the untextured color binding for the fragment.
10. **Depth testing** — NDC depth is mapped to `[0, 1]` and compared against a floating-point z-buffer.
11. **Framebuffer / image output** — RGB float pixels plus depth are stored in CPU memory and emitted as binary PPM (`P6`) without a GUI.

Indexed meshes are deliberately a submission/assembly layer above this pipeline: triangle indices reference reusable vertices, topology and varying contracts are validated before drawing, and each assembled face then enters the same clipping/rasterization/depth path as an explicitly submitted triangle. The OBJ importer is another layer above `Mesh`: it parses a deliberately small asset subset, normalizes OBJ's independent position/UV index pairs into unified vertices, and then hands the resulting ordinary `Mesh` to the unchanged renderer.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/tiny_renderer_sample milestone1.ppm
```

The original sample scene still renders three colored triangles through a perspective camera. Two overlap at different depths to make z-buffer visibility obvious, and one crosses the left clip plane to exercise clipping. Texture sampling and OBJ import are exercised by dedicated tests without changing this baseline sample or its deterministic framebuffer contract.

## Architecture

- `include/tiny_renderer/math.hpp` — vectors, matrices, camera/view and perspective projection math.
- `include/tiny_renderer/mesh.hpp` — vertices, fixed-capacity `VaryingPack`, per-channel `Interpolation` qualifiers, indexed triangle topology, and the reusable mesh data model.
- `include/tiny_renderer/obj_loader.hpp`, `src/obj_loader.cpp` — bounded OBJ stream/file parsing, source-line diagnostics, and deterministic position/UV pair normalization into `Mesh`.
- `include/tiny_renderer/texture.hpp`, `src/texture.cpp` — validated in-memory RGB textures plus deterministic normalized-coordinate addressing and nearest/bilinear sampling.
- `include/tiny_renderer/framebuffer.hpp`, `src/framebuffer.cpp` — RGB/depth storage, depth writes, deterministic byte conversion and PPM output.
- `include/tiny_renderer/rasterizer.hpp`, `src/rasterizer.cpp` — color/texture binding, mesh preflight/assembly, qualifier-aware clip-space interpolation, perspective divide, fixed-point subpixel coverage, qualified raster interpolation, fragment color selection and depth testing.
- `src/main.cpp` — deterministic end-to-end baseline sample scene.
- `tests/test_main.cpp` — dependency-free mathematical, rasterization, mesh, varying, and integration correctness tests.
- `tests/test_fixed_point.cpp` — fixed-point ownership, quantization-stability, and subpixel-degeneracy regressions.
- `tests/test_interpolation.cpp` — analytic smooth/noperspective/flat semantics, clipping behavior, provoking-vertex preservation, and fail-closed qualifier-layout regressions.
- `tests/test_texture.cpp` — sampler addressing/filtering, perspective-correct UV, clipping continuity, and fail-closed texture-binding regressions.
- `tests/test_obj_loader.cpp`, `tests/fixtures/textured_quad.obj` — OBJ syntax/index normalization, rejection, diagnostics, and textured-render equivalence regressions.
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

## Intentionally not implemented yet

The project does **not** yet include image-file texture loading, general/full OBJ support, normal import, material interpretation, Phong or physically based lighting, shadows, ray tracing, GPU acceleration, anti-aliasing, a scene graph, programmable shaders, or a windowing/GUI layer.

## Numerical and graphics limitations

- Homogeneous clipping and attribute interpolation still use single-precision floating point, so geometry extremely close to clip planes remains subject to float precision.
- The varying payload is deliberately fixed at eight scalar channels for deterministic storage and simple teaching value; there is no dynamic shader interface or semantic type system.
- `flat` currently uses the first submitted vertex as the fixed provoking-vertex convention; this is deliberate and not configurable yet.
- Textures are RGB float arrays supplied by the caller; there are no mipmaps, anisotropic filtering, sRGB conversion, compressed formats, or image-file decoders yet.
- Texture bindings are non-owning and the bound `Texture2D` must outlive rasterizer draw calls.
- OBJ import is deliberately bounded to positions, 2-D texture coordinates, positive absolute indices, and triangle `v/vt` faces. It does not triangulate polygons, resolve relative indices, import normals, or evaluate material libraries.
- OBJ texture coordinates are preserved verbatim; because this renderer's in-memory texture rows use a top-origin convention, callers remain responsible for any asset-specific V-coordinate convention conversion.
- Fixed-point coverage quantizes screen-space positions to 1/256 pixel. Geometry smaller than the quantized grid can collapse to zero area by design.
- Raster targets are rejected if their dimensions would make 64-bit fixed-point edge arithmetic unsafe; this is an explicit fail-closed numerical bound.
- Depth uses the conventional finite OpenGL-style projection and a strict `<` comparison; there is no configurable depth function, reversed-Z, polygon offset, or depth precision analysis yet.
- No back-face culling is performed; both orientations are accepted and normalized for rasterization.
- Mesh topology is triangle-list only with 32-bit unified indices; there are no strips, adjacency data, vertex-cache optimization, or general-purpose scene/model containers.

## Next milestone

The highest-value next step is a **deterministic PPM texture-file import vertical slice** that closes the remaining asset-I/O gap without pulling in a third-party image library. Decode a tightly specified PPM subset into `Texture2D`, reject malformed/truncated/overflowing images before exposing a texture, and add a file-driven OBJ + PPM integration fixture whose render is byte-identical to the same mesh and texels constructed programmatically. Image color-space conversion, PNG/JPEG, mipmaps, lighting, and materials should remain out of scope until that file-to-textured-render path is proven.
