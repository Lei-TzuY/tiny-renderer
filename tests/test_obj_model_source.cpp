#include <cstddef>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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

void test_single_pass_source_preserves_geometry_and_material_order() {
    const std::filesystem::path path = fixture_path("material_texture_sequence.obj");
    const Mesh legacy = load_obj_file(path);
    const ObjModelSource source = load_obj_model_source_file(path);

    check(same_mesh(legacy, source.mesh),
          "strict model-source parsing preserves the exact canonical geometry produced by legacy load_obj");
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
        "mtllib diffuse.mtl unexpected-extra\n"
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

}  // namespace

int main() {
    try {
        test_single_pass_source_preserves_geometry_and_material_order();
        test_legacy_metadata_ignore_remains_source_compatible();
        test_strict_material_metadata_line_diagnostics();
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
