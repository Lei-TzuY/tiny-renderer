#include "tiny_renderer/rasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace tiny_renderer {

float signed_area_twice(const Vec2& a, const Vec2& b, const Vec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

std::optional<Vec3> barycentric_coordinates(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& p) {
    const float area = signed_area_twice(a, b, c);
    if (std::fabs(area) <= kEpsilon) {
        return std::nullopt;
    }
    const float w0 = signed_area_twice(b, c, p) / area;
    const float w1 = signed_area_twice(c, a, p) / area;
    const float w2 = 1.0F - w0 - w1;
    return Vec3{w0, w1, w2};
}

bool barycentric_inside(const Vec3& barycentric, float epsilon) {
    return barycentric.x >= -epsilon && barycentric.y >= -epsilon && barycentric.z >= -epsilon;
}

namespace {

struct ClipVertex {
    Vec4 position;
    VaryingPack varyings;
};

struct ScreenVertex {
    Vec2 position;
    float ndc_z{};
    float inv_w{};
    VaryingPack varyings_over_w;
};

void validate_pack(const VaryingPack& pack) {
    if (pack.count > kMaxVaryings) {
        throw std::invalid_argument("varying pack count exceeds fixed capacity");
    }
}

void validate_binding(const ColorBinding& binding, std::size_t varying_count) {
    if (binding.red >= varying_count || binding.green >= varying_count || binding.blue >= varying_count) {
        throw std::out_of_range("color binding references unavailable varying channel");
    }
}

void validate_triangle_varyings(const Triangle& triangle, const ColorBinding& binding) {
    const std::size_t count = triangle[0].varyings.count;
    validate_pack(triangle[0].varyings);
    validate_binding(binding, count);
    for (std::size_t i = 1; i < triangle.size(); ++i) {
        validate_pack(triangle[i].varyings);
        if (triangle[i].varyings.count != count) {
            throw std::invalid_argument("triangle vertices must use the same varying count");
        }
    }
}

VaryingPack interpolate(const VaryingPack& a, const VaryingPack& b, float t) {
    if (a.count != b.count) {
        throw std::invalid_argument("cannot interpolate mismatched varying packs");
    }
    VaryingPack result;
    result.count = a.count;
    for (std::size_t i = 0; i < result.count; ++i) {
        result.values[i] = a.values[i] + (b.values[i] - a.values[i]) * t;
    }
    return result;
}

VaryingPack scale(const VaryingPack& pack, float factor) {
    VaryingPack result;
    result.count = pack.count;
    for (std::size_t i = 0; i < result.count; ++i) {
        result.values[i] = pack.values[i] * factor;
    }
    return result;
}

float plane_distance(const ClipVertex& v, int plane) {
    switch (plane) {
        case 0: return v.position.w + v.position.x;
        case 1: return v.position.w - v.position.x;
        case 2: return v.position.w + v.position.y;
        case 3: return v.position.w - v.position.y;
        case 4: return v.position.w + v.position.z;
        case 5: return v.position.w - v.position.z;
        default: return -1.0F;
    }
}

ClipVertex lerp(const ClipVertex& a, const ClipVertex& b, float t) {
    return {
        a.position + (b.position - a.position) * t,
        interpolate(a.varyings, b.varyings, t),
    };
}

std::vector<ClipVertex> clip_against_plane(const std::vector<ClipVertex>& input, int plane) {
    std::vector<ClipVertex> output;
    if (input.empty()) {
        return output;
    }
    output.reserve(input.size() + 1U);

    ClipVertex previous = input.back();
    float previous_distance = plane_distance(previous, plane);
    bool previous_inside = previous_distance >= 0.0F;

    for (const ClipVertex& current : input) {
        const float current_distance = plane_distance(current, plane);
        const bool current_inside = current_distance >= 0.0F;

        if (current_inside != previous_inside) {
            const float denominator = previous_distance - current_distance;
            if (std::fabs(denominator) > kEpsilon) {
                const float t = previous_distance / denominator;
                output.push_back(lerp(previous, current, std::clamp(t, 0.0F, 1.0F)));
            }
        }
        if (current_inside) {
            output.push_back(current);
        }

        previous = current;
        previous_distance = current_distance;
        previous_inside = current_inside;
    }
    return output;
}

std::vector<ClipVertex> clip_triangle(const std::array<ClipVertex, 3>& triangle) {
    std::vector<ClipVertex> polygon(triangle.begin(), triangle.end());
    for (int plane = 0; plane < 6 && !polygon.empty(); ++plane) {
        polygon = clip_against_plane(polygon, plane);
    }
    return polygon;
}

bool finite(const Vec4& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
}

std::optional<ScreenVertex> to_screen(const ClipVertex& vertex, std::size_t width, std::size_t height) {
    if (!finite(vertex.position) || std::fabs(vertex.position.w) <= kEpsilon) {
        return std::nullopt;
    }

    const float inv_w = 1.0F / vertex.position.w;
    const float ndc_x = vertex.position.x * inv_w;
    const float ndc_y = vertex.position.y * inv_w;
    const float ndc_z = vertex.position.z * inv_w;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || !std::isfinite(ndc_z)) {
        return std::nullopt;
    }

    const float max_x = static_cast<float>(width - 1U);
    const float max_y = static_cast<float>(height - 1U);
    return ScreenVertex{
        {(ndc_x * 0.5F + 0.5F) * max_x, (1.0F - (ndc_y * 0.5F + 0.5F)) * max_y},
        ndc_z,
        inv_w,
        scale(vertex.varyings, inv_w),
    };
}

float edge(const Vec2& a, const Vec2& b, const Vec2& p) {
    return signed_area_twice(a, b, p);
}

bool is_top_left(const Vec2& a, const Vec2& b) {
    const float dy = b.y - a.y;
    const float dx = b.x - a.x;
    return (dy < 0.0F) || (nearly_equal(dy, 0.0F) && dx > 0.0F);
}

bool edge_accept(float value, bool top_left) {
    if (value > kEpsilon) {
        return true;
    }
    return std::fabs(value) <= kEpsilon && top_left;
}

Vec3 interpolate_color(const std::array<ScreenVertex, 3>& v, const Vec3& bary, float reciprocal_w, const ColorBinding& binding) {
    VaryingPack result;
    result.count = v[0].varyings_over_w.count;
    for (std::size_t i = 0; i < result.count; ++i) {
        const float numerator = v[0].varyings_over_w.values[i] * bary.x
            + v[1].varyings_over_w.values[i] * bary.y
            + v[2].varyings_over_w.values[i] * bary.z;
        result.values[i] = numerator / reciprocal_w;
    }
    return {result.values[binding.red], result.values[binding.green], result.values[binding.blue]};
}

void rasterize_screen_triangle(Framebuffer& framebuffer, std::array<ScreenVertex, 3> v, const ColorBinding& binding) {
    float area = edge(v[0].position, v[1].position, v[2].position);
    if (!std::isfinite(area) || std::fabs(area) <= kEpsilon) {
        return;
    }
    if (area < 0.0F) {
        std::swap(v[1], v[2]);
        area = -area;
    }

    const float min_x_f = std::min({v[0].position.x, v[1].position.x, v[2].position.x});
    const float max_x_f = std::max({v[0].position.x, v[1].position.x, v[2].position.x});
    const float min_y_f = std::min({v[0].position.y, v[1].position.y, v[2].position.y});
    const float max_y_f = std::max({v[0].position.y, v[1].position.y, v[2].position.y});

    const int width = static_cast<int>(framebuffer.width());
    const int height = static_cast<int>(framebuffer.height());
    const int min_x = std::max(0, static_cast<int>(std::floor(min_x_f)));
    const int max_x = std::min(width - 1, static_cast<int>(std::ceil(max_x_f)));
    const int min_y = std::max(0, static_cast<int>(std::floor(min_y_f)));
    const int max_y = std::min(height - 1, static_cast<int>(std::ceil(max_y_f)));

    if (min_x > max_x || min_y > max_y) {
        return;
    }

    const bool tl0 = is_top_left(v[1].position, v[2].position);
    const bool tl1 = is_top_left(v[2].position, v[0].position);
    const bool tl2 = is_top_left(v[0].position, v[1].position);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const Vec2 p{static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F};
            const float e0 = edge(v[1].position, v[2].position, p);
            const float e1 = edge(v[2].position, v[0].position, p);
            const float e2 = edge(v[0].position, v[1].position, p);
            if (!edge_accept(e0, tl0) || !edge_accept(e1, tl1) || !edge_accept(e2, tl2)) {
                continue;
            }

            const Vec3 bary{e0 / area, e1 / area, e2 / area};
            const float ndc_z = bary.x * v[0].ndc_z + bary.y * v[1].ndc_z + bary.z * v[2].ndc_z;
            const float depth = ndc_z * 0.5F + 0.5F;
            if (depth < 0.0F || depth > 1.0F || !std::isfinite(depth)) {
                continue;
            }

            const float reciprocal_w = bary.x * v[0].inv_w + bary.y * v[1].inv_w + bary.z * v[2].inv_w;
            if (std::fabs(reciprocal_w) <= kEpsilon || !std::isfinite(reciprocal_w)) {
                continue;
            }
            const Vec3 color = interpolate_color(v, bary, reciprocal_w, binding);
            framebuffer.depth_test_and_write(static_cast<std::size_t>(x), static_cast<std::size_t>(y), depth, color);
        }
    }
}

