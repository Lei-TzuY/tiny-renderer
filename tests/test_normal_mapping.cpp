#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/mtl_loader.hpp"
#include "tiny_renderer/obj_loader.hpp"
#include "tiny_renderer/ppm_loader.hpp"
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

void check_near(
    float actual,
    float expected,
    const std::string& message,
    float epsilon = 3.0e-3F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

Vertex uv_normal_vertex(
    const Vec3& position,
    float u,
    float v,
    const Vec3& normal) {
    return Vertex::with_varyings(position, VaryingPack{
        u, v, normal.x, normal.y, normal.z,
    });
}

DirectionalLight directional_light(const Vec3& direction) {
    return DirectionalLight{
        true,
        {2U, 3U, 4U},
        direction,
        0.0F,
        1.0F,
    };
}

TextureBinding normal_binding(const Texture2D& texture) {
    TextureBinding binding{};
    binding.u_channel = 0U;
    binding.v_channel = 1U;
    binding.normal_texture = &texture;
    return binding;
}

Triangle canonical_triangle() {
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    return Triangle{{
        uv_normal_vertex({-0.65F, -0.65F, 0.0F}, 0.0F, 0.0F, normal),
        uv_normal_vertex({0.65F, -0.65F, 0.0F}, 1.0F, 0.0F, normal),
        uv_normal_vertex({0.0F, 0.65F, 0.0F}, 0.5F, 1.0F, normal),
    }};
}

float max_red(const Framebuffer& framebuffer) {
    float result = 0.0F;
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            result = std::max(result, framebuffer.color_at(x, y).x);
        }
    }
    return result;
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

ModelAsset model_from_triangle(
    const Triangle& triangle,
    std::shared_ptr<const Texture2D> normal_texture) {
    ModelAsset asset;
    asset.mesh.vertices = {triangle[0], triangle[1], triangle[2]};
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "normal";
    draw.normal_texture = std::move(normal_texture);
    asset.draws.push_back(std::move(draw));
    return asset;
}

ModelRenderOptions normal_model_options(const Vec3& direction) {
    ModelRenderOptions options;
    options.directional_light = directional_light(direction);
    return options;
}

void test_known_tangent_normal_rotates_lambert_response() {
    const Texture2D normal_map(1U, 1U, {{1.0F, 0.5F, 0.5F}});
    const Triangle triangle = canonical_triangle();

    Framebuffer mapped(65U, 65U);
    Rasterizer mapped_rasterizer(
        mapped,
        {},
        normal_binding(normal_map),
        directional_light({1.0F, 0.0F, 0.0F}),
        {},
        BaseColorSource::ConstantWhite);
    mapped_rasterizer.draw_triangle(
        triangle,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());

    Framebuffer geometric(65U, 65U);
    Rasterizer geometric_rasterizer(
        geometric,
        {},
        {},
        directional_light({1.0F, 0.0F, 0.0F}),
        {},
        BaseColorSource::ConstantWhite);
    geometric_rasterizer.draw_triangle(
        triangle,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());

    check_near(max_red(mapped), 1.0F,
               "tangent +X normal map aligns Lambert response with +X light");
    check_near(max_red(geometric), 0.0F,
               "geometric +Z normal remains dark under +X light");
}

void test_non_uniform_model_scale_transforms_tangent_separately_from_normal() {
    const Vec3 p0{-0.25F, -0.25F, -0.25F};
    const Vec3 p1{0.15F, -0.25F, 0.15F};
    const Vec3 p2{-0.25F, 0.25F, -0.25F};
    const Vec3 edge1 = p1 - p0;
    const Vec3 edge2 = p2 - p0;
    const Vec3 object_normal = normalize(cross(edge1, edge2));
    const Triangle triangle{{
        uv_normal_vertex(p0, 0.0F, 0.0F, object_normal),
        uv_normal_vertex(p1, 1.0F, 0.0F, object_normal),
        uv_normal_vertex(p2, 0.0F, 1.0F, object_normal),
    }};
    const Texture2D normal_map(1U, 1U, {{1.0F, 0.5F, 0.5F}});
    const Mat4 model = Mat4::scale({2.0F, 1.0F, 0.5F});
    const Vec3 expected_world_tangent = normalize(Vec3{0.8F, 0.0F, 0.2F});

    Framebuffer framebuffer(65U, 65U);
    Rasterizer rasterizer(
        framebuffer,
        {},
        normal_binding(normal_map),
        directional_light(expected_world_tangent),
        {},
        BaseColorSource::ConstantWhite);
    rasterizer.draw_triangle(
        triangle,
        model,
        Mat4::identity(),
        Mat4::identity());

    check_near(max_red(framebuffer), 1.0F,
               "non-uniform model scale uses model-linear tangent and inverse-transpose normal transforms");
}

