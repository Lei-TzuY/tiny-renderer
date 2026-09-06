#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/rasterizer.hpp"
#include "tiny_renderer/texture.hpp"

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
        message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

void check_color(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check_near(actual.x, expected.x, message + " red");
    check_near(actual.y, expected.y, message + " green");
    check_near(actual.z, expected.z, message + " blue");
}

Vertex bare_vertex(const Vec3& position) {
    Vertex vertex;
    vertex.position = position;
    return vertex;
}

Vertex uv_vertex(const Vec3& position, float u, float v) {
    Vertex vertex;
    vertex.position = position;
    vertex.varyings.count = 2U;
    vertex.varyings.values[0] = u;
    vertex.varyings.values[1] = v;
    vertex.varyings.interpolation[0] = Interpolation::Smooth;
    vertex.varyings.interpolation[1] = Interpolation::Smooth;
    return vertex;
}

Triangle full_coverage_triangle() {
    return Triangle{{
        bare_vertex({-1.0F, -1.0F, 0.0F}),
        bare_vertex({3.0F, -1.0F, 0.0F}),
        bare_vertex({-1.0F, 3.0F, 0.0F}),
    }};
}

Triangle full_coverage_uv_triangle() {
    return Triangle{{
        uv_vertex({-1.0F, -1.0F, 0.0F}, 0.0F, 0.5F),
        uv_vertex({3.0F, -1.0F, 0.0F}, 2.0F, 0.5F),
        uv_vertex({-1.0F, 3.0F, 0.0F}, 0.0F, 0.5F),
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

void draw_uniform_alpha(
    Framebuffer& framebuffer,
    float opacity,
    AlphaToCoverageState alpha_to_coverage,
    BlendState blend_state = {},
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
        blend_state,
        alpha_to_coverage);
    rasterizer.draw_triangle(full_coverage_triangle(), Mat4::identity());
}

void check_center_coverage_count(
    const Framebuffer& framebuffer,
    std::size_t expected_count,
    const std::string& context) {
    constexpr Vec3 clear_color{0.0F, 0.0F, 1.0F};
    constexpr Vec3 written_color{1.0F, 0.0F, 0.0F};
    constexpr std::size_t x = 2U;
    constexpr std::size_t y = 2U;

    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        const bool expected_written = sample < expected_count;
        check_color(
            framebuffer.sample_color_at(x, y, sample),
            expected_written ? written_color : clear_color,
            context + " sample " + std::to_string(sample) + " color");
        check_near(
            framebuffer.sample_depth_at(x, y, sample),
            expected_written ? 0.5F : 0.9F,
            context + " sample " + std::to_string(sample) + " depth");
        check(
            framebuffer.sample_stencil_at(x, y, sample) == (expected_written ? 42U : 7U),
            context + " sample " + std::to_string(sample) + " stencil ownership");
    }
}

void test_uniform_thresholds_and_fixed_sample_order() {
    const std::array<std::pair<float, std::size_t>, 6> cases{{
        {0.0F, 0U},
        {0.125F, 1U},
        {0.375F, 2U},
        {0.625F, 3U},
        {0.875F, 4U},
        {1.0F, 4U},
    }};

    for (const auto& [opacity, expected_count] : cases) {
        Framebuffer framebuffer(5U, 5U, SampleCount::Four);
        framebuffer.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
        draw_uniform_alpha(framebuffer, opacity, AlphaToCoverageState{true});
        check_center_coverage_count(
            framebuffer,
            expected_count,
            "opacity " + std::to_string(opacity));
    }
}

void test_disabled_state_preserves_all_geometric_samples() {
    Framebuffer framebuffer(5U, 5U, SampleCount::Four);
    framebuffer.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    draw_uniform_alpha(framebuffer, 0.0F, AlphaToCoverageState{false});
    check_center_coverage_count(
        framebuffer,
        4U,
        "disabled alpha-to-coverage ignores zero opacity for coverage");
}

void test_single_sample_rejects_before_ownership() {
    Framebuffer framebuffer(5U, 5U, SampleCount::One);
    framebuffer.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_uniform_alpha(framebuffer, 0.5F, AlphaToCoverageState{true});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "alpha-to-coverage rejects a 1x target");
    check(framebuffer.rgb8() == before, "1x alpha-to-coverage rejection preserves RGB");
    check_near(framebuffer.depth_at(2U, 2U), 0.9F, "1x rejection preserves depth");
    check(framebuffer.stencil_at(2U, 2U) == 7U, "1x rejection preserves stencil");
}

