#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include "tiny_renderer/fragment_program.hpp"
#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/material.hpp"
#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/point_shadow.hpp"
#include "tiny_renderer/shadow.hpp"
#include "tiny_renderer/spot_shadow.hpp"
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
    const Texture2D* opacity_texture{nullptr};
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
    std::optional<RasterRect> viewport{};
    std::optional<RasterRect> scissor{};
};

struct AlphaToCoverageState {
    bool enabled{false};
};

struct AlphaTestState {
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
    Vec3 viewer_position{0.0F, 0.0F, 0.0F};
    // Linear teaching-space RGB multiplier shared by this light's ambient,
    // diffuse, and specular contributions. White preserves legacy behavior.
    Vec3 color{1.0F, 1.0F, 1.0F};
};

struct PointLight {
    bool enabled{false};
    NormalBinding normal{};
    Vec3 position{0.0F, 0.0F, 1.0F};
    float ambient{0.0F};
    float diffuse{1.0F};
    Vec3 viewer_position{0.0F, 0.0F, 0.0F};
    float linear_attenuation{0.0F};
    float quadratic_attenuation{0.0F};
    Vec3 color{1.0F, 1.0F, 1.0F};
};

struct SpotLight {
    bool enabled{false};
    NormalBinding normal{};
    Vec3 position{0.0F, 0.0F, 1.0F};
    // Direction points outward from the light toward the center of the cone.
    Vec3 direction{0.0F, 0.0F, -1.0F};
    float ambient{0.0F};
    float diffuse{1.0F};
    Vec3 viewer_position{0.0F, 0.0F, 0.0F};
    float linear_attenuation{0.0F};
    float quadratic_attenuation{0.0F};
    // Cone falloff is linear in cosine space. A fragment is fully lit at or
    // above inner_cone_cos, fully outside at or below outer_cone_cos, and
    // interpolated between them. Validation requires inner > outer.
    float inner_cone_cos{0.9F};
    float outer_cone_cos{0.8F};
    Vec3 color{1.0F, 1.0F, 1.0F};
};

constexpr std::size_t kMaxFixedLights = 4U;

enum class FixedLightType {
    Directional,
    Point,
    Spot,
};

struct FixedLight {
    FixedLightType type{FixedLightType::Directional};
    DirectionalLight directional{};
    PointLight point{};
    SpotLight spot{};
};

struct FixedLightCollection {
    std::array<FixedLight, kMaxFixedLights> lights{};
    std::size_t count{0U};
    std::optional<std::size_t> shadowed_directional_index{};
    std::optional<std::size_t> shadowed_point_index{};
    std::optional<std::size_t> shadowed_spot_index{};
    SpotShadowState spot_shadow_state{};
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
        VertexProgramPtr vertex_program = {},
        PointLight point_light = {},
        FixedLightCollection fixed_lights = {},
        PointShadowState point_shadow_state = {})
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
          vertex_program_(std::move(vertex_program)),
          point_light_(point_light),
          fixed_lights_(std::move(fixed_lights)),
          point_shadow_state_(std::move(point_shadow_state)) {}

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
    PointLight point_light_;
    FixedLightCollection fixed_lights_;
    PointShadowState point_shadow_state_;
};

}  // namespace tiny_renderer