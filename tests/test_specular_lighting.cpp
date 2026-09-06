#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/mtl_loader.hpp"
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

Vertex normal_vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.0F, 0.0F, 1.0F});
}

Triangle canonical_triangle() {
    return Triangle{{
        normal_vertex({-0.7F, -0.7F, 0.0F}),
        normal_vertex({0.7F, -0.7F, 0.0F}),
        normal_vertex({0.0F, 0.7F, 0.0F}),
    }};
}

DirectionalLight specular_light(const Vec3& viewer_position) {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.direction_to_light = {0.0F, 0.0F, 1.0F};
    light.ambient = 0.0F;
    light.diffuse = 1.0F;
    light.viewer_position = viewer_position;
    return light;
}

MaterialState glossy_material() {
    MaterialState material;
    material.albedo = {0.0F, 0.0F, 0.0F};
    material.opacity = 1.0F;
    material.specular = {1.0F, 1.0F, 1.0F};
    material.shininess = 32.0F;
    return material;
}

float render_center_red(const MaterialState& material, const Vec3& viewer_position) {
    Framebuffer framebuffer(65U, 65U);
    Rasterizer rasterizer(
        framebuffer,
        {},
        {},
        specular_light(viewer_position),
        material,
        BaseColorSource::ConstantWhite);
    rasterizer.draw_triangle(
        canonical_triangle(),
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    return framebuffer.color_at(32U, 32U).x;
}

ModelAsset model_from_triangle(const MaterialState& material) {
    const Triangle triangle = canonical_triangle();
    ModelAsset asset;
    asset.mesh.vertices = {triangle[0], triangle[1], triangle[2]};
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "glossy";
    draw.material = material;
    asset.draws.push_back(std::move(draw));
    return asset;
}

ModelRenderOptions glossy_options(const Vec3& viewer_position) {
    ModelRenderOptions options;
    options.directional_light = specular_light(viewer_position);
    return options;
}

void test_viewer_position_changes_blinn_phong_highlight() {
    const MaterialState material = glossy_material();
    const float aligned = render_center_red(material, {0.0F, 0.0F, 4.0F});
    const float off_axis = render_center_red(material, {4.0F, 0.0F, 4.0F});

    check(aligned > 0.9F,
          "viewer aligned with the reflection axis produces a strong specular highlight");
    check(off_axis < aligned * 0.5F,
          "moving the viewer off axis reduces the Blinn-Phong highlight");
}

void test_zero_specular_preserves_lambert_output() {
    MaterialState material;
    material.albedo = {0.4F, 0.2F, 0.1F};
    material.specular = {0.0F, 0.0F, 0.0F};

    Framebuffer first(65U, 65U);
    Rasterizer first_rasterizer(
        first,
        {},
        {},
        specular_light({0.0F, 0.0F, 4.0F}),
        material,
        BaseColorSource::ConstantWhite);
    first_rasterizer.draw_triangle(
        canonical_triangle(),
        Mat4::identity(), Mat4::identity(), Mat4::identity());

    Framebuffer second(65U, 65U);
    Rasterizer second_rasterizer(
        second,
        {},
        {},
        specular_light({4.0F, 2.0F, 1.0F}),
        material,
        BaseColorSource::ConstantWhite);
    second_rasterizer.draw_triangle(
        canonical_triangle(),
        Mat4::identity(), Mat4::identity(), Mat4::identity());

    check(first.rgb8() == second.rgb8(),
          "zero specular reflectance preserves Lambert output independent of viewer position");
}

void test_bounded_mtl_ks_ns_contract() {
    {
        std::istringstream input(
            "newmtl glossy\n"
            "Kd 0.2 0.3 0.4\n"
            "Ks 0.5 0.6 0.7\n"
            "Ns 64\n");
        const MaterialLibrary library = load_mtl(input);
        const MaterialState& material = library.at("glossy");
        check_near(material.specular.x, 0.5F, "MTL Ks red is imported");
        check_near(material.specular.y, 0.6F, "MTL Ks green is imported");
        check_near(material.specular.z, 0.7F, "MTL Ks blue is imported");
        check_near(material.shininess, 64.0F, "MTL Ns shininess is imported");
    }

    {
        std::istringstream input(
            "newmtl matte\n"
            "Kd 0.2 0.3 0.4\n");
        const MaterialLibrary library = load_mtl(input);
        const MaterialState& material = library.at("matte");
        check(material.specular.x == 0.0F
                  && material.specular.y == 0.0F
                  && material.specular.z == 0.0F,
              "missing Ks preserves zero-specular compatibility");
        check_near(material.shininess, 32.0F,
                   "missing Ns preserves the bounded default exponent");
    }

    const auto expect_failure = [](const std::string& text) {
        std::istringstream input(text);
        try {
            (void)load_mtl(input);
        } catch (const MtlParseError&) {
            return true;
        }
        return false;
    };

    check(expect_failure(
              "newmtl x\nKd 1 1 1\nKs 0.1 0.2 0.3\nKs 0.2 0.3 0.4\n"),
          "duplicate Ks is rejected deterministically");
    check(expect_failure(
              "newmtl x\nKd 1 1 1\nKs 1.1 0 0\n"),
          "out-of-range Ks is rejected");
    check(expect_failure(
              "newmtl x\nKd 1 1 1\nNs 16\nNs 32\n"),
          "duplicate Ns is rejected deterministically");
    check(expect_failure(
              "newmtl x\nKd 1 1 1\nNs 0\n"),
          "shininess below the bounded subset is rejected");
    check(expect_failure(
              "newmtl x\nKd 1 1 1\nNs 1001\n"),
          "shininess above the bounded subset is rejected");
}

void test_file_driven_specular_material_matches_programmatic_state() {
    const std::filesystem::path fixture =
        std::filesystem::path(TINY_RENDERER_SOURCE_DIR)
        / "tests" / "fixtures" / "specular_material.obj";

    ModelAsset imported = load_obj_model_asset_file(fixture);
    check(imported.draws.size() == 1U,
          "specular fixture produces one canonical material draw");
    check_near(imported.draws[0].material.specular.x, 1.0F,
               "file-driven model retains Ks red");
    check_near(imported.draws[0].material.specular.y, 0.5F,
               "file-driven model retains Ks green");
    check_near(imported.draws[0].material.specular.z, 0.25F,
               "file-driven model retains Ks blue");
    check_near(imported.draws[0].material.shininess, 32.0F,
               "file-driven model retains Ns shininess");

    ModelAsset manual = imported;
    manual.draws[0].material = MaterialState{};
    manual.draws[0].material.albedo = {0.0F, 0.0F, 0.0F};
    manual.draws[0].material.specular = {1.0F, 0.5F, 0.25F};
    manual.draws[0].material.shininess = 32.0F;

    const ModelRenderOptions options = glossy_options({0.0F, 0.0F, 4.0F});
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
          "imported Ks/Ns render byte-identically to equivalent programmatic material state");
}