void test_source_alpha_blending_composes_after_coverage_gate() {
    BlendState blend;
    blend.enabled = true;
    blend.source_factor = BlendFactor::SourceAlpha;
    blend.destination_factor = BlendFactor::OneMinusSourceAlpha;
    blend.operation = BlendOp::Add;

    Framebuffer framebuffer(5U, 5U, SampleCount::Four);
    framebuffer.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    draw_uniform_alpha(framebuffer, 0.5F, AlphaToCoverageState{true}, blend);

    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        const bool accepted = sample < 2U;
        check_color(
            framebuffer.sample_color_at(2U, 2U, sample),
            accepted ? Vec3{0.5F, 0.0F, 0.5F} : Vec3{0.0F, 0.0F, 1.0F},
            "source-alpha composition sample " + std::to_string(sample));
        check_near(
            framebuffer.sample_depth_at(2U, 2U, sample),
            accepted ? 0.5F : 0.9F,
            "source-alpha composition depth " + std::to_string(sample));
    }
}

void test_opacity_texture_is_sampled_per_multisample_location() {
    const Texture2D opacity_texture(
        2U,
        1U,
        std::vector<Vec3>{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}});
    TextureBinding binding;
    binding.u_channel = 0U;
    binding.v_channel = 1U;
    binding.sampler = {AddressMode::Clamp, AddressMode::Clamp, FilterMode::Nearest};
    binding.opacity_texture = &opacity_texture;

    MaterialState material;
    material.albedo = {1.0F, 0.0F, 0.0F};
    material.opacity = 1.0F;

    Framebuffer framebuffer(2U, 2U, SampleCount::Four);
    framebuffer.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    Rasterizer rasterizer(
        framebuffer,
        ColorBinding{99U, 99U, 99U},
        binding,
        {},
        material,
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        DepthState{},
        {},
        replace_stencil(42U),
        {},
        AlphaToCoverageState{true});
    rasterizer.draw_triangle(full_coverage_uv_triangle(), Mat4::identity());

    const std::array<bool, 4> expected_written{{false, true, false, true}};
    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        check_color(
            framebuffer.sample_color_at(0U, 0U, sample),
            expected_written[sample] ? Vec3{1.0F, 0.0F, 0.0F} : Vec3{0.0F, 0.0F, 1.0F},
            "per-sample opacity texture sample " + std::to_string(sample));
        check(
            framebuffer.sample_stencil_at(0U, 0U, sample) == (expected_written[sample] ? 42U : 7U),
            "per-sample opacity texture stencil ownership " + std::to_string(sample));
    }
}

void test_scissor_intersects_alpha_coverage() {
    ViewportState viewport;
    viewport.scissor = RasterRect{1U, 1U, 1U, 1U};

    Framebuffer framebuffer(4U, 4U, SampleCount::Four);
    framebuffer.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    draw_uniform_alpha(
        framebuffer,
        0.5F,
        AlphaToCoverageState{true},
        {},
        viewport);

    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        check_color(
            framebuffer.sample_color_at(1U, 1U, sample),
            sample < 2U ? Vec3{1.0F, 0.0F, 0.0F} : Vec3{0.0F, 0.0F, 1.0F},
            "scissored alpha coverage inside sample " + std::to_string(sample));
        check_color(
            framebuffer.sample_color_at(0U, 0U, sample),
            {0.0F, 0.0F, 1.0F},
            "scissored alpha coverage leaves outside sample untouched " + std::to_string(sample));
        check(framebuffer.sample_stencil_at(0U, 0U, sample) == 7U,
              "scissored alpha coverage leaves outside stencil untouched");
    }
}

void test_selected_range_propagates_alpha_to_coverage() {
    Mesh mesh;
    mesh.vertices = {
        bare_vertex({-1.0F, -1.0F, 0.0F}),
        bare_vertex({3.0F, -1.0F, 0.0F}),
        bare_vertex({-1.0F, 3.0F, 0.0F}),
    };
    mesh.triangles = {{0U, 1U, 2U}};

    MaterialState material;
    material.albedo = {1.0F, 0.0F, 0.0F};
    material.opacity = 0.5F;

    Framebuffer framebuffer(5U, 5U, SampleCount::Four);
    framebuffer.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
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
        {},
        replace_stencil(42U),
        {},
        AlphaToCoverageState{true});
    rasterizer.draw_mesh_range(mesh, DrawRange{0U, 1U}, Mat4::identity());
    check_center_coverage_count(framebuffer, 2U, "selected range alpha-to-coverage");
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

