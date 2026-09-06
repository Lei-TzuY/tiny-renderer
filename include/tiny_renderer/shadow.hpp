#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/shadow_sampling.hpp"

namespace tiny_renderer {

class DepthTexture2D {
public:
    DepthTexture2D(std::size_t width, std::size_t height, std::vector<float> depths);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] float depth_at(std::size_t x, std::size_t y) const;

private:
    std::size_t width_{};
    std::size_t height_{};
    std::vector<float> depths_;
};

[[nodiscard]] DepthTexture2D capture_depth_texture(const Framebuffer& framebuffer);

struct ShadowState {
    bool enabled{false};
    std::shared_ptr<const DepthTexture2D> map;
    Mat4 light_view_projection{Mat4::identity()};
    float bias{0.0F};
    ShadowSamplingMode sampling{ShadowSamplingMode::Hard};
};

}  // namespace tiny_renderer
