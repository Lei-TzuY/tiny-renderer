#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/material.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 1.0e-6F) {
    check(std::fabs(actual - expected) <= epsilon,
          message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

MaterialLibrary parse_mtl(std::string_view text) {
    std::istringstream input{std::string(text)};
    return load_mtl(input);
}

void expect_mtl_error(std::string_view text, const std::string& message) {
    bool threw = false;
    try {
        (void)parse_mtl(text);
    } catch (const MtlParseError&) {
        threw = true;
    }
    check(threw, message);
}

void test_mtl_diffuse_subset_and_rejections() {
    const MaterialLibrary library = parse_mtl(
        "# diffuse only\n"
        "newmtl warm\n"
        "Kd 1 0.5 0.25\n"
        "newmtl cool\n"
        "Kd 0.25 0.5 1\n");
    check(library.size() == 2U, "two bounded MTL materials are parsed");
    if (const auto warm = library.find("warm"); warm != library.end()) {
        check_near(warm->second.albedo.x, 1.0F, "warm Kd red maps to runtime albedo");
        check_near(warm->second.albedo.y, 0.5F, "warm Kd green maps to runtime albedo");
        check_near(warm->second.albedo.z, 0.25F, "warm Kd blue maps to runtime albedo");
    } else {
        check(false, "warm material exists");
    }

    expect_mtl_error("Kd 1 1 1\n", "Kd before newmtl is rejected");
    expect_mtl_error("newmtl a\n", "material missing required Kd is rejected");
    expect_mtl_error("newmtl a\nKd 1 1 1\nKd 1 1 1\n", "duplicate Kd is rejected");
    expect_mtl_error("newmtl a\nKd 1.01 1 1\n", "Kd above one is rejected");
    expect_mtl_error("newmtl a\nKd -0.01 1 1\n", "negative Kd is rejected");
    expect_mtl_error("newmtl a\nKd 1 1 nope\n", "non-numeric Kd is rejected");
    expect_mtl_error(
        "newmtl a\nKd 1 1 1\nnewmtl a\nKd 0 0 0\n",
        "duplicate material names are rejected");
    expect_mtl_error("newmtl a\nKd 1 1 1\nKs 1 1 1\n", "unsupported MTL directives fail closed");
}

std::filesystem::path fixture_path(const char* name) {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for material import fixture tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

void test_contiguous_batch_order_and_material_values() {
    const std::vector<MaterialBatch> batches =
        load_obj_material_batches_file(fixture_path("material_sequence.obj"));
    check(batches.size() == 3U, "A-B-A material sequence remains three contiguous draw batches");
    if (batches.size() != 3U) {
        return;
    }
    check(batches[0].material_name == "warm", "first batch keeps warm material");
    check(batches[1].material_name == "cool", "second batch keeps cool material");
    check(batches[2].material_name == "warm", "third batch preserves repeated warm material rather than regrouping");
    for (const MaterialBatch& batch : batches) {
        check(batch.mesh.triangles.size() == 1U, "each contiguous material run contains its submitted face");
        check(batch.mesh.vertices.size() == 4U, "material batches retain canonical unified geometry vertices");
    }
    check_near(batches[0].material.albedo.x, 1.0F, "warm batch albedo red");
    check_near(batches[0].material.albedo.y, 0.5F, "warm batch albedo green");
    check_near(batches[0].material.albedo.z, 0.25F, "warm batch albedo blue");
    check_near(batches[1].material.albedo.x, 0.25F, "cool batch albedo red");
    check_near(batches[1].material.albedo.y, 0.5F, "cool batch albedo green");
    check_near(batches[1].material.albedo.z, 1.0F, "cool batch albedo blue");
}

void test_material_batches_render_like_programmatic_submission() {
    const std::vector<MaterialBatch> imported =
        load_obj_material_batches_file(fixture_path("material_sequence.obj"));
    const Mesh geometry = load_obj_file(fixture_path("material_sequence.obj"));
    const Texture2D texture = load_ppm_file(fixture_path("checker.ppm"));
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

    Framebuffer imported_fb(65U, 65U);
    for (const MaterialBatch& batch : imported) {
        Rasterizer rasterizer(imported_fb, {}, texture_binding, light, batch.material);
        rasterizer.draw_mesh(batch.mesh, Mat4::identity(), Mat4::identity(), Mat4::identity());
    }

    const MaterialState warm{{1.0F, 0.5F, 0.25F}};
    const MaterialState cool{{0.25F, 0.5F, 1.0F}};
    const std::vector<MaterialState> expected_materials{warm, cool, warm};
    Framebuffer manual_fb(65U, 65U);
    check(geometry.triangles.size() == expected_materials.size(), "manual material fixture has one triangle per expected batch");
    for (std::size_t i = 0U; i < geometry.triangles.size() && i < expected_materials.size(); ++i) {
        Mesh one_face;
        one_face.vertices = geometry.vertices;
        one_face.triangles.push_back(geometry.triangles[i]);
        Rasterizer rasterizer(manual_fb, {}, texture_binding, light, expected_materials[i]);
        rasterizer.draw_mesh(one_face, Mat4::identity(), Mat4::identity(), Mat4::identity());
    }

    check(imported_fb.rgb8() == manual_fb.rgb8(),
          "file-driven MTL/OBJ material batches render byte-identically to programmatic material submissions");
    check(imported_fb.fnv1a64() == manual_fb.fnv1a64(),
          "file-driven material batches preserve deterministic programmatic framebuffer hash");
}

void test_legacy_obj_without_material_library_becomes_default_batch() {
    const std::filesystem::path path = fixture_path("lit_textured_quad.obj");
    const Mesh legacy = load_obj_file(path);
    const std::vector<MaterialBatch> batches = load_obj_material_batches_file(path);
    check(batches.size() == 1U, "legacy OBJ without mtllib becomes one default material batch");
    if (batches.size() != 1U) {
        return;
    }
    check(batches[0].material_name.empty(), "legacy default batch has no synthetic material name");
    check_near(batches[0].material.albedo.x, 1.0F, "legacy default material red is white");
    check_near(batches[0].material.albedo.y, 1.0F, "legacy default material green is white");
    check_near(batches[0].material.albedo.z, 1.0F, "legacy default material blue is white");
    check(batches[0].mesh.vertices.size() == legacy.vertices.size(), "legacy default batch preserves unified vertices");
    check(batches[0].mesh.triangles == legacy.triangles, "legacy default batch preserves triangle order exactly");
}

void expect_obj_material_error(const char* fixture, const std::string& message) {
    bool threw = false;
    try {
        (void)load_obj_material_batches_file(fixture_path(fixture));
    } catch (const ObjParseError&) {
        threw = true;
    }
    check(threw, message);
}

void test_obj_material_metadata_fail_closed() {
    expect_obj_material_error("unknown_material.obj", "unknown usemtl reference is rejected");
    expect_obj_material_error("usemtl_without_library.obj", "usemtl without mtllib is rejected");
    expect_obj_material_error("parent_mtllib.obj", "mtllib parent-path escape is rejected by bounded file loader");
}

}  // namespace

int main() {
    try {
        test_mtl_diffuse_subset_and_rejections();
        test_contiguous_batch_order_and_material_values();
        test_material_batches_render_like_programmatic_submission();
        test_legacy_obj_without_material_library_becomes_default_batch();
        test_obj_material_metadata_fail_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " material import test(s) failed\n";
        return 1;
    }
    std::cout << "all material import tests passed\n";
    return 0;
}
