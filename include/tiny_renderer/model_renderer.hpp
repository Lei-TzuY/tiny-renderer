#pragma once

#include <cstddef>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model.hpp"
#include "tiny_renderer/rasterizer.hpp"

namespace tiny_renderer {

struct ModelRenderOptions {
    std::size_t u_channel{0U};
    std::size_t v_channel{1U};
    SamplerState sampler{};
    DirectionalLight directional_light{};
};

void draw_model_asset(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection,
    ModelRenderOptions options = {});

void draw_model_asset(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const Mat4& mvp,
    ModelRenderOptions options = {});

}  // namespace tiny_renderer