void test_clipping_preserves_normal_mapped_result() {
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    const Triangle crossing{{
        uv_normal_vertex({-1.5F, 0.0F, 0.0F}, 0.0F, 0.5F, normal),
        uv_normal_vertex({0.0F, -0.7F, 0.0F}, 1.0F, 0.0F, normal),
        uv_normal_vertex({0.0F, 0.7F, 0.0F}, 1.0F, 1.0F, normal),
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

    const Texture2D normal_map(1U, 1U, {{0.8F, 0.5F, 0.8F}});
    const TextureBinding binding = normal_binding(normal_map);
    const DirectionalLight light = directional_light(normalize(Vec3{0.4F, 0.0F, 1.0F}));

    Framebuffer automatic(65U, 65U);
    Rasterizer automatic_rasterizer(
        automatic, {}, binding, light, {}, BaseColorSource::ConstantWhite);
    automatic_rasterizer.draw_triangle(
        crossing,
        Mat4::identity(), Mat4::identity(), Mat4::identity());

    Framebuffer manual(65U, 65U);
    Rasterizer manual_rasterizer(
        manual, {}, binding, light, {}, BaseColorSource::ConstantWhite);
    manual_rasterizer.draw_triangle(
        manual_a,
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    manual_rasterizer.draw_triangle(
        manual_b,
        Mat4::identity(), Mat4::identity(), Mat4::identity());

    check(automatic.rgb8() == manual.rgb8(),
          "normal-mapped homogeneous clipping matches equivalent explicit clipped geometry");
}

void test_invalid_normal_mapping_contracts_fail_closed() {
    const Triangle triangle = canonical_triangle();
    const auto valid_map = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{1.0F, 0.5F, 0.5F}});

    {
        Triangle degenerate_uv = triangle;
        degenerate_uv[0].varyings.values[0] = 0.0F;
        degenerate_uv[0].varyings.values[1] = 0.0F;
        degenerate_uv[1].varyings.values[0] = 0.5F;
        degenerate_uv[1].varyings.values[1] = 0.5F;
        degenerate_uv[2].varyings.values[0] = 1.0F;
        degenerate_uv[2].varyings.values[1] = 1.0F;

        Framebuffer framebuffer(33U, 33U);
        framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 7U);
        const auto before = framebuffer.rgb8();
        const float before_depth = framebuffer.depth_at(16U, 16U);
        const std::uint8_t before_stencil = framebuffer.stencil_at(16U, 16U);
        Rasterizer rasterizer(
            framebuffer,
            {},
            normal_binding(*valid_map),
            directional_light({1.0F, 0.0F, 0.0F}),
            {},
            BaseColorSource::ConstantWhite);
        bool threw = false;
        try {
            rasterizer.draw_triangle(
                degenerate_uv,
                Mat4::identity(), Mat4::identity(), Mat4::identity());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "degenerate normal-map UV basis is rejected");
        check(framebuffer.rgb8() == before,
              "degenerate normal-map UV rejection happens before color writes");
        check(framebuffer.depth_at(16U, 16U) == before_depth,
              "degenerate normal-map UV rejection happens before depth writes");
        check(framebuffer.stencil_at(16U, 16U) == before_stencil,
              "degenerate normal-map UV rejection happens before stencil writes");
    }

    {
        const Texture2D invalid_map(1U, 1U, {{1.2F, 0.5F, 0.5F}});
        Framebuffer framebuffer(33U, 33U);
        const auto before = framebuffer.rgb8();
        Rasterizer rasterizer(
            framebuffer,
            {},
            normal_binding(invalid_map),
            directional_light({1.0F, 0.0F, 0.0F}),
            {},
            BaseColorSource::ConstantWhite);
        bool threw = false;
        try {
            rasterizer.draw_triangle(
                triangle,
                Mat4::identity(), Mat4::identity(), Mat4::identity());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "out-of-range normal-map texel is rejected");
        check(framebuffer.rgb8() == before,
              "invalid normal-map texel is fail-closed");
    }

    {
        Framebuffer framebuffer(33U, 33U);
        Rasterizer rasterizer(
            framebuffer,
            {},
            normal_binding(*valid_map),
            {},
            {},
            BaseColorSource::ConstantWhite);
        bool threw = false;
        try {
            rasterizer.draw_triangle(
                triangle,
                Mat4::identity(), Mat4::identity(), Mat4::identity());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "normal map without directional lighting is rejected");
    }

    {
        Framebuffer framebuffer(33U, 33U);
        Rasterizer rasterizer(
            framebuffer,
            {},
            normal_binding(*valid_map),
            directional_light({1.0F, 0.0F, 0.0F}),
            {},
            BaseColorSource::ConstantWhite);
        bool threw = false;
        try {
            rasterizer.draw_triangle(triangle, Mat4::identity());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "normal mapping MVP-only submission is rejected");
    }
}

