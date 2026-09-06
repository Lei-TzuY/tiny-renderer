#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/shadow_sampling.hpp"

namespace tiny_renderer {

class DepthTexture2D {
public:
    DepthTexture2D(
        std::size_t width,
        std::size_t height,
        std::vector<float> depths);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] float depth_at(std::size_t x, std::size_t y) const;

private:
    std::size_t width_{};
    std::size_t height_{};
    std::vector<float> depths_;
};

[[nodiscard]] DepthTexture2D capture_depth_texture(const Framebuffer& framebuffer);

constexpr std::size_t kMaxDirectionalShadowCascades = 4U;

struct DirectionalShadowCascade {
    // Positive camera/view-space depth. Cascade i owns the half-open interval
    // [previous_split, split_view_depth); exact split equality selects the
    // following cascade. Depth at or beyond the final split is unshadowed.
    float split_view_depth{0.0F};
    std::shared_ptr<const DepthTexture2D> map;
    Mat4 light_view_projection{Mat4::identity()};
    float bias{0.0F};
    ShadowSamplingMode sampling{ShadowSamplingMode::Hard};
};

class CascadedDirectionalShadowMap {
public:
    CascadedDirectionalShadowMap(
        Mat4 camera_view,
        std::array<DirectionalShadowCascade, kMaxDirectionalShadowCascades> cascades,
        std::size_t count);

    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] const Mat4& camera_view() const noexcept { return camera_view_; }
    [[nodiscard]] const DirectionalShadowCascade& cascade(std::size_t index) const;
    [[nodiscard]] const DirectionalShadowCascade* cascade_for_view_depth(
        float view_depth) const noexcept;
    [[nodiscard]] float view_depth_for_world_position(const Vec3& world_position) const;

private:
    Mat4 camera_view_{Mat4::identity()};
    std::array<DirectionalShadowCascade, kMaxDirectionalShadowCascades> cascades_{};
    std::size_t count_{0U};
};

struct ShadowState {
    bool enabled{false};
    std::shared_ptr<const DepthTexture2D> map;
    Mat4 light_view_projection{Mat4::identity()};
    float bias{0.0F};
    ShadowSamplingMode sampling{ShadowSamplingMode::Hard};
    // Optional bounded cascade set for per-record directional shadows. When
    // present, the single-map fields above are not used by camera shading.
    // Trailing placement preserves all established aggregate initialization.
    std::shared_ptr<const CascadedDirectionalShadowMap> cascades;
};

}  // namespace tiny_renderer
