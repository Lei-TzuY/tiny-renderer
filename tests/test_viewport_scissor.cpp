#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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

bool inside_rect(const RasterRect& rect, std::size_t x, std::size_t y) {
    if (rect.width == 0U || rect.height == 0U) {
        return false;
    }
    return x >= rect.x && y >= rect.y
        && x < rect.x + rect.width && y < rect.y + rect.height;
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

Triangle colored_triangle(float z, const Vec3& color) {
    return {{
        Vertex{{-0.72F, -0.62F, z}, color},
        Vertex{{0.68F, -0.58F, z}, color},
        Vertex{{-0.05F, 0.73F, z}, color},
    }};
}

Triangle left_clipped_triangle(const Vec3& color) {
    return {{
        Vertex{{-2.0F, 0.0F, 0.0F}, color},
        Vertex{{0.0F, -0.72F, 0.0F}, color},
        Vertex{{0.0F, 0.72F, 0.0F}, color},
    }};
}

Mesh full_screen_quad(const Vec3& color) {
    Mesh mesh;
    mesh.vertices = {
        Vertex{{-1.0F, -1.0F, 0.0F}, color},
        Vertex{{1.0F, -1.0F, 0.0F}, color},
        Vertex{{1.0F, 1.0F, 0.0F}, color},
        Vertex{{-1.0F, 1.0F, 0.0F}, color},
    };
    mesh.triangles = {{0U, 1U, 2U}, {0U, 2U, 3U}};
    return mesh;
}

Rasterizer rasterizer_with_viewport(
    Framebuffer& framebuffer,
    ViewportState state,
    CullMode cull_mode = CullMode::None) {
    return Rasterizer(
        framebuffer,
        {},
        {},
        {},
        {},
        BaseColorSource::Auto,
        cull_mode,
        FrontFace::CounterClockwise,
        {},
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

void check_region_matches(
    const Framebuffer& target,
    const RasterRect& target_region,
    const Framebuffer& reference,
    const std::string& context) {
    check(target_region.width == reference.width() && target_region.height == reference.height(),
          context + " uses matching reference dimensions");

    const std::vector<std::uint8_t> target_bytes = target.rgb8();
    const std::vector<std::uint8_t> reference_bytes = reference.rgb8();
    bool colors_match = true;
    bool depths_match = true;

    for (std::size_t y = 0U; y < reference.height(); ++y) {
        for (std::size_t x = 0U; x < reference.width(); ++x) {
            const std::size_t target_pixel = (target_region.y + y) * target.width() + target_region.x + x;
            const std::size_t reference_pixel = y * reference.width() + x;
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
                colors_match = colors_match
                    && target_bytes[target_pixel * 3U + channel] == reference_bytes[reference_pixel * 3U + channel];
            }

            const float target_depth = target.depth_at(target_region.x + x, target_region.y + y);
            const float reference_depth = reference.depth_at(x, y);
            if (std::isinf(target_depth) || std::isinf(reference_depth)) {
                depths_match = depths_match && std::isinf(target_depth) && std::isinf(reference_depth);
            } else {
                depths_match = depths_match && near(target_depth, reference_depth);
            }
        }
    }

    check(colors_match, context + " is byte-identical inside the translated viewport");
    check(depths_match, context + " preserves depth values inside the translated viewport");
}

void check_outside_region_unwritten(
    const Framebuffer& framebuffer,
    const RasterRect& region,
    const std::string& context) {
    const std::vector<std::uint8_t> bytes = framebuffer.rgb8();
    bool color_unchanged = true;
    bool depth_unchanged = true;
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            if (inside_rect(region, x, y)) {
                continue;
            }
            const std::size_t pixel = y * framebuffer.width() + x;
            color_unchanged = color_unchanged
                && bytes[pixel * 3U] == 0U
                && bytes[pixel * 3U + 1U] == 0U
                && bytes[pixel * 3U + 2U] == 0U;
            depth_unchanged = depth_unchanged && std::isinf(framebuffer.depth_at(x, y));
        }
    }
    check(color_unchanged, context + " does not write color outside the viewport");
    check(depth_unchanged, context + " does not write depth outside the viewport");
}