void test_prepared_list_preflights_later_bad_tangent_before_first_write() {
    const auto normal_map = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{1.0F, 0.5F, 0.5F}});
    const Triangle good_triangle = canonical_triangle();
    Triangle bad_triangle = good_triangle;
    bad_triangle[0].varyings.values[0] = 0.0F;
    bad_triangle[0].varyings.values[1] = 0.0F;
    bad_triangle[1].varyings.values[0] = 0.5F;
    bad_triangle[1].varyings.values[1] = 0.5F;
    bad_triangle[2].varyings.values[0] = 1.0F;
    bad_triangle[2].varyings.values[1] = 1.0F;

    const ModelRenderOptions options = normal_model_options({1.0F, 0.0F, 0.0F});
    const PreparedModelSubmission good = prepare_model_asset(
        model_from_triangle(good_triangle, normal_map), options);
    const PreparedModelSubmission bad = prepare_model_asset(
        model_from_triangle(bad_triangle, normal_map), options);
    const PreparedModelListEntry entries[] = {
        {&good, Mat4::identity()},
        {&bad, Mat4::identity()},
    };

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 9U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);
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
    check(threw, "later prepared-list degenerate tangent frame is rejected");
    check(framebuffer.rgb8() == before,
          "prepared-list tangent preflight rejects before earlier color writes");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "prepared-list tangent preflight rejects before earlier depth writes");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "prepared-list tangent preflight rejects before earlier stencil writes");
}

void test_bounded_mtl_map_bump_contract() {
    {
        std::istringstream input(
            "newmtl mapped\n"
            "Kd 1 1 1\n"
            "map_Bump normal.ppm\n");
        const MaterialAssetLibrary library = load_mtl_assets(input);
        check(library.at("mapped").normal_map_filename.has_value(),
              "rich MTL parser captures map_Bump filename");
        check(*library.at("mapped").normal_map_filename == "normal.ppm",
              "rich MTL parser preserves map_Bump sibling filename");
    }

    const auto expect_rich_failure = [](const std::string& text) {
        std::istringstream input(text);
        try {
            (void)load_mtl_assets(input);
        } catch (const MtlParseError&) {
            return true;
        }
        return false;
    };
    check(expect_rich_failure(
              "newmtl mapped\nKd 1 1 1\nmap_Bump a.ppm\nmap_Bump b.ppm\n"),
          "duplicate map_Bump is rejected deterministically");
    check(expect_rich_failure(
              "newmtl mapped\nKd 1 1 1\nmap_Bump -bm 0.5 normal.ppm\n"),
          "option-heavy map_Bump syntax is outside the bounded contract");
    check(expect_rich_failure(
              "newmtl mapped\nKd 1 1 1\nmap_Bump ../normal.ppm\n"),
          "unsafe parent map_Bump path is rejected");

    {
        std::istringstream input(
            "newmtl mapped\n"
            "Kd 1 1 1\n"
            "map_Bump normal.ppm\n");
        bool threw = false;
        try {
            (void)load_mtl(input);
        } catch (const MtlParseError&) {
            threw = true;
        }
        check(threw, "legacy strict MTL loader still rejects map directives");
    }
}

