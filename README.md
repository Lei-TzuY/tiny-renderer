# tiny-renderer

`tiny-renderer` is a correctness-first educational CPU software rasterizer. Milestone 1 implements the triangle pipeline directly in modern C++20 without OpenGL, Vulkan, Direct3D, SDL rendering APIs, or an existing rasterization library.

## Milestone 1 pipeline

The renderer follows this path for every input triangle:

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
- `include/tiny_renderer/framebuffer.hpp`, `src/framebuffer.cpp` — RGB/depth storage, depth writes, deterministic byte conversion and PPM output.
- `include/tiny_renderer/rasterizer.hpp`, `src/rasterizer.cpp` — clip-space polygon clipping, perspective divide, viewport mapping, barycentric/edge-function rasterization and depth testing.
- `src/main.cpp` — deterministic end-to-end sample scene.
- `tests/test_main.cpp` — dependency-free correctness tests.
- `.github/workflows/ci.yml` — Linux/macOS build/test plus Linux ASan/UBSan coverage.

## Implemented

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

## Intentionally not implemented yet

Milestone 1 does **not** include textures, OBJ/model loading, Phong or physically based lighting, shadows, ray tracing, GPU acceleration, anti-aliasing, a scene graph, programmable shaders, or a windowing/GUI layer.

## Numerical and graphics limitations

- The pipeline uses single-precision floating point, so nearly-degenerate triangles and geometry extremely close to clip planes can be sensitive to the fixed epsilon.
- Clipping linearly interpolates current vertex attributes in homogeneous edge parameter space; the raster stage then performs perspective-correct interpolation. This is sufficient for the current RGB demonstration but the attribute system is not generalized yet.
- Coverage uses floating-point edge functions rather than fixed-point subpixel coordinates, so exact cross-platform bit identity is expected for the tested inputs but is not presented as a formal IEEE-754 portability guarantee.
- Depth uses the conventional finite OpenGL-style projection and a strict `<` comparison; there is no configurable depth function, reversed-Z, polygon offset, or depth precision analysis yet.
- No back-face culling is performed; both orientations are accepted and normalized for rasterization.

## Next milestone

The highest-value next step is a **general vertex-attribute pipeline plus mesh/indexed-triangle input**, followed by tests for shared-edge crack freedom and perspective-correct interpolation. That deepens the raster pipeline without jumping ahead to textures, lighting, or model-file loading.
