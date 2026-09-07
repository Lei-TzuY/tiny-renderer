#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/environment.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check_vec3_near(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check(
        nearly_equal(actual.x, expected.x)
            && nearly_equal(actual.y, expected.y)
            && nearly_equal(actual.z, expected.z),
        message);
}

template <typename Exception, typename Function>
void check_throws(Function&& function, const std::string& message) {
    bool threw_expected = false;
    try {
        function();
    } catch (const Exception&) {
        threw_expected = true;
    } catch (...) {
    }
    check(threw_expected, message);
}

Texture2D checker_texture(std::size_t width = 64U, std::size_t height = 32U) {
    std::vector<Vec3> texels;
    texels.reserve(width * height);
    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            const float value = ((x + y) & 1U) == 0U ? 0.0F : 1.0F;
            texels.push_back({value, 1.0F - value, value});
        }
    }
    return Texture2D(width, height, std::move(texels));
}

PerspectiveCameraState camera_toward_negative_z(float aspect) {
    return {
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, -1.0F},
        {0.0F, 1.0F, 0.0F},
        radians(90.0F),
        aspect,
    };
}

PerspectiveCameraState camera_toward_positive_z(float aspect) {
    return {
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {0.0F, 1.0F, 0.0F},
        radians(90.0F),
        aspect,
    };
}

float wrapped_u_delta(float from, float to) {
    float delta = to - from;
    if (delta > 0.5F) {
        delta -= 1.0F;
    } else if (delta < -0.5F) {
        delta += 1.0F;
    }
    return delta;
}

Vec2 uv_at(
    const PerspectiveCameraState& camera,
    std::size_t width,
    std::size_t height,
    SampleCount sample_count,
    std::size_t x,
    std::size_t y,
    std::size_t sample_index,
    float yaw) {
    return equirectangular_uv(
        perspective_sample_direction(camera, width, height, sample_count, x, y, sample_index),
        yaw);
}

TextureGradients expected_gradients(
    const PerspectiveCameraState& camera,
    std::size_t width,
    std::size_t height,
    SampleCount sample_count,
    std::size_t x,
    std::size_t y,
    std::size_t sample_index,
    float yaw) {
    const Vec2 center = uv_at(camera, width, height, sample_count, x, y, sample_index, yaw);
    TextureGradients gradients{};
    if (width > 1U) {
        Vec2 from = center;
        Vec2 to{};
        if (x + 1U < width) {
            to = uv_at(camera, width, height, sample_count, x + 1U, y, sample_index, yaw);
        } else {
            from = uv_at(camera, width, height, sample_count, x - 1U, y, sample_index, yaw);
            to = center;
        }
        gradients.dx = {wrapped_u_delta(from.x, to.x), to.y - from.y};
    }
    if (height > 1U) {
        Vec2 from = center;
        Vec2 to{};
        if (y + 1U < height) {
            to = uv_at(camera, width, height, sample_count, x, y + 1U, sample_index, yaw);
        } else {
            from = uv_at(camera, width, height, sample_count, x, y - 1U, sample_index, yaw);
            to = center;
        }
        gradients.dy = {wrapped_u_delta(from.x, to.x), to.y - from.y};
    }
    return gradients;
}

EnvironmentBackgroundState base_state(const Texture2D& texture) {
    EnvironmentBackgroundState state;
    state.texture = &texture;
    state.sampler.address_u = AddressMode::Repeat;
    state.sampler.address_v = AddressMode::Clamp;
    state.sampler.filter = FilterMode::Nearest;
    state.sampler.mip_filter = MipFilterMode::Disabled;
    state.intensity = 1.0F;
    state.yaw_radians = 0.0F;
    state.mip_policy = EnvironmentMipPolicy::BaseLevel;
    return state;
}

