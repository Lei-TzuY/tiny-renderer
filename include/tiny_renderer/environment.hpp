#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

// Explicit perspective-camera state used only to reconstruct background rays.
// Geometry submission remains on the existing model/view/projection path.
struct PerspectiveCameraState {
    Vec3 eye{0.0F, 0.0F, 1.0F};
    Vec3 target{0.0F, 0.0F, 0.0F};
    Vec3 up{0.0F, 1.0F, 0.0F};
    float vertical_fov_radians{radians(60.0F)};
    float aspect{1.0F};
};

// A borrowed linear-HDR equirectangular environment. The texture remains owned
// by the caller. U defaults to repeat across the longitude seam while V clamps
// at the poles. This first slice intentionally requires base-level sampling;
// it does not invent ray differentials or implicit environment mip selection.
struct EnvironmentBackgroundState {
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

namespace environment_detail {

constexpr float kMaxEnvironmentIntensity = 1.0e6F;
constexpr float kMaxCameraAspect = 1.0e6F;

inline bool finite_vec3(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline float finite_length(const Vec3& value, const char* label) {
    if (!finite_vec3(value)) {
        throw std::invalid_argument(std::string(label) + " must be finite");
    }
    const float result = length(value);
    if (!std::isfinite(result) || result <= kEpsilon) {
        throw std::invalid_argument(std::string(label) + " must be non-zero");
    }
    return result;
}

inline float wrap_longitude(float longitude) {
    const float two_pi = 2.0F * kPi;
    float wrapped = std::fmod(longitude + kPi, two_pi);
    if (wrapped < 0.0F) {
        wrapped += two_pi;
    }
    return wrapped - kPi;
}

inline Vec2 sample_offset(SampleCount sample_count, std::size_t sample_index) {
    switch (sample_count) {
        case SampleCount::One:
            if (sample_index == 0U) {
                return {0.5F, 0.5F};
            }
            break;
        case SampleCount::Four:
            switch (sample_index) {
                case 0U: return {0.25F, 0.25F};
                case 1U: return {0.75F, 0.25F};
                case 2U: return {0.25F, 0.75F};
                case 3U: return {0.75F, 0.75F};
                default: break;
            }
            break;
    }
    throw std::out_of_range("environment background sample index is invalid for the framebuffer sample count");
}

struct CameraBasis {
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    float tan_half_y{};
    float tan_half_x{};
};

inline CameraBasis prepare_camera(const PerspectiveCameraState& camera) {
    if (!finite_vec3(camera.eye) || !finite_vec3(camera.target) || !finite_vec3(camera.up)) {
        throw std::invalid_argument("environment camera vectors must be finite");
    }
    if (!std::isfinite(camera.vertical_fov_radians)
        || camera.vertical_fov_radians <= 0.0F
        || camera.vertical_fov_radians >= kPi) {
        throw std::invalid_argument("environment camera vertical field of view must be finite and within (0, pi)");
    }
    if (!std::isfinite(camera.aspect)
        || camera.aspect <= 0.0F
        || camera.aspect > kMaxCameraAspect) {
        throw std::invalid_argument("environment camera aspect must be finite and within (0, 1e6]");
    }

    const Vec3 forward_unscaled = camera.target - camera.eye;
    (void)finite_length(forward_unscaled, "environment camera forward vector");
    (void)finite_length(camera.up, "environment camera up vector");
    const Vec3 forward = normalize(forward_unscaled);
    const Vec3 right_unscaled = cross(forward, camera.up);
    (void)finite_length(right_unscaled, "environment camera right vector");
    const Vec3 right = normalize(right_unscaled);
    const Vec3 corrected_up = cross(right, forward);

    const float tan_half_y = std::tan(camera.vertical_fov_radians * 0.5F);
    const float tan_half_x = tan_half_y * camera.aspect;
    if (!std::isfinite(tan_half_y) || tan_half_y <= 0.0F
        || !std::isfinite(tan_half_x) || tan_half_x <= 0.0F) {
        throw std::invalid_argument("environment camera projection cannot be represented finitely");
    }
    return {forward, right, corrected_up, tan_half_y, tan_half_x};
}

inline Vec3 checked_scaled_radiance(const Vec3& sampled, float intensity) {
    if (!finite_vec3(sampled)
        || sampled.x < 0.0F || sampled.y < 0.0F || sampled.z < 0.0F) {
        throw std::invalid_argument("environment radiance samples must be finite and non-negative");
    }
    const auto scale_component = [intensity](float value) {
        const double result = static_cast<double>(value) * static_cast<double>(intensity);
        if (!std::isfinite(result)
            || result > static_cast<double>(std::numeric_limits<float>::max())) {
            throw std::overflow_error("environment radiance exceeds finite float range after intensity scaling");
        }
        return static_cast<float>(result);
    };
    return {
        scale_component(sampled.x),
        scale_component(sampled.y),
        scale_component(sampled.z),
    };
}

}  // namespace environment_detail

inline void validate_environment_background_state(const EnvironmentBackgroundState& environment) {
    if (environment.texture == nullptr) {
        throw std::invalid_argument("environment background requires a texture");
    }
    if (environment.texture->source_transfer_function() != TextureTransferFunction::Linear) {
        throw std::invalid_argument("environment background texture must be in the linear texture domain");
    }
    validate_sampler_state(environment.sampler);
    if (environment.sampler.mip_filter != MipFilterMode::Disabled) {
        throw std::invalid_argument("environment background requires disabled mip filtering without ray differentials");
    }
    if (!std::isfinite(environment.intensity)
        || environment.intensity < 0.0F
        || environment.intensity > environment_detail::kMaxEnvironmentIntensity) {
        throw std::invalid_argument("environment intensity must be finite and within [0, 1e6]");
    }
    if (!std::isfinite(environment.yaw_radians)
        || environment.yaw_radians < -kPi
        || environment.yaw_radians > kPi) {
        throw std::invalid_argument("environment yaw must be finite and within [-pi, pi]");
    }
}

// World +Y is the north pole (v=0), -Y is the south pole (v=1). With zero
// yaw, world -Z maps to the horizontal center (u=0.5), +X increases u, and the
// longitude seam is world +Z and is canonicalized to u=0. At an exact pole the
// otherwise undefined longitude is deterministically treated as zero before
// yaw is applied.
inline Vec2 equirectangular_uv(const Vec3& world_direction, float yaw_radians = 0.0F) {
    if (!environment_detail::finite_vec3(world_direction)) {
        throw std::invalid_argument("environment direction must be finite");
    }
    if (!std::isfinite(yaw_radians) || yaw_radians < -kPi || yaw_radians > kPi) {
        throw std::invalid_argument("environment yaw must be finite and within [-pi, pi]");
    }
    const float direction_length = length(world_direction);
    if (!std::isfinite(direction_length) || direction_length <= kEpsilon) {
        throw std::invalid_argument("environment direction must be non-zero");
    }
    const Vec3 direction = world_direction / direction_length;
    const float horizontal_squared = direction.x * direction.x + direction.z * direction.z;
    float longitude = 0.0F;
    if (horizontal_squared > kEpsilon * kEpsilon) {
        longitude = std::atan2(direction.x, -direction.z);
    }
    longitude = environment_detail::wrap_longitude(longitude + yaw_radians);
    const float u = longitude / (2.0F * kPi) + 0.5F;
    const float v = std::acos(std::clamp(direction.y, -1.0F, 1.0F)) / kPi;
    return {u, v};
}

inline Vec3 perspective_sample_direction(
    const PerspectiveCameraState& camera,
    std::size_t framebuffer_width,
    std::size_t framebuffer_height,
    SampleCount sample_count,
    std::size_t x,
    std::size_t y,
    std::size_t sample_index) {
    if (framebuffer_width == 0U || framebuffer_height == 0U) {
        throw std::invalid_argument("environment ray reconstruction requires non-zero framebuffer dimensions");
    }
    if (x >= framebuffer_width || y >= framebuffer_height) {
        throw std::out_of_range("environment ray pixel coordinate exceeds framebuffer bounds");
    }
    const environment_detail::CameraBasis basis = environment_detail::prepare_camera(camera);
    const Vec2 offset = environment_detail::sample_offset(sample_count, sample_index);
    const double sample_x = static_cast<double>(x) + static_cast<double>(offset.x);
    const double sample_y = static_cast<double>(y) + static_cast<double>(offset.y);
    const float ndc_x = static_cast<float>(
        2.0 * sample_x / static_cast<double>(framebuffer_width) - 1.0);
    const float ndc_y = static_cast<float>(
        1.0 - 2.0 * sample_y / static_cast<double>(framebuffer_height));
    const Vec3 direction = basis.forward
        + basis.right * (ndc_x * basis.tan_half_x)
        + basis.up * (ndc_y * basis.tan_half_y);
    if (!environment_detail::finite_vec3(direction) || length(direction) <= kEpsilon) {
        throw std::invalid_argument("environment camera produced an invalid sample direction");
    }
    return normalize(direction);
}

// Draw this pass before geometry. Every framebuffer sample receives one
// environment sample. The pass deliberately writes color only: depth testing
// always passes but depth writes are disabled, stencil is disabled, and blend
// state is replacement. Geometry submitted afterward therefore occludes the
// background through the renderer's unchanged depth/stencil/raster path.
//
// All rays and scaled radiance are computed into temporary storage first, so
// invalid camera/environment/radiance state rejects before framebuffer mutation.
inline void draw_environment_background(
    Framebuffer& framebuffer,
    const PerspectiveCameraState& camera,
    const EnvironmentBackgroundState& environment) {
    validate_environment_background_state(environment);
    (void)environment_detail::prepare_camera(camera);

    const std::size_t samples_per_pixel = framebuffer.samples_per_pixel();
    if (framebuffer.width() > std::numeric_limits<std::size_t>::max() / framebuffer.height()) {
        throw std::overflow_error("environment background pixel count overflows size_t");
    }
    const std::size_t pixel_count = framebuffer.width() * framebuffer.height();
    if (pixel_count > std::numeric_limits<std::size_t>::max() / samples_per_pixel) {
        throw std::overflow_error("environment background sample count overflows size_t");
    }
    std::vector<Vec3> radiance(pixel_count * samples_per_pixel);

    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            for (std::size_t sample_index = 0U; sample_index < samples_per_pixel; ++sample_index) {
                const Vec3 direction = perspective_sample_direction(
                    camera,
                    framebuffer.width(),
                    framebuffer.height(),
                    framebuffer.sample_count(),
                    x,
                    y,
                    sample_index);
                const Vec2 uv = equirectangular_uv(direction, environment.yaw_radians);
                const Vec3 sampled = environment.texture->sample(uv, environment.sampler);
                radiance[(y * framebuffer.width() + x) * samples_per_pixel + sample_index] =
                    environment_detail::checked_scaled_radiance(sampled, environment.intensity);
            }
        }
    }

    const DepthState color_only_depth{DepthCompare::Always, false};
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            for (std::size_t sample_index = 0U; sample_index < samples_per_pixel; ++sample_index) {
                const Vec3& color = radiance[
                    (y * framebuffer.width() + x) * samples_per_pixel + sample_index];
                const bool wrote = framebuffer.test_and_write_sample(
                    x,
                    y,
                    sample_index,
                    0.0F,
                    color,
                    color_only_depth,
                    {},
                    {},
                    1.0F);
                if (!wrote) {
                    throw std::logic_error("validated environment background color-only write unexpectedly failed");
                }
            }
        }
    }
}

}  // namespace tiny_renderer
