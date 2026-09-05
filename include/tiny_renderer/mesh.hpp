#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <vector>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

constexpr std::size_t kMaxVaryings = 8U;

enum class Interpolation {
    Smooth,
    NoPerspective,
    Flat,
};

struct VaryingPack {
    std::array<float, kMaxVaryings> values{};
    std::array<Interpolation, kMaxVaryings> interpolation{};
    std::size_t count{};

    VaryingPack() = default;

    VaryingPack(const Vec3& color) : values{color.x, color.y, color.z}, count(3U) {}

    VaryingPack(std::initializer_list<float> init) : count(init.size()) {
        if (count > kMaxVaryings) {
            throw std::invalid_argument("varying pack exceeds fixed capacity");
        }
        std::size_t i = 0;
        for (float value : init) {
            values[i++] = value;
        }
    }

    VaryingPack& set_interpolation(std::size_t index, Interpolation mode) {
        if (index >= count) {
            throw std::out_of_range("varying interpolation index out of range");
        }
        interpolation[index] = mode;
        return *this;
    }

    [[nodiscard]] float operator[](std::size_t index) const { return values.at(index); }
    [[nodiscard]] float& operator[](std::size_t index) { return values.at(index); }
    [[nodiscard]] Interpolation interpolation_at(std::size_t index) const { return interpolation.at(index); }
};

struct Vertex {
    Vec3 position;
    VaryingPack varyings;

    Vertex() = default;
    Vertex(const Vec3& position_in, const Vec3& color) : position(position_in), varyings(color) {}
    [[nodiscard]] static Vertex with_varyings(const Vec3& position_in, const VaryingPack& varyings_in) {
        Vertex vertex;
        vertex.position = position_in;
        vertex.varyings = varyings_in;
        return vertex;
    }
};

using Triangle = std::array<Vertex, 3>;
using TriangleIndices = std::array<std::uint32_t, 3>;

struct DrawRange {
    std::size_t first_triangle{0U};
    std::size_t triangle_count{0U};
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<TriangleIndices> triangles;
};

}  // namespace tiny_renderer
