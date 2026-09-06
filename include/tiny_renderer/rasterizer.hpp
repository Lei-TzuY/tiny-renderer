#pragma once

#include <cstddef>
#include <optional>
#include <utility>

#include "tiny_renderer/fragment_program.hpp"
#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/material.hpp"
#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/shadow.hpp"
#include "tiny_renderer/texture.hpp"
#include "tiny_renderer/vertex_program.hpp"

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
    // Optional opacity-map role sharing the same UV channels and sampler.
    // This keeps texture-coordinate ownership singular while allowing an
    // opacity texture even when RGB base color is not texture sourced.
    const Texture2D* opacity_texture{nullptr};
    // Optional tangent-space normal map. It shares the same UV channels and
    // sampler and is consumed only by the existing directional-light stage.
    const Texture2D* normal_texture{nullptr};
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

struct RasterRect {
    std::size_t x{0U};
    std::size_t y{0U};
    std::size_t width{0U};
    std::size_t height{0U};
};

struct ViewportState {
    // null viewport means the complete framebuffer. A present viewport must
    // have non-zero extent and fit fully inside the target.
    std::optional<RasterRect> viewport{};
    // null scissor disables scissoring. A zero-extent present scissor is a
    // valid deterministic empty clip; non-empty scissors use half-open bounds.
    std::optional<RasterRect> scissor{};
};

struct AlphaToCoverageState {
    // Alpha-to-coverage is deliberately a raster coverage state rather than a
    // framebuffer ownership state. When enabled it requires a 4x target.
    bool enabled{false};
};

struct AlphaTestState {
    // A fragment/sample survives exactly when opacity >= threshold. The test
    // runs after opacity interpolation/sampling and before framebuffer
    // ownership or alpha-to-coverage.
    bool enabled{false};
    float threshold{0.5F};
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
    // Explicit world-space eye position for view-dependent specular lighting.
    // It is inert while the bound material has zero specular reflectance.
    Vec3 viewer_position{0.0F, 0.0F, 0.0F};
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
        FrontFace front_face = FrontFace::CounterClockwise,
        DepthState depth_state = {},
        ViewportState viewport_state = {},
        StencilState stencil_state = {},
        BlendState blend_state = {},
        AlphaToCoverageState alpha_to_coverage_state = {},
        ShadowState shadow_state = {},
        AlphaTestState alpha_test_state = {},
        FragmentProgramPtr fragment_program = {},
        VertexProgramPtr vertex_program = {})
        : framebuffer_(framebuffer),
          color_binding_(color_binding),
          texture_binding_(texture_binding),
          directional_light_(directional_light),
          material_state_(material_state),
          base_color_source_(base_color_source),
          cull_mode_(cull_mode),
          front_face_(front_face),
          depth_state_(depth_state),
          viewport_state_(viewport_state),
          stencil_state_(stencil_state),
          blend_state_(blend_state),
          alpha_to_coverage_state_(alpha_to_coverage_state),
          shadow_state_(shadow_state),
          alpha_test_state_(alpha_test_state),
          fragment_program_(std::move(fragment_program)),
          vertex_program_(std::move(vertex_program)) {}

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
    DepthState depth_state_;
    ViewportState viewport_state_;
    StencilState stencil_state_;
    BlendState blend_state_;
    AlphaToCoverageState alpha_to_coverage_state_;
    ShadowState shadow_state_;
    AlphaTestState alpha_test_state_;
    FragmentProgramPtr fragment_program_;
    VertexProgramPtr vertex_program_;
};

}  // namespace tiny_renderer
