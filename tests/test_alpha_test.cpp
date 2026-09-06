#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/obj_loader.hpp"
#include "tiny_renderer/rasterizer.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 1.0e-6F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

void check_color(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check_near(actual.x, expected.x, message + " red");
    check_near(actual.y, expected.y, message + " green");
    check_near(actual.z, expected.z, message + " blue");
}

std::filesystem::path fixture_path(const char* name) {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for alpha test fixture tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

Vertex bare_vertex(const Vec3& position) {
    Vertex vertex;
    vertex.position = position;
    return vertex;
}

Triangle full_coverage_triangle() {
    return Triangle{{
        bare_vertex({-1.0F, -1.0F, 0.0F}),
        bare_vertex({3.0F, -1.0F, 0.0F}),
        bare_vertex({-1.0F, 3.0F, 0.0F}),
    }};
}

StencilState replace_stencil(std::uint8_t reference) {
    StencilState state;
    state.enabled = true;
    state.compare = StencilCompare::Always;
    state.reference = reference;
    state.pass = StencilOp::Replace;
    return state;
}

void draw_uniform_alpha_test(
    Framebuffer& framebuffer,
    float opacity,
    AlphaTestState alpha_test,
    AlphaToCoverageState alpha_to_coverage = {},
    ViewportState viewport_state = {}) {
    MaterialState material;
    material.albedo = {1.0F, 0.0F, 0.0F};
    material.opacity = opacity;

    Rasterizer rasterizer(
        framebuffer,
        ColorBinding{99U, 99U, 99U},
        {},
        {},
        material,
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        DepthState{},
        viewport_state,
        replace_stencil(42U),
        {},
        alpha_to_coverage,
        {},
        alpha_test);
    rasterizer.draw_triangle(full_coverage_triangle(), Mat4::identity());
}

void test_threshold_boundary_and_discard_side_effects() {
    Framebuffer survives(5U, 5U, SampleCount::One);
    survives.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    draw_uniform_alpha_test(survives, 0.5F, AlphaTestState{true, 0.5F});
    check_color(survives.color_at(2U, 2U), {1.0F, 0.0F, 0.0F},
                "opacity exactly equal to threshold survives");
    check_near(survives.depth_at(2U, 2U), 0.5F,
               "surviving alpha-tested fragment owns depth");
    check(survives.stencil_at(2U, 2U) == 42U,
          "surviving alpha-tested fragment owns stencil");

    Framebuffer discarded(5U, 5U, SampleCount::One);
    discarded.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    draw_uniform_alpha_test(discarded, 0.499F, AlphaTestState{true, 0.5F});
    check_color(discarded.color_at(2U, 2U), {0.0F, 0.0F, 1.0F},
                "opacity below threshold leaves RGB untouched");
    check_near(discarded.depth_at(2U, 2U), 0.9F,
               "discard happens before depth ownership");
    check(discarded.stencil_at(2U, 2U) == 7U,
          "discard happens before stencil ownership");
}

void test_disabled_state_preserves_opaque_pipeline_behavior() {
    Framebuffer framebuffer(5U, 5U, SampleCount::One);
    framebuffer.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    draw_uniform_alpha_test(framebuffer, 0.0F, AlphaTestState{false, 1.0F});
    check_color(framebuffer.color_at(2U, 2U), {1.0F, 0.0F, 0.0F},
                "disabled alpha test does not reject zero-opacity geometric coverage");
    check_near(framebuffer.depth_at(2U, 2U), 0.5F,
               "disabled alpha test preserves depth ownership");
    check(framebuffer.stencil_at(2U, 2U) == 42U,
          "disabled alpha test preserves stencil ownership");
}

void test_invalid_threshold_rejects_before_mutation() {
    Framebuffer framebuffer(5U, 5U, SampleCount::One);
    framebuffer.clear({0.1F, 0.2F, 0.3F}, 0.9F, 7U);
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_uniform_alpha_test(
            framebuffer,
            1.0F,
            AlphaTestState{true, std::numeric_limits<float>::quiet_NaN()});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "non-finite alpha threshold is rejected");
    check(framebuffer.rgb8() == before, "invalid alpha threshold preserves RGB");
    check_near(framebuffer.depth_at(2U, 2U), 0.9F,
               "invalid alpha threshold preserves depth");
    check(framebuffer.stencil_at(2U, 2U) == 7U,
          "invalid alpha threshold preserves stencil");
}

