#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/model.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/rasterizer.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 2.0e-4F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

void check_color(
    const Vec3& actual,
    const Vec3& expected,
    const std::string& message,
    float epsilon = 2.0e-4F) {
    check_near(actual.x, expected.x, message + " red", epsilon);
    check_near(actual.y, expected.y, message + " green", epsilon);
    check_near(actual.z, expected.z, message + " blue", epsilon);
}

template <typename Exception, typename Function>
void check_throws(Function&& function, const std::string& message) {
    bool threw = false;
    try {
        function();
    } catch (const Exception&) {
        threw = true;
    }
    check(threw, message);
}

Vertex normal_vertex(const Vec3& position, const Vec3& normal = {0.0F, 0.0F, 1.0F}) {
    return Vertex::with_varyings(
        position,
        VaryingPack{normal.x, normal.y, normal.z});
}

Triangle triangle_at_depth(float z) {
    return Triangle{{
        normal_vertex({-0.8F, -0.8F, z}),
        normal_vertex({0.8F, -0.8F, z}),
        normal_vertex({0.0F, 0.8F, z}),
    }};
}

ModelAsset model_from_triangle(const Triangle& triangle, float opacity = 1.0F) {
    ModelAsset asset;
    asset.mesh.vertices.assign(triangle.begin(), triangle.end());
    asset.mesh.triangles.push_back({0U, 1U, 2U});
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "material";
    draw.material = MaterialState{{1.0F, 1.0F, 1.0F}, opacity};
    asset.draws.push_back(std::move(draw));
    return asset;
}

DirectionalLight full_white_light() {
    return DirectionalLight{
        true,
        NormalBinding{0U, 1U, 2U},
        {0.0F, 0.0F, 1.0F},
        0.2F,
        0.8F,
    };
}

ShadowState shadow_state(
    std::shared_ptr<const DepthTexture2D> map,
    float bias = 0.0F,
    Mat4 light_view_projection = Mat4::identity()) {
    return ShadowState{true, std::move(map), light_view_projection, bias};
}

Rasterizer shadowed_rasterizer(
    Framebuffer& framebuffer,
    const ShadowState& shadow,
    AlphaToCoverageState alpha_to_coverage = {},
    MaterialState material = {}) {
    return Rasterizer(
        framebuffer,
        {},
        {},
        full_white_light(),
        material,
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        {},
        {},
        {},
        {},
        alpha_to_coverage,
        shadow);
}

void test_depth_resource_validation_and_capture() {
    DepthTexture2D texture{2U, 2U, {0.0F, 0.25F, 0.5F, 1.0F}};
    check_near(texture.depth_at(1U, 0U), 0.25F, "depth texture exposes immutable normalized samples");
    check_throws<std::out_of_range>(
        [&] { (void)texture.depth_at(2U, 0U); },
        "depth texture rejects out-of-range coordinates");
    check_throws<std::invalid_argument>(
        [] { DepthTexture2D invalid{0U, 1U, {}}; },
        "depth texture rejects zero dimensions");
    check_throws<std::invalid_argument>(
        [] { DepthTexture2D invalid{2U, 2U, {0.0F}}; },
        "depth texture rejects mismatched storage");
    check_throws<std::invalid_argument>(
        [] { DepthTexture2D invalid{1U, 1U, {std::numeric_limits<float>::infinity()}}; },
        "depth texture rejects non-finite depth");
    check_throws<std::invalid_argument>(
        [] { DepthTexture2D invalid{1U, 1U, {1.1F}}; },
        "depth texture rejects depth outside normalized range");

    Framebuffer source(3U, 2U, SampleCount::One);
    source.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
    source.depth_test_and_write(1U, 0U, 0.3F, {1.0F, 1.0F, 1.0F});
    const DepthTexture2D captured = capture_depth_texture(source);
    check(captured.width() == 3U && captured.height() == 2U,
          "depth capture preserves framebuffer dimensions");
    check_near(captured.depth_at(1U, 0U), 0.3F, "depth capture preserves written depth");
    check_near(captured.depth_at(0U, 1U), 1.0F, "depth capture preserves finite far clear depth");

    Framebuffer uncleared(2U, 2U, SampleCount::One);
    check_throws<std::invalid_argument>(
        [&] { (void)capture_depth_texture(uncleared); },
        "depth capture rejects non-finite unused depth instead of storing infinity");

    Framebuffer multisampled(2U, 2U, SampleCount::Four);
    multisampled.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
    check_throws<std::invalid_argument>(
        [&] { (void)capture_depth_texture(multisampled); },
        "depth capture rejects multisample targets instead of silently choosing sample zero");
}

