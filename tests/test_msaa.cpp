#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/rasterizer.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check_near(float actual, float expected, const std::string& message, float epsilon = 1.0e-5F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

void check_vec3_near(
    const Vec3& actual,
    const Vec3& expected,
    const std::string& message,
    float epsilon = 1.0e-5F) {
    check_near(actual.x, expected.x, message + " x", epsilon);
    check_near(actual.y, expected.y, message + " y", epsilon);
    check_near(actual.z, expected.z, message + " z", epsilon);
}

bool is_black(const Vec3& color) {
    return color.x == 0.0F && color.y == 0.0F && color.z == 0.0F;
}

Triangle colored_triangle(float z, const Vec3& color) {
    return Triangle{{
        {{-0.75F, -0.75F, z}, color},
        {{0.75F, -0.75F, z}, color},
        {{0.0F, 0.75F, z}, color},
    }};
}

ModelAsset one_triangle_asset(const Vec3& albedo) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex{{-0.8F, -0.8F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        Vertex{{0.8F, -0.8F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        Vertex{{0.0F, 0.8F, 0.0F}, {1.0F, 1.0F, 1.0F}},
    };
    asset.mesh.triangles = {{0U, 1U, 2U}};

    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material.albedo = albedo;
    asset.draws = {draw};
    return asset;
}

std::size_t count_non_black_samples(const Framebuffer& framebuffer) {
    std::size_t count = 0U;
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            for (std::size_t sample = 0U; sample < framebuffer.samples_per_pixel(); ++sample) {
                if (!is_black(framebuffer.sample_color_at(x, y, sample))) {
                    ++count;
                }
            }
        }
    }
    return count;
}

void check_multisample_storage_equal(
    const Framebuffer& actual,
    const Framebuffer& expected,
    const std::string& context) {
    check(actual.width() == expected.width(), context + " width");
    check(actual.height() == expected.height(), context + " height");
    check(actual.sample_count() == expected.sample_count(), context + " sample count");
    if (actual.width() != expected.width()
        || actual.height() != expected.height()
        || actual.samples_per_pixel() != expected.samples_per_pixel()) {
        return;
    }

    for (std::size_t y = 0U; y < actual.height(); ++y) {
        for (std::size_t x = 0U; x < actual.width(); ++x) {
            for (std::size_t sample = 0U; sample < actual.samples_per_pixel(); ++sample) {
                const Vec3& a = actual.sample_color_at(x, y, sample);
                const Vec3& b = expected.sample_color_at(x, y, sample);
                check(
                    a.x == b.x && a.y == b.y && a.z == b.z,
                    context + " sample color equality");

                const float actual_depth = actual.sample_depth_at(x, y, sample);
                const float expected_depth = expected.sample_depth_at(x, y, sample);
                check(
                    actual_depth == expected_depth
                        || (std::isinf(actual_depth) && std::isinf(expected_depth)),
                    context + " sample depth equality");
                check(
                    actual.sample_stencil_at(x, y, sample)
                        == expected.sample_stencil_at(x, y, sample),
                    context + " sample stencil equality");
            }
        }
    }
}

void test_single_sample_compatibility() {
    Framebuffer legacy(16U, 16U);
    Framebuffer explicit_one(16U, 16U, SampleCount::One);
    Rasterizer legacy_rasterizer(legacy);
    Rasterizer explicit_rasterizer(explicit_one);

    const Triangle triangle = colored_triangle(0.0F, {0.25F, 0.5F, 0.75F});
    legacy_rasterizer.draw_triangle(triangle, Mat4::identity());
    explicit_rasterizer.draw_triangle(triangle, Mat4::identity());

    constexpr std::uint64_t expected_hash = 0x224b8e00590a737cULL;
    check(legacy.samples_per_pixel() == 1U, "default framebuffer remains single-sample");
    check(explicit_one.samples_per_pixel() == 1U, "explicit SampleCount::One has one sample");
    check(legacy.rgb8() == explicit_one.rgb8(), "default and explicit 1x raster output are byte-identical");
    check(legacy.fnv1a64() == expected_hash, "1x deterministic hash remains unchanged");
    check(explicit_one.fnv1a64() == expected_hash, "explicit 1x deterministic hash remains unchanged");
}