void test_default_full_viewport_is_legacy_compatible() {
    const Triangle far_triangle = colored_triangle(0.45F, {0.15F, 0.25F, 0.9F});
    const Triangle near_triangle = colored_triangle(-0.35F, {0.9F, 0.25F, 0.1F});

    Framebuffer legacy(65U, 65U);
    Rasterizer legacy_rasterizer(legacy);
    legacy_rasterizer.draw_triangle(far_triangle, Mat4::identity());
    legacy_rasterizer.draw_triangle(near_triangle, Mat4::identity());

    ViewportState full_state;
    full_state.viewport = RasterRect{0U, 0U, 65U, 65U};
    Framebuffer explicit_full(65U, 65U);
    Rasterizer explicit_rasterizer = rasterizer_with_viewport(explicit_full, full_state);
    explicit_rasterizer.draw_triangle(far_triangle, Mat4::identity());
    explicit_rasterizer.draw_triangle(near_triangle, Mat4::identity());

    check(explicit_full.rgb8() == legacy.rgb8(),
          "explicit full-frame viewport is byte-identical to the legacy default path");
    check(explicit_full.fnv1a64() == legacy.fnv1a64(),
          "explicit full-frame viewport preserves the legacy framebuffer hash");
    check(near(explicit_full.depth_at(32U, 32U), legacy.depth_at(32U, 32U)),
          "explicit full-frame viewport preserves legacy depth at an overlapping sample");
}

void test_subviewport_matches_small_framebuffer_reference() {
    const Triangle triangle = colored_triangle(0.0F, {0.8F, 0.2F, 0.35F});

    Framebuffer reference(33U, 29U);
    Rasterizer reference_rasterizer(reference);
    reference_rasterizer.draw_triangle(triangle, Mat4::identity());

    const RasterRect viewport{17U, 13U, 33U, 29U};
    ViewportState state;
    state.viewport = viewport;
    Framebuffer target(79U, 67U);
    rasterizer_with_viewport(target, state).draw_triangle(triangle, Mat4::identity());

    check_region_matches(target, viewport, reference, "sub-viewport triangle rendering");
    check_outside_region_unwritten(target, viewport, "sub-viewport triangle rendering");
}

void test_scissor_uses_half_open_sample_bounds() {
    const RasterRect scissor{9U, 11U, 7U, 5U};
    ViewportState state;
    state.scissor = scissor;

    Framebuffer framebuffer(65U, 65U);
    rasterizer_with_viewport(framebuffer, state)
        .draw_mesh(full_screen_quad({0.25F, 0.75F, 0.5F}), Mat4::identity());

    bool mask_matches = true;
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            const Vec3 color = framebuffer.color_at(x, y);
            const bool written = color.x != 0.0F || color.y != 0.0F || color.z != 0.0F;
            const bool expected = inside_rect(scissor, x, y);
            mask_matches = mask_matches && written == expected;
            if (expected) {
                mask_matches = mask_matches && std::isfinite(framebuffer.depth_at(x, y));
            } else {
                mask_matches = mask_matches && std::isinf(framebuffer.depth_at(x, y));
            }
        }
    }

    check(mask_matches,
          "scissor includes exactly [x,x+width) x [y,y+height) samples for an interior covered region");
    check(count_non_black(framebuffer) == scissor.width * scissor.height,
          "scissor half-open edge ownership produces the exact expected covered sample count");
}

