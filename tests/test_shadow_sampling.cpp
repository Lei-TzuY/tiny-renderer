#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/point_shadow.hpp"
#include "tiny_renderer/rasterizer.hpp"
#include "tiny_renderer/shadow.hpp"
#include "tiny_renderer/spot_shadow.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check_near(
    float actual,
    float expected,
    const std::string& message,
    float epsilon = 8.0e-3F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

Vertex normal_vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.0F, 0.0F, 1.0F});
}

ModelAsset model_asset() {
    ModelAsset asset;
    asset.mesh.vertices = {
        normal_vertex({-0.7F, -0.7F, 0.0F}),
        normal_vertex({0.7F, -0.7F, 0.0F}),
        normal_vertex({0.0F, 0.7F, 0.0F}),
    };
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "shadow-sampling";
    draw.material.albedo = {1.0F, 1.0F, 1.0F};
    draw.material.specular = {0.0F, 0.0F, 0.0F};
    asset.draws.push_back(draw);
    return asset;
}

DirectionalLight directional() {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.direction_to_light = {0.0F, 0.0F, 1.0F};
    light.ambient = 0.0F;
    light.diffuse = 1.0F;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    return light;
}

PointLight point() {
    PointLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = {0.0F, 0.0F, 2.0F};
    light.ambient = 0.0F;
    light.diffuse = 1.0F;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    return light;
}

SpotLight spot() {
    SpotLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = {0.0F, 0.0F, 2.0F};
    light.direction = {0.0F, 0.0F, -1.0F};
    light.ambient = 0.0F;
    light.diffuse = 1.0F;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    light.inner_cone_cos = 0.9F;
    light.outer_cone_cos = 0.8F;
    return light;
}

FixedLight fixed_directional(const DirectionalLight& light) {
    FixedLight result;
    result.type = FixedLightType::Directional;
    result.directional = light;
    return result;
}

FixedLight fixed_point(const PointLight& light) {
    FixedLight result;
    result.type = FixedLightType::Point;
    result.point = light;
    return result;
}

FixedLight fixed_spot(const SpotLight& light) {
    FixedLight result;
    result.type = FixedLightType::Spot;
    result.spot = light;
    return result;
}

FixedLightCollection collection(std::initializer_list<FixedLight> lights) {
    FixedLightCollection result;
    result.count = lights.size();
    std::size_t index = 0U;
    for (const FixedLight& light : lights) {
        result.lights[index++] = light;
    }
    return result;
}

std::shared_ptr<const DepthTexture2D> projected_center_occluded() {
    std::vector<float> depths(9U, 1.0F);
    depths[4U] = 0.0F;
    return std::make_shared<const DepthTexture2D>(3U, 3U, std::move(depths));
}

std::shared_ptr<const DepthCubemap> point_center_occluded(const Vec3& position) {
    std::array<Mat4, kCubemapFaceCount> transforms{};
    transforms.fill(Mat4::identity());
    std::vector<float> depths(kCubemapFaceCount * 9U, 1.0F);
    depths[5U * 9U + 4U] = 0.0F;
    return std::make_shared<const DepthCubemap>(
        3U, position, transforms, std::move(depths));
}

std::shared_ptr<const SpotShadowMap> spot_center_occluded(const SpotLight& light) {
    std::vector<float> depths(9U, 1.0F);
    depths[4U] = 0.0F;
    return std::make_shared<const SpotShadowMap>(
        light.position,
        normalize(light.direction),
        light.outer_cone_cos,
        Mat4::identity(),
        DepthTexture2D(3U, 3U, std::move(depths)));
}

Vec3 render_center(const ModelRenderOptions& options) {
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        model_asset(),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return framebuffer.color_at(32U, 32U);
}

std::pair<std::vector<std::uint8_t>, std::uint64_t> render_signature(
    const ModelRenderOptions& options) {
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        model_asset(),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return {framebuffer.rgb8(), framebuffer.fnv1a64()};
}

