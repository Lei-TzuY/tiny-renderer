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

Mesh quad(float min_x, float min_y, float max_x, float max_y, float z = 0.0F) {
    Mesh mesh;
    mesh.vertices = {
        Vertex{{min_x, min_y, z}, {1.0F, 1.0F, 1.0F}},
        Vertex{{max_x, min_y, z}, {1.0F, 1.0F, 1.0F}},
        Vertex{{max_x, max_y, z}, {1.0F, 1.0F, 1.0F}},
        Vertex{{min_x, max_y, z}, {1.0F, 1.0F, 1.0F}},
    };
    mesh.triangles = {{0U, 1U, 2U}, {0U, 2U, 3U}};
    return mesh;
}

Rasterizer material_rasterizer(
    Framebuffer& framebuffer,
    const Vec3& albedo,
    BlendState blend_state = {},
    DepthState depth_state = {DepthCompare::Always, false},
    ViewportState viewport_state = {},
    StencilState stencil_state = {}) {
    return Rasterizer(
        framebuffer,
        {},
        {},
        {},
        MaterialState{albedo},
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        depth_state,
        viewport_state,
        stencil_state,
        blend_state);
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

BlendState additive_blend() {
    BlendState state;
    state.enabled = true;
    state.source_factor = BlendFactor::One;
    state.destination_factor = BlendFactor::One;
    state.operation = BlendOp::Add;
    return state;
}

BlendState source_weighted_blend() {
    BlendState state;
    state.enabled = true;
    state.source_factor = BlendFactor::SourceColor;
    state.destination_factor = BlendFactor::OneMinusSourceColor;
    state.operation = BlendOp::Add;
    return state;
}

void test_default_raster_blend_is_legacy_compatible() {
    const Mesh geometry = quad(-0.8F, -0.8F, 0.8F, 0.8F, 0.0F);
    const Vec3 albedo{0.5F, 0.25F, 0.75F};

    Framebuffer legacy(65U, 65U);
    Rasterizer legacy_rasterizer(
        legacy,
        {},
        {},
        {},
        MaterialState{albedo},
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        DepthState{DepthCompare::Always, false});
    legacy_rasterizer.draw_mesh(geometry, Mat4::identity());

    Framebuffer explicit_disabled(65U, 65U);
    material_rasterizer(explicit_disabled, albedo, {})
        .draw_mesh(geometry, Mat4::identity());

    check(explicit_disabled.rgb8() == legacy.rgb8(),
          "explicit disabled raster blending is byte-identical to the pre-blend replacement path");
    check(explicit_disabled.fnv1a64() == legacy.fnv1a64(),
          "explicit disabled raster blending preserves the deterministic framebuffer hash");
}

void test_non_commutative_blend_makes_draw_order_observable() {
    const Mesh geometry = quad(-0.8F, -0.8F, 0.8F, 0.8F);
    const BlendState blend = source_weighted_blend();
    const Vec3 a{0.5F, 0.25F, 0.0F};
    const Vec3 b{0.0F, 0.5F, 0.75F};

    Framebuffer ab(65U, 65U);
    material_rasterizer(ab, a, blend).draw_mesh(geometry, Mat4::identity());
    material_rasterizer(ab, b, blend).draw_mesh(geometry, Mat4::identity());

    Framebuffer ba(65U, 65U);
    material_rasterizer(ba, b, blend).draw_mesh(geometry, Mat4::identity());
    material_rasterizer(ba, a, blend).draw_mesh(geometry, Mat4::identity());

    check(!same_color(ab.color_at(32U, 32U), ba.color_at(32U, 32U))
              && ab.fnv1a64() != ba.fnv1a64(),
          "source-weighted RGB blending makes caller draw order observably non-commutative");
    check(same_color(ab.color_at(32U, 32U), {0.25F, 0.28125F, 0.5625F}),
          "A-then-B center sample matches the analytic source-weighted RGB composition");
    check(same_color(ba.color_at(32U, 32U), {0.25F, 0.25F, 0.5625F}),
          "B-then-A center sample matches the analytic source-weighted RGB composition");
}

void test_scissor_bounds_blended_output() {
    const Vec3 destination{0.25F, 0.25F, 0.25F};
    const Vec3 source{0.25F, 0.5F, 0.0F};
    const RasterRect scissor{11U, 9U, 8U, 6U};
    ViewportState viewport;
    viewport.scissor = scissor;

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear(destination);
    material_rasterizer(framebuffer, source, additive_blend(), {DepthCompare::Always, false}, viewport)
        .draw_mesh(quad(-1.0F, -1.0F, 1.0F, 1.0F), Mat4::identity());

    bool exact = true;
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            const bool inside = x >= scissor.x && x < scissor.x + scissor.width
                && y >= scissor.y && y < scissor.y + scissor.height;
            const Vec3 expected = inside ? Vec3{0.5F, 0.75F, 0.25F} : destination;
            exact = exact && same_color(framebuffer.color_at(x, y), expected);
        }
    }
    check(exact,
          "half-open scissor bounds additive RGB blend ownership without changing blend math");
}