void validate_mesh(const Mesh& mesh, const ColorBinding& binding) {
    for (const TriangleIndices& triangle : mesh.triangles) {
        for (const std::uint32_t index : triangle) {
            if (static_cast<std::size_t>(index) >= mesh.vertices.size()) {
                throw std::out_of_range("mesh triangle index out of range");
            }
        }
    }
    if (mesh.vertices.empty()) {
        return;
    }
    const std::size_t count = mesh.vertices.front().varyings.count;
    validate_pack(mesh.vertices.front().varyings);
    validate_binding(binding, count);
    for (const Vertex& vertex : mesh.vertices) {
        validate_pack(vertex.varyings);
        if (vertex.varyings.count != count) {
            throw std::invalid_argument("mesh vertices must use the same varying count");
        }
    }
}

Triangle assemble_triangle(const Mesh& mesh, const TriangleIndices& indices) {
    return Triangle{mesh.vertices[indices[0]], mesh.vertices[indices[1]], mesh.vertices[indices[2]]};
}

}  // namespace

void Rasterizer::draw_triangle(const Triangle& triangle, const Mat4& model, const Mat4& view, const Mat4& projection) {
    draw_triangle(triangle, projection * view * model);
}

void Rasterizer::draw_triangle(const Triangle& triangle, const Mat4& mvp) {
    validate_triangle_varyings(triangle, color_binding_);
    std::array<ClipVertex, 3> clip{};
    for (std::size_t i = 0; i < triangle.size(); ++i) {
        clip[i] = {
            mvp * Vec4{triangle[i].position.x, triangle[i].position.y, triangle[i].position.z, 1.0F},
            triangle[i].varyings,
        };
    }

    const std::vector<ClipVertex> polygon = clip_triangle(clip);
    if (polygon.size() < 3U) {
        return;
    }

    for (std::size_t i = 1; i + 1U < polygon.size(); ++i) {
        const auto a = to_screen(polygon[0], framebuffer_.width(), framebuffer_.height());
        const auto b = to_screen(polygon[i], framebuffer_.width(), framebuffer_.height());
        const auto c = to_screen(polygon[i + 1U], framebuffer_.width(), framebuffer_.height());
        if (!a || !b || !c) {
            continue;
        }
        rasterize_screen_triangle(framebuffer_, {*a, *b, *c}, color_binding_);
    }
}

void Rasterizer::draw_mesh(const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& projection) {
    draw_mesh(mesh, projection * view * model);
}

void Rasterizer::draw_mesh(const Mesh& mesh, const Mat4& mvp) {
    validate_mesh(mesh, color_binding_);
    for (const TriangleIndices& indices : mesh.triangles) {
        draw_triangle(assemble_triangle(mesh, indices), mvp);
    }
}

}  // namespace tiny_renderer
