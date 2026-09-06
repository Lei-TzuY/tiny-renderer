#include "tiny_renderer/shadow.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace tiny_renderer {

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
