#pragma once

#include <array>
#include <optional>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

struct Vertex {
    Vec3 position;
    Vec3 color;
};

using Triangle = std::array<Vertex, 3>;

[[nodiscard]] float signed_area_twice(const Vec2& a, const Vec2& b, const Vec2& c);
[[nodiscard]] std::optional<Vec3> barycentric_coordinates(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& p);
[[nodiscard]] bool barycentric_inside(const Vec3& barycentric, float epsilon = 1.0e-6F);

class Rasterizer {
public:
    explicit Rasterizer(Framebuffer& framebuffer) : framebuffer_(framebuffer) {}

    void draw_triangle(const Triangle& triangle, const Mat4& model, const Mat4& view, const Mat4& projection);
    void draw_triangle(const Triangle& triangle, const Mat4& mvp);

private:
    Framebuffer& framebuffer_;
};

}  // namespace tiny_renderer
