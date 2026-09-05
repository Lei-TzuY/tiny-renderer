#pragma once

#include <optional>

#include "tiny_renderer/rasterizer.hpp"

namespace tiny_renderer::detail {

struct ResolvedViewportState {
    RasterRect viewport;
    std::optional<RasterRect> scissor;
};

void validate_face_culling(CullMode cull_mode, FrontFace front_face);
void validate_viewport_state_definition(const ViewportState& state);
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
    const MaterialState& material_state,
    BaseColorSource base_color_source,
    CullMode cull_mode,
    FrontFace front_face,
    const DepthState& depth_state,
    const ViewportState& viewport_state,
    const Mat4* model,
    bool mvp_only);

}  // namespace tiny_renderer::detail
