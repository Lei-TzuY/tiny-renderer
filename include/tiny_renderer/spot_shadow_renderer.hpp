#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/spot_shadow.hpp"

namespace tiny_renderer {

struct SpotShadowMapOptions {
    std::size_t size{0U};
    float near_plane{0.1F};
    float far_plane{100.0F};
    CullMode cull_mode{CullMode::None};
    FrontFace front_face{FrontFace::CounterClockwise};
};

// Captures one square perspective depth view whose full field of view is
// twice the spotlight outer-cone half angle. The complete spotlight state is
// validated first; shadow capture additionally requires 0 < outer_cos < 1.
[[nodiscard]] std::shared_ptr<const SpotShadowMap> render_spot_shadow_map(
    std::span<const PreparedModelListEntry> entries,
    const SpotLight& light,
    SpotShadowMapOptions options);

}  // namespace tiny_renderer
