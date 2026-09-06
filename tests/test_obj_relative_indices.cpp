#include <cmath>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/obj_loader.hpp"
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

Mesh parse_mesh(std::string_view text) {
    std::istringstream input{std::string(text)};
    return load_obj(input);
}

ObjModelSource parse_model_source(std::string_view text) {
    std::istringstream input{std::string(text)};
    return load_obj_model_source(input);
}

void check_mesh_equal(const Mesh& actual, const Mesh& expected, const std::string& label) {
    check(actual.triangles == expected.triangles, label + ": triangle indices match");
    check(actual.vertices.size() == expected.vertices.size(), label + ": unified vertex count matches");
    if (actual.vertices.size() != expected.vertices.size()) {
        return;
    }
    for (std::size_t i = 0U; i < actual.vertices.size(); ++i) {
        const Vertex& a = actual.vertices[i];
        const Vertex& b = expected.vertices[i];
        check_near(a.position.x, b.position.x, label + ": position x");
        check_near(a.position.y, b.position.y, label + ": position y");
        check_near(a.position.z, b.position.z, label + ": position z");
        check(a.varyings.count == b.varyings.count, label + ": varying count matches");
        if (a.varyings.count != b.varyings.count) {
            continue;
        }
        for (std::size_t channel = 0U; channel < a.varyings.count; ++channel) {
            check_near(a.varyings.values[channel], b.varyings.values[channel], label + ": varying value");
            check(a.varyings.interpolation[channel] == b.varyings.interpolation[channel],
                  label + ": interpolation qualifier matches");
        }
    }
}

std::string common_geometry() {
    return
        "v -0.8 -0.8 0\n"
        "v 0.8 -0.8 0\n"
        "v 0.8 0.8 0\n"
        "v -0.8 0.8 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vt 0 1\n";
}

std::string pentagon_geometry() {
    return
        "v -0.8 -0.6 0\n"
        "v 0.8 -0.6 0\n"
        "v 0.9 0.2 0\n"
        "v 0 0.9 0\n"
        "v -0.9 0.2 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 0.5\n"
        "vt 0.5 1\n"
        "vt 0 0.5\n";
}

void test_relative_position_texcoord_matches_absolute() {
    const std::string prefix = common_geometry();
    const Mesh absolute = parse_mesh(
        prefix +
        "f 1/1 2/2 3/3\n"
        "f 1/1 3/3 4/4\n");
    const Mesh relative = parse_mesh(
        prefix +
        "f -4/-4 -3/-3 -2/-2\n"
        "f 1/1 -2/-2 -1/-1\n");
    check_mesh_equal(relative, absolute, "relative v/vt canonicalization");
}

void test_relative_normal_indices_match_absolute() {
    const std::string prefix = common_geometry()
        + "vn 0 0 1\n"
        + "vn 0.6 0 0.8\n";
    const Mesh absolute = parse_mesh(
        prefix +
        "f 1/1/1 2/2/1 3/3/1\n"
        "f 1/1/1 3/3/2 4/4/2\n");
    const Mesh relative = parse_mesh(
        prefix +
        "f -4/-4/-2 -3/-3/-2 -2/-2/-2\n"
        "f 1/1/-2 -2/-2/-1 -1/-1/-1\n");
    check_mesh_equal(relative, absolute, "relative v/vt/vn canonicalization");
}

void test_relative_indices_use_face_definition_extent() {
    const Mesh mesh = parse_mesh(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f -3/-3 -2/-2 -1/-1\n"
        "v 9 9 9\n"
        "vt 0.5 0.5\n");
    check(mesh.vertices.size() == 3U, "relative indices bind against records available at the face line");
    check(mesh.triangles.size() == 1U, "face-definition extent test emits one triangle");
    if (mesh.vertices.size() == 3U) {
        check_near(mesh.vertices[0].position.x, 0.0F, "late vertex does not retarget relative position -3");
        check_near(mesh.vertices[1].position.x, 1.0F, "late vertex does not retarget relative position -2");
        check_near(mesh.vertices[2].position.y, 1.0F, "late vertex does not retarget relative position -1");
    }
}

