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

std::size_t count_non_black(const Framebuffer& framebuffer) {
    const std::vector<std::uint8_t> bytes = framebuffer.rgb8();
    std::size_t count = 0U;
    for (std::size_t i = 0U; i + 2U < bytes.size(); i += 3U) {
        if (bytes[i] != 0U || bytes[i + 1U] != 0U || bytes[i + 2U] != 0U) {
            ++count;
        }
    }
    return count;
}

void check_unchanged(
    const Framebuffer& framebuffer,
    const std::vector<std::uint8_t>& before,
    const std::string& context) {
    check(framebuffer.rgb8() == before, context + " preserves framebuffer color");
    check(std::isinf(framebuffer.depth_at(32U, 32U)), context + " preserves framebuffer depth");
}

Triangle ccw_triangle(const Vec3& color = {1.0F, 0.25F, 0.125F}) {
    return {{
        Vertex{{-0.7F, -0.6F, 0.0F}, color},
        Vertex{{0.7F, -0.6F, 0.0F}, color},
        Vertex{{0.0F, 0.7F, 0.0F}, color},
    }};
}

Triangle reverse_triangle(const Triangle& triangle) {
    return {{triangle[0], triangle[2], triangle[1]}};
}

Rasterizer culling_rasterizer(
    Framebuffer& framebuffer,
    CullMode cull_mode,
    FrontFace front_face = FrontFace::CounterClockwise) {
    return Rasterizer(
        framebuffer,
        {},
        {},
        {},
        {},
        BaseColorSource::Auto,
        cull_mode,
        front_face);
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

void test_none_preserves_legacy_orientation_behavior() {
    const Triangle front = ccw_triangle();
    const Triangle back = reverse_triangle(front);

    Framebuffer legacy(65U, 65U);
    Rasterizer legacy_rasterizer(legacy);
    legacy_rasterizer.draw_triangle(front, Mat4::identity());

    Framebuffer explicit_none(65U, 65U);
    culling_rasterizer(explicit_none, CullMode::None).draw_triangle(front, Mat4::identity());

    Framebuffer reversed(65U, 65U);
    Rasterizer reversed_rasterizer(reversed);
    reversed_rasterizer.draw_triangle(back, Mat4::identity());

    check(explicit_none.rgb8() == legacy.rgb8(),
          "explicit CullMode::None is byte-identical to the legacy default path");
    check(explicit_none.fnv1a64() == legacy.fnv1a64(),
          "explicit CullMode::None preserves the legacy framebuffer hash");
    check(reversed.rgb8() == legacy.rgb8(),
          "legacy no-culling path remains orientation-normalized for reversed triangles");
}

void test_front_face_and_cull_modes_in_ndc() {
    const Triangle front = ccw_triangle();
    const Triangle back = reverse_triangle(front);

    Framebuffer back_ccw(65U, 65U);
    culling_rasterizer(back_ccw, CullMode::Back, FrontFace::CounterClockwise)
        .draw_triangle(front, Mat4::identity());
    check(count_non_black(back_ccw) > 0U, "CCW NDC triangle survives back-face culling when CCW is front");

    Framebuffer back_cw(65U, 65U);
    culling_rasterizer(back_cw, CullMode::Back, FrontFace::Clockwise)
        .draw_triangle(front, Mat4::identity());
    check(count_non_black(back_cw) == 0U, "CCW NDC triangle is culled when CW is configured as front");

    Framebuffer front_ccw(65U, 65U);
    culling_rasterizer(front_ccw, CullMode::Front, FrontFace::CounterClockwise)
        .draw_triangle(front, Mat4::identity());
    check(count_non_black(front_ccw) == 0U, "front-face culling removes a CCW front triangle");

    Framebuffer front_cw(65U, 65U);
    culling_rasterizer(front_cw, CullMode::Front, FrontFace::Clockwise)
        .draw_triangle(front, Mat4::identity());
    check(count_non_black(front_cw) > 0U, "front-face culling keeps a CCW triangle when CW is configured as front");

    Framebuffer reversed_back_ccw(65U, 65U);
    culling_rasterizer(reversed_back_ccw, CullMode::Back, FrontFace::CounterClockwise)
        .draw_triangle(back, Mat4::identity());
    check(count_non_black(reversed_back_ccw) == 0U, "reversing submitted winding reverses back-face classification");
}

void test_clipped_fan_uses_post_clip_ndc_winding() {
    const Vec3 color{0.25F, 0.8F, 0.2F};
    const Triangle crossing{{
        Vertex{{-2.0F, 0.0F, 0.0F}, color},
        Vertex{{0.0F, -0.7F, 0.0F}, color},
        Vertex{{0.0F, 0.7F, 0.0F}, color},
    }};

    Framebuffer none(65U, 65U);
    culling_rasterizer(none, CullMode::None).draw_triangle(crossing, Mat4::identity());

    Framebuffer kept(65U, 65U);
    culling_rasterizer(kept, CullMode::Back, FrontFace::CounterClockwise)
        .draw_triangle(crossing, Mat4::identity());

    Framebuffer reversed(65U, 65U);
    culling_rasterizer(reversed, CullMode::Back, FrontFace::CounterClockwise)
        .draw_triangle(reverse_triangle(crossing), Mat4::identity());

    check(count_non_black(none) > 0U, "left-clipped fixture produces visible fragments without culling");
    check(kept.rgb8() == none.rgb8(),
          "front-facing clipped fan is byte-identical to the no-culling clipped result");
    check(reversed.rgb8() != none.rgb8() && count_non_black(reversed) == 0U,
          "reversed clipped polygon is culled after clipping and perspective divide");
}

void test_indexed_mesh_and_range_share_culling_state() {
    Mesh mesh;
    mesh.vertices = {
        Vertex{{-0.9F, -0.7F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        Vertex{{-0.1F, -0.7F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        Vertex{{-0.5F, 0.7F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        Vertex{{0.1F, -0.7F, 0.0F}, {0.0F, 1.0F, 0.0F}},
        Vertex{{0.9F, -0.7F, 0.0F}, {0.0F, 1.0F, 0.0F}},
        Vertex{{0.5F, 0.7F, 0.0F}, {0.0F, 1.0F, 0.0F}},
    };
    mesh.triangles = {{0U, 1U, 2U}, {3U, 5U, 4U}};

    Framebuffer full(65U, 65U);
    culling_rasterizer(full, CullMode::Back).draw_mesh(mesh, Mat4::identity());

    Framebuffer front_range(65U, 65U);
    culling_rasterizer(front_range, CullMode::Back)
        .draw_mesh_range(mesh, DrawRange{0U, 1U}, Mat4::identity());

    Framebuffer back_range(65U, 65U);
    culling_rasterizer(back_range, CullMode::Back)
        .draw_mesh_range(mesh, DrawRange{1U, 1U}, Mat4::identity());

    check(full.rgb8() == front_range.rgb8(),
          "indexed full-mesh culling matches submitting only its front-facing range");
    check(count_non_black(front_range) > 0U, "front-facing selected range survives back-face culling");
    check(count_non_black(back_range) == 0U, "back-facing selected range is culled through the shared range path");
}

void test_prepared_model_list_propagates_culling() {
    const Triangle front = ccw_triangle();
    const Triangle back = reverse_triangle(front);

    ModelRenderOptions options;
    options.cull_mode = CullMode::Back;
    options.front_face = FrontFace::CounterClockwise;

    PreparedModelSubmission front_plan = prepare_model_asset(
        model_asset_from_triangle(front, {0.9F, 0.1F, 0.1F}),
        options);
    PreparedModelSubmission back_plan = prepare_model_asset(
        model_asset_from_triangle(back, {0.1F, 0.9F, 0.1F}),
        options);

    const std::array<PreparedModelListEntry, 2U> entries{{
        {&front_plan, Mat4::identity()},
        {&back_plan, Mat4::identity()},
    }};

    Framebuffer listed(65U, 65U);
    draw_prepared_model_list(
        listed,
        std::span<const PreparedModelListEntry>{entries},
        Mat4::identity(),
        Mat4::identity());

    Framebuffer front_only(65U, 65U);
    draw_prepared_model(
        front_only,
        front_plan,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());

    check(listed.rgb8() == front_only.rgb8(),
          "heterogeneous prepared-list execution propagates culling and removes its back-facing plan");
}

void test_invalid_culling_state_fails_closed() {
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
        static_cast<CullMode>(99),
        FrontFace::CounterClockwise);
    bool raster_threw = false;
    try {
        invalid.draw_triangle(ccw_triangle(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        raster_threw = true;
    }
    check(raster_threw, "unknown CullMode is rejected by direct triangle submission");
    check_unchanged(framebuffer, before, "unknown CullMode rejection");

    ModelRenderOptions options;
    options.front_face = static_cast<FrontFace>(99);
    bool prepare_threw = false;
    try {
        (void)prepare_model_asset(
            model_asset_from_triangle(ccw_triangle(), {1.0F, 1.0F, 1.0F}),
            options);
    } catch (const std::invalid_argument&) {
        prepare_threw = true;
    }
    check(prepare_threw, "unknown FrontFace is rejected during framebuffer-independent model preparation");
}

void test_degenerate_triangle_is_not_misclassified_as_a_face() {
    const Triangle degenerate{{
        Vertex{{-0.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        Vertex{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        Vertex{{0.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}},
    }};

    Framebuffer framebuffer(65U, 65U);
    culling_rasterizer(framebuffer, CullMode::Back).draw_triangle(degenerate, Mat4::identity());
    check(count_non_black(framebuffer) == 0U,
          "degenerate projected triangle remains deterministically discarded under culling");
}

}  // namespace

int main() {
    try {
        test_none_preserves_legacy_orientation_behavior();
        test_front_face_and_cull_modes_in_ndc();
        test_clipped_fan_uses_post_clip_ndc_winding();
        test_indexed_mesh_and_range_share_culling_state();
        test_prepared_model_list_propagates_culling();
        test_invalid_culling_state_fails_closed();
        test_degenerate_triangle_is_not_misclassified_as_a_face();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " culling test(s) failed\n";
        return 1;
    }
    std::cout << "all culling tests passed\n";
    return 0;
}
