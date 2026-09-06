#pragma once

#include <memory>

#include "tiny_renderer/math.hpp"
#include "tiny_renderer/shadow.hpp"

namespace tiny_renderer {

// Immutable 2D spotlight depth resource. The capture identity is retained so
// later fixed-light submission can reject a map produced for a different cone.
class SpotShadowMap {
public:
    SpotShadowMap(
        Vec3 light_position,
        Vec3 light_direction,
        float outer_cone_cos,
        Mat4 light_view_projection,
        DepthTexture2D depth_texture);

    [[nodiscard]] const Vec3& light_position() const noexcept { return light_position_; }
    [[nodiscard]] const Vec3& light_direction() const noexcept { return light_direction_; }
    [[nodiscard]] float outer_cone_cos() const noexcept { return outer_cone_cos_; }
    [[nodiscard]] const Mat4& light_view_projection() const noexcept {
        return light_view_projection_;
    }
    [[nodiscard]] const DepthTexture2D& depth_texture() const noexcept {
        return depth_texture_;
    }

private:
    Vec3 light_position_{};
    Vec3 light_direction_{0.0F, 0.0F, -1.0F};
    float outer_cone_cos_{0.8F};
    Mat4 light_view_projection_{Mat4::identity()};
    DepthTexture2D depth_texture_;
};

struct SpotShadowState {
    bool enabled{false};
    std::shared_ptr<const SpotShadowMap> map;
    float bias{0.0F};
    ShadowSamplingMode sampling{ShadowSamplingMode::Hard};
};

}  // namespace tiny_renderer