void test_rich_material_source_preserves_metadata() {
    const std::string geometry = common_geometry() + "vn 0 0 1\n";
    const ObjModelSource absolute = parse_model_source(
        "mtllib material.mtl\n" + geometry +
        "usemtl first\n"
        "f 1/1/1 2/2/1 3/3/1\n"
        "usemtl second\n"
        "f 1/1/1 3/3/1 4/4/1\n");
    const ObjModelSource relative = parse_model_source(
        "mtllib material.mtl\n" + geometry +
        "usemtl first\n"
        "f -4/-4/-1 -3/-3/-1 -2/-2/-1\n"
        "usemtl second\n"
        "f 1/1/-1 -2/-2/-1 -1/-1/-1\n");

    check_mesh_equal(relative.mesh, absolute.mesh, "relative rich-model canonical geometry");
    check(relative.material_library_filename == absolute.material_library_filename,
          "relative rich model preserves mtllib metadata");
    check(relative.face_materials == absolute.face_materials,
          "relative rich model preserves ordered face-material binding");
    check(relative.used_materials.size() == absolute.used_materials.size(),
          "relative rich model preserves material-use count");
    if (relative.used_materials.size() == absolute.used_materials.size()) {
        for (std::size_t i = 0U; i < relative.used_materials.size(); ++i) {
            check(relative.used_materials[i].name == absolute.used_materials[i].name,
                  "relative rich model preserves material-use names");
            check(relative.used_materials[i].line == absolute.used_materials[i].line,
                  "relative rich model preserves material-use source lines");
        }
    }
}

void test_polygon_fan_triangulation_matches_explicit_triangles() {
    const std::string prefix = pentagon_geometry();
    const Mesh explicit_pair = parse_mesh(
        prefix +
        "f 1/1 2/2 3/3\n"
        "f 1/1 3/3 4/4\n"
        "f 1/1 4/4 5/5\n");
    const Mesh polygon_pair = parse_mesh(prefix + "f 1/1 2/2 3/3 4/4 5/5\n");
    check_mesh_equal(polygon_pair, explicit_pair, "v/vt polygon deterministic fan triangulation");

    const std::string normal_prefix = prefix + "vn 0 0 1\n";
    const Mesh explicit_triple = parse_mesh(
        normal_prefix +
        "f 1/1/1 2/2/1 3/3/1\n"
        "f 1/1/1 3/3/1 4/4/1\n"
        "f 1/1/1 4/4/1 5/5/1\n");
    const Mesh polygon_triple = parse_mesh(
        normal_prefix + "f 1/1/1 2/2/1 3/3/1 4/4/1 5/5/1\n");
    check_mesh_equal(polygon_triple, explicit_triple, "v/vt/vn polygon deterministic fan triangulation");
}

void test_relative_polygon_matches_absolute() {
    const std::string prefix = pentagon_geometry();
    const Mesh absolute = parse_mesh(prefix + "f 1/1 2/2 3/3 4/4 5/5\n");
    const Mesh relative = parse_mesh(prefix + "f -5/-5 -4/-4 -3/-3 -2/-2 -1/-1\n");
    check_mesh_equal(relative, absolute, "relative polygon canonicalization");
}

void test_rich_polygon_material_expands_per_generated_triangle() {
    const ObjModelSource source = parse_model_source(
        "mtllib material.mtl\n" + common_geometry() +
        "usemtl first\n"
        "f 1/1 2/2 3/3 4/4\n"
        "usemtl second\n"
        "f 1/1 3/3 4/4\n");

    check(source.mesh.triangles.size() == 3U, "quad plus triangle emits three canonical triangles");
    check(source.face_materials.size() == source.mesh.triangles.size(),
          "rich polygon material metadata expands to one entry per generated triangle");
    if (source.face_materials.size() == 3U) {
        check(source.face_materials[0] == "first", "first fan triangle retains active material");
        check(source.face_materials[1] == "first", "second fan triangle retains active material");
        check(source.face_materials[2] == "second", "following triangle retains subsequent material");
    }
}

void expect_error(
    std::string_view text,
    std::size_t expected_line,
    std::string_view expected_fragment,
    const std::string& message) {
    bool threw = false;
    try {
        (void)parse_mesh(text);
    } catch (const ObjParseError& error) {
        threw = true;
        check(error.line() == expected_line, message + ": source line is deterministic");
        check(std::string_view(error.what()).find(expected_fragment) != std::string_view::npos,
              message + ": diagnostic identifies the rejected index");
    }
    check(threw, message + ": parser rejects input");
}

