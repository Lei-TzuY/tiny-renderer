#pragma once

#include "tiny_renderer/rasterizer.hpp"

namespace tiny_renderer::detail {

void preflight_mesh_range_submission(
    const Framebuffer& framebuffer,
    const Mesh& mesh,
    DrawRange range,
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    const DirectionalLight& directional_light,
    const MaterialState& material_state,
    BaseColorSource base_color_source,
    const Mat4* model,
    bool mvp_only);

}  // namespace tiny_renderer::detail