void test_hard_default_is_explicitly_equivalent() {
    const auto map = projected_center_occluded();
    ModelRenderOptions implicit;
    implicit.fixed_lights = collection({fixed_directional(directional())});
    implicit.fixed_lights.lights[0].directional_shadow = {
        true, map, Mat4::identity(), 0.0F};

    ModelRenderOptions explicit_hard = implicit;
    explicit_hard.fixed_lights.lights[0].directional_shadow.sampling =
        ShadowSamplingMode::Hard;
    check(
        render_signature(implicit) == render_signature(explicit_hard),
        "Hard is byte/hash-equivalent to the historical default shadow state");
}

void test_fixed_kernel_pcf_for_all_typed_records() {
    constexpr float expected = 8.0F / 9.0F;
    {
        FixedLight light = fixed_directional(directional());
        light.directional_shadow = {
            true,
            projected_center_occluded(),
            Mat4::identity(),
            0.0F,
            ShadowSamplingMode::Pcf3x3,
        };
        ModelRenderOptions options;
        options.fixed_lights = collection({light});
        const Vec3 color = render_center(options);
        check_near(color.x, expected, "directional PCF averages eight visible taps");
        check_near(color.y, expected, "directional PCF preserves white light equally");
        check_near(color.z, expected, "directional PCF preserves white light equally");
    }
    {
        const PointLight point_light = point();
        FixedLight light = fixed_point(point_light);
        light.point_shadow = {
            true,
            point_center_occluded(point_light.position),
            0.0F,
            ShadowSamplingMode::Pcf3x3,
        };
        ModelRenderOptions options;
        options.fixed_lights = collection({light});
        check_near(
            render_center(options).x,
            expected,
            "point PCF averages within the selected cubemap face");
    }
    {
        const SpotLight spot_light = spot();
        FixedLight light = fixed_spot(spot_light);
        light.spot_shadow = {
            true,
            spot_center_occluded(spot_light),
            0.0F,
            ShadowSamplingMode::Pcf3x3,
        };
        ModelRenderOptions options;
        options.fixed_lights = collection({light});
        check_near(
            render_center(options).x,
            expected,
            "spot PCF averages the projected 3x3 kernel");
    }
}

void test_same_type_mixed_policies_are_per_record() {
    const auto map = projected_center_occluded();

    FixedLight hard = fixed_directional(directional());
    hard.directional_shadow = {
        true,
        map,
        Mat4::identity(),
        0.0F,
        ShadowSamplingMode::Hard,
    };

    FixedLight pcf = fixed_directional(directional());
    pcf.directional_shadow = {
        true,
        map,
        Mat4::identity(),
        0.0F,
        ShadowSamplingMode::Pcf3x3,
    };

    ModelRenderOptions options;
    options.fixed_lights = collection({hard, pcf});
    check_near(
        render_center(options).x,
        8.0F / 9.0F,
        "same-type directional records retain independent Hard and PCF policies");
}

void test_projected_edge_taps_clamp_to_map() {
    const auto map = std::make_shared<const DepthTexture2D>(
        2U,
        2U,
        std::vector<float>{1.0F, 0.0F, 0.0F, 0.0F});
    FixedLight light = fixed_directional(directional());
    light.directional_shadow = {
        true,
        map,
        Mat4::translation({0.75F, 0.75F, 0.0F}),
        0.0F,
        ShadowSamplingMode::Pcf3x3,
    };
    ModelRenderOptions options;
    options.fixed_lights = collection({light});
    check_near(
        render_center(options).x,
        2.0F / 9.0F,
        "projected PCF clamps duplicate taps at the 2D map edge");
}

void test_point_edge_taps_stay_on_selected_face() {
    const PointLight point_light = point();
    std::array<Mat4, kCubemapFaceCount> transforms{};
    transforms.fill(Mat4::identity());
    transforms[5U] = Mat4::translation({0.75F, 0.75F, 0.0F});
    std::vector<float> depths(kCubemapFaceCount * 4U, 1.0F);
    const std::size_t selected_base = 5U * 4U;
    depths[selected_base + 0U] = 1.0F;
    depths[selected_base + 1U] = 0.0F;
    depths[selected_base + 2U] = 0.0F;
    depths[selected_base + 3U] = 0.0F;
    const auto map = std::make_shared<const DepthCubemap>(
        2U,
        point_light.position,
        transforms,
        std::move(depths));

    FixedLight light = fixed_point(point_light);
    light.point_shadow = {
        true,
        map,
        0.0F,
        ShadowSamplingMode::Pcf3x3,
    };
    ModelRenderOptions options;
    options.fixed_lights = collection({light});
    check_near(
        render_center(options).x,
        2.0F / 9.0F,
        "point PCF clamps within the selected cubemap face without seam filtering");
}