void test_viewport_and_scissor_compose_without_changing_shading() {
    const RasterRect viewport{12U, 10U, 33U, 29U};
    const RasterRect scissor{5U, 15U, 50U, 8U};
    const Mesh quad = full_screen_quad({0.35F, 0.6F, 0.85F});

    ViewportState viewport_only;
    viewport_only.viewport = viewport;
    Framebuffer reference(65U, 65U);
    rasterizer_with_viewport(reference, viewport_only).draw_mesh(quad, Mat4::identity());

    ViewportState combined;
    combined.viewport = viewport;
    combined.scissor = scissor;
    Framebuffer clipped(65U, 65U);
    rasterizer_with_viewport(clipped, combined).draw_mesh(quad, Mat4::identity());

    const std::vector<std::uint8_t> reference_bytes = reference.rgb8();
    const std::vector<std::uint8_t> clipped_bytes = clipped.rgb8();
    bool composition_matches = true;
    for (std::size_t y = 0U; y < clipped.height(); ++y) {
        for (std::size_t x = 0U; x < clipped.width(); ++x) {
            const std::size_t pixel = y * clipped.width() + x;
            if (inside_rect(scissor, x, y)) {
                for (std::size_t channel = 0U; channel < 3U; ++channel) {
                    composition_matches = composition_matches
                        && clipped_bytes[pixel * 3U + channel] == reference_bytes[pixel * 3U + channel];
                }
                const float reference_depth = reference.depth_at(x, y);
                const float clipped_depth = clipped.depth_at(x, y);
                if (std::isinf(reference_depth)) {
                    composition_matches = composition_matches && std::isinf(clipped_depth);
                } else {
                    composition_matches = composition_matches && near(reference_depth, clipped_depth);
                }
            } else {
                composition_matches = composition_matches
                    && clipped_bytes[pixel * 3U] == 0U
                    && clipped_bytes[pixel * 3U + 1U] == 0U
                    && clipped_bytes[pixel * 3U + 2U] == 0U
                    && std::isinf(clipped.depth_at(x, y));
            }
        }
    }

    check(composition_matches,
          "scissor masks the existing viewport result without changing covered color or depth semantics");
    check(count_non_black(clipped) > 0U && count_non_black(clipped) < count_non_black(reference),
          "viewport-plus-scissor fixture exercises a non-empty strict subset of viewport coverage");
}

void test_clipped_geometry_preserves_ndc_culling_before_subviewport_mapping() {
    const Triangle crossing = left_clipped_triangle({0.2F, 0.8F, 0.4F});

    Framebuffer reference(37U, 31U);
    rasterizer_with_viewport(reference, {}, CullMode::Back)
        .draw_triangle(crossing, Mat4::identity());

    const RasterRect viewport{20U, 12U, 37U, 31U};
    ViewportState state;
    state.viewport = viewport;
    Framebuffer target(83U, 61U);
    rasterizer_with_viewport(target, state, CullMode::Back)
        .draw_triangle(crossing, Mat4::identity());

    check(count_non_black(reference) > 0U,
          "left-clipped CCW fixture remains front-facing under NDC back-face culling");
    check_region_matches(target, viewport, reference,
                         "clipped-and-culled sub-viewport rendering");
    check_outside_region_unwritten(target, viewport,
                                   "clipped-and-culled sub-viewport rendering");
}

void test_selected_range_propagates_viewport_state() {
    const Triangle triangle = colored_triangle(0.0F, {0.75F, 0.3F, 0.15F});
    Mesh mesh;
    mesh.vertices.assign(triangle.begin(), triangle.end());
    mesh.triangles = {{0U, 1U, 2U}};

    ViewportState state;
    state.viewport = RasterRect{10U, 8U, 31U, 27U};

    Framebuffer direct(65U, 65U);
    rasterizer_with_viewport(direct, state).draw_triangle(triangle, Mat4::identity());

    Framebuffer ranged(65U, 65U);
    rasterizer_with_viewport(ranged, state)
        .draw_mesh_range(mesh, DrawRange{0U, 1U}, Mat4::identity());

    check(ranged.rgb8() == direct.rgb8(),
          "selected range execution propagates viewport state byte-identically to direct triangle submission");
    check(ranged.fnv1a64() == direct.fnv1a64(),
          "selected range execution preserves deterministic viewport hashing");
}

void test_zero_area_scissor_is_a_deterministic_noop() {
    ViewportState state;
    state.scissor = RasterRect{10U, 10U, 0U, 5U};

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.25F, 0.375F}, 0.75F);
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    rasterizer_with_viewport(framebuffer, state)
        .draw_mesh(full_screen_quad({1.0F, 0.0F, 0.0F}), Mat4::identity());

    check(framebuffer.rgb8() == before,
          "zero-width scissor is a legal deterministic color no-op");
    check(near(framebuffer.depth_at(32U, 32U), 0.75F),
          "zero-width scissor preserves existing depth values");
}

