#include <array>
#include <cstddef>
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

bool same_color(const Vec3& a, const Vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

Mesh quad(float min_x, float min_y, float max_x, float max_y, const Vec3& color, float z = 0.0F) {
    Mesh mesh;
    mesh.vertices = {
        Vertex{{min_x, min_y, z}, color},
        Vertex{{max_x, min_y, z}, color},
        Vertex{{max_x, max_y, z}, color},
        Vertex{{min_x, max_y, z}, color},
    };
    mesh.triangles = {{0U, 1U, 2U}, {0U, 2U, 3U}};
    return mesh;
}

Rasterizer stencil_rasterizer(
    Framebuffer& framebuffer,
    StencilState stencil_state,
    DepthState depth_state = {},
    ViewportState viewport_state = {}) {
    return Rasterizer(
        framebuffer,
        {},
        {},
        {},
        {},
        BaseColorSource::Auto,
        CullMode::None,
        FrontFace::CounterClockwise,
        depth_state,
        viewport_state,
        stencil_state);
}

StencilState replace_on_pass(std::uint8_t reference) {
    StencilState state;
    state.enabled = true;
    state.compare = StencilCompare::Always;
    state.reference = reference;
    state.pass = StencilOp::Replace;
    return state;
}

StencilState equal_keep(std::uint8_t reference) {
    StencilState state;
    state.enabled = true;
    state.compare = StencilCompare::Equal;
    state.reference = reference;
    return state;
}

ModelAsset model_asset_from_mesh(Mesh mesh, const Vec3& albedo) {
    ModelAsset asset;
    asset.mesh = std::move(mesh);
    MaterialDraw draw;
    draw.range = {0U, asset.mesh.triangles.size()};
    draw.material.albedo = albedo;
    asset.draws = {draw};
    return asset;
}

void test_disabled_stencil_is_legacy_compatible() {
    const Mesh far_quad = quad(-0.8F, -0.8F, 0.8F, 0.8F, {0.2F, 0.4F, 0.8F}, 0.4F);
    const Mesh near_quad = quad(-0.5F, -0.5F, 0.5F, 0.5F, {0.9F, 0.3F, 0.1F}, -0.3F);

    Framebuffer legacy(65U, 65U);
    Rasterizer legacy_rasterizer(legacy);
    legacy_rasterizer.draw_mesh(far_quad, Mat4::identity());
    legacy_rasterizer.draw_mesh(near_quad, Mat4::identity());

    StencilState disabled;
    disabled.enabled = false;
    Framebuffer explicit_disabled(65U, 65U);
    stencil_rasterizer(explicit_disabled, disabled).draw_mesh(far_quad, Mat4::identity());
    stencil_rasterizer(explicit_disabled, disabled).draw_mesh(near_quad, Mat4::identity());

    check(explicit_disabled.rgb8() == legacy.rgb8(),
          "explicit disabled stencil is byte-identical to legacy raster output");
    check(explicit_disabled.fnv1a64() == legacy.fnv1a64(),
          "explicit disabled stencil preserves legacy framebuffer hash");
    check(explicit_disabled.depth_at(32U, 32U) == legacy.depth_at(32U, 32U),
          "explicit disabled stencil preserves legacy depth ownership");
    check(explicit_disabled.stencil_at(32U, 32U) == 0U,
          "disabled stencil leaves the clear value untouched");
}

void test_stencil_mask_prepass_controls_colored_pass() {
    const Mesh mask = quad(-0.85F, -0.75F, -0.05F, 0.75F, {0.0F, 0.0F, 0.0F});
    const Mesh color = quad(-0.95F, -0.9F, 0.95F, 0.9F, {0.2F, 0.8F, 0.4F});

    Framebuffer framebuffer(65U, 65U);
    stencil_rasterizer(
        framebuffer,
        replace_on_pass(1U),
        DepthState{DepthCompare::Always, false})
        .draw_mesh(mask, Mat4::identity());

    stencil_rasterizer(
        framebuffer,
        equal_keep(1U),
        DepthState{DepthCompare::Always, false})
        .draw_mesh(color, Mat4::identity());

    std::size_t mask_samples = 0U;
    std::size_t colored_samples = 0U;
    bool mask_matches_color = true;
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            const bool in_mask = framebuffer.stencil_at(x, y) == 1U;
            const Vec3 pixel = framebuffer.color_at(x, y);
            const bool colored = pixel.x != 0.0F || pixel.y != 0.0F || pixel.z != 0.0F;
            mask_samples += in_mask ? 1U : 0U;
            colored_samples += colored ? 1U : 0U;
            mask_matches_color = mask_matches_color && in_mask == colored;
            if (colored) {
                mask_matches_color = mask_matches_color
                    && same_color(pixel, Vec3{0.2F, 0.8F, 0.4F});
            }
        }
    }

    check(mask_samples > 0U && mask_samples < framebuffer.width() * framebuffer.height(),
          "stencil prepass creates a non-empty strict subset mask");
    check(mask_matches_color && colored_samples == mask_samples,
          "colored pass owns exactly the samples marked by the stencil prepass");
}

