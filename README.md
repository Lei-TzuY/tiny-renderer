# tiny-renderer

`tiny-renderer` is a correctness-first educational CPU software rasterizer written in modern C++20 without OpenGL, Vulkan, Direct3D, SDL rendering APIs, or an existing rasterization library. Milestone 1 established the end-to-end triangle pipeline, Milestone 2 added indexed meshes, and Milestone 3 generalizes vertex varyings through clipping and perspective interpolation.

## Rendering pipeline

The renderer follows this path for every submitted triangle:

1. **Object space** — user-supplied positions plus a fixed-capacity scalar varying payload. The convenience RGB vertex form occupies varying channels 0–2.
2. **Model / view / projection** — home-grown `Vec*` and `Mat4` types transform each position into homogeneous clip space.
3. **Homogeneous clipping** — Sutherland-Hodgman clipping against all six canonical clip planes (`-w <= x,y,z <= w`) prevents invalid/off-screen geometry from reaching rasterization and linearly interpolates every active varying channel at generated clip vertices.
4. **Perspective divide** — `(x, y, z) / w` produces normalized device coordinates.
5. **Viewport transform** — NDC is mapped to pixel coordinates with a top-left image origin.
6. **Triangle setup/rasterization** — a bounded integer pixel box and edge functions test pixel centers deterministically; winding is normalized and zero-area triangles are discarded.
7. **Barycentric interpolation** — barycentric weights interpolate depth; active varying channels use perspective-correct interpolation through `varying / w` and `1 / w`.
8. **Color binding** — three validated varying-channel indices select the RGB value written to the framebuffer; the default binding is channels 0, 1, and 2.
9. **Depth testing** — NDC depth is mapped to `[0, 1]` and compared against a floating-point z-buffer.
10. **Framebuffer / image output** — RGB float pixels plus depth are stored in CPU memory and emitted as binary PPM (`P6`) without a GUI.

Indexed meshes are deliberately a submission/assembly layer above this pipeline: triangle indices reference reusable vertices, topology and varying contracts are validated before drawing, and each assembled face then enters the same clipping/rasterization/depth path as an explicitly submitted triangle.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/tiny_renderer_sample milestone1.ppm
```

The sample scene renders three colored triangles through a perspective camera. Two overlap at different depths to make z-buffer visibility obvious, and one crosses the left clip plane to exercise clipping. The executable also prints a deterministic FNV-1a hash of the RGB framebuffer.

## Architecture

- `include/tiny_renderer/math.hpp` — vectors, matrices, camera/view and perspective projection math.
- `include/tiny_renderer/mesh.hpp` — vertices, fixed-capacity `VaryingPack`, indexed triangle topology, and the reusable mesh data model.
- `include/tiny_renderer/framebuffer.hpp`, `src/framebuffer.cpp` — RGB/depth storage, depth writes, deterministic byte conversion and PPM output.
- `include/tiny_renderer/rasterizer.hpp`, `src/rasterizer.cpp` — color-channel binding, mesh preflight/assembly, clip-space polygon clipping, perspective divide, generic varying interpolation, viewport mapping, edge-function rasterization and depth testing.
- `src/main.cpp` — deterministic end-to-end sample scene.
- `tests/test_main.cpp` — dependency-free mathematical, rasterization, mesh, varying, and integration correctness tests.
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

## Intentionally not implemented yet

The project does **not** yet include textures, OBJ/model-file loading, Phong or physically based lighting, shadows, ray tracing, GPU acceleration, anti-aliasing, a scene graph, programmable shaders, or a windowing/GUI layer.

## Numerical and graphics limitations

- The pipeline uses single-precision floating point, so nearly-degenerate triangles and geometry extremely close to clip planes can be sensitive to the fixed epsilon.
- Varyings currently use one interpolation class: smooth perspective-correct interpolation. There are no `flat` or `noperspective` qualifiers yet.
- The varying payload is deliberately fixed at eight scalar channels for deterministic storage and simple teaching value; there is no dynamic shader interface or semantic type system.
- Coverage uses floating-point edge functions rather than fixed-point subpixel coordinates, so exact cross-platform bit identity is expected for the tested inputs but is not presented as a formal IEEE-754 portability guarantee.
- Depth uses the conventional finite OpenGL-style projection and a strict `<` comparison; there is no configurable depth function, reversed-Z, polygon offset, or depth precision analysis yet.
- No back-face culling is performed; both orientations are accepted and normalized for rasterization.
- Mesh topology is currently triangle-list only with 32-bit indices; there are no strips, adjacency data, vertex-cache optimization, or model-file import facilities.

## Next milestone

The highest-value next step is **fixed-point subpixel triangle setup and coverage**. Replacing floating-point edge acceptance with an integer subpixel grid will make the top-left rule and shared-edge ownership materially stronger and more reproducible, while keeping barycentric/depth/varying interpolation mathematically explicit. Interpolation qualifiers (`flat` / `noperspective`) are the next attribute-layer extension after coverage semantics are hardened.
