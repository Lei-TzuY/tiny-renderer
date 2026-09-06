#include "tiny_renderer/spot_shadow.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace tiny_renderer {
namespace {

bool finite_vec3(const Vec3& value) {
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

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

}  // namespace

SpotShadowMap::SpotShadowMap(
    Vec3 light_position,
    Vec3 light_direction,
    float outer_cone_cos,
    Mat4 light_view_projection,
    DepthTexture2D depth_texture)
    : light_position_(light_position),
      outer_cone_cos_(outer_cone_cos),
      light_view_projection_(light_view_projection),
      depth_texture_(std::move(depth_texture)) {
    if (!finite_vec3(light_position_)) {
        throw std::invalid_argument("spot shadow capture position must be finite");
    }
    if (!finite_vec3(light_direction) || length(light_direction) <= kEpsilon) {
        throw std::invalid_argument("spot shadow capture direction must be finite and non-zero");
    }
    light_direction_ = normalize(light_direction);
    if (!std::isfinite(outer_cone_cos_) || outer_cone_cos_ <= 0.0F || outer_cone_cos_ >= 1.0F) {
        throw std::invalid_argument(
            "spot shadow outer cone cosine must be finite and strictly within (0, 1)");
    }
    if (!finite_mat4(light_view_projection_)) {
        throw std::invalid_argument("spot shadow view-projection transform must be finite");
    }
}

}  // namespace tiny_renderer
