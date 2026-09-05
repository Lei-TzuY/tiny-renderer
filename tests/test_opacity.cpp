#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>

#include "tiny_renderer/mtl_loader.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/obj_loader.hpp"
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

ModelAsset model_asset_from_mesh(Mesh mesh, const Vec3& albedo, float opacity) {
    ModelAsset asset;
    asset.mesh = std::move(mesh);
    MaterialDraw draw;
    draw.range = {0U, asset.mesh.triangles.size()};
    draw.material = MaterialState{albedo, opacity};
    asset.draws = {draw};
    return asset;
}

BlendState source_alpha_blend() {
    BlendState state;
    state.enabled = true;
    state.source_factor = BlendFactor::SourceAlpha;
    state.destination_factor = BlendFactor::OneMinusSourceAlpha;
    state.operation = BlendOp::Add;
    return state;
}

ModelRenderOptions transparent_options() {
    ModelRenderOptions options;
    options.depth_state = {DepthCompare::Always, false};
    options.blend_state = source_alpha_blend();
    return options;
}

void test_source_alpha_factor_math() {
    const Vec3 destination{0.0F, 0.0F, 1.0F};
    const Vec3 source{1.0F, 0.0F, 0.0F};
    const BlendState blend = source_alpha_blend();

    const auto render = [&](float opacity) {
        Framebuffer framebuffer(1U, 1U);
        framebuffer.clear(destination, 0.75F, 4U);
        const bool passed = framebuffer.test_and_write(
            0U,
            0U,
            0.25F,
            source,
            DepthState{DepthCompare::Always, false},
            {},
            blend,
            opacity);
        check(passed, "source-alpha analytic fixture passes ownership");
        return framebuffer.color_at(0U, 0U);
    };

    check(same_color(render(0.0F), destination),
          "source alpha zero preserves destination RGB");
    check(same_color(render(0.5F), {0.5F, 0.0F, 0.5F}),
          "source alpha one-half produces the analytic half-source half-destination RGB result");
    check(same_color(render(1.0F), source),
          "source alpha one produces source RGB");
}

void test_invalid_fragment_alpha_fails_closed() {
    const Vec3 original{0.25F, 0.5F, 0.75F};
    const auto reject = [&](float opacity, const std::string& label) {
        Framebuffer framebuffer(1U, 1U);
        framebuffer.clear(original, 0.75F, 9U);
        bool threw = false;
        try {
            (void)framebuffer.test_and_write(
                0U,
                0U,
                0.25F,
                {1.0F, 0.0F, 0.0F},
                DepthState{DepthCompare::Less, true},
                {},
                source_alpha_blend(),
                opacity);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, label + " fragment alpha is rejected");
        check(same_color(framebuffer.color_at(0U, 0U), original)
                  && framebuffer.depth_at(0U, 0U) == 0.75F
                  && framebuffer.stencil_at(0U, 0U) == 9U,
              label + " fragment alpha rejection occurs before framebuffer mutation");
    };

    reject(-0.01F, "negative");
    reject(1.01F, "greater-than-one");
    reject(std::numeric_limits<float>::quiet_NaN(), "non-finite");
}

void test_mtl_opacity_parsing_and_defaults() {
    std::istringstream input(
        "newmtl glass\n"
        "Kd 1 0 0\n"
        "d 0.5\n"
        "newmtl opaque\n"
        "Kd 0 1 0\n");
    const MaterialLibrary library = load_mtl(input);
    check(library.size() == 2U, "bounded MTL parser retains both opacity fixture materials");
    check(library.at("glass").opacity == 0.5F,
          "MTL d value is preserved in MaterialState opacity");
    check(library.at("opaque").opacity == 1.0F,
          "missing MTL d defaults to fully opaque");

    std::istringstream rich_input(
        "newmtl glass\n"
        "Kd 1 0 0\n"
        "d 0.25\n");
    const MaterialAssetLibrary assets = load_mtl_assets(rich_input);
    check(assets.at("glass").material.opacity == 0.25F,
          "rich material asset parsing preserves the same opacity field");
}

void test_invalid_mtl_opacity_is_rejected() {
    const auto reject = [&](const std::string& source, const std::string& label) {
        std::istringstream input(source);
        bool threw = false;
        try {
            (void)load_mtl(input);
        } catch (const MtlParseError&) {
            threw = true;
        }
        check(threw, label + " MTL opacity is rejected deterministically");
    };

    reject("d 0.5\nnewmtl x\nKd 1 1 1\n", "pre-newmtl");
    reject("newmtl x\nKd 1 1 1\nd 0.5\nd 0.5\n", "duplicate");
    reject("newmtl x\nKd 1 1 1\nd 0.5 extra\n", "extra-token");
    reject("newmtl x\nKd 1 1 1\nd -0.1\n", "negative");
    reject("newmtl x\nKd 1 1 1\nd 1.1\n", "greater-than-one");
    reject("newmtl x\nKd 1 1 1\nd nan\n", "non-finite");
}

