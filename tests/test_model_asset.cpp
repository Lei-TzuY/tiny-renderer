#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
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

std::filesystem::path fixture_path(const char* name) {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for model asset tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

std::size_t count_non_black(const Framebuffer& framebuffer) {
    const std::vector<std::uint8_t> bytes = framebuffer.rgb8();
    std::size_t count = 0U;
    for (std::size_t i = 0U; i + 2U < bytes.size(); i += 3U) {
        if (bytes[i] != 0U || bytes[i + 1U] != 0U || bytes[i + 2U] != 0U) {
            ++count;
        }
    }
    return count;
}

Mesh make_two_triangle_mesh() {
    Mesh mesh;
    mesh.vertices = {
        Vertex{{-0.9F, -0.7F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        Vertex{{-0.1F, -0.7F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        Vertex{{-0.5F, 0.7F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        Vertex{{0.1F, -0.7F, 0.0F}, {0.0F, 1.0F, 0.0F}},
        Vertex{{0.9F, -0.7F, 0.0F}, {0.0F, 1.0F, 0.0F}},
        Vertex{{0.5F, 0.7F, 0.0F}, {0.0F, 1.0F, 0.0F}},
    };
    mesh.triangles = {{0U, 1U, 2U}, {3U, 4U, 5U}};
    return mesh;
}

void test_draw_range_matches_explicit_submesh() {
    const Mesh mesh = make_two_triangle_mesh();

    Framebuffer ranged_fb(65U, 65U);
    Rasterizer ranged(ranged_fb);
    ranged.draw_mesh_range(mesh, DrawRange{1U, 1U}, Mat4::identity());

    Mesh explicit_mesh;
    explicit_mesh.vertices = mesh.vertices;
    explicit_mesh.triangles.push_back(mesh.triangles[1]);
    Framebuffer explicit_fb(65U, 65U);
    Rasterizer explicit_rasterizer(explicit_fb);
    explicit_rasterizer.draw_mesh(explicit_mesh, Mat4::identity());

    check(ranged_fb.rgb8() == explicit_fb.rgb8(),
          "draw_mesh_range is byte-identical to submitting the same explicit triangle subset");
    check(ranged_fb.fnv1a64() == explicit_fb.fnv1a64(),
          "draw_mesh_range preserves deterministic framebuffer hashing");
}

void test_draw_range_fail_closed_and_selected_only() {
    Mesh mesh = make_two_triangle_mesh();
    mesh.triangles[1] = {3U, 4U, 999U};

    Framebuffer selected_fb(65U, 65U);
    Rasterizer selected(selected_fb);
    bool selected_threw = false;
    try {
        selected.draw_mesh_range(mesh, DrawRange{0U, 1U}, Mat4::identity());
    } catch (const std::exception&) {
        selected_threw = true;
    }
    check(!selected_threw, "invalid triangle outside the selected range does not poison a valid range submission");
    check(count_non_black(selected_fb) > 0U, "valid selected range still reaches the framebuffer");

    Framebuffer invalid_index_fb(65U, 65U);
    invalid_index_fb.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before_index = invalid_index_fb.rgb8();
    Rasterizer invalid_index(invalid_index_fb);
    bool index_threw = false;
    try {
        invalid_index.draw_mesh_range(mesh, DrawRange{0U, 2U}, Mat4::identity());
    } catch (const std::out_of_range&) {
        index_threw = true;
    }
    check(index_threw, "selected invalid triangle index is rejected");
    check(invalid_index_fb.rgb8() == before_index,
          "selected invalid triangle index is rejected before any framebuffer color mutation");
    check(std::isinf(invalid_index_fb.depth_at(32U, 32U)),
          "selected invalid triangle index is rejected before any depth mutation");

    Framebuffer bounds_fb(65U, 65U);
    bounds_fb.clear({0.25F, 0.125F, 0.375F});
    const std::vector<std::uint8_t> before_bounds = bounds_fb.rgb8();
    Rasterizer bounds(bounds_fb);
    bool bounds_threw = false;
    try {
        bounds.draw_mesh_range(mesh, DrawRange{1U, 2U}, Mat4::identity());
    } catch (const std::out_of_range&) {
        bounds_threw = true;
    }
    check(bounds_threw, "draw range extending past the triangle list is rejected");
    check(bounds_fb.rgb8() == before_bounds,
          "out-of-bounds draw range fails before framebuffer mutation");
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

SamplerState fixture_sampler() {
    return SamplerState{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear};
}

void render_model_asset(const ModelAsset& asset, Framebuffer& framebuffer) {
    const DirectionalLight light = fixture_light();
    for (const MaterialDraw& draw : asset.draws) {
        TextureBinding texture_binding{};
        BaseColorSource source = BaseColorSource::ConstantWhite;
        if (draw.diffuse_texture) {
            texture_binding = TextureBinding{draw.diffuse_texture.get(), 0U, 1U, fixture_sampler()};
            source = BaseColorSource::Texture;
        }
        Rasterizer rasterizer(
            framebuffer,
            ColorBinding{99U, 99U, 99U},
            texture_binding,
            light,
            draw.material,
            source);
        rasterizer.draw_mesh_range(
            asset.mesh,
            draw.range,
            Mat4::identity(),
            Mat4::identity(),
            Mat4::identity());
    }
}

void render_material_asset_batches(
    const std::vector<MaterialAssetBatch>& batches,
    Framebuffer& framebuffer) {
    const DirectionalLight light = fixture_light();
    for (const MaterialAssetBatch& batch : batches) {
        TextureBinding texture_binding{};
        BaseColorSource source = BaseColorSource::ConstantWhite;
        if (batch.diffuse_texture) {
            texture_binding = TextureBinding{batch.diffuse_texture.get(), 0U, 1U, fixture_sampler()};
            source = BaseColorSource::Texture;
        }
        Rasterizer rasterizer(
            framebuffer,
            ColorBinding{99U, 99U, 99U},
            texture_binding,
            light,
            batch.material,
            source);
        rasterizer.draw_mesh(
            batch.mesh,
            Mat4::identity(),
            Mat4::identity(),
            Mat4::identity());
    }
}

void check_contiguous_coverage(const ModelAsset& asset, const std::string& context) {
    std::size_t cursor = 0U;
    for (const MaterialDraw& draw : asset.draws) {
        check(draw.range.first_triangle == cursor, context + " draw ranges preserve canonical triangle order");
        check(draw.range.triangle_count > 0U, context + " draw ranges are non-empty");
        cursor += draw.range.triangle_count;
    }
    check(cursor == asset.mesh.triangles.size(), context + " draw ranges cover the canonical triangle sequence exactly once");
}

void test_model_asset_order_ownership_and_render_equivalence() {
    const std::filesystem::path path = fixture_path("material_texture_sequence.obj");
    const ModelAsset asset = load_obj_model_asset_file(path);
    check(asset.mesh.triangles.size() == 3U, "mixed model asset owns one canonical three-triangle mesh");
    check(asset.draws.size() == 3U, "mapped warm -> Kd-only cool -> mapped warm stays three ordered draw ranges");
    check_contiguous_coverage(asset, "mixed model asset");

    if (asset.draws.size() == 3U) {
        check(asset.draws[0].range.first_triangle == 0U && asset.draws[0].range.triangle_count == 1U,
              "first warm draw references canonical triangle zero");
        check(asset.draws[1].range.first_triangle == 1U && asset.draws[1].range.triangle_count == 1U,
              "cool draw references canonical triangle one");
        check(asset.draws[2].range.first_triangle == 2U && asset.draws[2].range.triangle_count == 1U,
              "second warm draw references canonical triangle two");
        check(asset.draws[0].material_name == "warm" && asset.draws[1].material_name == "cool"
                  && asset.draws[2].material_name == "warm",
              "A-B-A material order is not regrouped");
        check(asset.draws[0].diffuse_texture != nullptr, "mapped warm draw owns a diffuse texture");
        check(asset.draws[1].diffuse_texture == nullptr, "Kd-only cool draw owns no synthetic texture");
        check(asset.draws[0].diffuse_texture == asset.draws[2].diffuse_texture,
              "repeated warm draws share one decoded texture ownership object");
    }

    std::shared_ptr<const Texture2D> retained_texture;
    {
        ModelAsset temporary = load_obj_model_asset_file(path);
        if (!temporary.draws.empty()) {
            retained_texture = temporary.draws.front().diffuse_texture;
        }
    }
    check(retained_texture != nullptr, "model draw texture ownership survives destruction of the source ModelAsset");
    if (retained_texture) {
        const Vec3 sample = retained_texture->sample({0.0F, 0.0F}, fixture_sampler());
        check(std::isfinite(sample.x) && std::isfinite(sample.y) && std::isfinite(sample.z),
              "retained model texture remains sampleable after ModelAsset destruction");
    }

    const std::vector<MaterialAssetBatch> legacy_batches = load_obj_material_asset_batches_file(path);
    Framebuffer model_fb(65U, 65U);
    Framebuffer batch_fb(65U, 65U);
    render_model_asset(asset, model_fb);
    render_material_asset_batches(legacy_batches, batch_fb);
    check(model_fb.rgb8() == batch_fb.rgb8(),
          "single-mesh model draw ranges render byte-identically to Milestone 14 material asset batches");
    check(model_fb.fnv1a64() == batch_fb.fnv1a64(),
          "single-mesh model draw ranges preserve the Milestone 14 deterministic framebuffer hash");
}

void test_kd_only_and_default_model_compatibility() {
    const ModelAsset kd_asset = load_obj_model_asset_file(fixture_path("material_sequence.obj"));
    const std::vector<MaterialAssetBatch> kd_batches =
        load_obj_material_asset_batches_file(fixture_path("material_sequence.obj"));
    check_contiguous_coverage(kd_asset, "Kd-only model asset");
    for (const MaterialDraw& draw : kd_asset.draws) {
        check(draw.diffuse_texture == nullptr, "Kd-only model draw owns no texture");
    }

    Framebuffer model_fb(65U, 65U);
    Framebuffer batch_fb(65U, 65U);
    render_model_asset(kd_asset, model_fb);
    render_material_asset_batches(kd_batches, batch_fb);
    check(model_fb.rgb8() == batch_fb.rgb8(),
          "Kd-only canonical model rendering is byte-identical to the Milestone 14 compatibility batches");
    check(model_fb.fnv1a64() == batch_fb.fnv1a64(),
          "Kd-only canonical model rendering preserves deterministic hash compatibility");

    const ModelAsset default_asset = load_obj_model_asset_file(fixture_path("lit_textured_quad.obj"));
    check(default_asset.draws.size() == 1U, "OBJ without mtllib becomes one default model draw range");
    if (default_asset.draws.size() == 1U) {
        check(default_asset.draws[0].range.first_triangle == 0U,
              "default model draw starts at the first canonical triangle");
        check(default_asset.draws[0].range.triangle_count == default_asset.mesh.triangles.size(),
              "default model draw covers the full canonical mesh");
        check(default_asset.draws[0].material_name.empty(), "default model draw has no synthetic material name");
        check(default_asset.draws[0].diffuse_texture == nullptr, "default model draw has no synthetic texture");
    }
}

}  // namespace

int main() {
    try {
        test_draw_range_matches_explicit_submesh();
        test_draw_range_fail_closed_and_selected_only();
        test_model_asset_order_ownership_and_render_equivalence();
        test_kd_only_and_default_model_compatibility();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " model asset test(s) failed\n";
        return 1;
    }
    std::cout << "all model asset tests passed\n";
    return 0;
}
