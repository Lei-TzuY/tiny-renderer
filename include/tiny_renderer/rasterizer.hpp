#pragma once

#include <cstddef>
#include <optional>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/material.hpp"
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

enum class BaseColorSource {
    Auto,
    VaryingColor,
    Texture,
    ConstantWhite,
};

enum class CullMode {
    None,
    Back,
    Front,
};

// Front-face winding is defined in normalized device coordinates after
// homogeneous clipping and perspective divide, before the framebuffer's
// top-left-origin viewport transform can invert Y orientation.
enum class FrontFace {
    CounterClockwise,
    Clockwise,
};

struct NormalBinding {
    std::size_t x{0U};
    std::size_t y{1U};
    std::size_t z{2U};
};

struct DirectionalLight {
    bool enabled{false};
    NormalBinding normal{};
    Vec3 direction_to_light{0.0F, 0.0F, 1.0F};
    float ambient{0.0F};
    float diffuse{1.0F};
};

[[nodiscard]] float signed_area_twice(const Vec2& a, const Vec2& b, const Vec2& c);
[[nodiscard]] std::optional<Vec3> barycentric_coordinates(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& p);
[[nodiscard]] bool barycentric_inside(const Vec3& barycentric, float epsilon = 1.0e-6F);

class Rasterizer {
public:
    explicit Rasterizer(
        Framebuffer& framebuffer,
        ColorBinding color_binding = {},
        TextureBinding texture_binding = {},
        DirectionalLight directional_light = {},
        MaterialState material_state = {},
        BaseColorSource base_color_source = BaseColorSource::Auto,
        CullMode cull_mode = CullMode::None,
        FrontFace front_face = FrontFace::CounterClockwise)
        : framebuffer_(framebuffer),
          color_binding_(color_binding),
          texture_binding_(texture_binding),
          directional_light_(directional_light),
          material_state_(material_state),
          base_color_source_(base_color_source),
          cull_mode_(cull_mode),
          front_face_(front_face) {}

    void draw_triangle(const Triangle& triangle, const Mat4& model, const Mat4& view, const Mat4& projection);
    void draw_triangle(const Triangle& triangle, const Mat4& mvp);
    void draw_mesh(const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& projection);
    void draw_mesh(const Mesh& mesh, const Mat4& mvp);
    void draw_mesh_range(
        const Mesh& mesh,
        DrawRange range,
        const Mat4& model,
        const Mat4& view,
        const Mat4& projection);
    void draw_mesh_range(const Mesh& mesh, DrawRange range, const Mat4& mvp);

private:
    Framebuffer& framebuffer_;
    ColorBinding color_binding_;
    TextureBinding texture_binding_;
    DirectionalLight directional_light_;
    MaterialState material_state_;
    BaseColorSource base_color_source_;
    CullMode cull_mode_;
    FrontFace front_face_;
};

}  // namespace tiny_renderer
