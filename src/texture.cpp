#include "tiny_renderer/texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace tiny_renderer {

namespace {

bool finite(const Vec2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool within_unit_range(const Vec3& value) {
    return value.x >= 0.0F && value.x <= 1.0F
        && value.y >= 0.0F && value.y <= 1.0F
        && value.z >= 0.0F && value.z <= 1.0F;
}

std::size_t checked_texel_count(std::size_t width, std::size_t height) {
    if (width == 0U || height == 0U) {
        throw std::invalid_argument("texture dimensions must be non-zero");
    }
    if (width > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())
        || height > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("texture dimensions exceed sampler index range");
    }
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::overflow_error("texture dimensions overflow texel count");
    }
    return width * height;
}

std::size_t ceil_half(std::size_t value) {
    return value / 2U + value % 2U;
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

void validate_sampler_state(const SamplerState& sampler) {
    const auto validate_address = [](AddressMode mode) {
        switch (mode) {
            case AddressMode::Clamp:
            case AddressMode::Repeat:
                return;
        }
        throw std::invalid_argument("unsupported texture address mode");
    };
    validate_address(sampler.address_u);
    validate_address(sampler.address_v);

    switch (sampler.filter) {
        case FilterMode::Nearest:
        case FilterMode::Bilinear:
            break;
        default:
            throw std::invalid_argument("unsupported texture filter mode");
    }

    switch (sampler.mip_filter) {
        case MipFilterMode::Disabled:
        case MipFilterMode::Nearest:
        case MipFilterMode::Linear:
            return;
    }
    throw std::invalid_argument("unsupported texture mip filter mode");
}

Texture2D::Texture2D(std::size_t width, std::size_t height, std::vector<Vec3> texels) {
    const std::size_t base_count = checked_texel_count(width, height);
    if (texels.size() != base_count) {
        throw std::invalid_argument("texture texel count does not match dimensions");
    }
    for (const Vec3& value : texels) {
        if (!finite(value)) {
            throw std::invalid_argument("texture texels must be finite");
        }
        texels_within_unit_range_ = texels_within_unit_range_ && within_unit_range(value);
    }

    levels_.push_back(MipLevel{width, height, std::move(texels)});
    while (levels_.back().width > 1U || levels_.back().height > 1U) {
        const MipLevel& parent = levels_.back();
        const std::size_t next_width = ceil_half(parent.width);
        const std::size_t next_height = ceil_half(parent.height);
        const std::size_t next_count = checked_texel_count(next_width, next_height);
        std::vector<Vec3> next_texels(next_count);

        for (std::size_t y = 0U; y < next_height; ++y) {
            for (std::size_t x = 0U; x < next_width; ++x) {
                const std::size_t parent_x = x * 2U;
                const std::size_t parent_y = y * 2U;
                Vec3 sum{};
                std::size_t sample_count = 0U;
                for (std::size_t offset_y = 0U; offset_y < 2U; ++offset_y) {
                    const std::size_t source_y = parent_y + offset_y;
                    if (source_y >= parent.height) {
                        continue;
                    }
                    for (std::size_t offset_x = 0U; offset_x < 2U; ++offset_x) {
                        const std::size_t source_x = parent_x + offset_x;
                        if (source_x >= parent.width) {
                            continue;
                        }
                        sum = sum + parent.texels[source_y * parent.width + source_x];
                        ++sample_count;
                    }
                }
                if (sample_count == 0U) {
                    throw std::logic_error("mip generation produced an empty parent footprint");
                }
                next_texels[y * next_width + x] = sum / static_cast<float>(sample_count);
            }
        }
        levels_.push_back(MipLevel{next_width, next_height, std::move(next_texels)});
    }
}

std::size_t Texture2D::mip_width(std::size_t level) const {
    if (level >= levels_.size()) {
        throw std::out_of_range("texture mip level out of range");
    }
    return levels_[level].width;
}

std::size_t Texture2D::mip_height(std::size_t level) const {
    if (level >= levels_.size()) {
        throw std::out_of_range("texture mip level out of range");
    }
    return levels_[level].height;
}

const Vec3& Texture2D::texel(std::size_t x, std::size_t y) const {
    return mip_texel(0U, x, y);
}

const Vec3& Texture2D::mip_texel(std::size_t level, std::size_t x, std::size_t y) const {
    if (level >= levels_.size()) {
        throw std::out_of_range("texture mip level out of range");
    }
    const MipLevel& mip = levels_[level];
    if (x >= mip.width || y >= mip.height) {
        throw std::out_of_range("texture coordinate out of range");
    }
    return mip.texels[y * mip.width + x];
}

Vec3 Texture2D::sample_level(
    const Vec2& uv,
    const SamplerState& sampler,
    std::size_t level) const {
    const MipLevel& mip = levels_[level];
    const float u = address_coordinate(uv.x, sampler.address_u);
    const float v = address_coordinate(uv.y, sampler.address_v);

    if (sampler.filter == FilterMode::Nearest) {
        const std::int64_t x = static_cast<std::int64_t>(
            std::floor(static_cast<double>(u) * static_cast<double>(mip.width)));
        const std::int64_t y = static_cast<std::int64_t>(
            std::floor(static_cast<double>(v) * static_cast<double>(mip.height)));
        return mip_texel(
            level,
            addressed_index(x, mip.width, sampler.address_u),
            addressed_index(y, mip.height, sampler.address_v));
    }

    if (sampler.filter == FilterMode::Bilinear) {
        const double x = static_cast<double>(u) * static_cast<double>(mip.width) - 0.5;
        const double y = static_cast<double>(v) * static_cast<double>(mip.height) - 0.5;
        const std::int64_t x0 = static_cast<std::int64_t>(std::floor(x));
        const std::int64_t y0 = static_cast<std::int64_t>(std::floor(y));
        const std::int64_t x1 = x0 + 1;
        const std::int64_t y1 = y0 + 1;
        const float tx = static_cast<float>(x - std::floor(x));
        const float ty = static_cast<float>(y - std::floor(y));

        const Vec3 top = lerp(
            mip_texel(
                level,
                addressed_index(x0, mip.width, sampler.address_u),
                addressed_index(y0, mip.height, sampler.address_v)),
            mip_texel(
                level,
                addressed_index(x1, mip.width, sampler.address_u),
                addressed_index(y0, mip.height, sampler.address_v)),
            tx);
        const Vec3 bottom = lerp(
            mip_texel(
                level,
                addressed_index(x0, mip.width, sampler.address_u),
                addressed_index(y1, mip.height, sampler.address_v)),
            mip_texel(
                level,
                addressed_index(x1, mip.width, sampler.address_u),
                addressed_index(y1, mip.height, sampler.address_v)),
            tx);
        return lerp(top, bottom, ty);
    }

    throw std::logic_error("validated texture filter mode became invalid");
}

Vec3 Texture2D::sample(const Vec2& uv, const SamplerState& sampler) const {
    validate_sampler_state(sampler);
    return sample_level(uv, sampler, 0U);
}

Vec3 Texture2D::sample_lod(
    const Vec2& uv,
    float lod,
    const SamplerState& sampler) const {
    validate_sampler_state(sampler);
    if (!std::isfinite(lod)) {
        throw std::invalid_argument("texture LOD must be finite");
    }
    if (sampler.mip_filter == MipFilterMode::Disabled || levels_.size() == 1U) {
        return sample_level(uv, sampler, 0U);
    }

    const float max_lod = static_cast<float>(levels_.size() - 1U);
    const float clamped_lod = std::clamp(lod, 0.0F, max_lod);
    if (sampler.mip_filter == MipFilterMode::Nearest) {
        const std::size_t level = static_cast<std::size_t>(
            std::floor(clamped_lod + 0.5F));
        return sample_level(uv, sampler, level);
    }

    if (sampler.mip_filter == MipFilterMode::Linear) {
        const std::size_t lower = static_cast<std::size_t>(std::floor(clamped_lod));
        const std::size_t upper = std::min(lower + 1U, levels_.size() - 1U);
        const float fraction = clamped_lod - static_cast<float>(lower);
        return lerp(
            sample_level(uv, sampler, lower),
            sample_level(uv, sampler, upper),
            fraction);
    }

    throw std::logic_error("validated texture mip filter mode became invalid");
}

Vec3 Texture2D::sample_grad(
    const Vec2& uv,
    const TextureGradients& gradients,
    const SamplerState& sampler) const {
    validate_sampler_state(sampler);
    if (sampler.mip_filter == MipFilterMode::Disabled || levels_.size() == 1U) {
        return sample_level(uv, sampler, 0U);
    }
    if (!finite(gradients.dx) || !finite(gradients.dy)) {
        throw std::invalid_argument("texture gradients must be finite");
    }

    const double width_scale = static_cast<double>(width());
    const double height_scale = static_cast<double>(height());
    const double footprint_x = std::hypot(
        static_cast<double>(gradients.dx.x) * width_scale,
        static_cast<double>(gradients.dx.y) * height_scale);
    const double footprint_y = std::hypot(
        static_cast<double>(gradients.dy.x) * width_scale,
        static_cast<double>(gradients.dy.y) * height_scale);
    const double footprint = std::max(footprint_x, footprint_y);
    const double lod = footprint > 1.0 ? std::log2(footprint) : 0.0;
    if (!std::isfinite(lod)) {
        throw std::logic_error("finite texture gradients produced a non-finite LOD");
    }
    return sample_lod(uv, static_cast<float>(lod), sampler);
}

}  // namespace tiny_renderer