void test_default_material_opacity_is_opaque_compatible() {
    const Mesh geometry = quad(-0.8F, -0.8F, 0.8F, 0.8F);
    const Vec3 albedo{0.5F, 0.25F, 0.75F};

    Framebuffer implicit(65U, 65U);
    Rasterizer(
        implicit,
        {},
        {},
        {},
        MaterialState{albedo},
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        DepthState{DepthCompare::Always, false},
        {},
        {},
        source_alpha_blend())
        .draw_mesh(geometry, Mat4::identity());

    Framebuffer explicit_one(65U, 65U);
    Rasterizer(
        explicit_one,
        {},
        {},
        {},
        MaterialState{albedo, 1.0F},
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        DepthState{DepthCompare::Always, false},
        {},
        {},
        source_alpha_blend())
        .draw_mesh(geometry, Mat4::identity());

    check(implicit.rgb8() == explicit_one.rgb8()
              && implicit.fnv1a64() == explicit_one.fnv1a64(),
          "default material opacity one is byte/hash-identical to explicit opaque source-alpha rendering");
}

void test_invalid_material_opacity_is_preflighted() {
    const Mesh geometry = quad(-0.8F, -0.8F, 0.8F, 0.8F);
    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.25F, 0.5F, 0.75F}, 0.75F, 7U);
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool direct_threw = false;
    try {
        Rasterizer(
            framebuffer,
            {},
            {},
            {},
            MaterialState{{1.0F, 0.0F, 0.0F}, -0.25F},
            BaseColorSource::ConstantWhite,
            CullMode::None,
            FrontFace::CounterClockwise,
            DepthState{DepthCompare::Always, true},
            {},
            {},
            source_alpha_blend())
            .draw_mesh(geometry, Mat4::identity());
    } catch (const std::invalid_argument&) {
        direct_threw = true;
    }
    check(direct_threw, "direct Rasterizer preflight rejects invalid material opacity");
    check(framebuffer.rgb8() == before
              && framebuffer.depth_at(32U, 32U) == 0.75F
              && framebuffer.stencil_at(32U, 32U) == 7U,
          "invalid direct material opacity is rejected before framebuffer mutation");

    bool prepared_threw = false;
    try {
        (void)prepare_model_asset(
            model_asset_from_mesh(
                geometry,
                {1.0F, 1.0F, 1.0F},
                std::numeric_limits<float>::quiet_NaN()),
            transparent_options());
    } catch (const std::invalid_argument&) {
        prepared_threw = true;
    }
    check(prepared_threw,
          "framebuffer-independent prepared-model construction rejects invalid material opacity");
}

void test_file_driven_opacity_matches_programmatic_material() {
    const std::filesystem::path fixture =
        std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / "opacity.obj";
    const ModelAsset imported = load_obj_model_asset_file(fixture);
    check(imported.draws.size() == 1U,
          "file-driven opacity fixture produces one canonical material draw");
    check(imported.draws.front().material.opacity == 0.5F,
          "OBJ/MTL model asset preserves imported opacity");

    ModelAsset programmatic = imported;
    programmatic.draws.front().material = MaterialState{{1.0F, 0.0F, 0.0F}, 0.5F};

    ModelRenderOptions options = transparent_options();
    Framebuffer file_fb(65U, 65U);
    Framebuffer programmatic_fb(65U, 65U);
    const Vec3 background{0.0F, 0.0F, 1.0F};
    file_fb.clear(background, 0.75F);
    programmatic_fb.clear(background, 0.75F);

    draw_model_asset(file_fb, imported, Mat4::identity(), options);
    draw_model_asset(programmatic_fb, programmatic, Mat4::identity(), options);

    check(file_fb.rgb8() == programmatic_fb.rgb8()
              && file_fb.fnv1a64() == programmatic_fb.fnv1a64(),
          "file-driven MTL opacity renders byte/hash-equivalently to the same programmatic material");
    check(same_color(file_fb.color_at(32U, 32U), {0.5F, 0.0F, 0.5F}),
          "file-driven half-opacity red material analytically blends over blue at the center sample");
    check(file_fb.depth_at(32U, 32U) == 0.75F,
          "caller-selected depth-write disable preserves stored depth during transparent rendering");
}

