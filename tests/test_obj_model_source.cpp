#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/obj_loader.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check_near(float actual, float expected, const std::string& message, float epsilon = 1.0e-5F) {
    check(std::fabs(actual - expected) <= epsilon,
          message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

std::filesystem::path fixture_path(const char* name) {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for OBJ model-source tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

bool same_pack(const VaryingPack& a, const VaryingPack& b) {
    if (a.count != b.count) {
        return false;
    }
    for (std::size_t i = 0U; i < a.count; ++i) {
        if (a.values[i] != b.values[i] || a.interpolation[i] != b.interpolation[i]) {
            return false;
        }
    }
    return true;
}

bool same_mesh(const Mesh& a, const Mesh& b) {
    if (a.triangles != b.triangles || a.vertices.size() != b.vertices.size()) {
        return false;
    }
    for (std::size_t i = 0U; i < a.vertices.size(); ++i) {
        const Vertex& av = a.vertices[i];
        const Vertex& bv = b.vertices[i];
        if (av.position.x != bv.position.x || av.position.y != bv.position.y || av.position.z != bv.position.z
            || !same_pack(av.varyings, bv.varyings)) {
            return false;
        }
    }
    return true;
}

ObjModelSource parse_strict(const std::string& obj) {
    std::istringstream input(obj);
    return load_obj_model_source(input);
}

Vec3 vertex_normal(const Vertex& vertex) {
    check(vertex.varyings.count >= 5U, "normal-bearing OBJ vertex exposes UV plus xyz normal channels");
    if (vertex.varyings.count < 5U) {
        return {};
    }
    return {
        vertex.varyings.values[2],
        vertex.varyings.values[3],
        vertex.varyings.values[4],
    };
}

bool same_position(const Vec3& a, const Vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

std::vector<Vec3> normals_at_position(const Mesh& mesh, const Vec3& position) {
    std::vector<Vec3> result;
    for (const Vertex& vertex : mesh.vertices) {
        if (same_position(vertex.position, position)) {
            result.push_back(vertex_normal(vertex));
        }
    }
    return result;
}

void check_normal(const Vec3& actual, const Vec3& expected, const std::string& label) {
    check_near(actual.x, expected.x, label + " x");
    check_near(actual.y, expected.y, label + " y");
    check_near(actual.z, expected.z, label + " z");
}

std::string bent_geometry() {
    return
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "v 0 0 1\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vt 1 1\n";
}

ModelAsset one_draw_asset(const ObjModelSource& source) {
    ModelAsset asset;
    asset.mesh = source.mesh;
    if (!asset.mesh.triangles.empty()) {
        MaterialDraw draw;
        draw.range = {0U, asset.mesh.triangles.size()};
        asset.draws.push_back(draw);
    }
    return asset;
}

void test_single_pass_source_preserves_geometry_and_material_order() {
    const std::filesystem::path path = fixture_path("material_texture_sequence.obj");
    const Mesh legacy = load_obj_file(path);
    const ObjModelSource source = load_obj_model_source_file(path);

    check(same_mesh(legacy, source.mesh),
          "strict model-source parsing preserves explicit-normal canonical geometry produced by legacy load_obj");
    check(source.material_library_filename.has_value(), "strict model source captures mtllib");
    if (source.material_library_filename) {
        check(*source.material_library_filename == "diffuse_textured.mtl",
              "strict model source preserves the sibling material-library filename");
    }
    check(source.face_materials.size() == source.mesh.triangles.size(),
          "each accepted face emits geometry and one material association in the same parse event");
    check(source.face_materials.size() == 3U,
          "mixed material fixture produces three canonical face-material associations");
    if (source.face_materials.size() == 3U) {
        check(source.face_materials[0] == "warm" && source.face_materials[1] == "cool"
                  && source.face_materials[2] == "warm",
              "single-pass source preserves warm-cool-warm submission order");
    }
    check(source.used_materials.size() == 3U,
          "single-pass source records each material activation for deterministic validation diagnostics");
    if (source.used_materials.size() == 3U) {
        check(source.used_materials[0].name == "warm" && source.used_materials[0].line == 11U,
              "first material activation retains its OBJ source line");
        check(source.used_materials[1].name == "cool" && source.used_materials[1].line == 13U,
              "second material activation retains its OBJ source line");
        check(source.used_materials[2].name == "warm" && source.used_materials[2].line == 15U,
              "third material activation retains its OBJ source line");
    }
}

void test_legacy_metadata_ignore_remains_source_compatible() {
    const std::string obj =
        "mtllib\n"
        "usemtl\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n";

    std::istringstream legacy_input(obj);
    bool legacy_threw = false;
    Mesh legacy;
    try {
        legacy = load_obj(legacy_input);
    } catch (const std::exception&) {
        legacy_threw = true;
    }
    check(!legacy_threw, "legacy load_obj continues to ignore malformed material metadata records");
    check(legacy.triangles.size() == 1U, "legacy metadata-ignore mode still returns geometry");

    std::istringstream strict_input(obj);
    bool strict_threw = false;
    std::size_t strict_line = 0U;
    try {
        (void)load_obj_model_source(strict_input);
    } catch (const ObjParseError& error) {
        strict_threw = true;
        strict_line = error.line();
    }
    check(strict_threw, "strict model-source mode validates material metadata ignored by legacy load_obj");
    check(strict_line == 1U, "strict malformed mtllib diagnostic reports the exact source line");
}

void expect_strict_error_line(const std::string& obj, std::size_t expected_line, const std::string& message) {
    std::istringstream input(obj);
    bool threw = false;
    std::size_t line = 0U;
    try {
        (void)load_obj_model_source(input);
    } catch (const ObjParseError& error) {
        threw = true;
        line = error.line();
    }
    check(threw, message + " is rejected");
    check(line == expected_line, message + " reports deterministic OBJ source line");
}

void test_strict_material_metadata_line_diagnostics() {
    expect_strict_error_line(
        "usemtl warm\n"
        "v 0 0 0\n",
        1U,
        "usemtl before mtllib");

    expect_strict_error_line(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n"
        "mtllib diffuse.mtl\n",
        8U,
        "late mtllib after an accepted face");

    expect_strict_error_line(
        "mtllib diffuse.mtl\n"
        "usemtl warm extra\n",
        2U,
        "usemtl with extra tokens");

    expect_strict_error_line(
        "mtllib diffuse.mtl\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n",
        8U,
        "material-bound face without active usemtl");
}

void test_generated_flat_and_smooth_normals() {
    const std::string geometry = bent_geometry();
    const ObjModelSource flat = parse_strict(
        geometry
        + "f 1/1 2/2 3/3\n"
        + "f 1/1 4/4 2/2\n");

    check(flat.mesh.triangles.size() == 2U, "flat generated-normal source emits both triangles");
    check(flat.mesh.vertices.size() == 6U,
          "default smoothing-off mode splits shared source corners across original faces");
    const std::vector<Vec3> flat_origin_normals = normals_at_position(flat.mesh, {0.0F, 0.0F, 0.0F});
    check(flat_origin_normals.size() == 2U, "flat shared position owns one vertex per face-normal domain");
    if (flat_origin_normals.size() == 2U) {
        check_normal(flat_origin_normals[0], {0.0F, 0.0F, 1.0F}, "flat first-face normal");
        check_normal(flat_origin_normals[1], {0.0F, 1.0F, 0.0F}, "flat second-face normal");
    }

    const ObjModelSource smooth = parse_strict(
        geometry
        + "s 1\n"
        + "f 1/1 2/2 3/3\n"
        + "f 1/1 4/4 2/2\n");
    check(smooth.mesh.vertices.size() == 4U,
          "one smoothing group reuses shared position/UV corners across adjacent faces");
    const std::vector<Vec3> smooth_origin_normals = normals_at_position(smooth.mesh, {0.0F, 0.0F, 0.0F});
    check(smooth_origin_normals.size() == 1U, "smoothed shared position has one unified vertex for one UV");
    if (smooth_origin_normals.size() == 1U) {
        const float diagonal = std::sqrt(0.5F);
        check_normal(
            smooth_origin_normals[0],
            {0.0F, diagonal, diagonal},
            "area-weighted smoothing-group normal");
    }

    const ObjModelSource split_groups = parse_strict(
        geometry
        + "s 1\n"
        + "f 1/1 2/2 3/3\n"
        + "s 2\n"
        + "f 1/1 4/4 2/2\n");
    check(split_groups.mesh.vertices.size() == 6U,
          "changing smoothing group creates a deterministic normal seam at shared source corners");
}

void test_smoothing_accumulation_crosses_uv_seams() {
    const ObjModelSource source = parse_strict(
        bent_geometry()
        + "vt 0.25 0.75\n"
        + "s on\n"
        + "f 1/1 2/2 3/3\n"
        + "f 1/5 4/4 2/2\n");

    const std::vector<Vec3> origin_normals = normals_at_position(source.mesh, {0.0F, 0.0F, 0.0F});
    check(origin_normals.size() == 2U,
          "UV seam duplicates the render vertex while preserving one source-position smoothing domain");
    if (origin_normals.size() == 2U) {
        const float diagonal = std::sqrt(0.5F);
        check_normal(origin_normals[0], {0.0F, diagonal, diagonal}, "first UV-seam smoothed normal");
        check_normal(origin_normals[1], {0.0F, diagonal, diagonal}, "second UV-seam smoothed normal");
    }
}

void test_flat_polygon_generation_does_not_create_fan_seams() {
    const ObjModelSource source = parse_strict(
        "v -1 -1 0\n"
        "v 1 -1 0\n"
        "v 1 1 0\n"
        "v -1 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vt 0 1\n"
        "s off\n"
        "f 1/1 2/2 3/3 4/4\n");

    check(source.mesh.triangles.size() == 2U, "quad still triangulates into the canonical two-triangle fan");
    check(source.mesh.vertices.size() == 4U,
          "one flat polygon owns one normal domain across all generated fan triangles");
    for (const Vertex& vertex : source.mesh.vertices) {
        check_normal(vertex_normal(vertex), {0.0F, 0.0F, 1.0F}, "flat polygon generated normal");
    }
}

void test_explicit_normals_remain_authoritative() {
    const ObjModelSource source = parse_strict(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 0 0 2\n"
        "s 7\n"
        "f 1/1/1 2/2/1 3/3/1\n");

    check(source.mesh.vertices.size() == 3U, "explicit-normal face keeps canonical unified vertices");
    for (const Vertex& vertex : source.mesh.vertices) {
        check_normal(vertex_normal(vertex), {0.0F, 0.0F, 2.0F}, "explicit OBJ normal remains unmodified");
    }
}

void test_smoothing_directive_strictness_and_legacy_compatibility() {
    const std::string obj =
        "s nonsense extra\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n";

    std::istringstream legacy_input(obj);
    bool legacy_threw = false;
    Mesh legacy;
    try {
        legacy = load_obj(legacy_input);
    } catch (const std::exception&) {
        legacy_threw = true;
    }
    check(!legacy_threw, "legacy load_obj continues to ignore smoothing metadata syntax");
    check(legacy.vertices.size() == 3U && legacy.vertices[0].varyings.count == 2U,
          "legacy v/vt geometry remains UV-only and source-compatible");

    expect_strict_error_line(obj, 1U, "malformed strict smoothing directive");
    expect_strict_error_line("s 4294967296\n", 1U, "out-of-range smoothing group id");
}

void test_generated_normal_degenerate_face_fails_closed() {
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 2 0 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 2 0\n"
        "f 1/1 2/2 3/3\n";

    std::istringstream legacy_input(obj);
    bool legacy_threw = false;
    try {
        (void)load_obj(legacy_input);
    } catch (const std::exception&) {
        legacy_threw = true;
    }
    check(!legacy_threw, "legacy UV-only parser preserves degenerate geometry for downstream raster handling");
    expect_strict_error_line(obj, 7U, "degenerate face requiring a generated normal");
}

void test_generated_normals_render_like_equivalent_explicit_normals() {
    const std::string geometry =
        "v -0.7 -0.7 0\n"
        "v 0.7 -0.7 0\n"
        "v 0 0.7 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0.5 1\n";
    const ObjModelSource generated = parse_strict(
        geometry
        + "s 1\n"
        + "f 1/1 2/2 3/3\n");
    const ObjModelSource explicit_normals = parse_strict(
        geometry
        + "vn 0 0 1\n"
        + "f 1/1/1 2/2/1 3/3/1\n");

    ModelRenderOptions options;
    options.directional_light.enabled = true;
    options.directional_light.normal = {2U, 3U, 4U};
    options.directional_light.direction_to_light = {0.0F, 0.0F, 1.0F};
    options.directional_light.ambient = 0.15F;
    options.directional_light.diffuse = 0.85F;
    options.directional_light.viewer_position = {0.0F, 0.0F, 1.0F};

    Framebuffer generated_fb(41U, 41U);
    Framebuffer explicit_fb(41U, 41U);
    generated_fb.clear();
    explicit_fb.clear();

    draw_model_asset(
        generated_fb,
        one_draw_asset(generated),
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        options);
    draw_model_asset(
        explicit_fb,
        one_draw_asset(explicit_normals),
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        options);

    check(generated_fb.rgb8() == explicit_fb.rgb8(),
          "generated normals feed the existing fixed-light model path byte-identically to equivalent explicit vn data");
    check(generated_fb.fnv1a64() == explicit_fb.fnv1a64(),
          "generated-versus-explicit normal render hash is deterministic");
}

}  // namespace

int main() {
    try {
        test_single_pass_source_preserves_geometry_and_material_order();
        test_legacy_metadata_ignore_remains_source_compatible();
        test_strict_material_metadata_line_diagnostics();
        test_generated_flat_and_smooth_normals();
        test_smoothing_accumulation_crosses_uv_seams();
        test_flat_polygon_generation_does_not_create_fan_seams();
        test_explicit_normals_remain_authoritative();
        test_smoothing_directive_strictness_and_legacy_compatibility();
        test_generated_normal_degenerate_face_fails_closed();
        test_generated_normals_render_like_equivalent_explicit_normals();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " OBJ model-source test(s) failed\n";
        return 1;
    }
    std::cout << "all OBJ model-source tests passed\n";
    return 0;
}
