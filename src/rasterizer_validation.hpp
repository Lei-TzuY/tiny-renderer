#pragma once

#include <cmath>
#include <optional>
#include <stdexcept>

#include "tiny_renderer/rasterizer.hpp"

namespace tiny_renderer::detail {

struct ResolvedViewportState {
    RasterRect viewport;
    std::optional<RasterRect> scissor;
};

void validate_face_culling(CullMode cull_mode, FrontFace front_face);
void validate_viewport_state_definition(const ViewportState& state);
void validate_alpha_to_coverage_target(
    const Framebuffer& framebuffer,
    const AlphaToCoverageState& state);
void validate_fixed_lighting_definition(
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const FixedLightCollection& fixed_lights);
void validate_shadow_state_definition(
    const ShadowState& state,
    const DirectionalLight& directional_light,
    const FixedLightCollection& fixed_lights);
[[nodiscard]] bool fixed_lighting_enabled(
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const FixedLightCollection& fixed_lights);
[[nodiscard]] const NormalBinding* active_normal_binding(
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const FixedLightCollection& fixed_lights);
[[nodiscard]] bool fixed_lighting_world_position_required(
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const FixedLightCollection& fixed_lights,
    const MaterialState& material);

inline void validate_alpha_test_state(const AlphaTestState& state) {
    if (!std::isfinite(state.threshold)
        || state.threshold < 0.0F
        || state.threshold > 1.0F) {
        throw std::invalid_argument("alpha test threshold must be finite and within [0, 1]");
    }
}

[[nodiscard]] ResolvedViewportState resolve_viewport_state(
    const Framebuffer& framebuffer,
    const ViewportState& state);

void preflight_mesh_range_submission(
    const Framebuffer& framebuffer,
    const Mesh& mesh,
    DrawRange range,
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const FixedLightCollection& fixed_lights,
    const MaterialState& material_state,
    BaseColorSource base_color_source,
    CullMode cull_mode,
    FrontFace front_face,
    const DepthState& depth_state,
    const ViewportState& viewport_state,
    const StencilState& stencil_state,
    const BlendState& blend_state,
    const AlphaToCoverageState& alpha_to_coverage_state,
    const ShadowState& shadow_state,
    const Mat4* model,
    bool mvp_only);

}  // namespace tiny_renderer::detail
