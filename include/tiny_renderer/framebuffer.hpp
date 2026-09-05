#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

enum class DepthCompare {
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Always,
    Never,
};

struct DepthState {
    DepthCompare compare{DepthCompare::Less};
    bool write_enabled{true};
};

void validate_depth_state(const DepthState& state);

class Framebuffer {
public:
    Framebuffer(std::size_t width, std::size_t height);

    void clear(const Vec3& color = {0.0F, 0.0F, 0.0F}, float depth = std::numeric_limits<float>::infinity());
    bool depth_test_and_write(
        std::size_t x,
        std::size_t y,
        float depth,
        const Vec3& color,
        DepthState state = {});

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] const Vec3& color_at(std::size_t x, std::size_t y) const;
    [[nodiscard]] float depth_at(std::size_t x, std::size_t y) const;
    [[nodiscard]] std::vector<std::uint8_t> rgb8() const;
    [[nodiscard]] std::uint64_t fnv1a64() const;

    void write_ppm(const std::string& path) const;

private:
    [[nodiscard]] std::size_t index(std::size_t x, std::size_t y) const;

    std::size_t width_{};
    std::size_t height_{};
    std::vector<Vec3> color_;
    std::vector<float> depth_;
};

}  // namespace tiny_renderer
