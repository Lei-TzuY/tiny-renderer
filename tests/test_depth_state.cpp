#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

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

bool near(float a, float b, float epsilon = 1.0e-6F) {
    return std::fabs(a - b) <= epsilon;
}

Triangle colored_triangle(float z, const Vec3& color) {
    return {{
        Vertex{{-0.7F, -0.65F, z}, color},
        Vertex{{0.7F, -0.65F, z}, color},
        Vertex{{0.0F, 0.7F, z}, color},
    }};
}

Rasterizer rasterizer_with_depth(Framebuffer& framebuffer, DepthState state) {
    return Rasterizer(
        framebuffer,
        {},
        {},
        {},
        {},
        BaseColorSource::Auto,
        CullMode::None,
        FrontFace::CounterClockwise,
        state);
}

ModelAsset model_asset_from_triangle(const Triangle& triangle, const Vec3& albedo) {
    ModelAsset asset;
    asset.mesh.vertices.assign(triangle.begin(), triangle.end());
    asset.mesh.triangles = {{0U, 1U, 2U}};

    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material.albedo = albedo;
    asset.draws = {draw};
    return asset;
}

void test_framebuffer_compare_modes() {
    struct Case {
        DepthCompare compare;
        float incoming;
        bool expected;
        const char* name;
    };
    const std::array<Case, 10U> cases{{
        {DepthCompare::Less, 0.4F, true, "Less accepts smaller"},
        {DepthCompare::Less, 0.5F, false, "Less rejects equal"},
        {DepthCompare::LessEqual, 0.5F, true, "LessEqual accepts equal"},
        {DepthCompare::LessEqual, 0.6F, false, "LessEqual rejects larger"},
        {DepthCompare::Greater, 0.6F, true, "Greater accepts larger"},
        {DepthCompare::Greater, 0.5F, false, "Greater rejects equal"},
        {DepthCompare::GreaterEqual, 0.5F, true, "GreaterEqual accepts equal"},
        {DepthCompare::GreaterEqual, 0.4F, false, "GreaterEqual rejects smaller"},
        {DepthCompare::Always, 0.5F, true, "Always accepts"},
        {DepthCompare::Never, 0.4F, false, "Never rejects"},
    }};

    for (const Case& test_case : cases) {
        Framebuffer framebuffer(1U, 1U);
        framebuffer.clear({0.0F, 0.0F, 0.0F}, 0.5F);
        const bool accepted = framebuffer.depth_test_and_write(
            0U,
            0U,
            test_case.incoming,
            {1.0F, 0.0F, 0.0F},
            DepthState{test_case.compare, true});
        check(accepted == test_case.expected, test_case.name);
    }
}

void test_depth_write_disable_preserves_depth_but_updates_color() {
    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({0.0F, 0.0F, 0.0F}, 0.8F);

    const bool accepted = framebuffer.depth_test_and_write(
        0U,
        0U,
        0.2F,
        {0.25F, 0.5F, 0.75F},
        DepthState{DepthCompare::Less, false});

    check(accepted, "depth-write-disabled fragment still passes the configured comparison");
    check(near(framebuffer.depth_at(0U, 0U), 0.8F),
          "depth-write-disabled fragment leaves stored depth unchanged");
    const Vec3 color = framebuffer.color_at(0U, 0U);
    check(near(color.x, 0.25F) && near(color.y, 0.5F) && near(color.z, 0.75F),
          "depth-write-disabled fragment still updates color on a passing comparison");
}