void test_framebuffer_sample_storage_and_resolve() {
    Framebuffer framebuffer(1U, 1U, SampleCount::Four);
    check(framebuffer.samples_per_pixel() == 4U, "4x framebuffer exposes four samples per pixel");

    check(framebuffer.test_and_write_sample(0U, 0U, 0U, 0.25F, {1.0F, 0.0F, 0.0F}),
          "sample zero accepts a passing fragment");
    check(framebuffer.test_and_write_sample(0U, 0U, 2U, 0.5F, {0.0F, 1.0F, 0.0F}),
          "sample two accepts an independent passing fragment");

    check_vec3_near(framebuffer.sample_color_at(0U, 0U, 0U), {1.0F, 0.0F, 0.0F}, "sample zero color");
    check_vec3_near(framebuffer.sample_color_at(0U, 0U, 1U), {0.0F, 0.0F, 0.0F}, "sample one remains clear");
    check_vec3_near(framebuffer.sample_color_at(0U, 0U, 2U), {0.0F, 1.0F, 0.0F}, "sample two color");
    check_vec3_near(framebuffer.sample_color_at(0U, 0U, 3U), {0.0F, 0.0F, 0.0F}, "sample three remains clear");
    check_vec3_near(framebuffer.color_at(0U, 0U), {0.25F, 0.25F, 0.0F}, "4x resolve averages sample RGB");
    check_near(framebuffer.sample_depth_at(0U, 0U, 0U), 0.25F, "sample zero depth");
    check_near(framebuffer.sample_depth_at(0U, 0U, 2U), 0.5F, "sample two depth");
    check(std::isinf(framebuffer.sample_depth_at(0U, 0U, 1U)), "unwritten sample depth remains clear");
    check_near(framebuffer.depth_at(0U, 0U), 0.25F, "legacy depth view explicitly aliases sample zero");

    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    bool pixel_entry_threw = false;
    try {
        framebuffer.test_and_write(0U, 0U, 0.1F, {1.0F, 1.0F, 1.0F});
    } catch (const std::invalid_argument&) {
        pixel_entry_threw = true;
    }
    check(pixel_entry_threw, "pixel-level fragment ownership rejects multisample targets");
    check(framebuffer.rgb8() == before, "rejected pixel-level multisample write preserves resolved RGB");

    bool sample_index_threw = false;
    try {
        framebuffer.test_and_write_sample(0U, 0U, 4U, 0.1F, {1.0F, 1.0F, 1.0F});
    } catch (const std::out_of_range&) {
        sample_index_threw = true;
    }
    check(sample_index_threw, "out-of-range sample index is rejected");
    check(framebuffer.rgb8() == before, "out-of-range sample rejection preserves resolved RGB");
}

void test_source_alpha_blends_before_resolve() {
    Framebuffer framebuffer(1U, 1U, SampleCount::Four);
    framebuffer.clear({0.0F, 0.0F, 1.0F});

    BlendState blend;
    blend.enabled = true;
    blend.source_factor = BlendFactor::SourceAlpha;
    blend.destination_factor = BlendFactor::OneMinusSourceAlpha;

    framebuffer.test_and_write_sample(
        0U, 0U, 0U, 0.5F, {1.0F, 0.0F, 0.0F}, {}, {}, blend, 0.5F);
    framebuffer.test_and_write_sample(
        0U, 0U, 1U, 0.5F, {1.0F, 0.0F, 0.0F}, {}, {}, blend, 1.0F);

    check_vec3_near(
        framebuffer.sample_color_at(0U, 0U, 0U),
        {0.5F, 0.0F, 0.5F},
        "source alpha blends sample zero against its own destination");
    check_vec3_near(
        framebuffer.sample_color_at(0U, 0U, 1U),
        {1.0F, 0.0F, 0.0F},
        "opaque source alpha replaces sample one");
    check_vec3_near(
        framebuffer.color_at(0U, 0U),
        {0.375F, 0.0F, 0.625F},
        "resolved RGB averages already-blended sample colors");
}

