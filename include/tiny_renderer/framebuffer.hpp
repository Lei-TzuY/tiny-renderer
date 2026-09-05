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

enum class StencilCompare {
    Never,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
    Always,
};

enum class StencilOp {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
};

struct StencilState {
    bool enabled{false};
    StencilCompare compare{StencilCompare::Always};
    std::uint8_t reference{0U};
    std::uint8_t read_mask{0xFFU};
    std::uint8_t write_mask{0xFFU};
    StencilOp stencil_fail{StencilOp::Keep};
    StencilOp depth_fail{StencilOp::Keep};
    StencilOp pass{StencilOp::Keep};
};

void validate_depth_state(const DepthState& state);
void validate_stencil_state(const StencilState& state);

class Framebuffer {
public:
    Framebuffer(std::size_t width, std::size_t height);

    void clear(
        const Vec3& color = {0.0F, 0.0F, 0.0F},
        float depth = std::numeric_limits<float>::infinity(),
        std::uint8_t stencil = 0U);

    bool test_and_write(
        std::size_t x,
        std::size_t y,
        float depth,
        const Vec3& color,
        DepthState depth_state = {},
        StencilState stencil_state = {});

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
    [[nodiscard]] std::uint8_t stencil_at(std::size_t x, std::size_t y) const;
    [[nodiscard]] std::vector<std::uint8_t> rgb8() const;
    [[nodiscard]] std::uint64_t fnv1a64() const;

    void write_ppm(const std::string& path) const;

private:
    [[nodiscard]] std::size_t index(std::size_t x, std::size_t y) const;

    std::size_t width_{};
    std::size_t height_{};
    std::vector<Vec3> color_;
    std::vector<float> depth_;
    std::vector<std::uint8_t> stencil_;
};

}  // namespace tiny_renderer