void check_four_sample_ownership(
    const Framebuffer& framebuffer,
    std::size_t expected_written,
    const std::string& context) {
    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        const bool written = sample < expected_written;
        check_color(
            framebuffer.sample_color_at(2U, 2U, sample),
            written ? Vec3{1.0F, 0.0F, 0.0F} : Vec3{0.0F, 0.0F, 1.0F},
            context + " sample color " + std::to_string(sample));
        check_near(
            framebuffer.sample_depth_at(2U, 2U, sample),
            written ? 0.5F : 0.9F,
            context + " sample depth " + std::to_string(sample));
        check(
            framebuffer.sample_stencil_at(2U, 2U, sample) == (written ? 42U : 7U),
            context + " sample stencil " + std::to_string(sample));
    }
}

void test_alpha_test_precedes_alpha_to_coverage() {
    Framebuffer survives(5U, 5U, SampleCount::Four);
    survives.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    draw_uniform_alpha_test(
        survives,
        0.5F,
        AlphaTestState{true, 0.5F},
        AlphaToCoverageState{true});
    check_four_sample_ownership(
        survives,
        2U,
        "surviving alpha test then applies deterministic A2C mask");

    Framebuffer discarded(5U, 5U, SampleCount::Four);
    discarded.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    draw_uniform_alpha_test(
        discarded,
        0.5F,
        AlphaTestState{true, 0.5001F},
        AlphaToCoverageState{true});
    check_four_sample_ownership(
        discarded,
        0U,
        "alpha discard removes every sample before A2C ownership");
}

ModelAsset small_model(const Vec3& albedo, float opacity) {
    ModelAsset asset;
    asset.mesh.vertices = {
        bare_vertex({-0.28F, -0.28F, 0.0F}),
        bare_vertex({0.28F, -0.28F, 0.0F}),
        bare_vertex({0.0F, 0.28F, 0.0F}),
    };
    asset.mesh.triangles = {{0U, 1U, 2U}};

    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material.albedo = albedo;
    draw.material.opacity = opacity;
    asset.draws = {std::move(draw)};
    return asset;
}

void check_framebuffers_equal(
    const Framebuffer& actual,
    const Framebuffer& expected,
    const std::string& context) {
    check(actual.rgb8() == expected.rgb8(), context + " RGB");
    for (std::size_t y = 0U; y < actual.height(); ++y) {
        for (std::size_t x = 0U; x < actual.width(); ++x) {
            const float a_depth = actual.depth_at(x, y);
            const float b_depth = expected.depth_at(x, y);
            check(a_depth == b_depth || (std::isinf(a_depth) && std::isinf(b_depth)),
                  context + " depth");
            check(actual.stencil_at(x, y) == expected.stencil_at(x, y),
                  context + " stencil");
        }
    }
}

void test_prepared_list_matches_sequential_alpha_test() {
    ModelRenderOptions options;
    options.alpha_test_state = {true, 0.5F};

    const PreparedModelSubmission discarded = prepare_model_asset(
        small_model({1.0F, 0.0F, 0.0F}, 0.4F),
        options);
    const PreparedModelSubmission surviving = prepare_model_asset(
        small_model({0.0F, 1.0F, 0.0F}, 0.8F),
        options);

    const Mat4 left = Mat4::translation({-0.4F, 0.0F, 0.0F});
    const Mat4 right = Mat4::translation({0.4F, 0.0F, 0.0F});

    Framebuffer sequential(33U, 33U);
    sequential.clear({0.1F, 0.2F, 0.3F}, 0.9F, 5U);
    draw_prepared_model(
        sequential,
        discarded,
        left,
        Mat4::identity(),
        Mat4::identity());
    draw_prepared_model(
        sequential,
        surviving,
        right,
        Mat4::identity(),
        Mat4::identity());

    const std::array<PreparedModelListEntry, 2> entries{{
        {&discarded, left},
        {&surviving, right},
    }};
    Framebuffer listed(33U, 33U);
    listed.clear({0.1F, 0.2F, 0.3F}, 0.9F, 5U);
    draw_prepared_model_list(
        listed,
        entries,
        Mat4::identity(),
        Mat4::identity());

    check_framebuffers_equal(
        listed,
        sequential,
        "prepared list alpha test matches sequential execution");
}

