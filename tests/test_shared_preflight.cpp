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
#include "tiny_renderer/prepared_spatial.hpp"
#include "tiny_renderer/rasterizer.hpp"
#include "tiny_renderer/texture.hpp"
#include "tiny_renderer/vertex_program.hpp"

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
    check(nearly_equal(actual, expected), message);
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

ModelAsset two_draw_spatial_asset(float first_z = 0.5F, float second_z = -0.5F) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-2.0F, -1.0F, first_z}, VaryingPack{}),
        Vertex::with_varyings({0.0F, -1.0F, first_z}, VaryingPack{}),
        Vertex::with_varyings({-1.0F, 3.0F, first_z}, VaryingPack{}),
        Vertex::with_varyings({1.0F, -2.0F, second_z}, VaryingPack{}),
        Vertex::with_varyings({3.0F, -2.0F, second_z}, VaryingPack{}),
        Vertex::with_varyings({2.0F, 2.0F, second_z}, VaryingPack{}),
    };
    asset.mesh.triangles = {{0U, 1U, 2U}, {3U, 4U, 5U}};

    MaterialDraw first;
    first.range = {0U, 1U};
    first.material.albedo = {1.0F, 0.0F, 0.0F};

    MaterialDraw second;
    second.range = {1U, 1U};
    second.material.albedo = {0.0F, 0.0F, 1.0F};
    asset.draws = {first, second};
    return asset;
}

class IdentitySpatialVertexProgram final : public VertexProgram {
public:
    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        return {input.position, input.varyings};
    }
};

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

void test_prepared_draw_spatial_metadata_is_owned_and_exact() {
    ModelAsset source = two_draw_spatial_asset();
    const PreparedSpatialSubmission spatial = prepare_spatial_model(source);
    source.mesh.vertices[0].position = {99.0F, 99.0F, 99.0F};

    const auto draws = spatial.draws();
    check(draws.size() == 2U, "prepared spatial submission has one metadata record per material draw");
    if (draws.size() != 2U) {
        return;
    }

    check(draws[0].range.first_triangle == 0U && draws[0].range.triangle_count == 1U,
          "first prepared spatial record retains its canonical draw range");
    check_near(draws[0].min.x, -2.0F, "first prepared draw min x");
    check_near(draws[0].min.y, -1.0F, "first prepared draw min y");
    check_near(draws[0].min.z, 0.5F, "first prepared draw min z");
    check_near(draws[0].max.x, 0.0F, "first prepared draw max x");
    check_near(draws[0].max.y, 3.0F, "first prepared draw max y");
    check_near(draws[0].max.z, 0.5F, "first prepared draw max z");
    check_near(draws[0].center.x, -1.0F, "first prepared draw center x");
    check_near(draws[0].center.y, 1.0F, "first prepared draw center y");
    check_near(draws[0].center.z, 0.5F, "first prepared draw center z");

    check(draws[1].range.first_triangle == 1U && draws[1].range.triangle_count == 1U,
          "second prepared spatial record retains its canonical draw range");
    check_near(draws[1].min.x, 1.0F, "second prepared draw min x");
    check_near(draws[1].min.y, -2.0F, "second prepared draw min y");
    check_near(draws[1].min.z, -0.5F, "second prepared draw min z");
    check_near(draws[1].max.x, 3.0F, "second prepared draw max x");
    check_near(draws[1].max.y, 2.0F, "second prepared draw max y");
    check_near(draws[1].max.z, -0.5F, "second prepared draw max z");
    check_near(draws[1].center.x, 2.0F, "second prepared draw center x");
    check_near(draws[1].center.y, 0.0F, "second prepared draw center y");
    check_near(draws[1].center.z, -0.5F, "second prepared draw center z");

    check_near(
        spatial.prepared().asset().mesh.vertices[0].position.x,
        -2.0F,
        "prepared spatial metadata remains coupled to its owned model snapshot");
}

void test_prepared_draw_spatial_plan_flattens_and_sorts_draws() {
    const PreparedSpatialSubmission spatial = prepare_spatial_model(two_draw_spatial_asset());
    const std::array<PreparedSpatialListEntry, 1> entries{
        PreparedSpatialListEntry{&spatial, Mat4::identity()},
    };
    const std::vector<PreparedDrawOrderEntry> ordered =
        order_prepared_model_draws_back_to_front(entries, Mat4::identity());

    check(ordered.size() == 2U, "prepared draw ordering flattens every material draw");
    if (ordered.size() == 2U) {
        check(ordered[0].prepared == &spatial && ordered[0].draw_index == 1U,
              "far prepared material draw sorts first");
        check(ordered[1].prepared == &spatial && ordered[1].draw_index == 0U,
              "near prepared material draw sorts last");
        check(ordered[0].view_depth < ordered[1].view_depth,
              "prepared draw ordering exposes monotonic far-to-near view depths");
    }
}

