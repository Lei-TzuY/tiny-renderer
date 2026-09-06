#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 8.0e-3F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

Vertex normal_vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.0F, 0.0F, 1.0F});
}

Triangle camera_triangle() {
    return Triangle{{
        normal_vertex({-0.7F, -0.7F, 0.0F}),
        normal_vertex({0.7F, -0.7F, 0.0F}),
        normal_vertex({0.0F, 0.7F, 0.0F}),
    }};
}

ModelAsset model_from_triangle() {
    const Triangle triangle = camera_triangle();
    ModelAsset asset;
    asset.mesh.vertices = {triangle[0], triangle[1], triangle[2]};
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "per-record-shadow";
    draw.material.albedo = {1.0F, 1.0F, 1.0F};
    draw.material.specular = {0.0F, 0.0F, 0.0F};
    asset.draws.push_back(draw);
    return asset;
}

DirectionalLight directional(Vec3 color) {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.direction_to_light = {0.0F, 0.0F, 1.0F};
    light.ambient = 0.1F;
    light.diffuse = 0.4F;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    light.color = color;
    return light;
}

PointLight point(Vec3 color, float z = 2.0F) {
    PointLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = {0.0F, 0.0F, z};
    light.ambient = 0.1F;
    light.diffuse = 0.4F;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    light.color = color;
    return light;
}