ModelRenderOptions cutout_options(bool enabled) {
    ModelRenderOptions options;
    options.u_channel = 0U;
    options.v_channel = 1U;
    options.sampler = {AddressMode::Clamp, AddressMode::Clamp, FilterMode::Nearest};
    options.alpha_test_state = {enabled, 0.3F};
    return options;
}

PreparedModelSubmission load_cutout_prepared(bool enabled) {
    return prepare_model_asset(
        load_obj_model_asset_file(fixture_path("cutout_shadow.obj")),
        cutout_options(enabled));
}

void test_file_driven_cutout_camera_and_shadow_agree() {
    const PreparedModelSubmission cutout = load_cutout_prepared(true);

    Framebuffer camera(17U, 17U);
    camera.clear({0.0F, 0.0F, 1.0F}, 1.0F, 0U);
    draw_prepared_model(camera, cutout, Mat4::identity());

    check_color(camera.color_at(4U, 8U), {0.0F, 0.0F, 1.0F},
                "imported low-opacity texel is discarded in camera pass");
    check_near(camera.depth_at(4U, 8U), 1.0F,
               "camera cutout leaves low-opacity depth clear");
    check_color(camera.color_at(12U, 8U), {1.0F, 1.0F, 1.0F},
                "imported high-opacity texel survives in camera pass");
    check_near(camera.depth_at(12U, 8U), 0.5F,
               "camera surviving cutout writes expected depth");

    const std::array<PreparedModelListEntry, 1> entries{{
        {&cutout, Mat4::identity()},
    }};
    const std::shared_ptr<const DepthTexture2D> shadow = render_directional_shadow_map(
        entries,
        Mat4::identity(),
        DirectionalShadowMapOptions{17U, 17U, CullMode::None, FrontFace::CounterClockwise});
    check_near(shadow->depth_at(4U, 8U), 1.0F,
               "cutout shadow leaves low-opacity light depth clear");
    check_near(shadow->depth_at(12U, 8U), 0.5F,
               "cutout shadow keeps high-opacity caster depth");

    const PreparedModelSubmission solid = load_cutout_prepared(false);
    const std::array<PreparedModelListEntry, 1> solid_entries{{
        {&solid, Mat4::identity()},
    }};
    const std::shared_ptr<const DepthTexture2D> solid_shadow = render_directional_shadow_map(
        solid_entries,
        Mat4::identity(),
        DirectionalShadowMapOptions{17U, 17U, CullMode::None, FrontFace::CounterClockwise});
    check_near(solid_shadow->depth_at(4U, 8U), 0.5F,
               "disabled alpha test preserves M32 solid-caster behavior");
    check_near(solid_shadow->depth_at(12U, 8U), 0.5F,
               "disabled alpha test preserves solid caster across opacity texels");
}

void test_shadow_preflights_later_invalid_cutout_binding() {
    const PreparedModelSubmission good = load_cutout_prepared(true);

    ModelRenderOptions invalid_options = cutout_options(true);
    invalid_options.u_channel = 99U;
    const PreparedModelSubmission invalid = prepare_model_asset(
        load_obj_model_asset_file(fixture_path("cutout_shadow.obj")),
        invalid_options);

    const std::array<PreparedModelListEntry, 2> entries{{
        {&good, Mat4::identity()},
        {&invalid, Mat4::identity()},
    }};

    bool threw = false;
    try {
        (void)render_directional_shadow_map(
            entries,
            Mat4::identity(),
            DirectionalShadowMapOptions{17U, 17U, CullMode::None, FrontFace::CounterClockwise});
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(threw, "shadow pass rejects a later invalid opacity UV binding during preflight");
}

}  // namespace

int main() {
    test_threshold_boundary_and_discard_side_effects();
    test_disabled_state_preserves_opaque_pipeline_behavior();
    test_invalid_threshold_rejects_before_mutation();
    test_alpha_test_precedes_alpha_to_coverage();
    test_prepared_list_matches_sequential_alpha_test();
    test_file_driven_cutout_camera_and_shadow_agree();
    test_shadow_preflights_later_invalid_cutout_binding();

    if (failures != 0) {
        std::cerr << failures << " alpha test test(s) failed\n";
        return 1;
    }
    std::cout << "alpha test tests passed\n";
    return 0;
}