EnvironmentBackgroundState ray_state(const Texture2D& texture, MipFilterMode mip_filter) {
    EnvironmentBackgroundState state = base_state(texture);
    state.mip_policy = EnvironmentMipPolicy::RayFootprint;
    state.sampler.mip_filter = mip_filter;
    return state;
}

void test_base_level_remains_exact_reference() {
    const Texture2D texture = checker_texture();
    const EnvironmentBackgroundState state = base_state(texture);
    const std::size_t width = 5U;
    const std::size_t height = 3U;
    const PerspectiveCameraState camera = camera_toward_negative_z(
        static_cast<float>(width) / static_cast<float>(height));
    Framebuffer framebuffer(width, height, SampleCount::Four);
    framebuffer.clear({0.1F, 0.2F, 0.3F});
    draw_environment_background(framebuffer, camera, state);

    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            for (std::size_t sample = 0U; sample < framebuffer.samples_per_pixel(); ++sample) {
                const Vec2 uv = uv_at(camera, width, height, SampleCount::Four, x, y, sample, 0.0F);
                check_vec3_near(
                    framebuffer.sample_color_at(x, y, sample),
                    texture.sample(uv, state.sampler),
                    "base-level environment remains exact M53/M54 sampling reference");
            }
        }
    }
}

void test_ray_footprint_reuses_texture_sample_grad() {
    const Texture2D texture = checker_texture();
    const EnvironmentBackgroundState state = ray_state(texture, MipFilterMode::Nearest);
    const std::size_t width = 4U;
    const std::size_t height = 3U;
    const PerspectiveCameraState camera = camera_toward_negative_z(
        static_cast<float>(width) / static_cast<float>(height));
    Framebuffer framebuffer(width, height, SampleCount::One);
    framebuffer.clear();
    draw_environment_background(framebuffer, camera, state);

    const std::size_t x = 1U;
    const std::size_t y = 1U;
    const Vec2 uv = uv_at(camera, width, height, SampleCount::One, x, y, 0U, 0.0F);
    const TextureGradients gradients = expected_gradients(
        camera,
        width,
        height,
        SampleCount::One,
        x,
        y,
        0U,
        0.0F);
    const Vec3 expected = texture.sample_grad(uv, gradients, state.sampler);
    check_vec3_near(
        framebuffer.sample_color_at(x, y, 0U),
        expected,
        "ray-footprint environment delegates mip selection to Texture2D::sample_grad");

    SamplerState base_sampler = state.sampler;
    base_sampler.mip_filter = MipFilterMode::Disabled;
    const Vec3 base = texture.sample(uv, base_sampler);
    check(
        !nearly_equal(expected.x, base.x)
            || !nearly_equal(expected.y, base.y)
            || !nearly_equal(expected.z, base.z),
        "test scene exercises an actual minified mip result rather than base-level sampling");
}

void test_seam_uses_shortest_wrapped_longitude_derivative() {
    const Texture2D texture = checker_texture(128U, 32U);
    const EnvironmentBackgroundState state = ray_state(texture, MipFilterMode::Linear);
    const std::size_t width = 4U;
    const std::size_t height = 2U;
    const PerspectiveCameraState camera = camera_toward_positive_z(
        static_cast<float>(width) / static_cast<float>(height));
    const std::size_t x = 1U;
    const std::size_t y = 0U;
    const Vec2 center = uv_at(camera, width, height, SampleCount::One, x, y, 0U, 0.0F);
    const Vec2 right = uv_at(camera, width, height, SampleCount::One, x + 1U, y, 0U, 0.0F);
    const float raw_delta = right.x - center.x;
    const TextureGradients gradients = expected_gradients(
        camera,
        width,
        height,
        SampleCount::One,
        x,
        y,
        0U,
        0.0F);
    check(std::fabs(raw_delta) > 0.5F, "seam regression straddles the canonical u=0/1 boundary");
    check(std::fabs(gradients.dx.x) < 0.5F, "longitude derivative uses shortest wrapped seam distance");

    Framebuffer framebuffer(width, height, SampleCount::One);
    framebuffer.clear();
    draw_environment_background(framebuffer, camera, state);
    check_vec3_near(
        framebuffer.sample_color_at(x, y, 0U),
        texture.sample_grad(center, gradients, state.sampler),
        "seam-aware ray footprint matches wrapped-gradient texture sampling");
}