void test_invalid_rectangles_fail_closed() {
    const Triangle triangle = colored_triangle(0.0F, {1.0F, 1.0F, 1.0F});

    {
        Framebuffer framebuffer(65U, 65U);
        framebuffer.clear({0.125F, 0.25F, 0.375F});
        const std::vector<std::uint8_t> before = framebuffer.rgb8();
        ViewportState state;
        state.viewport = RasterRect{0U, 0U, 0U, 10U};
        bool threw = false;
        try {
            rasterizer_with_viewport(framebuffer, state).draw_triangle(triangle, Mat4::identity());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "zero-extent viewport is rejected by direct submission");
        check(framebuffer.rgb8() == before && std::isinf(framebuffer.depth_at(32U, 32U)),
              "zero-extent viewport rejection is fail-closed");
    }

    {
        Framebuffer framebuffer(65U, 65U);
        framebuffer.clear({0.125F, 0.25F, 0.375F});
        const std::vector<std::uint8_t> before = framebuffer.rgb8();
        ViewportState state;
        state.scissor = RasterRect{60U, 60U, 10U, 10U};
        bool threw = false;
        try {
            rasterizer_with_viewport(framebuffer, state).draw_triangle(triangle, Mat4::identity());
        } catch (const std::out_of_range&) {
            threw = true;
        }
        check(threw, "out-of-target scissor is rejected by direct submission");
        check(framebuffer.rgb8() == before && std::isinf(framebuffer.depth_at(32U, 32U)),
              "out-of-target scissor rejection is fail-closed");
    }

    {
        Framebuffer framebuffer(65U, 65U);
        framebuffer.clear({0.125F, 0.25F, 0.375F});
        const std::vector<std::uint8_t> before = framebuffer.rgb8();
        ViewportState state;
        state.viewport = RasterRect{
            std::numeric_limits<std::size_t>::max() - 1U,
            0U,
            4U,
            1U,
        };
        bool threw = false;
        try {
            rasterizer_with_viewport(framebuffer, state).draw_triangle(triangle, Mat4::identity());
        } catch (const std::overflow_error&) {
            threw = true;
        }
        check(threw, "overflowing viewport bounds are rejected before target arithmetic");
        check(framebuffer.rgb8() == before && std::isinf(framebuffer.depth_at(32U, 32U)),
              "overflowing viewport rejection is fail-closed");
    }
}

void test_prepared_model_validation_and_list_preflight_are_fail_closed() {
    const Triangle geometry = colored_triangle(0.0F, {1.0F, 1.0F, 1.0F});

    ModelRenderOptions invalid_static;
    invalid_static.viewport_state.viewport = RasterRect{0U, 0U, 0U, 10U};
    bool prepare_threw = false;
    try {
        (void)prepare_model_asset(
            model_asset_from_triangle(geometry, {1.0F, 1.0F, 1.0F}),
            invalid_static);
    } catch (const std::invalid_argument&) {
        prepare_threw = true;
    }
    check(prepare_threw,
          "framebuffer-independent prepared-model construction rejects an invalid viewport definition");

    PreparedModelSubmission valid = prepare_model_asset(
        model_asset_from_triangle(geometry, {0.9F, 0.1F, 0.1F}));

    ModelRenderOptions target_dependent;
    target_dependent.viewport_state.viewport = RasterRect{30U, 30U, 20U, 20U};
    PreparedModelSubmission later_invalid = prepare_model_asset(
        model_asset_from_triangle(geometry, {0.1F, 0.9F, 0.1F}),
        target_dependent);

    const std::array<PreparedModelListEntry, 2U> entries{{
        {&valid, Mat4::identity()},
        {&later_invalid, Mat4::identity()},
    }};

    Framebuffer framebuffer(40U, 40U);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    bool list_threw = false;
    try {
        draw_prepared_model_list(
            framebuffer,
            std::span<const PreparedModelListEntry>{entries},
            Mat4::identity(),
            Mat4::identity());
    } catch (const std::out_of_range&) {
        list_threw = true;
    }

    check(list_threw,
          "later target-dependent invalid viewport rejects the complete prepared list");
    check(framebuffer.rgb8() == before && std::isinf(framebuffer.depth_at(20U, 20U)),
          "prepared-list viewport preflight rejects before any earlier valid entry writes");
}

