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
#include "tiny_renderer/shadow.hpp"
#include "tiny_renderer/shadow_renderer.hpp"

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

ModelAsset two_depth_model() {
    ModelAsset asset;
    asset.mesh.vertices = {
        normal_vertex({-0.9F, -0.55F, -0.25F}),
        normal_vertex({-0.1F, -0.55F, -0.25F}),
        normal_vertex({-0.5F, 0.55F, -0.25F}),
        normal_vertex({0.1F, -0.55F, -0.75F}),
        normal_vertex({0.9F, -0.55F, -0.75F}),
        normal_vertex({0.5F, 0.55F, -0.75F}),
    };
    asset.mesh.triangles = {{0U, 1U, 2U}, {3U, 4U, 5U}};
    MaterialDraw draw;
    draw.range = {0U, 2U};
    draw.material_name = "cascade";
    draw.material.albedo = {1.0F, 1.0F, 1.0F};
    draw.material.specular = {0.0F, 0.0F, 0.0F};
    asset.draws.push_back(draw);
    return asset;
}

ModelAsset near_only_model() {
    ModelAsset asset = two_depth_model();
    asset.mesh.vertices.resize(3U);
    asset.mesh.triangles.resize(1U);
    asset.draws[0].range = {0U, 1U};
    return asset;
}

DirectionalLight directional(Vec3 color = {1.0F, 1.0F, 1.0F}) {
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

FixedLight fixed_directional(Vec3 color = {1.0F, 1.0F, 1.0F}) {
    FixedLight light;
    light.type = FixedLightType::Directional;
    light.directional = directional(color);
    return light;
}

std::shared_ptr<const DepthTexture2D> depth_map(float depth) {
    return std::make_shared<const DepthTexture2D>(
        1U, 1U, std::vector<float>{depth});
}

std::shared_ptr<const CascadedDirectionalShadowMap> cascades(
    float near_depth,
    float far_depth,
    ShadowSamplingMode near_sampling = ShadowSamplingMode::Hard,
    ShadowSamplingMode far_sampling = ShadowSamplingMode::Hard) {
    std::array<DirectionalShadowCascade, kMaxDirectionalShadowCascades> records{};
    records[0] = {
        0.5F,
        depth_map(near_depth),
        Mat4::identity(),
        0.0F,
        near_sampling,
    };
    records[1] = {
        1.0F,
        depth_map(far_depth),
        Mat4::identity(),
        0.0F,
        far_sampling,
    };
    return std::make_shared<const CascadedDirectionalShadowMap>(
        Mat4::identity(), records, 2U);
}

ModelRenderOptions options_with_shadow(const ShadowState& shadow) {
    ModelRenderOptions options;
    options.fixed_lights.count = 1U;
    options.fixed_lights.lights[0] = fixed_directional();
    options.fixed_lights.lights[0].directional_shadow = shadow;
    return options;
}

std::pair<std::vector<std::uint8_t>, std::uint64_t> render_signature(
    const ModelAsset& asset,
    const ModelRenderOptions& options) {
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return {framebuffer.rgb8(), framebuffer.fnv1a64()};
}

void test_resource_validation_and_half_open_selection() {
    const auto resource = cascades(0.0F, 1.0F);
    check(resource->count() == 2U, "cascade set preserves bounded active count");
    check(resource->cascade_for_view_depth(0.0F) == &resource->cascade(0U),
          "depth zero selects first half-open cascade");
    check(resource->cascade_for_view_depth(0.499F) == &resource->cascade(0U),
          "depth below first split selects first cascade");
    check(resource->cascade_for_view_depth(0.5F) == &resource->cascade(1U),
          "exact split equality selects following cascade");
    check(resource->cascade_for_view_depth(0.999F) == &resource->cascade(1U),
          "depth below final split selects final cascade");
    check(resource->cascade_for_view_depth(1.0F) == nullptr,
          "depth at final split is deterministically unshadowed");
    check(resource->cascade_for_view_depth(-0.01F) == nullptr,
          "negative camera depth does not select a cascade");
    check(resource->cascade_for_view_depth(std::numeric_limits<float>::infinity()) == nullptr,
          "non-finite camera depth does not select a cascade");

    bool threw = false;
    try {
        std::array<DirectionalShadowCascade, kMaxDirectionalShadowCascades> bad{};
        bad[0] = {0.5F, depth_map(1.0F), Mat4::identity(), 0.0F, ShadowSamplingMode::Hard};
        bad[1] = {0.5F, depth_map(1.0F), Mat4::identity(), 0.0F, ShadowSamplingMode::Hard};
        (void)CascadedDirectionalShadowMap{Mat4::identity(), bad, 2U};
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "non-increasing cascade split sequence is rejected");
}

void test_single_map_compatibility() {
    const auto map = depth_map(0.0F);
    ShadowState single{true, map, Mat4::identity(), 0.0F, ShadowSamplingMode::Hard};

    std::array<DirectionalShadowCascade, kMaxDirectionalShadowCascades> records{};
    records[0] = {1.0F, map, Mat4::identity(), 0.0F, ShadowSamplingMode::Hard};
    ShadowState cascaded;
    cascaded.enabled = true;
    cascaded.cascades = std::make_shared<const CascadedDirectionalShadowMap>(
        Mat4::identity(), records, 1U);

    check(
        render_signature(near_only_model(), options_with_shadow(single))
            == render_signature(near_only_model(), options_with_shadow(cascaded)),
        "one-cascade per-record binding is byte/hash-equivalent to the legacy single-map per-record path");
}

void test_distinct_near_and_far_visibility() {
    ShadowState state;
    state.enabled = true;
    state.cascades = cascades(0.0F, 1.0F);

    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        two_depth_model(),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options_with_shadow(state));

    const Vec3 near_color = framebuffer.color_at(16U, 32U);
    const Vec3 far_color = framebuffer.color_at(48U, 32U);
    check_near(near_color.x, 0.1F,
               "near cascade occlusion preserves only directional ambient term");
    check_near(far_color.x, 0.5F,
               "far cascade visibility preserves ambient plus direct term");
    check_near(near_color.y, near_color.x,
               "near cascade visibility modulates the selected directional record uniformly");
    check_near(far_color.y, far_color.x,
               "far cascade visibility modulates the selected directional record uniformly");
}

