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
        nearly_equal(actual.x, expected.x, 2.0e-4F)
            && nearly_equal(actual.y, expected.y, 2.0e-4F)
            && nearly_equal(actual.z, expected.z, 2.0e-4F),
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

VaryingPack normal_varyings() {
    VaryingPack varyings;
    varyings.count = 3U;
    varyings.values[0] = 0.0F;
    varyings.values[1] = 0.0F;
    varyings.values[2] = 1.0F;
    return varyings;
}

ModelAsset normal_triangle_asset() {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-1.0F, -1.0F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({1.0F, -1.0F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.0F, 1.0F, 0.0F}, normal_varyings()),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "preview";
    draw.material.albedo = {0.8F, 0.4F, 0.2F};
    asset.draws.push_back(draw);
    return asset;
}

Texture2D constant_environment() {
    return Texture2D(
        2U,
        2U,
        {
            {0.5F, 0.25F, 0.125F},
            {0.5F, 0.25F, 0.125F},
            {0.5F, 0.25F, 0.125F},
            {0.5F, 0.25F, 0.125F},
        });
}

EnvironmentBackgroundState background_state(const Texture2D& texture) {
    EnvironmentBackgroundState background;
    background.texture = &texture;
    background.intensity = 1.25F;
    background.yaw_radians = 0.3F;
    return background;
}

EnvironmentDiffuseLight diffuse_light(const Texture2D& texture) {
    EnvironmentDiffuseLight light;
    light.normal = {0U, 1U, 2U};
    light.environment.texture = &texture;
    light.environment.intensity = 0.75F;
    light.environment.yaw_radians = -0.2F;
    return light;
}

void test_background_and_lighting_are_independent() {
    const ModelAsset asset = normal_triangle_asset();
    const Texture2D texture = constant_environment();
    const EnvironmentBackgroundState background = background_state(texture);
    const EnvironmentDiffuseLight lighting = diffuse_light(texture);

    for (const SampleCount sample_count : {SampleCount::One, SampleCount::Four}) {
        OfflineRenderSettings base{};
        base.width = 49U;
        base.height = 37U;
        base.sample_count = sample_count;
        base.clear_color = {0.02F, 0.03F, 0.04F};

        const Framebuffer baseline = render_model_preview(asset, base);

        OfflineRenderSettings lighting_only = base;
        lighting_only.environment_lighting = lighting;
        const Framebuffer lit_a = render_model_preview(asset, lighting_only);
        const Framebuffer lit_b = render_model_preview(asset, lighting_only);

        OfflineRenderSettings background_only = base;
        background_only.environment = background;
        const Framebuffer background_frame = render_model_preview(asset, background_only);

        OfflineRenderSettings combined = base;
        combined.environment = background;
        combined.environment_lighting = lighting;
        const Framebuffer combined_a = render_model_preview(asset, combined);
        const Framebuffer combined_b = render_model_preview(asset, combined);

        check(
            lit_a.rgb8() == lit_b.rgb8(),
            sample_count == SampleCount::One
                ? "1x lighting-only preview is deterministic"
                : "4x lighting-only preview is deterministic");
        check(
            combined_a.rgb8() == combined_b.rgb8(),
            sample_count == SampleCount::One
                ? "1x combined environment preview is deterministic"
                : "4x combined environment preview is deterministic");

        const std::size_t center_x = base.width / 2U;
        const std::size_t center_y = base.height / 2U;
        const std::size_t corner_x = 0U;
        const std::size_t corner_y = 0U;

        check_vec3_near(
            lit_a.color_at(corner_x, corner_y),
            base.clear_color,
            "lighting-only preview leaves uncovered background at clear color");
        check_vec3_near(
            background_frame.color_at(center_x, center_y),
            baseline.color_at(center_x, center_y),
            "visible environment background does not implicitly enable diffuse lighting");
        check(
            !nearly_equal(
                lit_a.color_at(center_x, center_y).x,
                baseline.color_at(center_x, center_y).x),
            "diffuse environment lighting changes covered model shading without a visible background");
        check_vec3_near(
            combined_a.color_at(corner_x, corner_y),
            background_frame.color_at(corner_x, corner_y),
            "combined preview preserves background-only uncovered pixels");
        check_vec3_near(
            combined_a.color_at(center_x, center_y),
            lit_a.color_at(center_x, center_y),
            "combined preview preserves lighting-only covered model pixels");

        const Vec3 expected_lit{
            0.8F * 0.5F * 0.75F,
            0.4F * 0.25F * 0.75F,
            0.2F * 0.125F * 0.75F,
        };
        check_vec3_near(
            lit_a.color_at(center_x, center_y),
            expected_lit,
            "constant HDR environment reaches the existing Lambert material path analytically");
    }
}

void test_invalid_and_duplicate_headless_lighting_reject() {
    const ModelAsset asset = normal_triangle_asset();
    OfflineRenderSettings settings{};
    settings.environment_lighting = EnvironmentDiffuseLight{};
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings); },
        "headless preview rejects null diffuse environment texture");

    const Texture2D texture = constant_environment();
    const EnvironmentDiffuseLight lighting = diffuse_light(texture);
    settings.environment_lighting = lighting;
    settings.environment_lighting->environment.intensity = std::numeric_limits<float>::infinity();
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings); },
        "headless preview rejects invalid diffuse environment intensity");

    settings.environment_lighting = lighting;
    ModelRenderOptions options{};
    options.fixed_lights.environment_diffuse = lighting;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings, options); },
        "headless preview rejects duplicate environment lighting ownership");
}

}  // namespace

int main() {
    test_background_and_lighting_are_independent();
    test_invalid_and_duplicate_headless_lighting_reject();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "headless environment lighting tests passed\n";
    return 0;
}
