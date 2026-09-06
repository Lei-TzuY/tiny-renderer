#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/mesh.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 1.0e-6F) {
    check(std::fabs(actual - expected) <= epsilon,
          message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

Mesh parse_text(std::string_view text) {
    std::istringstream input{std::string(text)};
    return load_obj(input);
}

Vertex uv_vertex(const Vec3& position, float u, float v) {
    return Vertex::with_varyings(position, VaryingPack{u, v});
}

Vertex uvn_vertex(const Vec3& position, float u, float v, const Vec3& normal) {
    return Vertex::with_varyings(position, VaryingPack{u, v, normal.x, normal.y, normal.z});
}

std::size_t count_non_black(const Framebuffer& framebuffer) {
    std::size_t count = 0U;
    for (std::size_t y = 0; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0; x < framebuffer.width(); ++x) {
            const Vec3& color = framebuffer.color_at(x, y);
            if (color.x != 0.0F || color.y != 0.0F || color.z != 0.0F) {
                ++count;
            }
        }
    }
    return count;
}

void test_pair_normalization_and_first_seen_order() {
    const Mesh mesh = parse_text(
        "v -0.8 -0.8 0\n"
        "v 0.8 -0.8 0\n"
        "v 0.8 0.8 0\n"
        "v -0.8 0.8 0\n"
        "vt 1 1\n"
        "vt 0 0\n"
        "vt 0 1\n"
        "vt 1 0\n"
        "vt 0.25 0.75\n"
        "f 1/2 2/4 3/1\n"
        "f 1/2 3/5 4/3\n");

    check(mesh.vertices.size() == 5U, "position reused with a different UV is split into a distinct unified vertex");
    check(mesh.triangles.size() == 2U, "two OBJ triangle faces become two mesh triangles");
    if (mesh.triangles.size() == 2U) {
        check(mesh.triangles[0] == TriangleIndices{0U, 1U, 2U}, "first face preserves first-seen pair order");
        check(mesh.triangles[1] == TriangleIndices{0U, 3U, 4U}, "second face reuses and splits pair indices deterministically");
    }

    if (mesh.vertices.size() == 5U) {
        check_near(mesh.vertices[0].position.x, -0.8F, "first unified vertex position x");
        check_near(mesh.vertices[0].varyings[0], 0.0F, "first unified vertex u");
        check_near(mesh.vertices[0].varyings[1], 0.0F, "first unified vertex v");
        check_near(mesh.vertices[2].position.x, 0.8F, "first position-3 pair x");
        check_near(mesh.vertices[2].varyings[0], 1.0F, "first position-3 pair u");
        check_near(mesh.vertices[2].varyings[1], 1.0F, "first position-3 pair v");
        check_near(mesh.vertices[3].position.x, 0.8F, "split position-3 pair x");
        check_near(mesh.vertices[3].varyings[0], 0.25F, "split position-3 pair u");
        check_near(mesh.vertices[3].varyings[1], 0.75F, "split position-3 pair v");
        for (const Vertex& vertex : mesh.vertices) {
            check(vertex.varyings.count == 2U, "legacy OBJ v/vt import still emits exactly two UV varying channels");
            check(vertex.varyings.interpolation_at(0U) == Interpolation::Smooth,
                  "imported U defaults to smooth interpolation");
            check(vertex.varyings.interpolation_at(1U) == Interpolation::Smooth,
                  "imported V defaults to smooth interpolation");
        }
    }
}

void test_triple_normalization_and_normal_channels() {
    const Mesh mesh = parse_text(
        "v -0.8 -0.8 0\n"
        "v 0.8 -0.8 0\n"
        "v 0.8 0.8 0\n"
        "v -0.8 0.8 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vt 0.25 0.75\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "vn 0.6 0 0.8\n"
        "f 1/1/1 2/2/1 3/3/1\n"
        "f 1/1/1 3/4/2 4/5/2\n");

    check(mesh.vertices.size() == 5U,
          "position reused with a different UV/normal tuple is split into a distinct unified vertex");
    check(mesh.triangles.size() == 2U, "normal-bearing OBJ faces become indexed mesh triangles");
    if (mesh.triangles.size() == 2U) {
        check(mesh.triangles[0] == TriangleIndices{0U, 1U, 2U}, "first normal-bearing face preserves first-seen tuple order");
        check(mesh.triangles[1] == TriangleIndices{0U, 3U, 4U}, "second normal-bearing face reuses and splits tuple indices deterministically");
    }

    if (mesh.vertices.size() == 5U) {
        check_near(mesh.vertices[2].varyings[2], 0.0F, "first position-3 tuple normal x");
        check_near(mesh.vertices[2].varyings[3], 0.0F, "first position-3 tuple normal y");
        check_near(mesh.vertices[2].varyings[4], 1.0F, "first position-3 tuple normal z");
        check_near(mesh.vertices[3].varyings[0], 0.25F, "split position-3 tuple u");
        check_near(mesh.vertices[3].varyings[1], 0.75F, "split position-3 tuple v");
        check_near(mesh.vertices[3].varyings[2], 0.6F, "split position-3 tuple normal x");
        check_near(mesh.vertices[3].varyings[3], 0.0F, "split position-3 tuple normal y");
        check_near(mesh.vertices[3].varyings[4], 0.8F, "split position-3 tuple normal z");
        for (const Vertex& vertex : mesh.vertices) {
            check(vertex.varyings.count == 5U, "v/vt/vn import emits UV plus three normal channels");
            for (std::size_t channel = 0U; channel < 5U; ++channel) {
                check(vertex.varyings.interpolation_at(channel) == Interpolation::Smooth,
                      "all imported UV/normal channels default to smooth interpolation");
            }
        }
    }
}

