#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/point_shadow.hpp"

namespace tiny_renderer {

struct PointShadowCubemapOptions {
    std::size_t size{0U};
    float near_plane{0.1F};
    float far_plane{100.0F};
    CullMode cull_mode{CullMode::None};
    FrontFace front_face{FrontFace::CounterClockwise};
};

// Captures six canonical 90-degree depth views in CubemapFace enum order.
// Vertex-program geometry is prepared once and reused for all six faces.
[[nodiscard]] std::shared_ptr<const DepthCubemap> render_point_shadow_cubemap(
    std::span<const PreparedModelListEntry> entries,
    const Vec3& light_position,
    PointShadowCubemapOptions options);

}  // namespace tiny_renderer