void test_prepared_list_propagates_per_plan_viewports() {
    const Triangle geometry = colored_triangle(0.0F, {1.0F, 1.0F, 1.0F});

    ModelRenderOptions red_options;
    red_options.viewport_state.viewport = RasterRect{4U, 6U, 25U, 25U};
    ModelRenderOptions green_options;
    green_options.viewport_state.viewport = RasterRect{36U, 31U, 25U, 25U};

    PreparedModelSubmission red = prepare_model_asset(
        model_asset_from_triangle(geometry, {1.0F, 0.0F, 0.0F}),
        red_options);
    PreparedModelSubmission green = prepare_model_asset(
        model_asset_from_triangle(geometry, {0.0F, 1.0F, 0.0F}),
        green_options);

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

    Framebuffer sequential(65U, 65U);
    draw_prepared_model(
        sequential,
        red,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    draw_prepared_model(
        sequential,
        green,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());

    check(listed.rgb8() == sequential.rgb8(),
          "heterogeneous prepared list preserves each plan's viewport state byte-identically to sequential draws");
    check(listed.fnv1a64() == sequential.fnv1a64(),
          "heterogeneous prepared list preserves deterministic per-plan viewport hashing");
}

void test_prepared_instances_propagate_viewport_state() {
    const Triangle geometry = colored_triangle(0.0F, {1.0F, 1.0F, 1.0F});
    ModelRenderOptions options;
    options.viewport_state.viewport = RasterRect{15U, 12U, 35U, 35U};

    const PreparedModelSubmission prepared = prepare_model_asset(
        model_asset_from_triangle(geometry, {0.8F, 0.4F, 0.2F}),
        options);
    const std::array<Mat4, 2U> models{{
        Mat4::translation({-0.25F, 0.0F, 0.0F}) * Mat4::scale({0.7F, 0.7F, 1.0F}),
        Mat4::translation({0.25F, 0.0F, 0.0F}) * Mat4::scale({0.7F, 0.7F, 1.0F}),
    }};

    Framebuffer sequential(65U, 65U);
    for (const Mat4& model : models) {
        draw_prepared_model(
            sequential,
            prepared,
            model,
            Mat4::identity(),
            Mat4::identity());
    }

    Framebuffer batched(65U, 65U);
    draw_prepared_model_instances(
        batched,
        prepared,
        std::span<const Mat4>{models},
        Mat4::identity(),
        Mat4::identity());

    check(batched.rgb8() == sequential.rgb8(),
          "prepared instance batch propagates viewport state byte-identically to sequential prepared draws");
    check(batched.fnv1a64() == sequential.fnv1a64(),
          "prepared instance batch preserves deterministic viewport hashing");
}

}  // namespace

int main() {
    try {
        test_default_full_viewport_is_legacy_compatible();
        test_subviewport_matches_small_framebuffer_reference();
        test_scissor_uses_half_open_sample_bounds();
        test_viewport_and_scissor_compose_without_changing_shading();
        test_clipped_geometry_preserves_ndc_culling_before_subviewport_mapping();
        test_selected_range_propagates_viewport_state();
        test_zero_area_scissor_is_a_deterministic_noop();
        test_invalid_rectangles_fail_closed();
        test_prepared_model_validation_and_list_preflight_are_fail_closed();
        test_prepared_list_propagates_per_plan_viewports();
        test_prepared_instances_propagate_viewport_state();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " viewport/scissor test(s) failed\n";
        return 1;
    }

    std::cout << "all viewport/scissor tests passed\n";
    return 0;
}
