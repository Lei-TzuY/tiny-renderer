#include "tiny_renderer/shadow.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

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

void validate_sampling(ShadowSamplingMode sampling) {
    switch (sampling) {
        case ShadowSamplingMode::Hard:
        case ShadowSamplingMode::Pcf3x3:
            return;
    }
    throw std::invalid_argument("directional shadow cascade uses an unknown sampling mode");
}

void validate_camera_view(const Mat4& matrix) {
    if (!finite_mat4(matrix)) {
        throw std::invalid_argument("directional shadow cascade camera view must be finite");
    }
    if (matrix(3U, 0U) != 0.0F
        || matrix(3U, 1U) != 0.0F
        || matrix(3U, 2U) != 0.0F
        || matrix(3U, 3U) != 1.0F) {
        throw std::invalid_argument(
            "directional shadow cascade camera view must be an affine view transform");
    }
}

}  // namespace

DepthTexture2D::DepthTexture2D(
    std::size_t width,
    std::size_t height,
    std::vector<float> depths)
    : width_(width), height_(height), depths_(std::move(depths)) {
    if (width_ == 0U || height_ == 0U) {
        throw std::invalid_argument("depth texture dimensions must be non-zero");
    }
    if (width_ > std::numeric_limits<std::size_t>::max() / height_) {
        throw std::overflow_error("depth texture dimensions overflow storage size");
    }
    const std::size_t expected = width_ * height_;
    if (depths_.size() != expected) {
        throw std::invalid_argument("depth texture storage size does not match dimensions");
    }
    for (const float depth : depths_) {
        if (!std::isfinite(depth) || depth < 0.0F || depth > 1.0F) {
            throw std::invalid_argument("depth texture values must be finite and normalized to [0, 1]");
        }
    }
}

float DepthTexture2D::depth_at(std::size_t x, std::size_t y) const {
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("depth texture coordinate out of range");
    }
    return depths_[y * width_ + x];
}

CascadedDirectionalShadowMap::CascadedDirectionalShadowMap(
    Mat4 camera_view,
    std::array<DirectionalShadowCascade, kMaxDirectionalShadowCascades> cascades,
    std::size_t count)
    : camera_view_(camera_view), cascades_(std::move(cascades)), count_(count) {
    validate_camera_view(camera_view_);
    if (count_ == 0U || count_ > kMaxDirectionalShadowCascades) {
        throw std::invalid_argument(
            "directional shadow cascade count must be within the fixed capacity");
    }

    float previous_split = 0.0F;
    for (std::size_t index = 0U; index < count_; ++index) {
        const DirectionalShadowCascade& cascade = cascades_[index];
        if (!std::isfinite(cascade.split_view_depth)
            || cascade.split_view_depth <= previous_split) {
            throw std::invalid_argument(
                "directional shadow cascade splits must be finite, positive, and strictly increasing");
        }
        if (!cascade.map) {
            throw std::invalid_argument("directional shadow cascade requires a depth texture");
        }
        if (!finite_mat4(cascade.light_view_projection)) {
            throw std::invalid_argument(
                "directional shadow cascade view-projection must be finite");
        }
        if (!std::isfinite(cascade.bias) || cascade.bias < 0.0F) {
            throw std::invalid_argument(
                "directional shadow cascade bias must be finite and non-negative");
        }
        validate_sampling(cascade.sampling);
        previous_split = cascade.split_view_depth;
    }
}

const DirectionalShadowCascade& CascadedDirectionalShadowMap::cascade(
    std::size_t index) const {
    if (index >= count_) {
        throw std::out_of_range("directional shadow cascade index out of range");
    }
    return cascades_[index];
}

const DirectionalShadowCascade* CascadedDirectionalShadowMap::cascade_for_view_depth(
    float view_depth) const noexcept {
    if (!std::isfinite(view_depth) || view_depth < 0.0F) {
        return nullptr;
    }
    for (std::size_t index = 0U; index < count_; ++index) {
        if (view_depth < cascades_[index].split_view_depth) {
            return &cascades_[index];
        }
    }
    return nullptr;
}

float CascadedDirectionalShadowMap::view_depth_for_world_position(
    const Vec3& world_position) const {
    if (!std::isfinite(world_position.x)
        || !std::isfinite(world_position.y)
        || !std::isfinite(world_position.z)) {
        throw std::logic_error("cascade selection requires a finite world-space position");
    }
    const Vec4 view_position = camera_view_
        * Vec4{world_position.x, world_position.y, world_position.z, 1.0F};
    if (!std::isfinite(view_position.x)
        || !std::isfinite(view_position.y)
        || !std::isfinite(view_position.z)
        || !std::isfinite(view_position.w)) {
        throw std::logic_error("cascade camera transform produced a non-finite view position");
    }
    return -view_position.z;
}

DepthTexture2D capture_depth_texture(const Framebuffer& framebuffer) {
    if (framebuffer.sample_count() != SampleCount::One) {
        throw std::invalid_argument("depth texture capture requires a single-sample framebuffer");
    }

    if (framebuffer.width() > std::numeric_limits<std::size_t>::max() / framebuffer.height()) {
        throw std::overflow_error("framebuffer dimensions overflow depth capture storage size");
    }
    std::vector<float> depths;
    depths.reserve(framebuffer.width() * framebuffer.height());
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            const float depth = framebuffer.depth_at(x, y);
            if (!std::isfinite(depth) || depth < 0.0F || depth > 1.0F) {
                throw std::invalid_argument(
                    "depth texture capture requires finite normalized framebuffer depth; clear unused pixels to a finite far value");
            }
            depths.push_back(depth);
        }
    }
    return DepthTexture2D{framebuffer.width(), framebuffer.height(), std::move(depths)};
}

}  // namespace tiny_renderer