void test_scissor_prevents_stencil_side_effects_outside_bounds() {
    StencilState state;
    state.enabled = true;
    state.compare = StencilCompare::Never;
    state.reference = 7U;
    state.stencil_fail = StencilOp::Replace;

    const RasterRect scissor{11U, 9U, 8U, 6U};
    ViewportState viewport_state;
    viewport_state.scissor = scissor;

    Framebuffer framebuffer(65U, 65U);
    stencil_rasterizer(
        framebuffer,
        state,
        DepthState{DepthCompare::Always, false},
        viewport_state)
        .draw_mesh(quad(-1.0F, -1.0F, 1.0F, 1.0F, {1.0F, 0.0F, 0.0F}), Mat4::identity());

    bool exact_mask = true;
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            const bool inside = x >= scissor.x && x < scissor.x + scissor.width
                && y >= scissor.y && y < scissor.y + scissor.height;
            exact_mask = exact_mask && framebuffer.stencil_at(x, y) == (inside ? 7U : 0U);
            exact_mask = exact_mask && same_color(framebuffer.color_at(x, y), Vec3{});
        }
    }
    check(exact_mask,
          "scissor bounds both stencil-fail side effects and color ownership with half-open semantics");
}

void test_selected_range_propagates_stencil_state() {
    Mesh mesh = quad(-0.8F, -0.8F, 0.8F, 0.8F, {0.0F, 0.0F, 0.0F});
    StencilState state = replace_on_pass(3U);

    Framebuffer full(65U, 65U);
    stencil_rasterizer(full, state, DepthState{DepthCompare::Always, false})
        .draw_mesh(mesh, Mat4::identity());

    Framebuffer ranged(65U, 65U);
    Rasterizer ranged_rasterizer = stencil_rasterizer(
        ranged,
        state,
        DepthState{DepthCompare::Always, false});
    ranged_rasterizer.draw_mesh_range(mesh, DrawRange{0U, 1U}, Mat4::identity());
    ranged_rasterizer.draw_mesh_range(mesh, DrawRange{1U, 1U}, Mat4::identity());

    bool stencil_equal = true;
    for (std::size_t y = 0U; y < full.height(); ++y) {
        for (std::size_t x = 0U; x < full.width(); ++x) {
            stencil_equal = stencil_equal && full.stencil_at(x, y) == ranged.stencil_at(x, y);
        }
    }
    check(stencil_equal && ranged.rgb8() == full.rgb8(),
          "selected range execution propagates stencil state identically to full mesh execution");
}

void test_prepared_list_propagates_stencil_prepass_and_test_state() {
    ModelRenderOptions mask_options;
    mask_options.depth_state = {DepthCompare::Always, false};
    mask_options.stencil_state = replace_on_pass(1U);

    ModelRenderOptions color_options;
    color_options.depth_state = {DepthCompare::Always, false};
    color_options.stencil_state = equal_keep(1U);

    PreparedModelSubmission mask_plan = prepare_model_asset(
        model_asset_from_mesh(
            quad(-0.85F, -0.75F, -0.05F, 0.75F, {1.0F, 1.0F, 1.0F}),
            {0.0F, 0.0F, 0.0F}),
        mask_options);
    PreparedModelSubmission color_plan = prepare_model_asset(
        model_asset_from_mesh(
            quad(-0.95F, -0.9F, 0.95F, 0.9F, {1.0F, 1.0F, 1.0F}),
            {0.2F, 0.8F, 0.4F}),
        color_options);

    const std::array<PreparedModelListEntry, 2U> entries{{
        {&mask_plan, Mat4::identity()},
        {&color_plan, Mat4::identity()},
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
        mask_plan,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    draw_prepared_model(
        sequential,
        color_plan,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());

    bool stencil_equal = true;
    for (std::size_t y = 0U; y < listed.height(); ++y) {
        for (std::size_t x = 0U; x < listed.width(); ++x) {
            stencil_equal = stencil_equal && listed.stencil_at(x, y) == sequential.stencil_at(x, y);
        }
    }
    check(listed.rgb8() == sequential.rgb8() && listed.fnv1a64() == sequential.fnv1a64(),
          "heterogeneous prepared list preserves stencil-driven color output byte/hash-equivalently");
    check(stencil_equal,
          "heterogeneous prepared list preserves stencil attachment updates from sequential execution");
}

void test_invalid_prepared_stencil_state_is_rejected_before_execution() {
    ModelRenderOptions options;
    options.stencil_state.enabled = true;
    options.stencil_state.pass = static_cast<StencilOp>(99);

    bool threw = false;
    try {
        (void)prepare_model_asset(
            model_asset_from_mesh(
                quad(-0.5F, -0.5F, 0.5F, 0.5F, {1.0F, 1.0F, 1.0F}),
                {1.0F, 1.0F, 1.0F}),
            options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw,
          "framebuffer-independent prepared-model construction rejects an unknown stencil operation");
}

}  // namespace

int main() {
    try {
        test_disabled_stencil_is_legacy_compatible();
        test_stencil_mask_prepass_controls_colored_pass();
        test_scissor_prevents_stencil_side_effects_outside_bounds();
        test_selected_range_propagates_stencil_state();
        test_prepared_list_propagates_stencil_prepass_and_test_state();
        test_invalid_prepared_stencil_state_is_rejected_before_execution();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " stencil integration test(s) failed\n";
        return 1;
    }

    std::cout << "all stencil integration tests passed\n";
    return 0;
}
