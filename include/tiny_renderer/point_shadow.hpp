#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

constexpr std::size_t kCubemapFaceCount = 6U;

enum class CubemapFace : std::uint8_t {
    PositiveX,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ,
};

// Dominant-axis addressing uses deterministic X -> Y -> Z tie precedence.
// The input direction must be finite and non-zero.
[[nodiscard]] CubemapFace cubemap_face_for_direction(const Vec3& direction);

class DepthCubemap {
public:
    DepthCubemap(
        std::size_t size,
        Vec3 light_position,
        std::array<Mat4, kCubemapFaceCount> face_view_projections,
        std::vector<float> depths);

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] const Vec3& light_position() const noexcept { return light_position_; }
    [[nodiscard]] float depth_at(CubemapFace face, std::size_t x, std::size_t y) const;
    [[nodiscard]] const Mat4& face_view_projection(CubemapFace face) const;

private:
    [[nodiscard]] std::size_t storage_index(
        CubemapFace face,
        std::size_t x,
        std::size_t y) const;

    std::size_t size_{};
    Vec3 light_position_{};
    std::array<Mat4, kCubemapFaceCount> face_view_projections_{};
    std::vector<float> depths_;
};

struct PointShadowState {
    bool enabled{false};
    std::shared_ptr<const DepthCubemap> map;
    float bias{0.0F};
};

}  // namespace tiny_renderer