std::shared_ptr<const DepthTexture2D> make_occluder_shadow_map() {
    const PreparedModelSubmission caster = prepare_model_asset(
        model_from_triangle(triangle_at_depth(-0.5F)));
    const std::array<PreparedModelListEntry, 1> entries{{
        PreparedModelListEntry{&caster, Mat4::identity()},
    }};
    return render_directional_shadow_map(
        entries,
        Mat4::identity(),
        DirectionalShadowMapOptions{33U, 33U, CullMode::None, FrontFace::CounterClockwise});
}

void test_shadow_depth_pass_and_binary_visibility() {
    const auto map = make_occluder_shadow_map();
    check(map->width() == 33U && map->height() == 33U,
          "directional shadow pass produces requested reusable depth dimensions");
    check_near(map->depth_at(16U, 16U), 0.25F,
               "light depth pass reuses raster depth ownership for occluder depth");
    check_near(map->depth_at(0U, 0U), 1.0F,
               "light depth pass keeps uncovered texels at finite far depth");

    const Triangle receiver = triangle_at_depth(0.5F);

    Framebuffer unshadowed(33U, 33U);
    Rasterizer baseline(
        unshadowed,
        {},
        {},
        full_white_light(),
        {},
        BaseColorSource::ConstantWhite);
    baseline.draw_triangle(receiver, Mat4::identity(), Mat4::identity(), Mat4::identity());
    check_color(unshadowed.color_at(16U, 16U), {1.0F, 1.0F, 1.0F},
                "disabled shadow state preserves full ambient plus diffuse lighting");

    Framebuffer shadowed(33U, 33U);
    shadowed_rasterizer(shadowed, shadow_state(map)).draw_triangle(
        receiver,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    check_color(shadowed.color_at(16U, 16U), {0.2F, 0.2F, 0.2F},
                "shadowed fragment keeps ambient while binary visibility removes diffuse contribution");

    Framebuffer biased(33U, 33U);
    shadowed_rasterizer(biased, shadow_state(map, 0.5F)).draw_triangle(
        receiver,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    check_color(biased.color_at(16U, 16U), {1.0F, 1.0F, 1.0F},
                "configured non-negative comparison bias deterministically restores visibility");

    Framebuffer outside(33U, 33U);
    shadowed_rasterizer(
        outside,
        shadow_state(map, 0.0F, Mat4::translation({3.0F, 0.0F, 0.0F})))
        .draw_triangle(receiver, Mat4::identity(), Mat4::identity(), Mat4::identity());
    check_color(outside.color_at(16U, 16U), {1.0F, 1.0F, 1.0F},
                "fragments outside the light clip volume are unshadowed");
}

Vertex lerp_vertex(const Vertex& a, const Vertex& b, float t) {
    VaryingPack varyings;
    varyings.count = a.varyings.count;
    varyings.interpolation = a.varyings.interpolation;
    for (std::size_t channel = 0U; channel < varyings.count; ++channel) {
        varyings.values[channel] = a.varyings.values[channel]
            + (b.varyings.values[channel] - a.varyings.values[channel]) * t;
    }
    return Vertex::with_varyings(
        a.position + (b.position - a.position) * t,
        varyings);
}

void test_camera_clipping_interpolates_light_clip_coordinate() {
    const Triangle crossing{{
        normal_vertex({-1.5F, 0.0F, 0.8F}),
        normal_vertex({0.6F, -0.7F, -0.2F}),
        normal_vertex({0.6F, 0.7F, -0.2F}),
    }};

    const float d2 = 1.0F + crossing[2].position.x;
    const float d0 = 1.0F + crossing[0].position.x;
    const float d1 = 1.0F + crossing[1].position.x;
    const float t20 = d2 / (d2 - d0);
    const float t01 = d0 / (d0 - d1);
    const Vertex intersection_20 = lerp_vertex(crossing[2], crossing[0], t20);
    const Vertex intersection_01 = lerp_vertex(crossing[0], crossing[1], t01);
    const Triangle manual_a{{intersection_20, intersection_01, crossing[1]}};
    const Triangle manual_b{{intersection_20, crossing[1], crossing[2]}};

    const auto constant_map = std::make_shared<const DepthTexture2D>(
        65U,
        65U,
        std::vector<float>(65U * 65U, 0.5F));
    const ShadowState shadow = shadow_state(constant_map);

    Framebuffer automatic(65U, 65U);
    Rasterizer automatic_rasterizer = shadowed_rasterizer(automatic, shadow);
    automatic_rasterizer.draw_triangle(
        crossing,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());

    Framebuffer manual(65U, 65U);
    Rasterizer manual_rasterizer = shadowed_rasterizer(manual, shadow);
    manual_rasterizer.draw_triangle(
        manual_a,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    manual_rasterizer.draw_triangle(
        manual_b,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());

    check(automatic.rgb8() == manual.rgb8(),
          "camera clipping linearly carries light clip coordinates before projective screen interpolation");
}

void test_model_prepared_ownership_and_mvp_fail_closed() {
    const auto map = make_occluder_shadow_map();
    const ModelAsset receiver = model_from_triangle(triangle_at_depth(0.5F));
    ModelRenderOptions options;
    options.directional_light = full_white_light();
    options.shadow_state = shadow_state(map);

    Framebuffer direct(33U, 33U);
    draw_model_asset(
        direct,
        receiver,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        options);

    PreparedModelSubmission prepared = prepare_model_asset(receiver, options);
    options.shadow_state.map.reset();
    check(static_cast<bool>(prepared.options().shadow_state.map),
          "prepared model owns shadow depth resource independently of source binding lifetime");

    Framebuffer prepared_framebuffer(33U, 33U);
    draw_prepared_model(
        prepared_framebuffer,
        prepared,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    check(direct.rgb8() == prepared_framebuffer.rgb8(),
          "direct and prepared shadow-enabled model submission are byte-equivalent");

    Framebuffer mvp_only(33U, 33U);
    const std::uint64_t before = mvp_only.fnv1a64();
    check_throws<std::invalid_argument>(
        [&] {
            draw_prepared_model(
                mvp_only,
                prepared,
                Mat4::identity());
        },
        "shadow-enabled prepared MVP-only submission is rejected");
    check(mvp_only.fnv1a64() == before && std::isinf(mvp_only.depth_at(16U, 16U)),
          "MVP-only shadow rejection happens before framebuffer mutation");
}

void test_shadow_with_msaa_alpha_to_coverage_and_prepared_list() {
    auto map = make_occluder_shadow_map();
    const ModelAsset receiver = model_from_triangle(triangle_at_depth(0.5F), 0.5F);
    ModelRenderOptions options;
    options.directional_light = full_white_light();
    options.shadow_state = shadow_state(map);
    options.alpha_to_coverage_state.enabled = true;
    PreparedModelSubmission prepared = prepare_model_asset(receiver, options);
    map.reset();

    Framebuffer sequential(33U, 33U, SampleCount::Four);
    draw_prepared_model(
        sequential,
        prepared,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());

    check_color(sequential.sample_color_at(16U, 16U, 0U), {0.2F, 0.2F, 0.2F},
                "shadow comparison executes on accepted MSAA sample zero before resolve");
    check_color(sequential.sample_color_at(16U, 16U, 1U), {0.2F, 0.2F, 0.2F},
                "shadow comparison executes on accepted MSAA sample one before resolve");
    check_color(sequential.sample_color_at(16U, 16U, 2U), {0.0F, 0.0F, 0.0F},
                "alpha-to-coverage still rejects sample two after shadow shading");
    check_color(sequential.sample_color_at(16U, 16U, 3U), {0.0F, 0.0F, 0.0F},
                "alpha-to-coverage still rejects sample three after shadow shading");
    check_near(sequential.sample_depth_at(16U, 16U, 0U), 0.75F,
               "accepted shadowed sample keeps ordinary depth ownership");
    check(std::isinf(sequential.sample_depth_at(16U, 16U, 2U)),
          "alpha-rejected shadow sample keeps depth untouched");

    const std::array<PreparedModelListEntry, 1> entries{{
        PreparedModelListEntry{&prepared, Mat4::identity()},
    }};
    Framebuffer listed(33U, 33U, SampleCount::Four);
    draw_prepared_model_list(listed, entries, Mat4::identity(), Mat4::identity());
    check(sequential.rgb8() == listed.rgb8(),
          "prepared list shadow rendering resolves byte-equivalently to sequential prepared submission");
    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        check_color(
            listed.sample_color_at(16U, 16U, sample),
            sequential.sample_color_at(16U, 16U, sample),
            "prepared list preserves exact per-sample shadow color ownership");
        const float listed_depth = listed.sample_depth_at(16U, 16U, sample);
        const float sequential_depth = sequential.sample_depth_at(16U, 16U, sample);
        check(
            (std::isinf(listed_depth) && std::isinf(sequential_depth))
                || std::fabs(listed_depth - sequential_depth) <= 2.0e-4F,
            "prepared list preserves exact per-sample shadow depth ownership");
    }
}

void test_invalid_shadow_contracts_fail_closed() {
    const auto map = make_occluder_shadow_map();
    const ModelAsset receiver = model_from_triangle(triangle_at_depth(0.5F));

    ModelRenderOptions no_light;
    no_light.shadow_state = shadow_state(map);
    check_throws<std::invalid_argument>(
        [&] { (void)prepare_model_asset(receiver, no_light); },
        "prepared shadow state requires directional lighting");

    ModelRenderOptions missing_map;
    missing_map.directional_light = full_white_light();
    missing_map.shadow_state = ShadowState{true, nullptr, Mat4::identity(), 0.0F};
    check_throws<std::invalid_argument>(
        [&] { (void)prepare_model_asset(receiver, missing_map); },
        "prepared shadow state rejects missing depth texture");

    ModelRenderOptions negative_bias;
    negative_bias.directional_light = full_white_light();
    negative_bias.shadow_state = shadow_state(map, -0.01F);
    check_throws<std::invalid_argument>(
        [&] { (void)prepare_model_asset(receiver, negative_bias); },
        "prepared shadow state rejects negative bias");

    Mat4 non_finite = Mat4::identity();
    non_finite(0U, 0U) = std::numeric_limits<float>::quiet_NaN();
    ModelRenderOptions invalid_matrix;
    invalid_matrix.directional_light = full_white_light();
    invalid_matrix.shadow_state = shadow_state(map, 0.0F, non_finite);
    check_throws<std::invalid_argument>(
        [&] { (void)prepare_model_asset(receiver, invalid_matrix); },
        "prepared shadow state rejects non-finite light transform");

    Framebuffer direct(33U, 33U);
    const std::uint64_t before = direct.fnv1a64();
    Rasterizer rasterizer(
        direct,
        {},
        {},
        full_white_light(),
        {},
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        {},
        {},
        {},
        {},
        {},
        ShadowState{true, nullptr, Mat4::identity(), 0.0F});
    check_throws<std::invalid_argument>(
        [&] {
            rasterizer.draw_triangle(
                triangle_at_depth(0.5F),
                Mat4::identity(),
                Mat4::identity(),
                Mat4::identity());
        },
        "direct shadow state rejects missing map");
    check(direct.fnv1a64() == before && std::isinf(direct.depth_at(16U, 16U)),
          "invalid direct shadow state fails before color and depth mutation");
}

}  // namespace

int main() {
    test_depth_resource_validation_and_capture();
    test_shadow_depth_pass_and_binary_visibility();
    test_camera_clipping_interpolates_light_clip_coordinate();
    test_model_prepared_ownership_and_mvp_fail_closed();
    test_shadow_with_msaa_alpha_to_coverage_and_prepared_list();
    test_invalid_shadow_contracts_fail_closed();

    if (failures != 0) {
        std::cerr << failures << " shadow test(s) failed\n";
        return 1;
    }
    std::cout << "shadow tests passed\n";
    return 0;
}