void expect_parse_error(std::string_view text, const std::string& message) {
    bool threw = false;
    try {
        (void)parse_text(text);
    } catch (const ObjParseError&) {
        threw = true;
    }
    check(threw, message);
}

void test_uv_optional_layouts_and_invalid_input() {
    const std::string prefix =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n";
    const std::string normal_prefix = prefix + "vn 0 0 1\n";

    const Mesh position_only = parse_text(prefix + "f 1 2 3\n");
    check(position_only.triangles.size() == 1U,
          "position-only face is accepted without inventing texture-coordinate channels");
    check(position_only.vertices.size() == 3U,
          "position-only face preserves deterministic vertex unification");
    for (const Vertex& vertex : position_only.vertices) {
        check(vertex.varyings.count == 0U,
              "position-only legacy face exposes no synthetic varyings");
    }

    const Mesh normal_only = parse_text(normal_prefix + "f 1//1 2//1 3//1\n");
    check(normal_only.triangles.size() == 1U,
          "v//vn face is accepted without texture-coordinate indices");
    for (const Vertex& vertex : normal_only.vertices) {
        check(vertex.varyings.count == 3U,
              "v//vn legacy face exposes exactly xyz normal channels");
        if (vertex.varyings.count == 3U) {
            check_near(vertex.varyings[0], 0.0F, "v//vn normal x");
            check_near(vertex.varyings[1], 0.0F, "v//vn normal y");
            check_near(vertex.varyings[2], 1.0F, "v//vn normal z");
        }
    }

    expect_parse_error(prefix + "f 1/1 2/2 3/3 1/1\n", "polygon face with a repeated corner is rejected");
    expect_parse_error(prefix + "f 0/1 2/2 3/3\n", "OBJ index zero is rejected");
    expect_parse_error(prefix + "f 4/1 2/2 3/3\n", "out-of-range position index is rejected");
    expect_parse_error(prefix + "f 1/4 2/2 3/3\n", "out-of-range texture index is rejected");
    expect_parse_error("v nope 0 0\n", "malformed floating-point vertex coordinate is rejected");
    expect_parse_error("vn 0 nope 1\n", "malformed normal coordinate is rejected");
    expect_parse_error("vn 0 0 0\n", "zero-length OBJ normal is rejected at import time");
    expect_parse_error(normal_prefix + "f 1//1 2/2/1 3//1\n", "mixed v//vn and v/vt/vn corners are rejected");
    expect_parse_error(normal_prefix + "f 1/1/2 2/2/1 3/3/1\n", "out-of-range normal index is rejected");
    expect_parse_error(normal_prefix + "f 1/1/1 2/2 3/3/1\n", "mixed corner layouts inside one face are rejected");
    expect_parse_error(
        normal_prefix +
        "f 1/1 2/2 3/3\n"
        "f 1/1/1 2/2/1 3/3/1\n",
        "mixing v/vt and v/vt/vn faces in one OBJ mesh is rejected");
    expect_parse_error(prefix + "vp 0 0 0\n", "unsupported geometry directives remain fail-closed");
}

