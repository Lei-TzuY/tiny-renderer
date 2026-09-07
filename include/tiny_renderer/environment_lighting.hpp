#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include "tiny_renderer/environment.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

// First bounded diffuse-IBL contract. The environment is borrowed and must
// remain alive for every submission that references it. Mip filtering is
// deliberately disabled in this slice because the fixed hemispherical
// quadrature has no screen-space footprint from which to derive a mip LOD.
struct EnvironmentDiffuseState {
    const Texture2D* texture{nullptr};
    SamplerState sampler{
        AddressMode::Repeat,
        AddressMode::Clamp,
        FilterMode::Bilinear,
        MipFilterMode::Disabled,
    };
    float intensity{1.0F};
    float yaw_radians{0.0F};
};

namespace environment_lighting_detail {

// Sixteen fixed cosine-distributed local hemisphere directions. They are the
// centers of a deterministic 4x4 stratification in cosine-weighted disk space,
// ordered from the normal-facing ring outward and by increasing azimuth. Equal
// weights estimate irradiance as pi/N * sum(L_i).
inline constexpr std::array<Vec3, 16U> kCosineHemisphere16{{
    {0.250000000F, 0.935414347F, 0.250000000F},
    {-0.250000000F, 0.935414347F, 0.250000000F},
    {-0.250000000F, 0.935414347F, -0.250000000F},
    {0.250000000F, 0.935414347F, -0.250000000F},
    {0.433012702F, 0.790569415F, 0.433012702F},
    {-0.433012702F, 0.790569415F, 0.433012702F},
    {-0.433012702F, 0.790569415F, -0.433012702F},
    {0.433012702F, 0.790569415F, -0.433012702F},
    {0.559016994F, 0.612372436F, 0.559016994F},
    {-0.559016994F, 0.612372436F, 0.559016994F},
    {-0.559016994F, 0.612372436F, -0.559016994F},
    {0.559016994F, 0.612372436F, -0.559016994F},
    {0.661437828F, 0.353553391F, 0.661437828F},
    {-0.661437828F, 0.353553391F, 0.661437828F},
    {-0.661437828F, 0.353553391F, -0.661437828F},
    {0.661437828F, 0.353553391F, -0.661437828F},
}};

inline Vec3 checked_normal(const Vec3& normal) {
    if (!environment_detail::finite_vec3(normal)) {
        throw std::invalid_argument("environment diffuse normal must be finite");
    }
    const float normal_length = length(normal);
    if (!std::isfinite(normal_length) || normal_length <= kEpsilon) {
        throw std::invalid_argument("environment diffuse normal must be non-zero");
    }
    return normal / normal_length;
}

struct HemisphereBasis {
    Vec3 tangent;
    Vec3 normal;
    Vec3 bitangent;
};

inline HemisphereBasis hemisphere_basis(const Vec3& input_normal) {
    const Vec3 normal = checked_normal(input_normal);
    const Vec3 helper = std::fabs(normal.y) < 0.999F
        ? Vec3{0.0F, 1.0F, 0.0F}
        : Vec3{1.0F, 0.0F, 0.0F};
    const Vec3 tangent_unscaled = cross(helper, normal);
    const float tangent_length = length(tangent_unscaled);
    if (!std::isfinite(tangent_length) || tangent_length <= kEpsilon) {
        throw std::logic_error("environment diffuse basis construction became degenerate");
    }
    const Vec3 tangent = tangent_unscaled / tangent_length;
    const Vec3 bitangent = cross(normal, tangent);
    if (!environment_detail::finite_vec3(bitangent)) {
        throw std::logic_error("environment diffuse basis construction became non-finite");
    }
    return {tangent, normal, bitangent};
}

inline float checked_irradiance_component(double sum) {
    constexpr double weight = static_cast<double>(kPi) / 16.0;
    const double value = sum * weight;
    const double max_float = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(value) || value < 0.0 || value > max_float) {
        throw std::overflow_error("environment diffuse irradiance exceeds finite float range");
    }
    return static_cast<float>(value);
}

