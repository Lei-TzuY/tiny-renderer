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

void check_color(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check_near(actual.x, expected.x, message + " red", 2.0e-4F);
    check_near(actual.y, expected.y, message + " green", 2.0e-4F);
    check_near(actual.z, expected.z, message + " blue", 2.0e-4F);
}

MaterialLibrary parse_mtl(std::string_view text) {
    std::istringstream input{std::string(text)};
    return load_mtl(input);
}

MaterialAssetLibrary parse_mtl_assets(std::string_view text) {
    std::istringstream input{std::string(text)};
    return load_mtl_assets(input);
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

void expect_mtl_asset_error(std::string_view text, const std::string& message) {
    bool threw = false;
    try {
        (void)parse_mtl_assets(text);
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
    expect_mtl_error("newmtl a\nKd 1 1 1\nillum 2\n", "unsupported MTL directives fail closed");
}

void test_rich_mtl_diffuse_map_subset_and_legacy_strictness() {
    const std::string mapped =
        "newmtl mapped\n"
        "Kd 0.75 0.5 0.25\n"
        "map_Kd checker.ppm\n";
    const MaterialAssetLibrary library = parse_mtl_assets(mapped);
    check(library.size() == 1U, "rich MTL loader accepts one diffuse-map material");
    if (const auto entry = library.find("mapped"); entry != library.end()) {
        check(entry->second.diffuse_map_filename.has_value(), "rich material retains optional map_Kd filename");
        if (entry->second.diffuse_map_filename) {
            check(*entry->second.diffuse_map_filename == "checker.ppm", "map_Kd filename is preserved exactly");
        }
        check_near(entry->second.material.albedo.x, 0.75F, "rich material preserves Kd red");
    } else {
        check(false, "mapped rich material exists");
    }

    expect_mtl_error(mapped, "legacy strict MTL loader still rejects map_Kd rather than silently discarding it");
    expect_mtl_asset_error(
        "map_Kd checker.ppm\nnewmtl a\nKd 1 1 1\n",
        "map_Kd before newmtl is rejected");
    expect_mtl_asset_error(
        "newmtl a\nKd 1 1 1\nmap_Kd checker.ppm\nmap_Kd checker.ppm\n",
        "duplicate map_Kd is rejected");
    expect_mtl_asset_error(
        "newmtl a\nKd 1 1 1\nmap_Kd ../checker.ppm\n",
        "map_Kd parent path is rejected");
    expect_mtl_asset_error(
        "newmtl a\nKd 1 1 1\nmap_Kd textures/checker.ppm\n",
        "map_Kd nested path is rejected");
    expect_mtl_asset_error(
        "newmtl a\nKd 1 1 1\nmap_Kd -clamp on checker.ppm\n",
        "map_Kd options are rejected by the bounded subset");
}

std::filesystem::path fixture_path(const char* name) {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for material import fixture tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

DirectionalLight fixture_light() {
    return DirectionalLight{
        true,
        NormalBinding{2U, 3U, 4U},
        {0.0F, 0.0F, 1.0F},
        0.2F,
        0.8F,
    };
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
    const DirectionalLight light = fixture_light();

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

void test_kd_only_material_batches_render_without_texture() {
    const std::vector<MaterialBatch> imported =
        load_obj_material_batches_file(fixture_path("material_sequence.obj"));
    const Mesh geometry = load_obj_file(fixture_path("material_sequence.obj"));
    const DirectionalLight light = fixture_light();

    Framebuffer imported_fb(65U, 65U);
    for (const MaterialBatch& batch : imported) {
        Rasterizer rasterizer(
            imported_fb,
            ColorBinding{99U, 99U, 99U},
            {},
            light,
            batch.material,
            BaseColorSource::ConstantWhite);
        rasterizer.draw_mesh(batch.mesh, Mat4::identity(), Mat4::identity(), Mat4::identity());
    }

    const MaterialState warm{{1.0F, 0.5F, 0.25F}};
    const MaterialState cool{{0.25F, 0.5F, 1.0F}};
    const std::vector<MaterialState> expected_materials{warm, cool, warm};
    Framebuffer manual_fb(65U, 65U);
    for (std::size_t i = 0U; i < geometry.triangles.size() && i < expected_materials.size(); ++i) {
        Mesh one_face;
        one_face.vertices = geometry.vertices;
        one_face.triangles.push_back(geometry.triangles[i]);
        Rasterizer rasterizer(
            manual_fb,
            ColorBinding{99U, 99U, 99U},
            {},
            light,
            expected_materials[i],
            BaseColorSource::ConstantWhite);
        rasterizer.draw_mesh(one_face, Mat4::identity(), Mat4::identity(), Mat4::identity());
    }

    check(imported_fb.rgb8() == manual_fb.rgb8(),
          "Kd-only OBJ/MTL batches render byte-identically without requiring an unrelated texture");
    check(imported_fb.fnv1a64() == manual_fb.fnv1a64(),
          "Kd-only constant-white material rendering preserves deterministic submission semantics");
    check_color(imported_fb.color_at(48U, 48U), {1.0F, 0.5F, 0.25F},
                "warm Kd is the visible base color on the warm face without a texture");
    check_color(imported_fb.color_at(16U, 16U), {0.25F, 0.5F, 1.0F},
                "cool Kd is the visible base color on the cool face without a texture");
}

void render_asset_batches(Framebuffer& framebuffer, const std::vector<MaterialAssetBatch>& batches) {
    const DirectionalLight light = fixture_light();
    const SamplerState sampler{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear};
    for (const MaterialAssetBatch& batch : batches) {
        const bool textured = static_cast<bool>(batch.diffuse_texture);
        const TextureBinding texture_binding{
            textured ? batch.diffuse_texture.get() : nullptr,
            0U,
            1U,
            sampler,
        };
        Rasterizer rasterizer(
            framebuffer,
            ColorBinding{99U, 99U, 99U},
            texture_binding,
            light,
            batch.material,
            textured ? BaseColorSource::Texture : BaseColorSource::ConstantWhite);
        rasterizer.draw_mesh(batch.mesh, Mat4::identity(), Mat4::identity(), Mat4::identity());
    }
}

void test_owned_diffuse_texture_batches_and_render_equivalence() {
    std::vector<MaterialAssetBatch> imported =
        load_obj_material_asset_batches_file(fixture_path("material_texture_sequence.obj"));
    check(imported.size() == 3U, "mixed map_Kd A-B-A asset remains three ordered contiguous batches");
    if (imported.size() != 3U) {
        return;
    }
    check(imported[0].material_name == "warm", "mapped warm batch remains first");
    check(imported[1].material_name == "cool", "Kd-only cool batch remains second");
    check(imported[2].material_name == "warm", "repeated mapped warm batch remains third");
    check(static_cast<bool>(imported[0].diffuse_texture), "mapped warm batch owns a decoded texture");
    check(!imported[1].diffuse_texture, "Kd-only cool batch has no synthetic texture");
    check(static_cast<bool>(imported[2].diffuse_texture), "repeated mapped warm batch owns a texture reference");
    if (imported[0].diffuse_texture && imported[2].diffuse_texture) {
        check(imported[0].diffuse_texture.get() == imported[2].diffuse_texture.get(),
              "repeated map_Kd batches share one decoded texture ownership object");
    }

    std::vector<MaterialAssetBatch> surviving_copy = imported;
    imported.clear();
    check(static_cast<bool>(surviving_copy[0].diffuse_texture),
          "copied material batch retains owned texture after original batch vector is destroyed");
    if (surviving_copy[0].diffuse_texture) {
        const Vec3 sample = surviving_copy[0].diffuse_texture->sample({0.25F, 0.25F});
        check(std::isfinite(sample.x) && std::isfinite(sample.y) && std::isfinite(sample.z),
              "owned diffuse texture remains sampleable across batch copies");
    }

    Framebuffer imported_fb(65U, 65U);
    render_asset_batches(imported_fb, surviving_copy);

    const Mesh geometry = load_obj_file(fixture_path("material_texture_sequence.obj"));
    const Texture2D texture = load_ppm_file(fixture_path("checker.ppm"));
    const MaterialState warm{{1.0F, 0.5F, 0.25F}};
    const MaterialState cool{{0.25F, 0.5F, 1.0F}};
    const std::vector<MaterialState> materials{warm, cool, warm};
    const std::vector<bool> textured{true, false, true};
    const DirectionalLight light = fixture_light();
    const SamplerState sampler{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear};

    Framebuffer manual_fb(65U, 65U);
    for (std::size_t i = 0U; i < geometry.triangles.size() && i < materials.size(); ++i) {
        Mesh one_face;
        one_face.vertices = geometry.vertices;
        one_face.triangles.push_back(geometry.triangles[i]);
        const TextureBinding texture_binding{
            textured[i] ? &texture : nullptr,
            0U,
            1U,
            sampler,
        };
        Rasterizer rasterizer(
            manual_fb,
            ColorBinding{99U, 99U, 99U},
            texture_binding,
            light,
            materials[i],
            textured[i] ? BaseColorSource::Texture : BaseColorSource::ConstantWhite);
        rasterizer.draw_mesh(one_face, Mat4::identity(), Mat4::identity(), Mat4::identity());
    }

    check(imported_fb.rgb8() == manual_fb.rgb8(),
          "owned OBJ/MTL map_Kd batches render byte-identically to programmatic texture/material submissions");
    check(imported_fb.fnv1a64() == manual_fb.fnv1a64(),
          "owned map_Kd material rendering preserves deterministic framebuffer hash");
}

void test_rich_kd_only_batches_preserve_m13_output() {
    const std::vector<MaterialBatch> legacy =
        load_obj_material_batches_file(fixture_path("material_sequence.obj"));
    const std::vector<MaterialAssetBatch> rich =
        load_obj_material_asset_batches_file(fixture_path("material_sequence.obj"));
    check(rich.size() == legacy.size(), "rich Kd-only loader preserves legacy batch count");
    for (const MaterialAssetBatch& batch : rich) {
        check(!batch.diffuse_texture, "Kd-only rich batch does not invent a diffuse texture");
    }

    Framebuffer legacy_fb(65U, 65U);
    const DirectionalLight light = fixture_light();
    for (const MaterialBatch& batch : legacy) {
        Rasterizer rasterizer(
            legacy_fb,
            ColorBinding{99U, 99U, 99U},
            {},
            light,
            batch.material,
            BaseColorSource::ConstantWhite);
        rasterizer.draw_mesh(batch.mesh, Mat4::identity(), Mat4::identity(), Mat4::identity());
    }

    Framebuffer rich_fb(65U, 65U);
    render_asset_batches(rich_fb, rich);
    check(rich_fb.rgb8() == legacy_fb.rgb8(), "rich Kd-only asset path is byte-stable with Milestone 13 output");
    check(rich_fb.fnv1a64() == legacy_fb.fnv1a64(), "rich Kd-only asset path preserves the Milestone 13 hash");
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

void test_missing_diffuse_texture_fails_closed() {
    bool threw = false;
    try {
        (void)load_obj_material_asset_batches_file(fixture_path("missing_texture.obj"));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "missing map_Kd texture file is rejected before any draw submission exists");
}

}  // namespace

int main() {
    try {
        test_mtl_diffuse_subset_and_rejections();
        test_rich_mtl_diffuse_map_subset_and_legacy_strictness();
        test_contiguous_batch_order_and_material_values();
        test_material_batches_render_like_programmatic_submission();
        test_kd_only_material_batches_render_without_texture();
        test_owned_diffuse_texture_batches_and_render_equivalence();
        test_rich_kd_only_batches_preserve_m13_output();
        test_legacy_obj_without_material_library_becomes_default_batch();
        test_obj_material_metadata_fail_closed();
        test_missing_diffuse_texture_fails_closed();
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