void check_multisample_equal(
    const Framebuffer& actual,
    const Framebuffer& expected,
    const std::string& context) {
    check(actual.rgb8() == expected.rgb8(), context + " resolved RGB");
    for (std::size_t y = 0U; y < actual.height(); ++y) {
        for (std::size_t x = 0U; x < actual.width(); ++x) {
            for (std::size_t sample = 0U; sample < 4U; ++sample) {
                const Vec3& a = actual.sample_color_at(x, y, sample);
                const Vec3& b = expected.sample_color_at(x, y, sample);
                check(a.x == b.x && a.y == b.y && a.z == b.z,
                      context + " exact sample RGB");
                const float da = actual.sample_depth_at(x, y, sample);
                const float db = expected.sample_depth_at(x, y, sample);
                check(da == db || (std::isinf(da) && std::isinf(db)),
                      context + " exact sample depth");
                check(actual.sample_stencil_at(x, y, sample)
                          == expected.sample_stencil_at(x, y, sample),
                      context + " exact sample stencil");
            }
        }
    }
}

void test_prepared_list_matches_sequential_alpha_coverage() {
    ModelRenderOptions red_options;
    red_options.alpha_to_coverage_state.enabled = true;
    ModelRenderOptions green_options = red_options;

    const PreparedModelSubmission red = prepare_model_asset(
        small_model({1.0F, 0.0F, 0.0F}, 0.5F),
        red_options);
    const PreparedModelSubmission green = prepare_model_asset(
        small_model({0.0F, 1.0F, 0.0F}, 0.75F),
        green_options);

    const Mat4 left = Mat4::translation({-0.4F, 0.0F, 0.0F});
    const Mat4 right = Mat4::translation({0.4F, 0.0F, 0.0F});

    Framebuffer sequential(33U, 33U, SampleCount::Four);
    draw_prepared_model(sequential, red, left, Mat4::identity(), Mat4::identity());
    draw_prepared_model(sequential, green, right, Mat4::identity(), Mat4::identity());

    const std::array<PreparedModelListEntry, 2> entries{{
        {&red, left},
        {&green, right},
    }};
    Framebuffer listed(33U, 33U, SampleCount::Four);
    draw_prepared_model_list(
        listed,
        entries,
        Mat4::identity(),
        Mat4::identity());

    check_multisample_equal(
        listed,
        sequential,
        "prepared list alpha-to-coverage matches sequential execution");
}

void test_later_prepared_a2c_entry_rejects_one_x_before_any_write() {
    ModelRenderOptions ordinary_options;
    ModelRenderOptions a2c_options;
    a2c_options.alpha_to_coverage_state.enabled = true;

    const PreparedModelSubmission ordinary = prepare_model_asset(
        small_model({1.0F, 0.0F, 0.0F}, 1.0F),
        ordinary_options);
    const PreparedModelSubmission a2c = prepare_model_asset(
        small_model({0.0F, 1.0F, 0.0F}, 0.5F),
        a2c_options);

    const std::array<PreparedModelListEntry, 2> entries{{
        {&ordinary, Mat4::translation({-0.4F, 0.0F, 0.0F})},
        {&a2c, Mat4::translation({0.4F, 0.0F, 0.0F})},
    }};

    Framebuffer framebuffer(17U, 17U, SampleCount::One);
    framebuffer.clear({0.1F, 0.2F, 0.3F}, 0.9F, 9U);
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_prepared_model_list(
            framebuffer,
            entries,
            Mat4::identity(),
            Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "later alpha-to-coverage list entry rejects a 1x target");
    check(framebuffer.rgb8() == before, "later list rejection preserves earlier RGB");
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            check_near(framebuffer.depth_at(x, y), 0.9F, "later list rejection preserves all depth");
            check(framebuffer.stencil_at(x, y) == 9U, "later list rejection preserves all stencil");
        }
    }
}

}  // namespace

int main() {
    test_uniform_thresholds_and_fixed_sample_order();
    test_disabled_state_preserves_all_geometric_samples();
    test_single_sample_rejects_before_ownership();
    test_source_alpha_blending_composes_after_coverage_gate();
    test_opacity_texture_is_sampled_per_multisample_location();
    test_scissor_intersects_alpha_coverage();
    test_selected_range_propagates_alpha_to_coverage();
    test_prepared_list_matches_sequential_alpha_coverage();
    test_later_prepared_a2c_entry_rejects_one_x_before_any_write();

    if (failures != 0) {
        std::cerr << failures << " alpha-to-coverage test(s) failed\n";
        return 1;
    }
    std::cout << "alpha-to-coverage tests passed\n";
    return 0;
}
