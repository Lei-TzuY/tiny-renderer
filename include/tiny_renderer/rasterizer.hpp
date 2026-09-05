#pragma once

#include <cstddef>
#include <optional>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

struct ColorBinding {
    std::size_t red{0U};
    std::size_t green{1U};
    std::size_t blue{2U};
};

struct TextureBinding {
    const Texture2D* texture{nullptr};
    std::size_t u_channel{0U};
    std::size_t v_channel{1U};
    SamplerState sampler{};
};

[[nodiscard]] float signed_area_twice(const Vec2& a, const Vec2& b, const Vec2& c);
[[nodiscard]] std::optional<Vec3> barycentric_coordinates(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& p);
[[nodiscard]] bool barycentric_inside(const Vec3& barycentric, float epsilon = 1.0e-6F);

class Rasterizer {
public:
    explicit Rasterizer(
        Framebuffer& framebuffer,
        ColorBinding color_binding = {},
        TextureBinding texture_binding = {})
        : framebuffer_(framebuffer), color_binding_(color_binding), texture_binding_(texture_binding) {}

    void draw_triangle(const Triangle& triangle, const Mat4& model, const Mat4& view, const Mat4& projection);
    void draw_triangle(const Triangle& triangle, const Mat4& mvp);
    void draw_mesh(const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& projection);
    void draw_mesh(const Mesh& mesh, const Mat4& mvp);

private:
    Framebuffer& framebuffer_;
    ColorBinding color_binding_;
    TextureBinding texture_binding_;
};

}  // namespace tiny_renderer
