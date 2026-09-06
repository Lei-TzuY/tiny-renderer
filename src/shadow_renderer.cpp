#include "tiny_renderer/shadow_renderer.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>

#include "rasterizer_validation.hpp"

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

TextureBinding shadow_texture_binding(
    const MaterialDraw& draw,
    const ModelRenderOptions& options) {
    TextureBinding binding{};
    binding.u_channel = options.u_channel;
    binding.v_channel = options.v_channel;
    binding.sampler = options.sampler;
    binding.opacity_texture = draw.opacity_texture.get();
    return binding;
}

BlendState depth_only_blend_state() {
    BlendState state{};
    state.write_mask = {false, false, false};
    return state;
}

void preflight_shadow_entries(
    const Framebuffer& framebuffer,
    std::span<const PreparedModelListEntry> entries,
    CullMode cull_mode,
    FrontFace front_face) {
    const BlendState depth_only_blend = depth_only_blend_state();
    for (const PreparedModelListEntry& entry : entries) {
        if (entry.prepared == nullptr) {
            throw std::invalid_argument("shadow pass entry requires a prepared model");
        }
        const ModelAsset& asset = entry.prepared->asset();
        const ModelRenderOptions& options = entry.prepared->options();
        detail::validate_alpha_test_state(options.alpha_test_state);
        for (const MaterialDraw& draw : asset.draws) {
            detail::preflight_mesh_range_submission(
                framebuffer,
                asset.mesh,
                draw.range,
                {},
                shadow_texture_binding(draw, options),
                {},
                draw.material,
                BaseColorSource::ConstantWhite,
                cull_mode,
                front_face,
                DepthState{DepthCompare::Less, true},
                {},
                {},
                depth_only_blend,
                {},
                {},
                nullptr,
                true);
        }
    }
}

void draw_shadow_entries(
    Framebuffer& framebuffer,
    std::span<const PreparedModelListEntry> entries,
    const Mat4& light_view_projection,
    CullMode cull_mode,
    FrontFace front_face) {
    const BlendState depth_only_blend = depth_only_blend_state();
    for (const PreparedModelListEntry& entry : entries) {
        const ModelAsset& asset = entry.prepared->asset();
        const ModelRenderOptions& options = entry.prepared->options();
        const Mat4 light_mvp = light_view_projection * entry.model;
        for (const MaterialDraw& draw : asset.draws) {
            Rasterizer rasterizer(
                framebuffer,
                {},
                shadow_texture_binding(draw, options),
                {},
                draw.material,
                BaseColorSource::ConstantWhite,
                cull_mode,
                front_face,
                DepthState{DepthCompare::Less, true},
                {},
                {},
                depth_only_blend,
                {},
                {},
                options.alpha_test_state);
            rasterizer.draw_mesh_range(asset.mesh, draw.range, light_mvp);
        }
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

    Framebuffer framebuffer{options.width, options.height, SampleCount::One};
    preflight_shadow_entries(
        framebuffer,
        entries,
        options.cull_mode,
        options.front_face);
    framebuffer.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
    draw_shadow_entries(
        framebuffer,
        entries,
        light_view_projection,
        options.cull_mode,
        options.front_face);

    return std::make_shared<const DepthTexture2D>(capture_depth_texture(framebuffer));
}

}  // namespace tiny_renderer
