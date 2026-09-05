#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/mesh.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 2.0e-4F) {
    check(std::fabs(actual - expected) <= epsilon,
          message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

void check_color(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check_near(actual.x, expected.x, message + " red");
    check_near(actual.y, expected.y, message + " green");
    check_near(actual.z, expected.z, message + " blue");
}

Vertex colored_vertex(const Vec3& position, const Vec3& color) {
    return Vertex(position, color);
}

Vertex uv_normal_vertex(const Vec3& position, float u, float v, const Vec3& normal) {
    return Vertex::with_varyings(position, VaryingPack{
        u, v, normal.x, normal.y, normal.z,
    });
}

Triangle solid_triangle(const Vec3& color) {
    return Triangle{{
        colored_vertex({-0.6F, -0.6F, 0.0F}, color),
        colored_vertex({0.6F, -0.6F, 0.0F}, color),
        colored_vertex({0.0F, 0.6F, 0.0F}, color),
    }};
}

void test_default_white_material_is_byte_identical() {
    const Triangle triangle = solid_triangle({0.7F, 0.4F, 0.2F});

    Framebuffer implicit_fb(33U, 33U);
    Rasterizer implicit(implicit_fb);
    implicit.draw_triangle(triangle, Mat4::identity());

    Framebuffer explicit_fb(33U, 33U);
    Rasterizer explicit_white(explicit_fb, {}, {}, {}, MaterialState{{1.0F, 1.0F, 1.0F}});
    explicit_white.draw_triangle(triangle, Mat4::identity());

    check(implicit_fb.rgb8() == explicit_fb.rgb8(),
          "explicit white material is byte-identical to the default material state");
    check(implicit_fb.fnv1a64() == explicit_fb.fnv1a64(),
          "default material preserves the deterministic framebuffer hash");
}

void test_untextured_albedo_modulates_base_color() {
    const Triangle triangle = solid_triangle({0.8F, 0.6F, 0.4F});
    Framebuffer framebuffer(33U, 33U);
    Rasterizer rasterizer(
        framebuffer,
        {},
        {},
        {},
        MaterialState{{0.5F, 0.25F, 1.0F}});
    rasterizer.draw_triangle(triangle, Mat4::identity());

    check_color(framebuffer.color_at(16U, 16U), {0.4F, 0.15F, 0.4F},
                "material albedo modulates untextured varying color component-wise");
}

void test_textured_albedo_composes_with_lambert() {
    const Texture2D texture(1U, 1U, {{0.8F, 0.4F, 0.2F}});
    const TextureBinding texture_binding{&texture, 0U, 1U, {}};
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    const Triangle triangle{{
        uv_normal_vertex({-0.6F, -0.6F, 0.0F}, 0.0F, 0.0F, normal),
        uv_normal_vertex({0.6F, -0.6F, 0.0F}, 1.0F, 0.0F, normal),
        uv_normal_vertex({0.0F, 0.6F, 0.0F}, 0.5F, 1.0F, normal),
    }};
    const DirectionalLight light{
        true,
        NormalBinding{2U, 3U, 4U},
        {0.0F, 0.0F, 1.0F},
        0.25F,
        0.5F,
    };

    Framebuffer framebuffer(33U, 33U);
    Rasterizer rasterizer(
        framebuffer,
        {},
        texture_binding,
        light,
        MaterialState{{0.5F, 1.0F, 0.25F}});
    rasterizer.draw_triangle(triangle, Mat4::identity(), Mat4::identity(), Mat4::identity());

    check_color(framebuffer.color_at(16U, 16U), {0.3F, 0.3F, 0.0375F},
                "texture is modulated by material albedo before bounded Lambert intensity");
}

void test_material_clipping_matches_explicit_geometry() {
    const Vec3 white{1.0F, 1.0F, 1.0F};
    const Triangle crossing{{
        colored_vertex({-1.5F, 0.0F, 0.0F}, white),
        colored_vertex({0.0F, -0.7F, 0.0F}, white),
        colored_vertex({0.0F, 0.7F, 0.0F}, white),
    }};
    const Vertex intersection_20 = colored_vertex({-1.0F, 0.23333333F, 0.0F}, white);
    const Vertex intersection_01 = colored_vertex({-1.0F, -0.23333333F, 0.0F}, white);
    const Triangle manual_a{{intersection_20, intersection_01, crossing[1]}};
    const Triangle manual_b{{intersection_20, crossing[1], crossing[2]}};
    const MaterialState material{{0.35F, 0.65F, 0.9F}};

    Framebuffer automatic_fb(65U, 65U);
    Rasterizer automatic(automatic_fb, {}, {}, {}, material);
    automatic.draw_triangle(crossing, Mat4::identity());

    Framebuffer manual_fb(65U, 65U);
    Rasterizer manual(manual_fb, {}, {}, {}, material);
    manual.draw_triangle(manual_a, Mat4::identity());
    manual.draw_triangle(manual_b, Mat4::identity());

    check(automatic_fb.rgb8() == manual_fb.rgb8(),
          "material-modulated clipping is byte-identical to equivalent explicitly clipped geometry");
}

void test_explicit_source_modes_preserve_existing_paths() {
    const Triangle colored = solid_triangle({0.7F, 0.4F, 0.2F});
    Framebuffer auto_color_fb(33U, 33U);
    Rasterizer auto_color(auto_color_fb);
    auto_color.draw_triangle(colored, Mat4::identity());

    Framebuffer varying_fb(33U, 33U);
    Rasterizer varying(
        varying_fb,
        {},
        {},
        {},
        {},
        BaseColorSource::VaryingColor);
    varying.draw_triangle(colored, Mat4::identity());
    check(auto_color_fb.rgb8() == varying_fb.rgb8(),
          "explicit varying-color source is byte-identical to legacy automatic untextured selection");

    const Texture2D texture(1U, 1U, {{0.2F, 0.6F, 0.9F}});
    const TextureBinding texture_binding{&texture, 0U, 1U, {}};
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    const Triangle textured{{
        uv_normal_vertex({-0.6F, -0.6F, 0.0F}, 0.0F, 0.0F, normal),
        uv_normal_vertex({0.6F, -0.6F, 0.0F}, 1.0F, 0.0F, normal),
        uv_normal_vertex({0.0F, 0.6F, 0.0F}, 0.5F, 1.0F, normal),
    }};

    Framebuffer auto_texture_fb(33U, 33U);
    Rasterizer auto_texture(auto_texture_fb, {}, texture_binding);
    auto_texture.draw_triangle(textured, Mat4::identity());

    Framebuffer texture_fb(33U, 33U);
    Rasterizer explicit_texture(
        texture_fb,
        ColorBinding{99U, 99U, 99U},
        texture_binding,
        {},
        {},
        BaseColorSource::Texture);
    explicit_texture.draw_triangle(textured, Mat4::identity());
    check(auto_texture_fb.rgb8() == texture_fb.rgb8(),
          "explicit texture source is byte-identical to legacy automatic textured selection");
}

void test_constant_white_source_composes_with_material_and_lambert() {
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    const Triangle triangle{{
        uv_normal_vertex({-0.6F, -0.6F, 0.0F}, 0.0F, 0.0F, normal),
        uv_normal_vertex({0.6F, -0.6F, 0.0F}, 1.0F, 0.0F, normal),
        uv_normal_vertex({0.0F, 0.6F, 0.0F}, 0.5F, 1.0F, normal),
    }};
    const DirectionalLight light{
        true,
        NormalBinding{2U, 3U, 4U},
        {0.0F, 0.0F, 1.0F},
        0.2F,
        0.6F,
    };
    const MaterialState material{{0.5F, 0.25F, 1.0F}};

    Framebuffer framebuffer(33U, 33U);
    Rasterizer rasterizer(
        framebuffer,
        ColorBinding{99U, 99U, 99U},
        {},
        light,
        material,
        BaseColorSource::ConstantWhite);
    rasterizer.draw_triangle(triangle, Mat4::identity(), Mat4::identity(), Mat4::identity());

    check_color(framebuffer.color_at(16U, 16U), {0.4F, 0.2F, 0.8F},
                "constant-white source becomes pure material albedo before bounded Lambert intensity");
}

void test_constant_white_clipping_matches_explicit_geometry() {
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    const Triangle crossing{{
        uv_normal_vertex({-1.5F, 0.0F, 0.0F}, 0.0F, 0.5F, normal),
        uv_normal_vertex({0.0F, -0.7F, 0.0F}, 1.0F, 0.0F, normal),
        uv_normal_vertex({0.0F, 0.7F, 0.0F}, 1.0F, 1.0F, normal),
    }};
    const Vertex intersection_20 = uv_normal_vertex({-1.0F, 0.23333333F, 0.0F}, 0.33333333F, 0.66666667F, normal);
    const Vertex intersection_01 = uv_normal_vertex({-1.0F, -0.23333333F, 0.0F}, 0.33333333F, 0.33333333F, normal);
    const Triangle manual_a{{intersection_20, intersection_01, crossing[1]}};
    const Triangle manual_b{{intersection_20, crossing[1], crossing[2]}};
    const MaterialState material{{0.3F, 0.7F, 0.4F}};

    Framebuffer automatic_fb(65U, 65U);
    Rasterizer automatic(
        automatic_fb,
        ColorBinding{99U, 99U, 99U},
        {},
        {},
        material,
        BaseColorSource::ConstantWhite);
    automatic.draw_triangle(crossing, Mat4::identity());

    Framebuffer manual_fb(65U, 65U);
    Rasterizer manual(
        manual_fb,
        ColorBinding{99U, 99U, 99U},
        {},
        {},
        material,
        BaseColorSource::ConstantWhite);
    manual.draw_triangle(manual_a, Mat4::identity());
    manual.draw_triangle(manual_b, Mat4::identity());

    check(automatic_fb.rgb8() == manual_fb.rgb8(),
          "constant-white source uses the same clipping and coverage path as explicit geometry");
}

void expect_invalid_material(const MaterialState& material, const std::string& message) {
    const Triangle triangle = solid_triangle({1.0F, 1.0F, 1.0F});
    Framebuffer framebuffer(33U, 33U);
    const std::uint64_t before = framebuffer.fnv1a64();
    Rasterizer rasterizer(framebuffer, {}, {}, {}, material);
    bool threw = false;
    try {
        rasterizer.draw_triangle(triangle, Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, message);
    check(framebuffer.fnv1a64() == before, message + " before framebuffer mutation");
}

void test_invalid_materials_fail_closed() {
    expect_invalid_material(
        MaterialState{{std::numeric_limits<float>::quiet_NaN(), 1.0F, 1.0F}},
        "non-finite material albedo is rejected");
    expect_invalid_material(
        MaterialState{{-0.01F, 1.0F, 1.0F}},
        "negative material albedo is rejected");
    expect_invalid_material(
        MaterialState{{1.0F, 1.01F, 1.0F}},
        "material albedo above one is rejected");
}

void expect_invalid_source(
    BaseColorSource source,
    const TextureBinding& texture_binding,
    const std::string& message) {
    const Triangle triangle = solid_triangle({1.0F, 1.0F, 1.0F});
    Framebuffer framebuffer(33U, 33U);
    const std::uint64_t before = framebuffer.fnv1a64();
    Rasterizer rasterizer(framebuffer, {}, texture_binding, {}, {}, source);
    bool threw = false;
    try {
        rasterizer.draw_triangle(triangle, Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, message);
    check(framebuffer.fnv1a64() == before, message + " before framebuffer mutation");
}

void test_invalid_source_bindings_fail_closed() {
    expect_invalid_source(
        BaseColorSource::Texture,
        {},
        "explicit texture source without a bound texture is rejected");

    const Texture2D texture(1U, 1U, {{1.0F, 1.0F, 1.0F}});
    const TextureBinding texture_binding{&texture, 0U, 1U, {}};
    expect_invalid_source(
        BaseColorSource::VaryingColor,
        texture_binding,
        "explicit varying-color source rejects a conflicting bound texture");
    expect_invalid_source(
        BaseColorSource::ConstantWhite,
        texture_binding,
        "constant-white source rejects a conflicting bound texture");
    expect_invalid_source(
        static_cast<BaseColorSource>(255),
        {},
        "unknown base-color source enum value is rejected");
}

}  // namespace

int main() {
    try {
        test_default_white_material_is_byte_identical();
        test_untextured_albedo_modulates_base_color();
        test_textured_albedo_composes_with_lambert();
        test_material_clipping_matches_explicit_geometry();
        test_explicit_source_modes_preserve_existing_paths();
        test_constant_white_source_composes_with_material_and_lambert();
        test_constant_white_clipping_matches_explicit_geometry();
        test_invalid_materials_fail_closed();
        test_invalid_source_bindings_fail_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " material test(s) failed\n";
        return 1;
    }
    std::cout << "all material tests passed\n";
    return 0;
}
