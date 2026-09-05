# tiny-renderer

`tiny-renderer` is a correctness-first educational CPU software rasterizer written in modern C++20 without OpenGL, Vulkan, Direct3D, SDL rendering APIs, or an existing rasterization library. Milestone 1 established the end-to-end triangle pipeline; Milestone 2 adds indexed triangle-mesh submission while keeping the raster core explicit and testable.

## Rendering pipeline

The renderer follows this path for every submitted triangle:

1. **Object space** — user-supplied positions and vertex RGB values.
2. **Model / view / projection** — home-grown `Vec*` and `Mat4` types transform each position into homogeneous clip space.
3. **Homogeneous clipping** — Sutherland-Hodgman clipping against all six canonical clip planes (`-w <= x,y,z <= w`) prevents invalid/off-screen geometry from reaching rasterization.
4. **Perspective divide** — `(x, y, z) / w` produces normalized device coordinates.
5. **Viewport transform** — NDC is mapped to pixel coordinates with a top-left image origin.
6. **Triangle setup/rasterization** — a bounded integer pixel box and edge functions test pixel centers deterministically; winding is normalized and zero-area triangles are discarded.
7. **Barycentric interpolation** — barycentric weights interpolate depth; RGB vertex attributes use perspective-correct interpolation through `attribute / w` and `1 / w`.
8. **Depth testing** — NDC depth is mapped to `[0, 1]` and compared against a floating-point z-buffer.
9. **Framebuffer** — RGB float pixels plus depth are stored in CPU memory.
10. **Image output** — the framebuffer is emitted as a binary PPM (`P6`) file, so no GUI is required.

Indexed meshes are deliberately a submission/assembly layer above this pipeline: triangle indices reference reusable vertices, all indices are validated before any draw occurs, and each assembled face then enters the same clipping/rasterization/depth path as an explicitly submitted triangle.

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
- `include/tiny_renderer/mesh.hpp` — vertices, indexed triangle topology, and the reusable mesh data model.
- `include/tiny_renderer/framebuffer.hpp`, `src/framebuffer.cpp` — RGB/depth storage, depth writes, deterministic byte conversion and PPM output.
- `include/tiny_renderer/rasterizer.hpp`, `src/rasterizer.cpp` — mesh preflight/assembly, clip-space polygon clipping, perspective divide, viewport mapping, barycentric/edge-function rasterization and depth testing.
- `src/main.cpp` — deterministic end-to-end sample scene.
- `tests/test_main.cpp` — dependency-free mathematical, rasterization, mesh, and integration correctness tests.
- `.github/workflows/ci.yml` — Linux/macOS build/test plus Linux ASan/UBSan coverage.

## Implemented

### Milestone 1 — CPU triangle pipeline

- C++20 CPU-only rasterization
- vector/matrix math, model/view/projection transforms
- OpenGL-style homogeneous clip volume and six-plane clipping
- perspective projection/divide and viewport transform
- deterministic pixel-center triangle coverage with a top-left edge rule
- winding normalization and degenerate rejection
- barycentric coordinates and perspective-correct RGB interpolation
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

## Intentionally not implemented yet

The project does **not** yet include textures, OBJ/model-file loading, Phong or physically based lighting, shadows, ray tracing, GPU acceleration, anti-aliasing, a scene graph, programmable shaders, or a windowing/GUI layer.

## Numerical and graphics limitations

- The pipeline uses single-precision floating point, so nearly-degenerate triangles and geometry extremely close to clip planes can be sensitive to the fixed epsilon.
- Clipping linearly interpolates the current RGB attribute in homogeneous edge parameter space; the raster stage then performs perspective-correct interpolation. The attribute payload is still hard-coded rather than generalized.
- Coverage uses floating-point edge functions rather than fixed-point subpixel coordinates, so exact cross-platform bit identity is expected for the tested inputs but is not presented as a formal IEEE-754 portability guarantee.
- Depth uses the conventional finite OpenGL-style projection and a strict `<` comparison; there is no configurable depth function, reversed-Z, polygon offset, or depth precision analysis yet.
- No back-face culling is performed; both orientations are accepted and normalized for rasterization.
- Mesh topology is currently triangle-list only with 32-bit indices; there are no strips, adjacency data, vertex-cache optimization, or model-file import facilities.

## Next milestone

The highest-value next step is a **generalized vertex-output/varying pipeline**: replace the rasterizer's hard-coded RGB interpolation plumbing with an explicit varying payload and interpolation contract that survives clipping and perspective correction. That creates the architecture needed for later normals and UVs without prematurely adding textures, lighting, OBJ loading, or shader programmability.