// Caller must have run validate_environment_diffuse_state before framebuffer
// mutation. Runtime sample checks remain in place so a violated borrowed-lifetime
// contract or impossible sampler regression cannot silently inject bad radiance.
inline Vec3 diffuse_environment_irradiance_unchecked(
    const EnvironmentDiffuseState& state,
    const Vec3& world_normal) {
    if (state.texture == nullptr) {
        throw std::logic_error("validated environment diffuse state lost its texture");
    }
    const HemisphereBasis basis = hemisphere_basis(world_normal);

    double sum_r = 0.0;
    double sum_g = 0.0;
    double sum_b = 0.0;
    for (const Vec3& local : kCosineHemisphere16) {
        const Vec3 direction = basis.tangent * local.x
            + basis.normal * local.y
            + basis.bitangent * local.z;
        if (!environment_detail::finite_vec3(direction)) {
            throw std::logic_error("environment diffuse quadrature produced a non-finite direction");
        }
        const Vec2 uv = equirectangular_uv(direction, state.yaw_radians);
        const Vec3 sampled = state.texture->sample(uv, state.sampler);
        const Vec3 radiance = environment_detail::checked_scaled_radiance(
            sampled, state.intensity);
        sum_r += static_cast<double>(radiance.x);
        sum_g += static_cast<double>(radiance.y);
        sum_b += static_cast<double>(radiance.z);
    }
    return {
        checked_irradiance_component(sum_r),
        checked_irradiance_component(sum_g),
        checked_irradiance_component(sum_b),
    };
}

inline Vec3 diffuse_environment_lambert_factor_unchecked(
    const EnvironmentDiffuseState& state,
    const Vec3& world_normal) {
    return diffuse_environment_irradiance_unchecked(state, world_normal) * (1.0F / kPi);
}

}  // namespace environment_lighting_detail

inline void validate_environment_diffuse_state(const EnvironmentDiffuseState& state) {
    if (state.texture == nullptr) {
        throw std::invalid_argument("environment diffuse lighting requires a texture");
    }
    if (state.texture->source_transfer_function() != TextureTransferFunction::Linear) {
        throw std::invalid_argument("environment diffuse texture must be in the linear texture domain");
    }
    validate_sampler_state(state.sampler);
    if (state.sampler.mip_filter != MipFilterMode::Disabled) {
        throw std::invalid_argument("environment diffuse quadrature requires disabled mip filtering");
    }
    if (!std::isfinite(state.intensity)
        || state.intensity < 0.0F
        || state.intensity > environment_detail::kMaxEnvironmentIntensity) {
        throw std::invalid_argument("environment diffuse intensity must be finite and within [0, 1e6]");
    }
    if (!std::isfinite(state.yaw_radians)
        || state.yaw_radians < -kPi
        || state.yaw_radians > kPi) {
        throw std::invalid_argument("environment diffuse yaw must be finite and within [-pi, pi]");
    }

    // Validate the complete source radiance field once before submission. Mip
    // filtering is disabled, and nearest/bilinear filtering of non-negative
    // finite texels remains non-negative and bounded by their finite extrema.
    for (std::size_t y = 0U; y < state.texture->height(); ++y) {
        for (std::size_t x = 0U; x < state.texture->width(); ++x) {
            (void)environment_detail::checked_scaled_radiance(
                state.texture->texel(x, y), state.intensity);
        }
    }
}

// Returns diffuse irradiance E(n) ~= pi/N sum L(w_i) using the fixed
// cosine-weighted 16-direction quadrature above. This is a deterministic bounded
// teaching rule, not an importance-sampling convergence or PBR conformance claim.
inline Vec3 diffuse_environment_irradiance(
    const EnvironmentDiffuseState& state,
    const Vec3& world_normal) {
    validate_environment_diffuse_state(state);
    return environment_lighting_detail::diffuse_environment_irradiance_unchecked(
        state, world_normal);
}

// Lambert diffuse BRDF multiplies irradiance by 1/pi. Returning this factor
// separately makes the material-lighting accumulator explicit. A constant
// environment therefore produces exactly its intensity-scaled radiance.
inline Vec3 diffuse_environment_lambert_factor(
    const EnvironmentDiffuseState& state,
    const Vec3& world_normal) {
    validate_environment_diffuse_state(state);
    return environment_lighting_detail::diffuse_environment_lambert_factor_unchecked(
        state, world_normal);
}

}  // namespace tiny_renderer