void test_unknown_policy_fails_static_preparation() {
    FixedLight light = fixed_directional(directional());
    light.directional_shadow.sampling = static_cast<ShadowSamplingMode>(255);
    ModelRenderOptions options;
    options.fixed_lights = collection({light});
    bool threw = false;
    try {
        (void)prepare_model_asset(model_asset(), options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "unknown shadow sampling mode rejects during prepared-model validation");
}

void test_prepared_list_retains_pcf_resource_and_execution() {
    std::weak_ptr<const DepthTexture2D> retained;
    PreparedModelSubmission prepared = [&]() {
        auto map = projected_center_occluded();
        retained = map;
        FixedLight light = fixed_directional(directional());
        light.directional_shadow = {
            true,
            map,
            Mat4::identity(),
            0.0F,
            ShadowSamplingMode::Pcf3x3,
        };
        ModelRenderOptions options;
        options.fixed_lights = collection({light});
        return prepare_model_asset(model_asset(), options);
    }();
    check(!retained.expired(), "prepared PCF plan retains its shadow resource lifetime");

    Framebuffer sequential(65U, 65U);
    Framebuffer listed(65U, 65U);
    draw_prepared_model(
        sequential,
        prepared,
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    const std::array<PreparedModelListEntry, 1U> entries{{
        PreparedModelListEntry{&prepared, Mat4::identity()},
    }};
    draw_prepared_model_list(
        listed,
        std::span<const PreparedModelListEntry>{entries},
        Mat4::identity(),
        Mat4::identity());
    check(
        sequential.rgb8() == listed.rgb8()
            && sequential.fnv1a64() == listed.fnv1a64(),
        "prepared-list PCF execution is byte/hash-equivalent to sequential submission");
}

void test_list_preflight_prevents_earlier_pcf_writes() {
    FixedLight pcf_light = fixed_directional(directional());
    pcf_light.directional_shadow = {
        true,
        projected_center_occluded(),
        Mat4::identity(),
        0.0F,
        ShadowSamplingMode::Pcf3x3,
    };
    ModelRenderOptions pcf_options;
    pcf_options.fixed_lights = collection({pcf_light});
    const PreparedModelSubmission first = prepare_model_asset(model_asset(), pcf_options);

    ModelRenderOptions later_options;
    later_options.fixed_lights = collection({fixed_directional(directional())});
    const PreparedModelSubmission later = prepare_model_asset(model_asset(), later_options);

    const std::array<PreparedModelListEntry, 2U> entries{{
        PreparedModelListEntry{&first, Mat4::identity()},
        PreparedModelListEntry{&later, Mat4::scale({0.0F, 1.0F, 1.0F})},
    }};
    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.17F, 0.23F, 0.31F}, 0.73F, 41U);
    const auto before_rgb = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);
    bool threw = false;
    try {
        draw_prepared_model_list(
            framebuffer,
            std::span<const PreparedModelListEntry>{entries},
            Mat4::identity(),
            Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "later singular lit entry rejects the complete prepared list");
    check(framebuffer.rgb8() == before_rgb,
          "later invalid entry prevents earlier PCF color writes");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "later invalid entry prevents earlier PCF depth writes");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "later invalid entry prevents earlier PCF stencil writes");
}

}  // namespace

int main() {
    test_hard_default_is_explicitly_equivalent();
    test_fixed_kernel_pcf_for_all_typed_records();
    test_same_type_mixed_policies_are_per_record();
    test_projected_edge_taps_clamp_to_map();
    test_point_edge_taps_stay_on_selected_face();
    test_unknown_policy_fails_static_preparation();
    test_prepared_list_retains_pcf_resource_and_execution();
    test_list_preflight_prevents_earlier_pcf_writes();

    if (failures != 0) {
        std::cerr << failures << " shadow sampling test(s) failed\n";
        return 1;
    }
    std::cout << "shadow sampling tests passed\n";
    return 0;
}