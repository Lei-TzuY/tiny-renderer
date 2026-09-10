#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/offline_render.hpp"
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

void check_unchanged(
    const Framebuffer& framebuffer,
    const std::vector<std::uint8_t>& before,
    const std::string& context) {
    check(framebuffer.rgb8() == before, context + " preserves framebuffer color");
    check(std::isinf(framebuffer.depth_at(32U, 32U)), context + " preserves framebuffer depth");
}

Vertex uv_vertex(const Vec3& position, float u, float v) {
    return Vertex::with_varyings(position, VaryingPack{u, v});
}

Mesh make_two_triangle_uv_mesh() {
    Mesh mesh;
    mesh.vertices = {
        uv_vertex({-0.9F, -0.7F, 0.0F}, 0.0F, 0.0F),
        uv_vertex({-0.1F, -0.7F, 0.0F}, 1.0F, 0.0F),
        uv_vertex({-0.5F, 0.7F, 0.0F}, 0.5F, 1.0F),
        uv_vertex({0.1F, -0.7F, 0.0F}, 0.0F, 0.0F),
        uv_vertex({0.9F, -0.7F, 0.0F}, 1.0F, 0.0F),
        uv_vertex({0.5F, 0.7F, 0.0F}, 0.5F, 1.0F),
    };
    mesh.triangles = {{0U, 1U, 2U}, {3U, 4U, 5U}};
    return mesh;
}