void test_scissor_and_stencil_bound_opacity_side_effects() {
    const std::filesystem::path fixture =
        std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / "opacity.obj";
    const ModelAsset imported = load_obj_model_asset_file(fixture);

    ModelRenderOptions options = transparent_options();
    options.viewport_state.scissor = RasterRect{11U, 9U, 8U, 6U};
    options.stencil_state.enabled = true;
    options.stencil_state.compare = StencilCompare::Equal;
    options.stencil_state.reference = 2U;
    options.stencil_state.pass = StencilOp::IncrementClamp;

    Framebuffer framebuffer(65U, 65U);
    const Vec3 background{0.0F, 0.0F, 1.0F};
    framebuffer.clear(background, 0.75F, 2U);
    draw_model_asset(framebuffer, imported, Mat4::identity(), options);

    bool exact = true;
    const RasterRect& scissor = *options.viewport_state.scissor;
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            const bool inside = x >= scissor.x && x < scissor.x + scissor.width
                && y >= scissor.y && y < scissor.y + scissor.height;
            const Vec3 expected_color = inside ? Vec3{0.5F, 0.0F, 0.5F} : background;
            const std::uint8_t expected_stencil = inside ? 3U : 2U;
            exact = exact
                && same_color(framebuffer.color_at(x, y), expected_color)
                && framebuffer.stencil_at(x, y) == expected_stencil
                && framebuffer.depth_at(x, y) == 0.75F;
        }
    }
    check(exact,
          "scissor bounds source-alpha RGB and stencil pass side effects while transparent depth writes remain disabled");
}

void test_clipping_preserves_material_opacity() {
    Triangle triangle{
        Vertex{{-1.5F, -0.5F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        Vertex{{0.5F, -0.5F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        Vertex{{0.5F, 0.5F, 0.0F}, {1.0F, 1.0F, 1.0F}},
    };

    Framebuffer framebuffer(65U, 65U);
    const Vec3 background{0.0F, 0.0F, 1.0F};
    framebuffer.clear(background);
    Rasterizer(
        framebuffer,
        {},
        {},
        {},
        MaterialState{{1.0F, 0.0F, 0.0F}, 0.5F},
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        DepthState{DepthCompare::Always, false},
        {},
        {},
        source_alpha_blend())
        .draw_triangle(triangle, Mat4::identity());

    std::size_t covered = 0U;
    bool exact = true;
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            const Vec3 pixel = framebuffer.color_at(x, y);
            if (!same_color(pixel, background)) {
                ++covered;
                exact = exact && same_color(pixel, {0.5F, 0.0F, 0.5F});
            }
        }
    }
    check(covered > 0U && exact,
          "homogeneous clipping preserves one constant material opacity across every generated covered fragment");
}

void test_prepared_lists_preserve_opacity_and_caller_order() {
    const ModelRenderOptions options = transparent_options();
    PreparedModelSubmission red = prepare_model_asset(
        model_asset_from_mesh(quad(-0.8F, -0.8F, 0.8F, 0.8F), {1.0F, 0.0F, 0.0F}, 0.5F),
        options);
    PreparedModelSubmission green = prepare_model_asset(
        model_asset_from_mesh(quad(-0.8F, -0.8F, 0.8F, 0.8F), {0.0F, 1.0F, 0.0F}, 0.5F),
        options);

    const std::array<PreparedModelListEntry, 2U> red_then_green{{
        {&red, Mat4::identity()},
        {&green, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 2U> green_then_red{{
        {&green, Mat4::identity()},
        {&red, Mat4::identity()},
    }};

    const Vec3 background{0.0F, 0.0F, 1.0F};
    Framebuffer rg(65U, 65U);
    Framebuffer gr(65U, 65U);
    Framebuffer sequential(65U, 65U);
    rg.clear(background);
    gr.clear(background);
    sequential.clear(background);

    draw_prepared_model_list(
        rg,
        std::span<const PreparedModelListEntry>{red_then_green},
        Mat4::identity(),
        Mat4::identity());
    draw_prepared_model_list(
        gr,
        std::span<const PreparedModelListEntry>{green_then_red},
        Mat4::identity(),
        Mat4::identity());
    draw_prepared_model(sequential, red, Mat4::identity(), Mat4::identity(), Mat4::identity());
    draw_prepared_model(sequential, green, Mat4::identity(), Mat4::identity(), Mat4::identity());

    check(rg.rgb8() == sequential.rgb8() && rg.fnv1a64() == sequential.fnv1a64(),
          "heterogeneous prepared list preserves material opacity byte/hash-equivalently to sequential submission");
    check(same_color(rg.color_at(32U, 32U), {0.25F, 0.5F, 0.25F}),
          "red-then-green half-opacity caller order matches analytic RGB composition");
    check(same_color(gr.color_at(32U, 32U), {0.5F, 0.25F, 0.25F}),
          "green-then-red half-opacity caller order matches analytic RGB composition");
    check(rg.fnv1a64() != gr.fnv1a64(),
          "caller ordering remains observably significant for bounded transparency");
}

}  // namespace

int main() {
    try {
        test_source_alpha_factor_math();
        test_invalid_fragment_alpha_fails_closed();
        test_mtl_opacity_parsing_and_defaults();
        test_invalid_mtl_opacity_is_rejected();
        test_default_material_opacity_is_opaque_compatible();
        test_invalid_material_opacity_is_preflighted();
        test_file_driven_opacity_matches_programmatic_material();
        test_scissor_and_stencil_bound_opacity_side_effects();
        test_clipping_preserves_material_opacity();
        test_prepared_lists_preserve_opacity_and_caller_order();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " opacity test(s) failed\n";
        return 1;
    }

    std::cout << "all opacity tests passed\n";
    return 0;
}
