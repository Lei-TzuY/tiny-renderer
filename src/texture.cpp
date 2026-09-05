#include "tiny_renderer/texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace tiny_renderer {

namespace {

bool finite(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float address_coordinate(float value, AddressMode mode) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("texture coordinate must be finite");
    }
    switch (mode) {
        case AddressMode::Clamp:
            return std::clamp(value, 0.0F, 1.0F);
        case AddressMode::Repeat: {
            const float wrapped = value - std::floor(value);
            return wrapped < 1.0F ? wrapped : 0.0F;
        }
    }
    throw std::invalid_argument("unsupported texture address mode");
}

std::size_t addressed_index(std::int64_t index, std::size_t extent, AddressMode mode) {
    switch (mode) {
        case AddressMode::Clamp:
            if (index <= 0) {
                return 0U;
            }
            if (static_cast<std::uint64_t>(index) >= static_cast<std::uint64_t>(extent)) {
                return extent - 1U;
            }
            return static_cast<std::size_t>(index);
        case AddressMode::Repeat: {
            const std::int64_t signed_extent = static_cast<std::int64_t>(extent);
            std::int64_t wrapped = index % signed_extent;
            if (wrapped < 0) {
                wrapped += signed_extent;
            }
            return static_cast<std::size_t>(wrapped);
        }
    }
    throw std::invalid_argument("unsupported texture address mode");
}

Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return a * (1.0F - t) + b * t;
}

}  // namespace

Texture2D::Texture2D(std::size_t width, std::size_t height, std::vector<Vec3> texels)
    : width_(width), height_(height), texels_(std::move(texels)) {
    if (width_ == 0U || height_ == 0U) {
        throw std::invalid_argument("texture dimensions must be non-zero");
    }
    if (width_ > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())
        || height_ > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("texture dimensions exceed sampler index range");
    }
    if (width_ > std::numeric_limits<std::size_t>::max() / height_) {
        throw std::overflow_error("texture dimensions overflow texel count");
    }
    if (texels_.size() != width_ * height_) {
        throw std::invalid_argument("texture texel count does not match dimensions");
    }
    for (const Vec3& value : texels_) {
        if (!finite(value)) {
            throw std::invalid_argument("texture texels must be finite");
        }
    }
}

const Vec3& Texture2D::texel(std::size_t x, std::size_t y) const {
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("texture coordinate out of range");
    }
    return texels_[y * width_ + x];
}

Vec3 Texture2D::sample(const Vec2& uv, const SamplerState& sampler) const {
    const float u = address_coordinate(uv.x, sampler.address_u);
    const float v = address_coordinate(uv.y, sampler.address_v);

    if (sampler.filter == FilterMode::Nearest) {
        const std::int64_t x = static_cast<std::int64_t>(std::floor(static_cast<double>(u) * static_cast<double>(width_)));
        const std::int64_t y = static_cast<std::int64_t>(std::floor(static_cast<double>(v) * static_cast<double>(height_)));
        return texel(addressed_index(x, width_, sampler.address_u), addressed_index(y, height_, sampler.address_v));
    }

    if (sampler.filter == FilterMode::Bilinear) {
        const double x = static_cast<double>(u) * static_cast<double>(width_) - 0.5;
        const double y = static_cast<double>(v) * static_cast<double>(height_) - 0.5;
        const std::int64_t x0 = static_cast<std::int64_t>(std::floor(x));
        const std::int64_t y0 = static_cast<std::int64_t>(std::floor(y));
        const std::int64_t x1 = x0 + 1;
        const std::int64_t y1 = y0 + 1;
        const float tx = static_cast<float>(x - std::floor(x));
        const float ty = static_cast<float>(y - std::floor(y));

        const Vec3 top = lerp(
            texel(addressed_index(x0, width_, sampler.address_u), addressed_index(y0, height_, sampler.address_v)),
            texel(addressed_index(x1, width_, sampler.address_u), addressed_index(y0, height_, sampler.address_v)),
            tx);
        const Vec3 bottom = lerp(
            texel(addressed_index(x0, width_, sampler.address_u), addressed_index(y1, height_, sampler.address_v)),
            texel(addressed_index(x1, width_, sampler.address_u), addressed_index(y1, height_, sampler.address_v)),
            tx);
        return lerp(top, bottom, ty);
    }

    throw std::invalid_argument("unsupported texture filter mode");
}

}  // namespace tiny_renderer