void test_mixed_single_and_cascaded_records() {
    FixedLight red = fixed_directional({1.0F, 0.0F, 0.0F});
    red.directional_shadow = {
        true, depth_map(0.0F), Mat4::identity(), 0.0F, ShadowSamplingMode::Hard};
    FixedLight green = fixed_directional({0.0F, 1.0F, 0.0F});
    green.directional_shadow.enabled = true;
    green.directional_shadow.cascades = cascades(1.0F, 1.0F);

    ModelRenderOptions options;
    options.fixed_lights.count = 2U;
    options.fixed_lights.lights[0] = red;
    options.fixed_lights.lights[1] = green;

    const auto signature = render_signature(near_only_model(), options);
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        near_only_model(),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    const Vec3 center = framebuffer.color_at(16U, 32U);
    check_near(center.x, 0.1F,
               "single-map red record remains independently occluded beside cascaded record");
    check_near(center.y, 0.5F,
               "cascaded green record remains independently visible beside single-map record");
    check(signature.second == framebuffer.fnv1a64(),
          "mixed single/cascade directional records remain deterministic");
}

void test_capture_matches_independent_shadow_passes() {
    const PreparedModelSubmission prepared = prepare_model_asset(two_depth_model());
    const PreparedModelListEntry entries[] = {{&prepared, Mat4::identity()}};
    const DirectionalShadowCascadeCapture definitions[] = {
        {0.5F, Mat4::identity(), 0.001F, ShadowSamplingMode::Hard},
        {1.0F, Mat4::translation({0.1F, 0.0F, 0.0F}), 0.002F, ShadowSamplingMode::Pcf3x3},
    };
    const DirectionalShadowMapOptions map_options{17U, 17U};
    const auto captured = render_directional_shadow_cascades(
        entries, Mat4::identity(), definitions, map_options);
    check(captured->count() == 2U, "cascade capture preserves requested count");
    check(captured->cascade(0U).split_view_depth == 0.5F,
          "cascade capture preserves first split");
    check(captured->cascade(1U).sampling == ShadowSamplingMode::Pcf3x3,
          "cascade capture preserves per-cascade sampling policy");

    for (std::size_t index = 0U; index < captured->count(); ++index) {
        const auto independent = render_directional_shadow_map(
            entries, definitions[index].light_view_projection, map_options);
        const DepthTexture2D& actual = *captured->cascade(index).map;
        bool equal = actual.width() == independent->width()
            && actual.height() == independent->height();
        for (std::size_t y = 0U; equal && y < actual.height(); ++y) {
            for (std::size_t x = 0U; x < actual.width(); ++x) {
                if (actual.depth_at(x, y) != independent->depth_at(x, y)) {
                    equal = false;
                    break;
                }
            }
        }
        check(equal,
              "each cascade capture is depth-identical to the established independent shadow pass");
    }
}