void test_depth_and_stencil_are_sample_local() {
    Framebuffer framebuffer(1U, 1U, SampleCount::Four);

    StencilState stencil;
    stencil.enabled = true;
    stencil.reference = 7U;
    stencil.pass = StencilOp::Replace;
    stencil.depth_fail = StencilOp::IncrementClamp;

    check(framebuffer.test_and_write_sample(
              0U, 0U, 2U, 0.7F, {1.0F, 0.0F, 0.0F}, {}, stencil),
          "sample two initial depth/stencil write passes");
    check(framebuffer.sample_stencil_at(0U, 0U, 2U) == 7U,
          "sample two pass operation updates only sample two stencil");
    check_near(framebuffer.sample_depth_at(0U, 0U, 2U), 0.7F, "sample two stores depth");
    check(framebuffer.sample_stencil_at(0U, 0U, 1U) == 0U,
          "sample one stencil remains independent");
    check(std::isinf(framebuffer.sample_depth_at(0U, 0U, 1U)),
          "sample one depth remains independent");

    check(!framebuffer.test_and_write_sample(
              0U, 0U, 2U, 0.9F, {0.0F, 1.0F, 0.0F}, {}, stencil),
          "farther fragment fails depth on sample two");
    check(framebuffer.sample_stencil_at(0U, 0U, 2U) == 8U,
          "sample two depth-fail operation updates the same sample stencil");
    check_vec3_near(
        framebuffer.sample_color_at(0U, 0U, 2U),
        {1.0F, 0.0F, 0.0F},
        "depth rejection preserves sample two color");

    check(framebuffer.test_and_write_sample(
              0U, 0U, 1U, 0.9F, {0.0F, 1.0F, 0.0F}, {}, stencil),
          "same depth passes independently on clear sample one");
    check(framebuffer.sample_stencil_at(0U, 0U, 1U) == 7U,
          "sample one receives its own pass stencil operation");
    check_vec3_near(framebuffer.color_at(0U, 0U), {0.25F, 0.25F, 0.0F},
                    "resolved color reflects independent depth ownership");
}

