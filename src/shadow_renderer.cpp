#include "tiny_renderer/shadow_renderer.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>

namespace tiny_renderer {
namespace {

bool finite_mat4(const Mat4& matrix) {
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            if (!std::isfinite(matrix(row, column))) {
                return false;
            }
        }
    }
    return true;
}

void validate_culling(CullMode cull_mode, FrontFace front_face) {
    switch (cull_mode) {
        case CullMode::None:
        case CullMode::Back:
        case CullMode::Front:
            break;
        default:
            throw std::invalid_argument("shadow pass uses an unknown cull mode");
    }
    switch (front_face) {
        case FrontFace::CounterClockwise:
        case FrontFace::Clockwise:
            return;
        default:
            throw std::invalid_argument("shadow pass uses an unknown front-face winding");
    }
}

}  // namespace

std::shared_ptr<const DepthTexture2D> render_directional_shadow_map(
    std::span<const PreparedModelListEntry> entries,
    const Mat4& light_view_projection,
    DirectionalShadowMapOptions options) {
    if (options.width == 0U || options.height == 0U) {
        throw std::invalid_argument("shadow map dimensions must be non-zero");
    }
    if (!finite_mat4(light_view_projection)) {
        throw std::invalid_argument("shadow light view-projection transform must be finite");
    }
    validate_culling(options.cull_mode, options.front_face);
    for (const PreparedModelListEntry& entry : entries) {
        if (entry.prepared == nullptr) {
            throw std::invalid_argument("shadow pass entry requires a prepared model");
        }
    }

    Framebuffer framebuffer{options.width, options.height, SampleCount::One};
    framebuffer.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);

    BlendState depth_only_blend{};
    depth_only_blend.write_mask = {false, false, false};

    for (const PreparedModelListEntry& entry : entries) {
        Rasterizer rasterizer(
            framebuffer,
            {},
            {},
            {},
            {},
            BaseColorSource::ConstantWhite,
            options.cull_mode,
            options.front_face,
            DepthState{DepthCompare::Less, true},
            {},
            {},
            depth_only_blend,
            {});
        rasterizer.draw_mesh(
            entry.prepared->asset().mesh,
            light_view_projection * entry.model);
    }

    return std::make_shared<const DepthTexture2D>(capture_depth_texture(framebuffer));
}

}  // namespace tiny_renderer