void test_file_fixture_renders_identically_to_manual_mesh() {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for OBJ fixture tests
#endif
    const std::filesystem::path fixture =
        std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / "textured_quad.obj";
    const Mesh imported = load_obj_file(fixture);

    Mesh manual;
    manual.vertices = {
        uv_vertex({-0.8F, -0.8F, 0.0F}, 0.0F, 0.0F),
        uv_vertex({0.8F, -0.8F, 0.0F}, 1.0F, 0.0F),
        uv_vertex({0.8F, 0.8F, 0.0F}, 1.0F, 1.0F),
        uv_vertex({0.8F, 0.8F, 0.0F}, 0.25F, 0.75F),
        uv_vertex({-0.8F, 0.8F, 0.0F}, 0.0F, 1.0F),
    };
    manual.triangles = {
        TriangleIndices{0U, 1U, 2U},
        TriangleIndices{0U, 3U, 4U},
    };

    const Texture2D texture(2U, 2U, {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {1.0F, 1.0F, 1.0F},
    });
    const TextureBinding binding{
        &texture,
        0U,
        1U,
        SamplerState{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear},
    };

    Framebuffer imported_fb(65U, 65U);
    Rasterizer imported_rasterizer(imported_fb, {}, binding);
    imported_rasterizer.draw_mesh(imported, Mat4::identity());

    Framebuffer manual_fb(65U, 65U);
    Rasterizer manual_rasterizer(manual_fb, {}, binding);
    manual_rasterizer.draw_mesh(manual, Mat4::identity());

    check(count_non_black(imported_fb) > 0U, "imported textured fixture produces visible fragments");
    check(imported_fb.rgb8() == manual_fb.rgb8(),
          "legacy OBJ-imported mesh renders byte-identically to equivalent programmatic textured mesh");
    check(imported_fb.fnv1a64() == manual_fb.fnv1a64(),
          "legacy OBJ-imported mesh and manual mesh retain the same deterministic framebuffer hash");
}

void test_normal_fixture_drives_file_textured_lambert_path() {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for OBJ fixture tests
#endif
    const std::filesystem::path root = TINY_RENDERER_SOURCE_DIR;
    const Mesh imported = load_obj_file(root / "tests" / "fixtures" / "lit_textured_quad.obj");
    const Texture2D texture = load_ppm_file(root / "tests" / "fixtures" / "checker.ppm");

    Mesh manual;
    manual.vertices = {
        uvn_vertex({-0.8F, -0.8F, 0.0F}, 0.0F, 0.0F, {0.0F, 0.0F, 1.0F}),
        uvn_vertex({0.8F, -0.8F, 0.0F}, 1.0F, 0.0F, {0.0F, 0.0F, 1.0F}),
        uvn_vertex({0.8F, 0.8F, 0.0F}, 1.0F, 1.0F, {0.0F, 0.0F, 1.0F}),
        uvn_vertex({0.8F, 0.8F, 0.0F}, 0.25F, 0.75F, {0.6F, 0.0F, 0.8F}),
        uvn_vertex({-0.8F, 0.8F, 0.0F}, 0.0F, 1.0F, {0.6F, 0.0F, 0.8F}),
    };
    manual.triangles = {
        TriangleIndices{0U, 1U, 2U},
        TriangleIndices{0U, 3U, 4U},
    };

    const TextureBinding texture_binding{
        &texture,
        0U,
        1U,
        SamplerState{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear},
    };
    const DirectionalLight light{
        true,
        NormalBinding{2U, 3U, 4U},
        {0.0F, 0.0F, 1.0F},
        0.2F,
        0.8F,
    };
    const Mat4 model = Mat4::scale({1.1F, 0.75F, 1.0F});

    Framebuffer imported_fb(65U, 65U);
    Rasterizer imported_rasterizer(imported_fb, {}, texture_binding, light);
    imported_rasterizer.draw_mesh(imported, model, Mat4::identity(), Mat4::identity());

    Framebuffer manual_fb(65U, 65U);
    Rasterizer manual_rasterizer(manual_fb, {}, texture_binding, light);
    manual_rasterizer.draw_mesh(manual, model, Mat4::identity(), Mat4::identity());

    check(count_non_black(imported_fb) > 0U,
          "normal-bearing OBJ plus PPM fixture produces visible textured Lambert fragments");
    check(imported_fb.rgb8() == manual_fb.rgb8(),
          "file-imported OBJ normals feed the existing textured Lambert path byte-identically to manual assets");
    check(imported_fb.fnv1a64() == manual_fb.fnv1a64(),
          "file-imported normal mesh preserves the manual textured Lambert framebuffer hash");
}

void test_parse_error_reports_source_line() {
    bool checked = false;
    try {
        (void)parse_text(
            "# line 1\n"
            "v 0 0 0\n"
            "vt 0 0\n"
            "f 2/1 1/1 1/1\n");
    } catch (const ObjParseError& error) {
        checked = true;
        check(error.line() == 4U, "OBJ parse error reports the failing source line");
    }
    check(checked, "invalid fixture produces ObjParseError with source location");
}

}  // namespace

int main() {
    try {
        test_pair_normalization_and_first_seen_order();
        test_triple_normalization_and_normal_channels();
        test_uv_optional_layouts_and_invalid_input();
        test_file_fixture_renders_identically_to_manual_mesh();
        test_normal_fixture_drives_file_textured_lambert_path();
        test_parse_error_reports_source_line();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " OBJ loader test(s) failed\n";
        return 1;
    }
    std::cout << "all OBJ loader tests passed\n";
    return 0;
}
