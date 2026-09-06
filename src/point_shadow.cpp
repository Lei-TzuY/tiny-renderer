#include "tiny_renderer/point_shadow.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
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

std::size_t face_index(CubemapFace face) {
    switch (face) {
        case CubemapFace::PositiveX: return 0U;
        case CubemapFace::NegativeX: return 1U;
        case CubemapFace::PositiveY: return 2U;
        case CubemapFace::NegativeY: return 3U;
        case CubemapFace::PositiveZ: return 4U;
        case CubemapFace::NegativeZ: return 5U;
    }
    throw std::invalid_argument("unknown cubemap face");
}

}  // namespace

CubemapFace cubemap_face_for_direction(const Vec3& direction) {
    if (!finite_vec3(direction)) {
        throw std::invalid_argument("cubemap direction must be finite");
    }

    const float ax = std::fabs(direction.x);
    const float ay = std::fabs(direction.y);
    const float az = std::fabs(direction.z);
    const float dominant = std::max({ax, ay, az});
    if (dominant <= kEpsilon) {
        throw std::invalid_argument("cubemap direction must be non-zero");
    }

    if (ax >= ay && ax >= az) {
        return direction.x >= 0.0F ? CubemapFace::PositiveX : CubemapFace::NegativeX;
    }
    if (ay >= az) {
        return direction.y >= 0.0F ? CubemapFace::PositiveY : CubemapFace::NegativeY;
    }
    return direction.z >= 0.0F ? CubemapFace::PositiveZ : CubemapFace::NegativeZ;
}

DepthCubemap::DepthCubemap(
    std::size_t size,
    Vec3 light_position,
    std::array<Mat4, kCubemapFaceCount> face_view_projections,
    std::vector<float> depths)
    : size_(size),
      light_position_(light_position),
      face_view_projections_(std::move(face_view_projections)),
      depths_(std::move(depths)) {
    if (size_ == 0U) {
        throw std::invalid_argument("depth cubemap face size must be non-zero");
    }
    if (!finite_vec3(light_position_)) {
        throw std::invalid_argument("depth cubemap light position must be finite");
    }
    if (size_ > std::numeric_limits<std::size_t>::max() / size_) {
        throw std::overflow_error("depth cubemap face storage size overflows size_t");
    }
    const std::size_t face_pixels = size_ * size_;
    if (face_pixels > std::numeric_limits<std::size_t>::max() / kCubemapFaceCount) {
        throw std::overflow_error("depth cubemap storage size overflows size_t");
    }
    const std::size_t expected = face_pixels * kCubemapFaceCount;
    if (depths_.size() != expected) {
        throw std::invalid_argument("depth cubemap storage does not match six square faces");
    }
    for (const Mat4& matrix : face_view_projections_) {
        if (!finite_mat4(matrix)) {
            throw std::invalid_argument("depth cubemap face transform must be finite");
        }
    }
    for (const float depth : depths_) {
        if (!std::isfinite(depth) || depth < 0.0F || depth > 1.0F) {
            throw std::invalid_argument("depth cubemap values must be finite and within [0, 1]");
        }
    }
}

std::size_t DepthCubemap::storage_index(
    CubemapFace face,
    std::size_t x,
    std::size_t y) const {
    if (x >= size_ || y >= size_) {
        throw std::out_of_range("depth cubemap texel coordinate out of range");
    }
    const std::size_t face_pixels = size_ * size_;
    return face_index(face) * face_pixels + y * size_ + x;
}

float DepthCubemap::depth_at(CubemapFace face, std::size_t x, std::size_t y) const {
    return depths_[storage_index(face, x, y)];
}

const Mat4& DepthCubemap::face_view_projection(CubemapFace face) const {
    return face_view_projections_[face_index(face)];
}

}  // namespace tiny_renderer
