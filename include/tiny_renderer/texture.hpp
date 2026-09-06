#pragma once

#include <cstddef>
#include <vector>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

enum class AddressMode {
    Clamp,
    Repeat,
};

enum class FilterMode {
    Nearest,
    Bilinear,
};

struct SamplerState {
    AddressMode address_u{AddressMode::Clamp};
    AddressMode address_v{AddressMode::Clamp};
    FilterMode filter{FilterMode::Nearest};
};

class Texture2D {
public:
    Texture2D(std::size_t width, std::size_t height, std::vector<Vec3> texels);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] bool texels_within_unit_range() const noexcept { return texels_within_unit_range_; }
    [[nodiscard]] const Vec3& texel(std::size_t x, std::size_t y) const;
    [[nodiscard]] Vec3 sample(const Vec2& uv, const SamplerState& sampler = {}) const;

private:
    std::size_t width_{};
    std::size_t height_{};
    std::vector<Vec3> texels_;
    bool texels_within_unit_range_{true};
};

}  // namespace tiny_renderer