void test_fixed_pattern_quarter_coverage() {
    // On a 3x3 target this maps to screen vertices (0,0), (2,0), (0,1.5).
    // Pixel (1,0) contains the fixed 4x sample points at quarter offsets; only
    // (1.25, 0.25), sample zero, lies inside the sloped edge.
    const Triangle triangle{{
        {{-1.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        {{1.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        {{-1.0F, -0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}},
    }};

    Framebuffer framebuffer(3U, 3U, SampleCount::Four);
    Rasterizer rasterizer(framebuffer);
    rasterizer.draw_triangle(triangle, Mat4::identity());

    check_vec3_near(framebuffer.sample_color_at(1U, 0U, 0U), {1.0F, 0.0F, 0.0F},
                    "quarter-coverage sample zero is covered");
    for (std::size_t sample = 1U; sample < 4U; ++sample) {
        check(is_black(framebuffer.sample_color_at(1U, 0U, sample)),
              "quarter-coverage remaining samples are uncovered");
    }
    check_vec3_near(framebuffer.color_at(1U, 0U), {0.25F, 0.0F, 0.0F},
                    "one covered sample resolves to quarter-intensity RGB");
    check_vec3_near(framebuffer.color_at(0U, 0U), {1.0F, 0.0F, 0.0F},
                    "fully covered neighboring pixel resolves opaque red");
}

void test_shared_edge_has_single_sample_owner() {
    const Triangle lower_right{{
        {{-0.5F, -0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        {{0.5F, -0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        {{0.5F, 0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}},
    }};
    const Triangle upper_left{{
        {{-0.5F, -0.5F, 0.0F}, {0.0F, 1.0F, 0.0F}},
        {{0.5F, 0.5F, 0.0F}, {0.0F, 1.0F, 0.0F}},
        {{-0.5F, 0.5F, 0.0F}, {0.0F, 1.0F, 0.0F}},
    }};

    Framebuffer forward(33U, 33U, SampleCount::Four);
    Rasterizer forward_rasterizer(forward);
    forward_rasterizer.draw_triangle(lower_right, Mat4::identity());
    forward_rasterizer.draw_triangle(upper_left, Mat4::identity());

    Framebuffer reverse(33U, 33U, SampleCount::Four);
    Rasterizer reverse_rasterizer(reverse);
    reverse_rasterizer.draw_triangle(upper_left, Mat4::identity());
    reverse_rasterizer.draw_triangle(lower_right, Mat4::identity());

    check(forward.rgb8() == reverse.rgb8(),
          "4x shared-edge resolved ownership is independent of draw order");
    check_multisample_storage_equal(forward, reverse, "4x shared-edge exact sample ownership");
    check(count_non_black_samples(forward) == 1024U,
          "two 4x triangles cover the 16x16 quad at exactly four samples per pixel");
}

void test_clipping_and_scissor_bound_multisample_side_effects() {
    Framebuffer framebuffer(9U, 9U, SampleCount::Four);
    ViewportState viewport;
    viewport.scissor = RasterRect{2U, 2U, 3U, 3U};
    Rasterizer rasterizer(
        framebuffer,
        {},
        {},
        {},
        {},
        BaseColorSource::Auto,
        CullMode::None,
        FrontFace::CounterClockwise,
        {},
        viewport);

    const Triangle crossing{{
        {{-3.0F, -0.8F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        {{0.9F, -0.8F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        {{0.2F, 0.95F, 0.0F}, {1.0F, 1.0F, 1.0F}},
    }};
    rasterizer.draw_triangle(crossing, Mat4::identity());

    std::size_t inside_samples = 0U;
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            const bool inside_scissor = x >= 2U && x < 5U && y >= 2U && y < 5U;
            for (std::size_t sample = 0U; sample < 4U; ++sample) {
                const bool written = !is_black(framebuffer.sample_color_at(x, y, sample));
                if (inside_scissor && written) {
                    ++inside_samples;
                }
                if (!inside_scissor) {
                    check(!written, "scissor prevents color writes to every outside sample");
                    check(std::isinf(framebuffer.sample_depth_at(x, y, sample)),
                          "scissor prevents depth writes to every outside sample");
                    check(framebuffer.sample_stencil_at(x, y, sample) == 0U,
                          "scissor prevents stencil writes to every outside sample");
                }
            }
        }
    }
    check(inside_samples > 0U, "clipped triangle produces multisample coverage inside the scissor");
}

void test_prepared_list_matches_sequential_on_4x_target() {
    PreparedModelSubmission red = prepare_model_asset(one_triangle_asset({1.0F, 0.0F, 0.0F}));
    PreparedModelSubmission green = prepare_model_asset(one_triangle_asset({0.0F, 1.0F, 0.0F}));

    const Mat4 left = Mat4::translation({-0.35F, 0.0F, 0.0F}) * Mat4::scale({0.55F, 0.55F, 1.0F});
    const Mat4 right = Mat4::translation({0.35F, 0.0F, -0.2F}) * Mat4::scale({0.55F, 0.55F, 1.0F});
    const std::array<PreparedModelListEntry, 2U> entries{{
        {&red, left},
        {&green, right},
    }};

    Framebuffer sequential(65U, 65U, SampleCount::Four);
    draw_prepared_model(sequential, red, left, Mat4::identity(), Mat4::identity());
    draw_prepared_model(sequential, green, right, Mat4::identity(), Mat4::identity());

    Framebuffer listed(65U, 65U, SampleCount::Four);
    draw_prepared_model_list(
        listed,
        std::span<const PreparedModelListEntry>{entries},
        Mat4::identity(),
        Mat4::identity());

    check(listed.rgb8() == sequential.rgb8(),
          "4x heterogeneous prepared list resolves byte-identically to sequential submission");
    check(listed.fnv1a64() == sequential.fnv1a64(),
          "4x heterogeneous prepared list preserves deterministic resolved hashing");
    check_multisample_storage_equal(listed, sequential, "4x prepared list exact sample storage");
    check(count_non_black_samples(listed) > 0U, "4x prepared list fixture actually rasterizes samples");
}

void test_invalid_sample_count_and_storage_overflow_fail_closed() {
    bool invalid_count_threw = false;
    try {
        Framebuffer invalid(1U, 1U, static_cast<SampleCount>(2U));
        (void)invalid;
    } catch (const std::invalid_argument&) {
        invalid_count_threw = true;
    }
    check(invalid_count_threw, "unsupported framebuffer sample count is rejected");

    bool overflow_threw = false;
    try {
        Framebuffer impossible(std::numeric_limits<std::size_t>::max(), 2U, SampleCount::Four);
        (void)impossible;
    } catch (const std::overflow_error&) {
        overflow_threw = true;
    }
    check(overflow_threw, "framebuffer pixel/sample storage multiplication fails closed on overflow");
}

}  // namespace

int main() {
    try {
        test_single_sample_compatibility();
        test_framebuffer_sample_storage_and_resolve();
        test_source_alpha_blends_before_resolve();
        test_depth_and_stencil_are_sample_local();
        test_fixed_pattern_quarter_coverage();
        test_shared_edge_has_single_sample_owner();
        test_clipping_and_scissor_bound_multisample_side_effects();
        test_prepared_list_matches_sequential_on_4x_target();
        test_invalid_sample_count_and_storage_overflow_fail_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " MSAA test(s) failed\n";
        return 1;
    }
    std::cout << "all deterministic MSAA tests passed\n";
    return 0;
}