void test_fully_masked_raster_color_preserves_depth_and_stencil_side_effects() {
    Framebuffer framebuffer(65U, 65U);
    const Vec3 destination{0.25F, 0.5F, 0.75F};
    framebuffer.clear(destination, 0.75F, 3U);

    BlendState blend;
    blend.write_mask = {false, false, false};

    StencilState stencil;
    stencil.enabled = true;
    stencil.compare = StencilCompare::Equal;
    stencil.reference = 3U;
    stencil.pass = StencilOp::IncrementClamp;

    material_rasterizer(
        framebuffer,
        {1.0F, 0.0F, 0.0F},
        blend,
        DepthState{DepthCompare::Less, true},
        {},
        stencil)
        .draw_mesh(quad(-0.8F, -0.8F, 0.8F, 0.8F, -0.5F), Mat4::identity());

    check(same_color(framebuffer.color_at(32U, 32U), destination),
          "fully masked raster color store preserves destination RGB");
    check(framebuffer.depth_at(32U, 32U) == 0.25F,
          "fully masked raster color store still performs the passing depth write");
    check(framebuffer.stencil_at(32U, 32U) == 4U,
          "fully masked raster color store still performs the stencil pass operation");
}

void test_selected_ranges_propagate_blend_state() {
    const Mesh geometry = quad(-0.8F, -0.8F, 0.8F, 0.8F);
    const Vec3 source{0.25F, 0.5F, 0.0F};
    const BlendState blend = additive_blend();

    Framebuffer full(65U, 65U);
    full.clear({0.25F, 0.25F, 0.25F});
    material_rasterizer(full, source, blend).draw_mesh(geometry, Mat4::identity());

    Framebuffer ranged(65U, 65U);
    ranged.clear({0.25F, 0.25F, 0.25F});
    Rasterizer rasterizer = material_rasterizer(ranged, source, blend);
    rasterizer.draw_mesh_range(geometry, DrawRange{0U, 1U}, Mat4::identity());
    rasterizer.draw_mesh_range(geometry, DrawRange{1U, 1U}, Mat4::identity());

    check(ranged.rgb8() == full.rgb8() && ranged.fnv1a64() == full.fnv1a64(),
          "selected range execution propagates RGB blend state byte/hash-identically to full mesh execution");
}

void test_prepared_list_propagates_blend_state() {
    ModelRenderOptions options;
    options.depth_state = {DepthCompare::Always, false};
    options.blend_state = additive_blend();

    PreparedModelSubmission red = prepare_model_asset(
        model_asset_from_mesh(quad(-0.8F, -0.8F, 0.8F, 0.8F), {0.5F, 0.0F, 0.0F}),
        options);
    PreparedModelSubmission green = prepare_model_asset(
        model_asset_from_mesh(quad(-0.8F, -0.8F, 0.8F, 0.8F), {0.0F, 0.25F, 0.0F}),
        options);

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

    check(listed.rgb8() == sequential.rgb8() && listed.fnv1a64() == sequential.fnv1a64(),
          "heterogeneous prepared list preserves RGB blend output byte/hash-equivalently to sequential submission");
    check(same_color(listed.color_at(32U, 32U), {0.5F, 0.25F, 0.0F}),
          "prepared-list additive blending produces the expected center RGB value");
}

void test_invalid_prepared_blend_state_is_rejected() {
    ModelRenderOptions options;
    options.blend_state.enabled = true;
    options.blend_state.operation = static_cast<BlendOp>(99);

    bool threw = false;
    try {
        (void)prepare_model_asset(
            model_asset_from_mesh(
                quad(-0.5F, -0.5F, 0.5F, 0.5F),
                {1.0F, 1.0F, 1.0F}),
            options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw,
          "framebuffer-independent prepared-model construction rejects an unknown RGB blend operation");
}

}  // namespace

int main() {
    try {
        test_default_raster_blend_is_legacy_compatible();
        test_non_commutative_blend_makes_draw_order_observable();
        test_scissor_bounds_blended_output();
        test_fully_masked_raster_color_preserves_depth_and_stencil_side_effects();
        test_selected_ranges_propagate_blend_state();
        test_prepared_list_propagates_blend_state();
        test_invalid_prepared_blend_state_is_rejected();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " blend integration test(s) failed\n";
        return 1;
    }

    std::cout << "all blend integration tests passed\n";
    return 0;
}