void test_prepared_ownership_and_list_fail_closed() {
    std::shared_ptr<const CascadedDirectionalShadowMap> resource = cascades(0.0F, 1.0F);
    std::weak_ptr<const CascadedDirectionalShadowMap> weak = resource;
    ShadowState state;
    state.enabled = true;
    state.cascades = resource;
    ModelRenderOptions options = options_with_shadow(state);
    const PreparedModelSubmission prepared = prepare_model_asset(near_only_model(), options);
    options.fixed_lights.lights[0].directional_shadow.cascades.reset();
    state.cascades.reset();
    resource.reset();
    check(!weak.expired(), "prepared plan retains cascaded directional shadow resource lifetime");

    Framebuffer single(65U, 65U);
    draw_prepared_model(
        single, prepared, Mat4::identity(), Mat4::identity(), Mat4::identity());
    Framebuffer listed(65U, 65U);
    const PreparedModelListEntry one_entry[] = {{&prepared, Mat4::identity()}};
    draw_prepared_model_list(
        listed, one_entry, Mat4::identity(), Mat4::identity());
    check(listed.rgb8() == single.rgb8(),
          "prepared list remains byte-equivalent with owned cascade resource");
    check(listed.fnv1a64() == single.fnv1a64(),
          "prepared list remains hash-equivalent with owned cascade resource");

    Framebuffer fail_closed(65U, 65U);
    fail_closed.clear({0.2F, 0.3F, 0.4F}, 0.9F, 23U);
    const auto before_rgb = fail_closed.rgb8();
    const float before_depth = fail_closed.depth_at(16U, 32U);
    const std::uint8_t before_stencil = fail_closed.stencil_at(16U, 32U);
    Mat4 bad_model = Mat4::identity();
    bad_model(0U, 0U) = std::numeric_limits<float>::quiet_NaN();
    const PreparedModelListEntry entries[] = {
        {&prepared, Mat4::identity()},
        {&prepared, bad_model},
    };
    bool threw = false;
    try {
        draw_prepared_model_list(
            fail_closed, entries, Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "later invalid cascade world transform rejects the complete prepared list");
    check(fail_closed.rgb8() == before_rgb,
          "later invalid cascade transform rejects before earlier list color mutation");
    check(fail_closed.depth_at(16U, 32U) == before_depth,
          "later invalid cascade transform rejects before earlier list depth mutation");
    check(fail_closed.stencil_at(16U, 32U) == before_stencil,
          "later invalid cascade transform rejects before earlier list stencil mutation");
}

void test_double_binding_rejected() {
    ShadowState state;
    state.enabled = true;
    state.map = depth_map(1.0F);
    state.cascades = cascades(1.0F, 1.0F);
    bool threw = false;
    try {
        (void)prepare_model_asset(near_only_model(), options_with_shadow(state));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "per-record directional shadow cannot bind single map and cascades together");
}

}  // namespace

int main() {
    test_resource_validation_and_half_open_selection();
    test_single_map_compatibility();
    test_distinct_near_and_far_visibility();
    test_mixed_single_and_cascaded_records();
    test_capture_matches_independent_shadow_passes();
    test_prepared_ownership_and_list_fail_closed();
    test_double_binding_rejected();
    if (failures != 0) {
        std::cerr << failures << " cascaded directional shadow test(s) failed\n";
        return 1;
    }
    std::cout << "cascaded directional shadow tests passed\n";
    return 0;
}
