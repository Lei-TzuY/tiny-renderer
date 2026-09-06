#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

void check_near(float actual, float expected, const std::string& message) {
    check(std::fabs(actual - expected) <= 1.0e-6F, message);
}

std::filesystem::path fixture_path(const char* name) {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for multiple material library tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

ObjModelSource parse_strict(const std::string& obj) {
    std::istringstream input(obj);
    return load_obj_model_source(input);
}

void expect_obj_error_line(
    const std::string& obj,
    std::size_t expected_line,
    const std::string& context) {
    bool threw = false;
    std::size_t line = 0U;
    try {
        (void)parse_strict(obj);
    } catch (const ObjParseError& error) {
        threw = true;
        line = error.line();
    }
    check(threw, context + " is rejected");
    check(line == expected_line, context + " reports the deterministic OBJ source line");
}

std::string one_triangle_geometry() {
    return
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n";
}

void test_parser_preserves_ordered_multiple_libraries() {
    const ObjModelSource source = parse_strict(
        "mtllib first.mtl second.mtl\n"
        + one_triangle_geometry()
        + "usemtl warm\n"
        + "f 1/1 2/2 3/3\n");

    check(source.material_libraries.size() == 2U,
          "one mtllib record can capture two ordered sibling libraries");
    if (source.material_libraries.size() == 2U) {
        check(source.material_libraries[0] == ObjMaterialLibraryRef{"first.mtl", 1U},
              "first mtllib filename and source line are preserved");
        check(source.material_libraries[1] == ObjMaterialLibraryRef{"second.mtl", 1U},
              "second mtllib filename and source line are preserved");
    }
    check(!source.material_library_filename.has_value(),
          "legacy single-library projection is empty when multiple libraries are declared");
    check(source.face_materials.size() == 1U && source.face_materials[0] == "warm",
          "material association remains coupled to canonical face emission");

    const ObjModelSource repeated = parse_strict(
        "mtllib first.mtl\n"
        "mtllib second.mtl third.mtl\n"
        + one_triangle_geometry()
        + "usemtl warm\n"
        + "f 1/1 2/2 3/3\n");
    check(repeated.material_libraries.size() == 3U,
          "multiple mtllib directives append to one bounded ordered library list");
    if (repeated.material_libraries.size() == 3U) {
        check(repeated.material_libraries[0].line == 1U
                  && repeated.material_libraries[1].line == 2U
                  && repeated.material_libraries[2].line == 2U,
              "each library reference retains its declaration line");
    }
}

void test_parser_bounds_paths_and_legacy_ignore() {
    expect_obj_error_line("mtllib\n", 1U, "empty mtllib record");
    expect_obj_error_line("mtllib ../escape.mtl good.mtl\n", 1U, "parent-path material library");
    expect_obj_error_line("mtllib same.mtl same.mtl\n", 1U, "duplicate material library filename");
    expect_obj_error_line(
        "mtllib a.mtl b.mtl c.mtl d.mtl e.mtl f.mtl g.mtl h.mtl i.mtl\n",
        1U,
        "ninth material library reference");
    expect_obj_error_line(
        one_triangle_geometry()
            + "f 1/1 2/2 3/3\n"
            + "mtllib late.mtl\n",
        8U,
        "material library declared after a face");

    const std::string malformed_metadata =
        "mtllib\n"
        "usemtl\n"
        + one_triangle_geometry()
        + "f 1/1 2/2 3/3\n";
    std::istringstream legacy_input(malformed_metadata);
    bool legacy_threw = false;
    Mesh legacy;
    try {
        legacy = load_obj(legacy_input);
    } catch (const std::exception&) {
        legacy_threw = true;
    }
    check(!legacy_threw, "legacy geometry-only load_obj continues to ignore malformed material metadata");
    check(legacy.triangles.size() == 1U, "legacy metadata-ignore path still returns canonical geometry");
}

void test_kd_libraries_merge_in_declaration_order() {
    const ObjModelSource source = load_obj_model_source_file(fixture_path("multi_material_kd.obj"));
    check(source.material_libraries.size() == 2U,
          "file-driven model source records both Kd material libraries");
    if (source.material_libraries.size() == 2U) {
        check(source.material_libraries[0].filename == "diffuse.mtl"
                  && source.material_libraries[1].filename == "extra_kd.mtl",
              "file-driven material-library declaration order is preserved");
    }

    const std::vector<MaterialBatch> batches =
        load_obj_material_batches_file(fixture_path("multi_material_kd.obj"));
    check(batches.size() == 3U, "warm-extra-warm remains three contiguous compatibility batches");
    if (batches.size() == 3U) {
        check(batches[0].material_name == "warm"
                  && batches[1].material_name == "extra"
                  && batches[2].material_name == "warm",
              "cross-library usemtl order is never regrouped by material name");
        check_near(batches[0].material.albedo.x, 1.0F, "first-library warm material resolves correctly");
        check_near(batches[1].material.albedo.x, 0.1F, "second-library extra red resolves correctly");
        check_near(batches[1].material.albedo.y, 0.8F, "second-library extra green resolves correctly");
        check_near(batches[1].material.albedo.z, 0.2F, "second-library extra blue resolves correctly");
    }
}

void test_rich_assets_share_texture_cache_across_libraries() {
    const std::filesystem::path path = fixture_path("multi_material_asset.obj");
    const ModelAsset asset = load_obj_model_asset_file(path);
    check(asset.draws.size() == 3U, "multi-library rich asset preserves warm-extra-warm draw ranges");
    if (asset.draws.size() == 3U) {
        check(asset.draws[0].material_name == "warm"
                  && asset.draws[1].material_name == "extra"
                  && asset.draws[2].material_name == "warm",
              "rich material draw order crosses MTL library boundaries deterministically");
        check(asset.draws[0].diffuse_texture != nullptr && asset.draws[1].diffuse_texture != nullptr,
              "both libraries can own mapped diffuse materials");
        check(asset.draws[0].diffuse_texture == asset.draws[1].diffuse_texture
                  && asset.draws[1].diffuse_texture == asset.draws[2].diffuse_texture,
              "different MTL libraries referencing the same normalized PPM path share one texture object");
    }

    const std::vector<MaterialAssetBatch> batches =
        load_obj_material_asset_batches_file(path);
    check(batches.size() == 3U, "compatibility asset batches derive from the same multi-library ModelAsset");
    if (batches.size() == 3U) {
        check(batches[0].diffuse_texture == batches[1].diffuse_texture
                  && batches[1].diffuse_texture == batches[2].diffuse_texture,
              "compatibility batches retain cross-library texture deduplication");
    }
}

void expect_file_loader_obj_error(
    const std::filesystem::path& path,
    bool rich,
    std::size_t expected_line,
    const std::string& context) {
    bool threw = false;
    std::size_t line = 0U;
    try {
        if (rich) {
            (void)load_obj_model_asset_file(path);
        } else {
            (void)load_obj_material_batches_file(path);
        }
    } catch (const ObjParseError& error) {
        threw = true;
        line = error.line();
    }
    check(threw, context + " is rejected");
    check(line == expected_line, context + " reports deterministic OBJ metadata line");
}

void test_cross_library_conflicts_and_unknown_materials_fail_closed() {
    const std::filesystem::path duplicate = fixture_path("multi_material_duplicate.obj");
    expect_file_loader_obj_error(
        duplicate,
        false,
        1U,
        "simple loader cross-library duplicate material definition");
    expect_file_loader_obj_error(
        duplicate,
        true,
        1U,
        "rich loader cross-library duplicate material definition");

    const std::filesystem::path unknown = fixture_path("multi_material_unknown.obj");
    expect_file_loader_obj_error(
        unknown,
        false,
        9U,
        "simple loader unknown multi-library usemtl");
    expect_file_loader_obj_error(
        unknown,
        true,
        9U,
        "rich loader unknown multi-library usemtl");
}

}  // namespace

int main() {
    test_parser_preserves_ordered_multiple_libraries();
    test_parser_bounds_paths_and_legacy_ignore();
    test_kd_libraries_merge_in_declaration_order();
    test_rich_assets_share_texture_cache_across_libraries();
    test_cross_library_conflicts_and_unknown_materials_fail_closed();

    if (failures == 0) {
        std::cout << "multiple material library tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
