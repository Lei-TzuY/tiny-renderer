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

enum class MipFilterMode {
    Disabled,
    Nearest,
    Linear,
};

// Describes how source texture samples are interpreted before they enter the
// renderer's linear-float texture domain. Linear preserves source values
// exactly; Srgb decodes normalized encoded RGB to linear RGB before mip
// generation, filtering, lighting, or any material use.
enum class TextureTransferFunction {
    Linear,
    Srgb,
};

struct SamplerState {
    AddressMode address_u{AddressMode::Clamp};
    AddressMode address_v{AddressMode::Clamp};
    FilterMode filter{FilterMode::Nearest};
    MipFilterMode mip_filter{MipFilterMode::Disabled};
    // Bounded anisotropic filtering applies only to gradient-based sampling.
    // One preserves the historical isotropic path exactly; two and four use a
    // deterministic major-axis multi-tap footprint with the existing mip
    // filter. Values greater than one therefore require mip filtering.
    std::size_t max_anisotropy{1U};
};

struct TextureGradients {
    Vec2 dx{};
    Vec2 dy{};
};

void validate_sampler_state(const SamplerState& sampler);
void validate_texture_transfer_function(TextureTransferFunction transfer_function);

class Texture2D {
public:
    Texture2D(
        std::size_t width,
        std::size_t height,
        std::vector<Vec3> texels,
        TextureTransferFunction transfer_function = TextureTransferFunction::Linear);

    [[nodiscard]] std::size_t width() const noexcept { return levels_.front().width; }
    [[nodiscard]] std::size_t height() const noexcept { return levels_.front().height; }
    [[nodiscard]] std::size_t mip_level_count() const noexcept { return levels_.size(); }
    [[nodiscard]] std::size_t mip_width(std::size_t level) const;
    [[nodiscard]] std::size_t mip_height(std::size_t level) const;
    [[nodiscard]] bool texels_within_unit_range() const noexcept { return texels_within_unit_range_; }
    [[nodiscard]] bool texels_nonnegative() const noexcept { return texels_nonnegative_; }
    [[nodiscard]] TextureTransferFunction source_transfer_function() const noexcept {
        return source_transfer_function_;
    }
    [[nodiscard]] const Vec3& texel(std::size_t x, std::size_t y) const;
    [[nodiscard]] const Vec3& mip_texel(std::size_t level, std::size_t x, std::size_t y) const;
    [[nodiscard]] Vec3 sample(const Vec2& uv, const SamplerState& sampler = {}) const;
    [[nodiscard]] Vec3 sample_lod(
        const Vec2& uv,
        float lod,
        const SamplerState& sampler = {}) const;
    [[nodiscard]] Vec3 sample_grad(
        const Vec2& uv,
        const TextureGradients& gradients,
        const SamplerState& sampler = {}) const;

private:
    struct MipLevel {
        std::size_t width{};
        std::size_t height{};
        std::vector<Vec3> texels;
    };

    [[nodiscard]] Vec3 sample_level(
        const Vec2& uv,
        const SamplerState& sampler,
        std::size_t level) const;

    std::vector<MipLevel> levels_;
    bool texels_within_unit_range_{true};
    bool texels_nonnegative_{true};
    TextureTransferFunction source_transfer_function_{TextureTransferFunction::Linear};
};

}  // namespace tiny_renderer