void test_ray_footprint_is_deterministic_for_1x_and_4x() {
    const Texture2D texture = checker_texture();
    const EnvironmentBackgroundState state = ray_state(texture, MipFilterMode::Linear);
    for (const SampleCount sample_count : {SampleCount::One, SampleCount::Four}) {
        const std::size_t width = 7U;
        const std::size_t height = 5U;
        const PerspectiveCameraState camera = camera_toward_negative_z(
            static_cast<float>(width) / static_cast<float>(height));
        Framebuffer a(width, height, sample_count);
        Framebuffer b(width, height, sample_count);
        a.clear();
        b.clear();
        draw_environment_background(a, camera, state);
        draw_environment_background(b, camera, state);
        check(a.rgb8() == b.rgb8(), "ray-footprint environment resolve is deterministic");
        bool samples_equal = true;
        for (std::size_t y = 0U; y < height; ++y) {
            for (std::size_t x = 0U; x < width; ++x) {
                for (std::size_t sample = 0U; sample < a.samples_per_pixel(); ++sample) {
                    const Vec3& av = a.sample_color_at(x, y, sample);
                    const Vec3& bv = b.sample_color_at(x, y, sample);
                    samples_equal = samples_equal
                        && av.x == bv.x && av.y == bv.y && av.z == bv.z;
                }
            }
        }
        check(samples_equal, "ray-footprint environment sample storage is deterministic");
    }
}

void test_invalid_mip_policy_fails_before_mutation() {
    const Texture2D texture = checker_texture();
    const PerspectiveCameraState camera = camera_toward_negative_z(1.0F);
    Framebuffer framebuffer(4U, 4U, SampleCount::Four);
    framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.75F, 7U);
    const auto before = framebuffer.rgb8();
    const Vec3 sample_before = framebuffer.sample_color_at(1U, 1U, 2U);
    const float depth_before = framebuffer.sample_depth_at(1U, 1U, 2U);
    const std::uint8_t stencil_before = framebuffer.sample_stencil_at(1U, 1U, 2U);

    EnvironmentBackgroundState invalid = base_state(texture);
    invalid.mip_policy = EnvironmentMipPolicy::RayFootprint;
    check_throws<std::invalid_argument>(
        [&] { draw_environment_background(framebuffer, camera, invalid); },
        "ray-footprint policy rejects disabled mip filtering");
    check(framebuffer.rgb8() == before, "invalid mip policy leaves resolved color untouched");
    check_vec3_near(
        framebuffer.sample_color_at(1U, 1U, 2U),
        sample_before,
        "invalid mip policy leaves sample color untouched");
    check(framebuffer.sample_depth_at(1U, 1U, 2U) == depth_before, "invalid mip policy leaves depth untouched");
    check(framebuffer.sample_stencil_at(1U, 1U, 2U) == stencil_before, "invalid mip policy leaves stencil untouched");

    invalid = base_state(texture);
    invalid.sampler.mip_filter = MipFilterMode::Nearest;
    check_throws<std::invalid_argument>(
        [&] { draw_environment_background(framebuffer, camera, invalid); },
        "base-level policy rejects enabled mip filtering rather than silently ignoring it");
}

}  // namespace

int main() {
    test_base_level_remains_exact_reference();
    test_ray_footprint_reuses_texture_sample_grad();
    test_seam_uses_shortest_wrapped_longitude_derivative();
    test_ray_footprint_is_deterministic_for_1x_and_4x();
    test_invalid_mip_policy_fails_before_mutation();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "environment mip tests passed\n";
    return 0;
}