void test_default_depth_state_is_legacy_byte_compatible() {
    const Triangle far_triangle = colored_triangle(0.5F, {0.1F, 0.2F, 0.9F});
    const Triangle near_triangle = colored_triangle(-0.5F, {0.9F, 0.2F, 0.1F});

    Framebuffer legacy(65U, 65U);
    Rasterizer legacy_rasterizer(legacy);
    legacy_rasterizer.draw_triangle(far_triangle, Mat4::identity());
    legacy_rasterizer.draw_triangle(near_triangle, Mat4::identity());

    Framebuffer explicit_state(65U, 65U);
    Rasterizer explicit_rasterizer = rasterizer_with_depth(
        explicit_state,
        DepthState{DepthCompare::Less, true});
    explicit_rasterizer.draw_triangle(far_triangle, Mat4::identity());
    explicit_rasterizer.draw_triangle(near_triangle, Mat4::identity());

    check(explicit_state.rgb8() == legacy.rgb8(),
          "explicit Less + depth-write enabled is byte-identical to the legacy default path");
    check(explicit_state.fnv1a64() == legacy.fnv1a64(),
          "explicit default depth state preserves the legacy framebuffer hash");
}

void test_equal_depth_strict_and_nonstrict_ownership() {
    const Triangle red = colored_triangle(0.0F, {1.0F, 0.0F, 0.0F});
    const Triangle green = colored_triangle(0.0F, {0.0F, 1.0F, 0.0F});

    Framebuffer strict(65U, 65U);
    Rasterizer strict_rasterizer = rasterizer_with_depth(
        strict,
        DepthState{DepthCompare::Less, true});
    strict_rasterizer.draw_triangle(red, Mat4::identity());
    strict_rasterizer.draw_triangle(green, Mat4::identity());

    Framebuffer nonstrict(65U, 65U);
    Rasterizer nonstrict_rasterizer = rasterizer_with_depth(
        nonstrict,
        DepthState{DepthCompare::LessEqual, true});
    nonstrict_rasterizer.draw_triangle(red, Mat4::identity());
    nonstrict_rasterizer.draw_triangle(green, Mat4::identity());

    const Vec3 strict_center = strict.color_at(32U, 32U);
    const Vec3 nonstrict_center = nonstrict.color_at(32U, 32U);
    check(strict_center.x > 0.9F && strict_center.y < 0.1F,
          "strict Less preserves first-submitted ownership at equal depth");
    check(nonstrict_center.y > 0.9F && nonstrict_center.x < 0.1F,
          "LessEqual permits the later equal-depth fragment to overwrite color/depth ownership");
}

Mat4 reversed_z_projection(float fov_y, float aspect, float near_plane, float far_plane) {
    Mat4 projection = Mat4::perspective(fov_y, aspect, near_plane, far_plane);
    for (std::size_t column = 0U; column < 4U; ++column) {
        projection(2U, column) = -projection(2U, column);
    }
    return projection;
}

void test_reversed_z_matches_forward_z_visibility() {
    const Triangle far_triangle = colored_triangle(-4.0F, {0.1F, 0.2F, 0.9F});
    const Triangle near_triangle = colored_triangle(-2.0F, {0.9F, 0.2F, 0.1F});
    const Mat4 forward_projection = Mat4::perspective(radians(70.0F), 1.0F, 0.5F, 10.0F);
    const Mat4 reverse_projection = reversed_z_projection(radians(70.0F), 1.0F, 0.5F, 10.0F);

    Framebuffer forward(97U, 97U);
    Rasterizer forward_rasterizer = rasterizer_with_depth(
        forward,
        DepthState{DepthCompare::Less, true});
    forward_rasterizer.draw_triangle(far_triangle, Mat4::identity(), Mat4::identity(), forward_projection);
    forward_rasterizer.draw_triangle(near_triangle, Mat4::identity(), Mat4::identity(), forward_projection);

    Framebuffer reversed(97U, 97U);
    reversed.clear({0.0F, 0.0F, 0.0F}, 0.0F);
    Rasterizer reversed_rasterizer = rasterizer_with_depth(
        reversed,
        DepthState{DepthCompare::Greater, true});
    reversed_rasterizer.draw_triangle(far_triangle, Mat4::identity(), Mat4::identity(), reverse_projection);
    reversed_rasterizer.draw_triangle(near_triangle, Mat4::identity(), Mat4::identity(), reverse_projection);

    check(reversed.rgb8() == forward.rgb8(),
          "reversed-Z Greater + zero clear resolves overlapping geometry byte-identically to forward-Z Less");
    check(reversed.fnv1a64() == forward.fnv1a64(),
          "reversed-Z preserves deterministic visible-color hashing for the equivalent scene");
    check(reversed.depth_at(48U, 48U) > 0.0F && forward.depth_at(48U, 48U) < 1.0F,
          "both forward and reversed depth configurations store a finite winning depth");
}