void test_prepared_static_specular_validation() {
    {
        MaterialState invalid = glossy_material();
        invalid.specular.x = 1.1F;
        bool threw = false;
        try {
            (void)prepare_model_asset(model_from_triangle(invalid));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "prepared model construction rejects out-of-range specular reflectance");
    }

    {
        MaterialState invalid = glossy_material();
        invalid.shininess = 0.0F;
        bool threw = false;
        try {
            (void)prepare_model_asset(model_from_triangle(invalid));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "prepared model construction rejects invalid shininess");
    }

    {
        ModelRenderOptions options = glossy_options({
            std::numeric_limits<float>::quiet_NaN(), 0.0F, 4.0F});
        bool threw = false;
        try {
            (void)prepare_model_asset(model_from_triangle(glossy_material()), options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "prepared specular model rejects a non-finite viewer position");
    }
}

void test_prepared_list_preflights_later_bad_world_transform_before_writes() {
    const PreparedModelSubmission prepared = prepare_model_asset(
        model_from_triangle(glossy_material()),
        glossy_options({0.0F, 0.0F, 4.0F}));

    const Mat4 invalid_projective({
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 0.0F,
    });
    const PreparedModelListEntry entries[] = {
        {&prepared, Mat4::identity()},
        {&prepared, invalid_projective},
    };

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 11U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);

    bool threw = false;
    try {
        draw_prepared_model_list(
            framebuffer,
            entries,
            Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw,
          "later prepared-list specular world transform is rejected");
    check(framebuffer.rgb8() == before,
          "later specular transform rejection happens before earlier color writes");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "later specular transform rejection happens before earlier depth writes");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "later specular transform rejection happens before earlier stencil writes");
}

}  // namespace

int main() {
    try {
        test_viewer_position_changes_blinn_phong_highlight();
        test_zero_specular_preserves_lambert_output();
        test_bounded_mtl_ks_ns_contract();
        test_file_driven_specular_material_matches_programmatic_state();
        test_prepared_static_specular_validation();
        test_prepared_list_preflights_later_bad_world_transform_before_writes();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " specular lighting test(s) failed\n";
        return 1;
    }
    std::cout << "all specular lighting tests passed\n";
    return 0;
}
