#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/shadow.hpp"

namespace tiny_renderer {

struct DirectionalShadowMapOptions {
    std::size_t width{0U};
    std::size_t height{0U};
    CullMode cull_mode{CullMode::None};
    FrontFace front_face{FrontFace::CounterClockwise};
};

[[nodiscard]] std::shared_ptr<const DepthTexture2D> render_directional_shadow_map(
    std::span<const PreparedModelListEntry> entries,
    const Mat4& light_view_projection,
    DirectionalShadowMapOptions options);

}  // namespace tiny_renderer