void test_file_driven_asset_matches_programmatic_and_prepared_lifetime() {
    const std::filesystem::path fixture_dir =
        std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures";
    const std::filesystem::path obj_path = fixture_dir / "normal_mapped.obj";
    const std::filesystem::path checker_path = fixture_dir / "checker.ppm";

    ModelAsset imported = load_obj_model_asset_file(obj_path);
    check(imported.draws.size() == 1U,
          "normal-map fixture produces one canonical material draw");
    check(imported.draws[0].normal_texture != nullptr,
          "normal-map fixture owns decoded normal texture");
    check(imported.draws[0].diffuse_texture != nullptr,
          "normal-map fixture owns decoded diffuse texture");
    check(imported.draws[0].normal_texture.get() == imported.draws[0].diffuse_texture.get(),
          "diffuse and normal roles deduplicate an identical texture file into one shared owner");

    ObjModelSource source = load_obj_model_source_file(obj_path);
    ModelAsset manual;
    manual.mesh = std::move(source.mesh);
    const auto shared_texture = std::make_shared<const Texture2D>(load_ppm_file(checker_path));
    MaterialDraw manual_draw;
    manual_draw.range = {0U, manual.mesh.triangles.size()};
    manual_draw.material_name = "mapped";
    manual_draw.diffuse_texture = shared_texture;
    manual_draw.normal_texture = shared_texture;
    manual.draws.push_back(std::move(manual_draw));

    const ModelRenderOptions options = normal_model_options(
        normalize(Vec3{0.3F, 0.2F, 1.0F}));
    Framebuffer imported_framebuffer(65U, 65U);
    draw_model_asset(
        imported_framebuffer,
        imported,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    Framebuffer manual_framebuffer(65U, 65U);
    draw_model_asset(
        manual_framebuffer,
        manual,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    check(imported_framebuffer.rgb8() == manual_framebuffer.rgb8(),
          "file-driven map_Bump model is byte-identical to equivalent programmatic normal texture submission");

    std::weak_ptr<const Texture2D> retained = imported.draws[0].normal_texture;
    const PreparedModelSubmission prepared = prepare_model_asset(std::move(imported), options);
    check(!retained.expired(),
          "prepared plan retains owned normal texture after source ModelAsset move");

    Framebuffer prepared_framebuffer(65U, 65U);
    draw_prepared_model(
        prepared_framebuffer,
        prepared,
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    check(prepared_framebuffer.rgb8() == manual_framebuffer.rgb8(),
          "prepared normal-mapped model remains byte-identical to direct programmatic submission");

    const PreparedModelListEntry entries[] = {
        {&prepared, Mat4::translation({-0.1F, 0.0F, 0.0F})},
        {&prepared, Mat4::translation({0.1F, 0.0F, 0.0F})},
    };
    Framebuffer list_framebuffer(65U, 65U);
    draw_prepared_model_list(
        list_framebuffer,
        entries,
        Mat4::identity(), Mat4::identity());

    Framebuffer sequential_framebuffer(65U, 65U);
    for (const PreparedModelListEntry& entry : entries) {
        draw_prepared_model(
            sequential_framebuffer,
            *entry.prepared,
            entry.model,
            Mat4::identity(), Mat4::identity());
    }
    check(list_framebuffer.rgb8() == sequential_framebuffer.rgb8(),
          "prepared normal-mapped list preserves deterministic sequential execution");
}

void test_no_normal_map_preserves_existing_lighting() {
    const Triangle triangle = canonical_triangle();
    Framebuffer framebuffer(65U, 65U);
    Rasterizer rasterizer(
        framebuffer,
        {},
        {},
        directional_light({0.0F, 0.0F, 1.0F}),
        {},
        BaseColorSource::ConstantWhite);
    rasterizer.draw_triangle(
        triangle,
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    check_near(max_red(framebuffer), 1.0F,
               "unmapped geometric-normal Lambert behavior remains unchanged");
}

}  // namespace

int main() {
    try {
        test_known_tangent_normal_rotates_lambert_response();
        test_non_uniform_model_scale_transforms_tangent_separately_from_normal();
        test_clipping_preserves_normal_mapped_result();
        test_invalid_normal_mapping_contracts_fail_closed();
        test_prepared_list_preflights_later_bad_tangent_before_first_write();
        test_bounded_mtl_map_bump_contract();
        test_file_driven_asset_matches_programmatic_and_prepared_lifetime();
        test_no_normal_map_preserves_existing_lighting();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " normal mapping test(s) failed\n";
        return 1;
    }
    std::cout << "all normal mapping tests passed\n";
    return 0;
}