void test_zero_and_out_of_range_indices_fail_closed() {
    const std::string prefix =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n";
    expect_error(prefix + "f 0/1 2/2 3/3\n", 7U, "must not be zero", "zero OBJ index");
    expect_error(prefix + "f -4/1 -2/2 -1/3\n", 7U, "position relative index is out of range",
                 "relative position underflow");
    expect_error(prefix + "f 1/-4 2/-2 3/-1\n", 7U, "texture-coordinate relative index is out of range",
                 "relative texture-coordinate underflow");
    expect_error(
        prefix + "vn 0 0 1\nf 1/1/-2 2/2/-1 3/3/-1\n",
        8U,
        "normal relative index is out of range",
        "relative normal underflow");
    expect_error(prefix + "f -9223372036854775808/1 -2/2 -1/3\n", 7U,
                 "position relative index is out of range", "minimum signed relative index");
    expect_error(prefix + "f 4/1 2/2 3/3\n", 7U, "position index is out of range",
                 "positive absolute out-of-range behavior remains fail-closed");
}

void test_polygon_corner_bounds_fail_closed() {
    expect_error(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "f 1/1 2/2\n",
        5U,
        "at least three corners",
        "polygon with fewer than three corners");

    std::string oversized;
    for (std::size_t i = 0U; i < 65U; ++i) {
        oversized += "v " + std::to_string(i) + " 0 0\n";
    }
    for (std::size_t i = 0U; i < 65U; ++i) {
        oversized += "vt 0 0\n";
    }
    oversized += "f";
    for (std::size_t i = 1U; i <= 65U; ++i) {
        oversized += " " + std::to_string(i) + "/" + std::to_string(i);
    }
    oversized += "\n";
    expect_error(oversized, 131U, "at most 64 corners", "polygon corner cap");
}

void test_relative_mesh_renders_identically_to_absolute() {
    const std::string prefix = common_geometry();
    const Mesh absolute = parse_mesh(
        prefix +
        "f 1/1 2/2 3/3\n"
        "f 1/1 3/3 4/4\n");
    const Mesh relative = parse_mesh(
        prefix +
        "f -4/-4 -3/-3 -2/-2\n"
        "f 1/1 -2/-2 -1/-1\n");

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

    Framebuffer absolute_fb(65U, 65U);
    Rasterizer absolute_rasterizer(absolute_fb, {}, binding);
    absolute_rasterizer.draw_mesh(absolute, Mat4::identity());

    Framebuffer relative_fb(65U, 65U);
    Rasterizer relative_rasterizer(relative_fb, {}, binding);
    relative_rasterizer.draw_mesh(relative, Mat4::identity());

    check(relative_fb.rgb8() == absolute_fb.rgb8(),
          "relative OBJ indices render byte-identically to equivalent absolute indices");
    check(relative_fb.fnv1a64() == absolute_fb.fnv1a64(),
          "relative OBJ indices preserve deterministic framebuffer hash");
}

void test_polygon_renders_identically_to_explicit_fan() {
    const std::string prefix = common_geometry();
    const Mesh explicit_mesh = parse_mesh(
        prefix +
        "f 1/1 2/2 3/3\n"
        "f 1/1 3/3 4/4\n");
    const Mesh polygon_mesh = parse_mesh(prefix + "f 1/1 2/2 3/3 4/4\n");

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

    Framebuffer explicit_fb(65U, 65U);
    Rasterizer explicit_rasterizer(explicit_fb, {}, binding);
    explicit_rasterizer.draw_mesh(explicit_mesh, Mat4::identity());

    Framebuffer polygon_fb(65U, 65U);
    Rasterizer polygon_rasterizer(polygon_fb, {}, binding);
    polygon_rasterizer.draw_mesh(polygon_mesh, Mat4::identity());

    check(polygon_fb.rgb8() == explicit_fb.rgb8(),
          "polygon fan renders byte-identically to the same explicit triangle sequence");
    check(polygon_fb.fnv1a64() == explicit_fb.fnv1a64(),
          "polygon fan preserves deterministic framebuffer hash");
}

}  // namespace

int main() {
    try {
        test_relative_position_texcoord_matches_absolute();
        test_relative_normal_indices_match_absolute();
        test_relative_indices_use_face_definition_extent();
        test_rich_material_source_preserves_metadata();
        test_polygon_fan_triangulation_matches_explicit_triangles();
        test_relative_polygon_matches_absolute();
        test_rich_polygon_material_expands_per_generated_triangle();
        test_zero_and_out_of_range_indices_fail_closed();
        test_polygon_corner_bounds_fail_closed();
        test_relative_mesh_renders_identically_to_absolute();
        test_polygon_renders_identically_to_explicit_fan();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " relative OBJ index/polygon test(s) failed\n";
        return 1;
    }
    std::cout << "all relative OBJ index/polygon tests passed\n";
    return 0;
}