void test_prepared_draw_spatial_plan_is_stable_at_equal_depth() {
    const PreparedSpatialSubmission spatial = prepare_spatial_model(
        two_draw_spatial_asset(0.0F, 0.0F));
    const std::array<PreparedSpatialListEntry, 1> entries{
        PreparedSpatialListEntry{&spatial, Mat4::identity()},
    };
    const std::vector<PreparedDrawOrderEntry> ordered =
        order_prepared_model_draws_back_to_front(entries, Mat4::identity());

    check(ordered.size() == 2U, "equal-depth prepared draw plan contains both draws");
    if (ordered.size() == 2U) {
        check(ordered[0].draw_index == 0U && ordered[1].draw_index == 1U,
              "equal-depth prepared draws preserve canonical draw order");
    }
}

void test_prepared_draw_spatial_plan_orders_across_models() {
    const PreparedSpatialSubmission near_model = prepare_spatial_model(
        solid_triangle_asset({1.0F, 0.0F, 0.0F}));
    const PreparedSpatialSubmission far_model = prepare_spatial_model(
        solid_triangle_asset({0.0F, 0.0F, 1.0F}));
    const std::array<PreparedSpatialListEntry, 2> entries{
        PreparedSpatialListEntry{&near_model, Mat4::translation({0.0F, 0.0F, 0.75F})},
        PreparedSpatialListEntry{&far_model, Mat4::translation({0.0F, 0.0F, -0.75F})},
    };
    const std::vector<PreparedDrawOrderEntry> ordered =
        order_prepared_model_draws_back_to_front(entries, Mat4::identity());

    check(ordered.size() == 2U, "prepared draw plan spans multiple prepared models");
    if (ordered.size() == 2U) {
        check(ordered[0].prepared == &far_model && ordered[1].prepared == &near_model,
              "prepared draw plan sorts globally across model transforms");
    }
}

void test_prepared_draw_spatial_rejects_unrepresentable_state() {
    ModelAsset non_finite = two_draw_spatial_asset();
    non_finite.mesh.vertices[0].position.x = std::numeric_limits<float>::infinity();
    bool non_finite_threw = false;
    try {
        (void)prepare_spatial_model(non_finite);
    } catch (const std::invalid_argument&) {
        non_finite_threw = true;
    }
    check(non_finite_threw, "prepared spatial metadata rejects non-finite referenced geometry");

    const PreparedSpatialSubmission spatial = prepare_spatial_model(two_draw_spatial_asset());
    const std::array<PreparedSpatialListEntry, 1> projective_entry{
        PreparedSpatialListEntry{
            &spatial,
            Mat4::perspective(radians(60.0F), 1.0F, 0.1F, 100.0F)},
    };
    bool projective_threw = false;
    try {
        (void)order_prepared_model_draws_back_to_front(
            projective_entry, Mat4::identity());
    } catch (const std::invalid_argument&) {
        projective_threw = true;
    }
    check(projective_threw, "prepared draw ordering rejects projective model transforms");

    ModelRenderOptions programmed_options;
    programmed_options.vertex_program = std::make_shared<IdentitySpatialVertexProgram>();
    const PreparedSpatialSubmission programmed = prepare_spatial_model(
        two_draw_spatial_asset(), programmed_options);
    const std::array<PreparedSpatialListEntry, 1> programmed_entry{
        PreparedSpatialListEntry{&programmed, Mat4::identity()},
    };
    bool programmed_threw = false;
    try {
        (void)order_prepared_model_draws_back_to_front(
            programmed_entry, Mat4::identity());
    } catch (const std::invalid_argument&) {
        programmed_threw = true;
    }
    check(programmed_threw,
          "prepared draw ordering rejects vertex-program geometry not represented by canonical bounds");

    const std::array<PreparedSpatialListEntry, 1> null_entry{
        PreparedSpatialListEntry{nullptr, Mat4::identity()},
    };
    bool null_threw = false;
    try {
        (void)order_prepared_model_draws_back_to_front(null_entry, Mat4::identity());
    } catch (const std::invalid_argument&) {
        null_threw = true;
    }
    check(null_threw, "prepared draw ordering rejects null prepared spatial entries");
}

void test_prepared_draw_spatial_empty_plan_is_deterministic() {
    const PreparedSpatialSubmission empty = prepare_spatial_model(ModelAsset{});
    check(empty.draws().empty(), "empty prepared spatial model owns no draw metadata");
    const std::array<PreparedSpatialListEntry, 1> entries{
        PreparedSpatialListEntry{&empty, Mat4::identity()},
    };
    const std::vector<PreparedDrawOrderEntry> ordered =
        order_prepared_model_draws_back_to_front(entries, Mat4::identity());
    check(ordered.empty(), "empty prepared spatial model contributes no planned draw");
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
        test_prepared_draw_spatial_metadata_is_owned_and_exact();
        test_prepared_draw_spatial_plan_flattens_and_sorts_draws();
        test_prepared_draw_spatial_plan_is_stable_at_equal_depth();
        test_prepared_draw_spatial_plan_orders_across_models();
        test_prepared_draw_spatial_rejects_unrepresentable_state();
        test_prepared_draw_spatial_empty_plan_is_deterministic();
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