ModelAsset solid_triangle_asset(const Vec3& color, float opacity = 1.0F) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.8F, -0.8F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({0.8F, -0.8F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({0.0F, 0.8F, 0.0F}, VaryingPack{}),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material.albedo = color;
    draw.material.opacity = opacity;
    asset.draws.push_back(draw);
    return asset;
}

void test_later_model_uv_binding_fails_before_earlier_draw_write() {
    ModelAsset asset;
    asset.mesh = make_two_triangle_uv_mesh();

    MaterialDraw first;
    first.range = {0U, 1U};
    first.material.albedo = {0.8F, 0.2F, 0.2F};

    MaterialDraw second;
    second.range = {1U, 1U};
    second.material.albedo = {1.0F, 1.0F, 1.0F};
    second.diffuse_texture = std::make_shared<const Texture2D>(
        1U,
        1U,
        std::vector<Vec3>{{0.2F, 0.8F, 0.2F}});

    asset.draws = {first, second};

    ModelRenderOptions options;
    options.u_channel = 2U;
    options.v_channel = 1U;

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_model_asset(framebuffer, asset, Mat4::identity(), options);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(threw, "later mapped model draw with invalid UV binding is rejected");
    check_unchanged(framebuffer, before, "later mapped model UV-binding rejection");
}

void test_direct_range_uses_same_uv_binding_preflight() {
    const Mesh mesh = make_two_triangle_uv_mesh();
    const Texture2D texture(1U, 1U, {{0.2F, 0.8F, 0.2F}});
    const TextureBinding binding{&texture, 2U, 1U, {}};

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.25F, 0.125F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    Rasterizer rasterizer(
        framebuffer,
        {},
        binding,
        {},
        {},
        BaseColorSource::Texture);
    bool threw = false;
    try {
        rasterizer.draw_mesh_range(mesh, DrawRange{0U, 1U}, Mat4::identity());
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(threw, "direct range rejects the same invalid UV binding as model preflight");
    check_unchanged(framebuffer, before, "direct range UV-binding rejection");
}

void test_range_preserves_all_vertex_uv_validation_contract() {
    Mesh mesh = make_two_triangle_uv_mesh();
    mesh.vertices[5].varyings.values[0] = std::numeric_limits<float>::quiet_NaN();

    const Texture2D texture(1U, 1U, {{1.0F, 1.0F, 1.0F}});
    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.375F, 0.25F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    Rasterizer ranged(
        framebuffer,
        {},
        TextureBinding{&texture, 0U, 1U, {}},
        {},
        {},
        BaseColorSource::Texture);

    bool threw = false;
    try {
        ranged.draw_mesh_range(mesh, DrawRange{0U, 1U}, Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw,
          "selected range still rejects invalid UV state on vertices outside the selected triangle set");
    check_unchanged(framebuffer, before, "all-vertex UV validation contract");
}

void test_prepared_list_preflight_rejects_later_a2c_without_writes() {
    const ModelAsset first_asset = solid_triangle_asset({0.8F, 0.2F, 0.1F});
    const ModelAsset second_asset = solid_triangle_asset({0.1F, 0.2F, 0.8F}, 0.5F);

    ModelRenderOptions second_options;
    second_options.alpha_to_coverage_state.enabled = true;
    const PreparedModelSubmission first = prepare_model_asset(first_asset);
    const PreparedModelSubmission second = prepare_model_asset(second_asset, second_options);
    const std::array<PreparedModelListEntry, 2> entries{
        PreparedModelListEntry{&first, Mat4::translation({-0.25F, 0.0F, 0.0F})},
        PreparedModelListEntry{&second, Mat4::translation({0.25F, 0.0F, 0.0F})},
    };

    Framebuffer framebuffer(65U, 65U, SampleCount::One);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    bool threw = false;
    try {
        preflight_prepared_model_list(framebuffer, entries);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "prepared-list preflight rejects a later alpha-to-coverage entry on a 1x target");
    check_unchanged(framebuffer, before, "prepared-list preflight rejection");
}

void test_mixed_scene_matches_explicit_depth_then_sorted_transparency() {
    ModelAsset opaque = solid_triangle_asset({0.1F, 0.9F, 0.2F});
    ModelAsset near_transparent = solid_triangle_asset({1.0F, 0.0F, 0.0F}, 0.5F);
    ModelAsset far_transparent = solid_triangle_asset({0.0F, 0.0F, 1.0F}, 0.5F);

    const OfflineSceneEntry opaque_entry{
        &opaque,
        Mat4::translation({-1.15F, 0.0F, 0.0F}),
        {},
        OfflineSceneTransparencyMode::Opaque,
    };
    const OfflineSceneEntry near_entry{
        &near_transparent,
        Mat4::translation({0.65F, 0.0F, 0.30F}),
        {},
        OfflineSceneTransparencyMode::SourceAlpha,
    };
    const OfflineSceneEntry far_entry{
        &far_transparent,
        Mat4::translation({0.65F, 0.0F, -0.30F}),
        {},
        OfflineSceneTransparencyMode::SourceAlpha,
    };

    const std::array<OfflineSceneEntry, 3> mixed_input{
        near_entry, opaque_entry, far_entry};
    const std::array<OfflineSceneEntry, 3> explicit_expected{
        opaque_entry, far_entry, near_entry};
    const std::array<OfflineSceneEntry, 3> explicit_unsorted{
        opaque_entry, near_entry, far_entry};

    OfflineRenderSettings settings;
    settings.width = 97U;
    settings.height = 65U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.0F, 0.0F, 0.0F};

    const Framebuffer mixed = render_scene_preview(
        mixed_input, settings, OfflineSceneOrdering::MixedTransparency);
    const Framebuffer expected = render_scene_preview(
        explicit_expected, settings, OfflineSceneOrdering::InputOrder);
    const Framebuffer unsorted = render_scene_preview(
        explicit_unsorted, settings, OfflineSceneOrdering::InputOrder);

    check(
        mixed.rgb8() == expected.rgb8() && mixed.fnv1a64() == expected.fnv1a64(),
        "mixed scene executes depth-writing entries first and source-alpha entries in stable back-to-front order");
    check(
        mixed.rgb8() != unsorted.rgb8(),
        "mixed scene source-alpha sorting is observably different from near-first blending");
}

void test_mixed_scene_requires_explicit_transparency_declarations() {
    const ModelAsset asset = solid_triangle_asset({0.6F, 0.4F, 0.2F});
    const std::array<OfflineSceneEntry, 1> undeclared{
        OfflineSceneEntry{&asset, Mat4::identity(), {}, std::nullopt},
    };
    bool threw = false;
    try {
        (void)render_scene_preview(
            undeclared,
            OfflineRenderSettings{},
            OfflineSceneOrdering::MixedTransparency);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "mixed scene rejects entries whose transparency class was not explicitly declared");
}

}  // namespace

int main() {
    try {
        test_later_model_uv_binding_fails_before_earlier_draw_write();
        test_direct_range_uses_same_uv_binding_preflight();
        test_range_preserves_all_vertex_uv_validation_contract();
        test_prepared_list_preflight_rejects_later_a2c_without_writes();
        test_mixed_scene_matches_explicit_depth_then_sorted_transparency();
        test_mixed_scene_requires_explicit_transparency_declarations();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " shared preflight test(s) failed\n";
        return 1;
    }
    std::cout << "all shared preflight tests passed\n";
    return 0;
}