void test_prepared_list_propagates_per_plan_depth_state() {
    const Triangle geometry = colored_triangle(0.0F, {1.0F, 1.0F, 1.0F});

    ModelRenderOptions strict_options;
    strict_options.depth_state = DepthState{DepthCompare::Less, true};
    ModelRenderOptions overwrite_options;
    overwrite_options.depth_state = DepthState{DepthCompare::LessEqual, true};

    PreparedModelSubmission red = prepare_model_asset(
        model_asset_from_triangle(geometry, {1.0F, 0.0F, 0.0F}),
        strict_options);
    PreparedModelSubmission green = prepare_model_asset(
        model_asset_from_triangle(geometry, {0.0F, 1.0F, 0.0F}),
        overwrite_options);

    const std::array<PreparedModelListEntry, 2U> entries{{
        {&red, Mat4::identity()},
        {&green, Mat4::identity()},
    }};

    Framebuffer listed(65U, 65U);
    draw_prepared_model_list(
        listed,
        std::span<const PreparedModelListEntry>{entries},
        Mat4::identity(),
        Mat4::identity());

    const Vec3 center = listed.color_at(32U, 32U);
    check(center.y > 0.9F && center.x < 0.1F,
          "heterogeneous prepared-list execution preserves each plan's depth comparison state");
}

void test_invalid_depth_state_fails_closed() {
    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    Rasterizer invalid(
        framebuffer,
        {},
        {},
        {},
        {},
        BaseColorSource::Auto,
        CullMode::None,
        FrontFace::CounterClockwise,
        DepthState{static_cast<DepthCompare>(99), true});

    bool raster_threw = false;
    try {
        invalid.draw_triangle(colored_triangle(0.0F, {1.0F, 1.0F, 1.0F}), Mat4::identity());
    } catch (const std::invalid_argument&) {
        raster_threw = true;
    }
    check(raster_threw, "unknown DepthCompare is rejected by direct raster submission");
    check(framebuffer.rgb8() == before, "invalid direct depth state preserves framebuffer color");
    check(std::isinf(framebuffer.depth_at(32U, 32U)), "invalid direct depth state preserves framebuffer depth");

    ModelRenderOptions options;
    options.depth_state = DepthState{static_cast<DepthCompare>(99), true};
    bool prepare_threw = false;
    try {
        (void)prepare_model_asset(
            model_asset_from_triangle(
                colored_triangle(0.0F, {1.0F, 1.0F, 1.0F}),
                {1.0F, 1.0F, 1.0F}),
            options);
    } catch (const std::invalid_argument&) {
        prepare_threw = true;
    }
    check(prepare_threw,
          "unknown DepthCompare is rejected during framebuffer-independent prepared-model construction");
}

}  // namespace

int main() {
    try {
        test_framebuffer_compare_modes();
        test_depth_write_disable_preserves_depth_but_updates_color();
        test_default_depth_state_is_legacy_byte_compatible();
        test_equal_depth_strict_and_nonstrict_ownership();
        test_reversed_z_matches_forward_z_visibility();
        test_prepared_list_propagates_per_plan_depth_state();
        test_invalid_depth_state_fails_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " depth-state test(s) failed\n";
        return 1;
    }

    std::cout << "all depth-state tests passed\n";
    return 0;
}
