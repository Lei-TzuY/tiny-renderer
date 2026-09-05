#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

struct Vertex {
    Vec3 position;
    Vec3 color;
};

using Triangle = std::array<Vertex, 3>;
using TriangleIndices = std::array<std::uint32_t, 3>;

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<TriangleIndices> triangles;
};

}  // namespace tiny_renderer
