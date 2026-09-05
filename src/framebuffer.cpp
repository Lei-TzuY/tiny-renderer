#include "tiny_renderer/framebuffer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace tiny_renderer {

namespace {
std::uint8_t to_u8(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
}

bool depth_compare_passes(DepthCompare compare, float incoming, float stored) {
    switch (compare) {
        case DepthCompare::Less:
            return incoming < stored;
        case DepthCompare::LessEqual:
            return incoming <= stored;
        case DepthCompare::Greater:
            return incoming > stored;
        case DepthCompare::GreaterEqual:
            return incoming >= stored;
        case DepthCompare::Always:
            return true;
        case DepthCompare::Never:
            return false;
    }
    throw std::invalid_argument("unknown depth comparison mode");
}
}  // namespace

void validate_depth_state(const DepthState& state) {
    switch (state.compare) {
        case DepthCompare::Less:
        case DepthCompare::LessEqual:
        case DepthCompare::Greater:
        case DepthCompare::GreaterEqual:
        case DepthCompare::Always:
        case DepthCompare::Never:
            return;
    }
    throw std::invalid_argument("unknown depth comparison mode");
}

Framebuffer::Framebuffer(std::size_t width, std::size_t height)
    : width_(width), height_(height), color_(width * height), depth_(width * height) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("framebuffer dimensions must be non-zero");
    }
    clear();
}

void Framebuffer::clear(const Vec3& color, float depth) {
    std::fill(color_.begin(), color_.end(), color);
    std::fill(depth_.begin(), depth_.end(), depth);
}

bool Framebuffer::depth_test_and_write(
    std::size_t x,
    std::size_t y,
    float depth,
    const Vec3& color,
    DepthState state) {
    validate_depth_state(state);
    if (x >= width_ || y >= height_ || !std::isfinite(depth)) {
        return false;
    }

    const std::size_t i = index(x, y);
    if (!depth_compare_passes(state.compare, depth, depth_[i])) {
        return false;
    }

    if (state.write_enabled) {
        depth_[i] = depth;
    }
    color_[i] = color;
    return true;
}

const Vec3& Framebuffer::color_at(std::size_t x, std::size_t y) const {
    return color_.at(index(x, y));
}

float Framebuffer::depth_at(std::size_t x, std::size_t y) const {
    return depth_.at(index(x, y));
}

std::vector<std::uint8_t> Framebuffer::rgb8() const {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(color_.size() * 3U);
    for (const Vec3& pixel : color_) {
        bytes.push_back(to_u8(pixel.x));
        bytes.push_back(to_u8(pixel.y));
        bytes.push_back(to_u8(pixel.z));
    }
    return bytes;
}

std::uint64_t Framebuffer::fnv1a64() const {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    for (const std::uint8_t byte : rgb8()) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= prime;
    }
    return hash;
}

void Framebuffer::write_ppm(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open PPM output: " + path);
    }
    out << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    const std::vector<std::uint8_t> bytes = rgb8();
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw std::runtime_error("failed while writing PPM output: " + path);
    }
}

std::size_t Framebuffer::index(std::size_t x, std::size_t y) const {
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("framebuffer coordinate out of range");
    }
    return y * width_ + x;
}

}  // namespace tiny_renderer