SpotLight spot(Vec3 color) {
    SpotLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = {0.0F, 0.0F, 2.0F};
    light.direction = {0.0F, 0.0F, -1.0F};
    light.ambient = 0.1F;
    light.diffuse = 0.4F;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    light.inner_cone_cos = 0.9F;
    light.outer_cone_cos = 0.8F;
    light.color = color;
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

std::shared_ptr<const DepthTexture2D> projected_map(float depth) {
    return std::make_shared<const DepthTexture2D>(
        1U, 1U, std::vector<float>{depth});
}

std::shared_ptr<const DepthCubemap> point_map(const Vec3& position, float depth) {
    std::array<Mat4, kCubemapFaceCount> transforms{};
    transforms.fill(Mat4::identity());
    return std::make_shared<const DepthCubemap>(
        1U,
        position,
        transforms,
        std::vector<float>(kCubemapFaceCount, depth));
}

std::shared_ptr<const SpotShadowMap> spot_map(const SpotLight& light, float depth) {
    return std::make_shared<const SpotShadowMap>(
        light.position,
        normalize(light.direction),
        light.outer_cone_cos,
        Mat4::identity(),
        DepthTexture2D(1U, 1U, std::vector<float>{depth}));
}

Vec3 render_center(const ModelRenderOptions& options) {
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        model_from_triangle(),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return framebuffer.color_at(32U, 32U);
}

std::pair<std::vector<std::uint8_t>, std::uint64_t> render_signature(
    const ModelRenderOptions& options) {
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        model_from_triangle(),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return {framebuffer.rgb8(), framebuffer.fnv1a64()};
}

void test_two_directional_records_have_independent_shadows() {
    FixedLight red = fixed_directional(directional({1.0F, 0.0F, 0.0F}));
    red.directional_shadow = {true, projected_map(0.0F), Mat4::identity(), 0.0F};
    FixedLight green = fixed_directional(directional({0.0F, 1.0F, 0.0F}));
    green.directional_shadow = {true, projected_map(1.0F), Mat4::identity(), 0.0F};
    ModelRenderOptions options;
    options.fixed_lights = collection({red, green});
    const Vec3 color = render_center(options);
    check_near(color.x, 0.1F,
               "occluded red directional record keeps only its ambient term");
    check_near(color.y, 0.5F,
               "visible green directional record keeps ambient plus direct term");
    check_near(color.z, 0.0F,
               "independent directional shadow bindings do not leak blue energy");
}

void test_two_point_records_have_independent_shadows() {
    FixedLight red = fixed_point(point({1.0F, 0.0F, 0.0F}));
    red.point_shadow = {true, point_map(red.point.position, 0.0F), 0.0F};
    FixedLight green = fixed_point(point({0.0F, 1.0F, 0.0F}));
    green.point_shadow = {true, point_map(green.point.position, 1.0F), 0.0F};
    ModelRenderOptions options;
    options.fixed_lights = collection({red, green});
    const Vec3 color = render_center(options);
    check_near(color.x, 0.1F,
               "occluded red point record keeps only its ambient term");
    check_near(color.y, 0.5F,
               "visible green point record keeps ambient plus direct term");
    check_near(color.z, 0.0F,
               "independent point shadow bindings do not leak blue energy");
}

void test_two_spot_records_have_independent_shadows() {
    FixedLight red = fixed_spot(spot({1.0F, 0.0F, 0.0F}));
    red.spot_shadow = {true, spot_map(red.spot, 0.0F), 0.0F};
    FixedLight green = fixed_spot(spot({0.0F, 1.0F, 0.0F}));
    green.spot_shadow = {true, spot_map(green.spot, 1.0F), 0.0F};
    ModelRenderOptions options;
    options.fixed_lights = collection({red, green});
    const Vec3 color = render_center(options);
    check_near(color.x, 0.1F,
               "occluded red spotlight record keeps only its ambient term");
    check_near(color.y, 0.5F,
               "visible green spotlight record keeps ambient plus direct term");
    check_near(color.z, 0.0F,
               "independent spotlight shadow bindings do not leak blue energy");
}

void test_legacy_singleton_migration_is_byte_equivalent() {
    {
        const auto map = projected_map(0.0F);
        ModelRenderOptions legacy;
        legacy.fixed_lights = collection({fixed_directional(directional({1.0F, 1.0F, 1.0F}))});
        legacy.fixed_lights.shadowed_directional_index = 0U;
        legacy.shadow_state = {true, map, Mat4::identity(), 0.0F};
        ModelRenderOptions modern;
        modern.fixed_lights = collection({fixed_directional(directional({1.0F, 1.0F, 1.0F}))});
        modern.fixed_lights.lights[0].directional_shadow = legacy.shadow_state;
        check(render_signature(legacy) == render_signature(modern),
              "legacy directional singleton is byte/hash-equivalent to per-record binding");
    }
    {
        const PointLight light = point({1.0F, 1.0F, 1.0F});
        const auto map = point_map(light.position, 0.0F);
        ModelRenderOptions legacy;
        legacy.fixed_lights = collection({fixed_point(light)});
        legacy.fixed_lights.shadowed_point_index = 0U;
        legacy.point_shadow_state = {true, map, 0.0F};
        ModelRenderOptions modern;
        modern.fixed_lights = collection({fixed_point(light)});
        modern.fixed_lights.lights[0].point_shadow = legacy.point_shadow_state;
        check(render_signature(legacy) == render_signature(modern),
              "legacy point singleton is byte/hash-equivalent to per-record binding");
    }
    {
        const SpotLight light = spot({1.0F, 1.0F, 1.0F});
        const auto map = spot_map(light, 0.0F);
        ModelRenderOptions legacy;
        legacy.fixed_lights = collection({fixed_spot(light)});
        legacy.fixed_lights.shadowed_spot_index = 0U;
        legacy.fixed_lights.spot_shadow_state = {true, map, 0.0F};
        ModelRenderOptions modern;
        modern.fixed_lights = collection({fixed_spot(light)});
        modern.fixed_lights.lights[0].spot_shadow = legacy.fixed_lights.spot_shadow_state;
        check(render_signature(legacy) == render_signature(modern),
              "legacy spot singleton is byte/hash-equivalent to per-record binding");
    }
}

void test_invalid_binding_and_capture_fail_closed() {
    const ModelAsset asset = model_from_triangle();
    {
        FixedLight light = fixed_point(point({1.0F, 1.0F, 1.0F}));
        light.directional_shadow = {
            true, projected_map(1.0F), Mat4::identity(), 0.0F};
        ModelRenderOptions options;
        options.fixed_lights = collection({light});
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "cross-type per-record directional shadow on a point record is rejected");
    }

    {
        FixedLight light = fixed_point(point({1.0F, 1.0F, 1.0F}, 2.0F));
        light.point_shadow = {
            true, point_map({0.0F, 0.0F, 3.0F}, 1.0F), 0.0F};
        ModelRenderOptions options;
        options.fixed_lights = collection({light});

        Framebuffer framebuffer(65U, 65U);
        framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 17U);
        const auto before_rgb = framebuffer.rgb8();
        const float before_depth = framebuffer.depth_at(32U, 32U);
        const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);
        bool threw = false;
        try {
            draw_model_asset(
                framebuffer,
                asset,
                Mat4::identity(), Mat4::identity(), Mat4::identity(),
                options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "per-record point shadow capture mismatch rejects direct submission");
        check(framebuffer.rgb8() == before_rgb,
              "capture mismatch rejects before any color mutation");
        check(framebuffer.depth_at(32U, 32U) == before_depth,
              "capture mismatch rejects before depth mutation");
        check(framebuffer.stencil_at(32U, 32U) == before_stencil,
              "capture mismatch rejects before stencil mutation");
    }
}

