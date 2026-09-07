#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "tiny_renderer/environment.hpp"
#include "tiny_renderer/offline_render.hpp"

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

ModelAsset triangle_asset() {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-1.0F, -1.0F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({1.0F, -1.0F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({0.0F, 1.0F, 0.0F}, VaryingPack{}),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "preview";
    draw.material.albedo = {0.8F, 0.2F, 0.1F};
    asset.draws.push_back(draw);
    return asset;
}

Texture2D environment_texture() {
    return Texture2D(
        4U,
        2U,
        {
            {4.0F, 0.25F, 0.5F},
            {2.0F, 0.5F, 0.25F},
            {0.25F, 3.0F, 0.5F},
            {0.5F, 0.25F, 5.0F},
            {6.0F, 0.5F, 0.25F},
            {0.25F, 7.0F, 0.5F},
            {0.5F, 0.25F, 8.0F},
            {9.0F, 1.0F, 0.25F},
        });
}

EnvironmentBackgroundState environment_state(const Texture2D& texture) {
    EnvironmentBackgroundState environment;
    environment.texture = &texture;
    environment.sampler.address_u = AddressMode::Repeat;
    environment.sampler.address_v = AddressMode::Clamp;
    environment.sampler.filter = FilterMode::Nearest;
    environment.sampler.mip_filter = MipFilterMode::Disabled;
    environment.intensity = 1.5F;
    environment.yaw_radians = 0.2F;
    return environment;
}

PerspectiveCameraState preview_camera(const OfflineRenderSettings& settings) {
    return {
        {0.0F, 0.0F, 3.0F},
        {0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        settings.vertical_fov_radians,
        static_cast<float>(settings.width) / static_cast<float>(settings.height),
    };
}

Vec3 expected_environment_sample(
    const Texture2D& texture,
    const EnvironmentBackgroundState& environment,
    const OfflineRenderSettings& settings,
    std::size_t x,
    std::size_t y,
    std::size_t sample_index) {
    const Vec3 direction = perspective_sample_direction(
        preview_camera(settings),
        settings.width,
        settings.height,
        settings.sample_count,
        x,
        y,
        sample_index);
    const Vec2 uv = equirectangular_uv(direction, environment.yaw_radians);
    return texture.sample(uv, environment.sampler) * environment.intensity;
}

void check_depth_stencil_equal(
    const Framebuffer& a,
    const Framebuffer& b,
    const std::string& message) {
    bool equal = a.width() == b.width()
        && a.height() == b.height()
        && a.samples_per_pixel() == b.samples_per_pixel();
    if (equal) {
        for (std::size_t y = 0U; y < a.height(); ++y) {
            for (std::size_t x = 0U; x < a.width(); ++x) {
                for (std::size_t sample = 0U; sample < a.samples_per_pixel(); ++sample) {
                    equal = equal
                        && a.sample_depth_at(x, y, sample) == b.sample_depth_at(x, y, sample)
                        && a.sample_stencil_at(x, y, sample) == b.sample_stencil_at(x, y, sample);
                }
            }
        }
    }
    check(equal, message);
}

void test_default_preview_remains_compatible() {
    const ModelAsset asset = triangle_asset();
    OfflineRenderSettings settings{};
    settings.width = 47U;
    settings.height = 31U;
    settings.sample_count = SampleCount::One;

    const Framebuffer historical = render_model_preview(asset, settings);
    settings.environment.reset();
    const Framebuffer explicit_no_environment = render_model_preview(asset, settings);
    check(
        historical.rgb8() == explicit_no_environment.rgb8(),
        "explicitly absent environment preserves historical preview bytes");
}

void test_environment_uses_exact_preview_camera_and_preserves_attachments() {
    const ModelAsset asset = triangle_asset();
    const Texture2D texture = environment_texture();
    const EnvironmentBackgroundState environment = environment_state(texture);

    for (const SampleCount sample_count : {SampleCount::One, SampleCount::Four}) {
        OfflineRenderSettings settings{};
        settings.width = 41U;
        settings.height = 29U;
        settings.sample_count = sample_count;
        const Framebuffer without_environment = render_model_preview(asset, settings);
        settings.environment = environment;
        const Framebuffer with_environment_a = render_model_preview(asset, settings);
        const Framebuffer with_environment_b = render_model_preview(asset, settings);

        check(
            with_environment_a.rgb8() == with_environment_b.rgb8(),
            sample_count == SampleCount::One
                ? "1x environment preview is deterministic"
                : "4x environment preview is deterministic");
        check(
            with_environment_a.rgb8() != without_environment.rgb8(),
            "environment changes uncovered preview color");
        check_depth_stencil_equal(
            with_environment_a,
            without_environment,
            "environment preview preserves geometry depth/stencil ownership");

        const std::size_t x = 0U;
        const std::size_t y = 0U;
        check(
            std::isinf(with_environment_a.sample_depth_at(x, y, 0U)),
            "corner used for camera contract remains uncovered by geometry");
        for (std::size_t sample = 0U; sample < with_environment_a.samples_per_pixel(); ++sample) {
            check_vec3_near(
                with_environment_a.sample_color_at(x, y, sample),
                expected_environment_sample(texture, environment, settings, x, y, sample),
                "uncovered sample uses M53 ray reconstructed from exact preview camera");
        }
    }
}

void test_invalid_environment_is_rejected() {
    const ModelAsset asset = triangle_asset();
    OfflineRenderSettings settings{};
    EnvironmentBackgroundState invalid;
    settings.environment = invalid;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings); },
        "preview rejects null environment texture");

    const Texture2D texture = environment_texture();
    invalid = environment_state(texture);
    invalid.intensity = std::numeric_limits<float>::infinity();
    settings.environment = invalid;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings); },
        "preview rejects invalid environment intensity");
}

}  // namespace

int main() {
    test_default_preview_remains_compatible();
    test_environment_uses_exact_preview_camera_and_preserves_attachments();
    test_invalid_environment_is_rejected();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "headless environment tests passed\n";
    return 0;
}
