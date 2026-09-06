#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

enum class SampleCount : std::uint8_t {
    One = 1U,
    Four = 4U,
};

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

enum class BlendFactor {
    Zero,
    One,
    SourceColor,
    OneMinusSourceColor,
    DestinationColor,
    OneMinusDestinationColor,
    ConstantColor,
    OneMinusConstantColor,
    SourceAlpha,
    OneMinusSourceAlpha,
};

enum class BlendOp {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

struct ColorWriteMask {
    bool red{true};
    bool green{true};
    bool blue{true};
};

struct BlendState {
    bool enabled{false};
    BlendFactor source_factor{BlendFactor::One};
    BlendFactor destination_factor{BlendFactor::Zero};
    BlendOp operation{BlendOp::Add};
    Vec3 constant_color{0.0F, 0.0F, 0.0F};
    ColorWriteMask write_mask{};
};

void validate_depth_state(const DepthState& state);
void validate_stencil_state(const StencilState& state);
void validate_blend_state(const BlendState& state);

class Framebuffer {
public:
    Framebuffer(
        std::size_t width,
        std::size_t height,
        SampleCount sample_count = SampleCount::One);

    void clear(
        const Vec3& color = {0.0F, 0.0F, 0.0F},
        float depth = std::numeric_limits<float>::infinity(),
        std::uint8_t stencil = 0U);

    // Legacy single-sample fragment ownership entry point. Multisample targets
    // must use test_and_write_sample explicitly rather than silently updating
    // only one sample of a pixel.
    bool test_and_write(
        std::size_t x,
        std::size_t y,
        float depth,
        const Vec3& color,
        DepthState depth_state = {},
        StencilState stencil_state = {},
        BlendState blend_state = {},
        float source_alpha = 1.0F);

    bool test_and_write_sample(
        std::size_t x,
        std::size_t y,
        std::size_t sample_index,
        float depth,
        const Vec3& color,
        DepthState depth_state = {},
        StencilState stencil_state = {},
        BlendState blend_state = {},
        float source_alpha = 1.0F);

    bool depth_test_and_write(
        std::size_t x,
        std::size_t y,
        float depth,
        const Vec3& color,
        DepthState state = {});

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] SampleCount sample_count() const noexcept { return sample_count_; }
    [[nodiscard]] std::size_t samples_per_pixel() const noexcept {
        return sample_count_ == SampleCount::Four ? 4U : 1U;
    }

    // color_at is always the resolved RGB value. For multisample targets,
    // depth_at/stencil_at retain a sample-0 compatibility view; use the
    // sample-specific accessors whenever per-sample ownership matters.
    [[nodiscard]] const Vec3& color_at(std::size_t x, std::size_t y) const;
    [[nodiscard]] float depth_at(std::size_t x, std::size_t y) const;
    [[nodiscard]] std::uint8_t stencil_at(std::size_t x, std::size_t y) const;
    [[nodiscard]] const Vec3& sample_color_at(
        std::size_t x,
        std::size_t y,
        std::size_t sample_index) const;
    [[nodiscard]] float sample_depth_at(
        std::size_t x,
        std::size_t y,
        std::size_t sample_index) const;
    [[nodiscard]] std::uint8_t sample_stencil_at(
        std::size_t x,
        std::size_t y,
        std::size_t sample_index) const;
    [[nodiscard]] std::vector<std::uint8_t> rgb8() const;
    [[nodiscard]] std::uint64_t fnv1a64() const;

    void write_ppm(const std::string& path) const;

private:
    [[nodiscard]] std::size_t pixel_index(std::size_t x, std::size_t y) const;
    [[nodiscard]] std::size_t sample_storage_index(
        std::size_t x,
        std::size_t y,
        std::size_t sample_index) const;
    void resolve_pixel(std::size_t pixel_index);

    std::size_t width_{};
    std::size_t height_{};
    SampleCount sample_count_{SampleCount::One};
    std::vector<Vec3> resolved_color_;
    std::vector<Vec3> sample_color_;
    std::vector<float> sample_depth_;
    std::vector<std::uint8_t> sample_stencil_;
};

}  // namespace tiny_renderer