void test_duplicate_legacy_and_per_record_binding_is_rejected() {
    const PointLight light = point({1.0F, 1.0F, 1.0F});
    const auto map = point_map(light.position, 1.0F);
    FixedLight record = fixed_point(light);
    record.point_shadow = {true, map, 0.0F};
    ModelRenderOptions options;
    options.fixed_lights = collection({record});
    options.fixed_lights.shadowed_point_index = 0U;
    options.point_shadow_state = {true, map, 0.0F};
    bool threw = false;
    try {
        (void)prepare_model_asset(model_from_triangle(), options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw,
          "one light cannot be double-bound by legacy and per-record point shadow state");
}

void test_prepared_ownership_and_list_equivalence() {
    std::shared_ptr<const DepthCubemap> map = point_map({0.0F, 0.0F, 2.0F}, 1.0F);
    std::weak_ptr<const DepthCubemap> weak = map;
    FixedLight record = fixed_point(point({0.3F, 0.8F, 0.2F}));
    record.point_shadow = {true, map, 0.0F};
    ModelRenderOptions options;
    options.fixed_lights = collection({record});
    const PreparedModelSubmission prepared = prepare_model_asset(
        model_from_triangle(), options);
    options.fixed_lights.lights[0].point_shadow.map.reset();
    map.reset();
    check(!weak.expired(),
          "prepared plan owns per-record point shadow resource lifetime");

    Framebuffer single(65U, 65U);
    draw_prepared_model(
        single,
        prepared,
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    Framebuffer listed(65U, 65U);
    const PreparedModelListEntry entries[] = {{&prepared, Mat4::identity()}};
    draw_prepared_model_list(
        listed, entries, Mat4::identity(), Mat4::identity());
    check(listed.rgb8() == single.rgb8(),
          "prepared list is byte-equivalent with owned per-record shadow resources");
    check(listed.fnv1a64() == single.fnv1a64(),
          "prepared list is hash-equivalent with owned per-record shadow resources");
}

}  // namespace

int main() {
    test_two_directional_records_have_independent_shadows();
    test_two_point_records_have_independent_shadows();
    test_two_spot_records_have_independent_shadows();
    test_legacy_singleton_migration_is_byte_equivalent();
    test_invalid_binding_and_capture_fail_closed();
    test_duplicate_legacy_and_per_record_binding_is_rejected();
    test_prepared_ownership_and_list_equivalence();
    if (failures != 0) {
        std::cerr << failures << " per-record shadow test(s) failed\n";
        return 1;
    }
    std::cout << "per-record shadow tests passed\n";
    return 0;
}
