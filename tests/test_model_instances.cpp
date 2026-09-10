#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/obj_loader.hpp"
#include "tiny_renderer/prepared_scene.hpp"

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
#error TINY_RENDERER_SOURCE_DIR must be provided for model instance tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

ModelRenderOptions lit_fixture_options() {
    ModelRenderOptions options;
    options.sampler = SamplerState{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear};
    options.directional_light = DirectionalLight{
        true,
        NormalBinding{2U, 3U, 4U},
        {0.0F, 0.0F, 1.0F},
        0.2F,
        0.8F,
    };
    return options;
}

ModelRenderOptions unlit_fixture_options() {
    ModelRenderOptions options;
    options.sampler = SamplerState{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear};
    return options;
}

ModelRenderOptions source_alpha_options() {
    ModelRenderOptions options;
    options.blend_state.enabled = true;
    options.blend_state.source_factor = BlendFactor::SourceAlpha;
    options.blend_state.destination_factor = BlendFactor::OneMinusSourceAlpha;
    options.blend_state.operation = BlendOp::Add;
    options.depth_state.write_enabled = false;
    return options;
}

ModelAsset scene_triangle_asset(
    const Vec3& color,
    float opacity = 1.0F) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.5F, -0.5F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({0.5F, -0.5F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({0.0F, 0.5F, 0.0F}, VaryingPack{}),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material.albedo = color;
    draw.material.opacity = opacity;
    asset.draws.push_back(draw);
    return asset;
}

std::array<Mat4, 2U> separated_instances() {
    return {
        Mat4::translation({-0.45F, 0.0F, 0.0F}) * Mat4::scale({0.45F, 0.45F, 1.0F}),
        Mat4::translation({0.45F, 0.0F, 0.0F}) * Mat4::scale({0.45F, 0.45F, 1.0F}),
    };
}

void check_unchanged(
    const Framebuffer& framebuffer,
    const std::vector<std::uint8_t>& before,
    const std::string& context) {
    check(framebuffer.rgb8() == before, context + " preserves framebuffer color");
    check(std::isinf(framebuffer.depth_at(32U, 32U)), context + " preserves framebuffer depth");
}

bool same_float(float left, float right) {
    return left == right || (std::isnan(left) && std::isnan(right));
}

void check_framebuffers_equal(
    const Framebuffer& left,
    const Framebuffer& right,
    const std::string& context) {
    check(left.width() == right.width() && left.height() == right.height(),
          context + " preserves framebuffer dimensions");
    check(left.sample_count() == right.sample_count(),
          context + " preserves sample count");
    if (left.width() != right.width()
        || left.height() != right.height()
        || left.sample_count() != right.sample_count()) {
        return;
    }

    for (std::size_t y = 0U; y < left.height(); ++y) {
        for (std::size_t x = 0U; x < left.width(); ++x) {
            for (std::size_t sample = 0U; sample < left.samples_per_pixel(); ++sample) {
                const Vec3& a = left.sample_color_at(x, y, sample);
                const Vec3& b = right.sample_color_at(x, y, sample);
                check(
                    same_float(a.x, b.x) && same_float(a.y, b.y) && same_float(a.z, b.z),
                    context + " preserves exact sample color");
                check(
                    same_float(left.sample_depth_at(x, y, sample), right.sample_depth_at(x, y, sample)),
                    context + " preserves exact sample depth");
                check(
                    left.sample_stencil_at(x, y, sample) == right.sample_stencil_at(x, y, sample),
                    context + " preserves exact sample stencil");
            }
        }
    }
}

void test_model_transform_batch_matches_sequential_submission() {
    ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
    const PreparedModelSubmission prepared = prepare_model_asset(std::move(asset), lit_fixture_options());
    const std::array<Mat4, 2U> models = separated_instances();

    Framebuffer sequential(97U, 65U);
    for (const Mat4& model : models) {
        draw_prepared_model(
            sequential,
            prepared,
            model,
            Mat4::identity(),
            Mat4::identity());
    }

    Framebuffer batched(97U, 65U);
    draw_prepared_model_instances(
        batched,
        prepared,
        std::span<const Mat4>{models},
        Mat4::identity(),
        Mat4::identity());

    check(batched.rgb8() == sequential.rgb8(),
          "model-transform instance batch is byte-identical to sequential prepared submissions");
    check(batched.fnv1a64() == sequential.fnv1a64(),
          "model-transform instance batch preserves sequential deterministic hashing");
}

void test_mvp_batch_matches_sequential_submission() {
    ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
    const PreparedModelSubmission prepared = prepare_model_asset(std::move(asset), unlit_fixture_options());
    const std::array<Mat4, 2U> mvps = separated_instances();

    Framebuffer sequential(97U, 65U);
    for (const Mat4& mvp : mvps) {
        draw_prepared_model(sequential, prepared, mvp);
    }

    Framebuffer batched(97U, 65U);
    draw_prepared_model_instances(batched, prepared, std::span<const Mat4>{mvps});

    check(batched.rgb8() == sequential.rgb8(),
          "MVP instance batch is byte-identical to sequential prepared submissions");
    check(batched.fnv1a64() == sequential.fnv1a64(),
          "MVP instance batch preserves sequential deterministic hashing");
}

void test_instance_order_is_observable_and_deterministic() {
    ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
    const PreparedModelSubmission prepared = prepare_model_asset(std::move(asset), lit_fixture_options());

    // Both transforms leave z=0 geometry at the same projected location/depth.
    // The second transform flips the normal, so Lambert shading differs. With
    // strict '<' depth testing the first submitted instance owns equal-depth
    // samples, making input order directly observable in framebuffer bytes.
    const Mat4 lit = Mat4::identity();
    const Mat4 flipped = Mat4::scale({1.0F, 1.0F, -1.0F});
    const std::array<Mat4, 2U> forward{lit, flipped};
    const std::array<Mat4, 2U> reverse{flipped, lit};

    Framebuffer forward_fb(65U, 65U);
    draw_prepared_model_instances(
        forward_fb,
        prepared,
        std::span<const Mat4>{forward},
        Mat4::identity(),
        Mat4::identity());

    Framebuffer reverse_fb(65U, 65U);
    draw_prepared_model_instances(
        reverse_fb,
        prepared,
        std::span<const Mat4>{reverse},
        Mat4::identity(),
        Mat4::identity());

    check(forward_fb.rgb8() != reverse_fb.rgb8(),
          "reversing equal-depth lit instances changes ownership, proving instance input order is preserved");
}

void test_later_singular_instance_fails_before_any_batch_write() {
    ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
    const PreparedModelSubmission prepared = prepare_model_asset(std::move(asset), lit_fixture_options());
    const std::array<Mat4, 2U> models{
        Mat4::scale({0.5F, 0.5F, 1.0F}),
        Mat4::scale({0.0F, 0.5F, 1.0F}),
    };

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_prepared_model_instances(
            framebuffer,
            prepared,
            std::span<const Mat4>{models},
            Mat4::identity(),
            Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw, "later singular lit instance is rejected during batch preflight");
    check_unchanged(framebuffer, before, "later singular instance rejection");
}

void test_empty_instance_batches_are_noops() {
    ModelAsset lit_asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
    const PreparedModelSubmission lit_prepared = prepare_model_asset(std::move(lit_asset), lit_fixture_options());

    Framebuffer model_framebuffer(65U, 65U);
    model_framebuffer.clear({0.25F, 0.125F, 0.375F});
    const std::vector<std::uint8_t> model_before = model_framebuffer.rgb8();
    draw_prepared_model_instances(
        model_framebuffer,
        lit_prepared,
        std::span<const Mat4>{},
        Mat4::identity(),
        Mat4::identity());
    check_unchanged(model_framebuffer, model_before, "empty model-transform instance batch");

    ModelAsset unlit_asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
    const PreparedModelSubmission unlit_prepared = prepare_model_asset(std::move(unlit_asset), unlit_fixture_options());

    Framebuffer mvp_framebuffer(65U, 65U);
    mvp_framebuffer.clear({0.375F, 0.125F, 0.25F});
    const std::vector<std::uint8_t> mvp_before = mvp_framebuffer.rgb8();
    draw_prepared_model_instances(
        mvp_framebuffer,
        unlit_prepared,
        std::span<const Mat4>{});
    check_unchanged(mvp_framebuffer, mvp_before, "empty MVP instance batch");
}

void test_nonempty_mvp_batch_preserves_lighting_restriction() {
    ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
    const PreparedModelSubmission prepared = prepare_model_asset(std::move(asset), lit_fixture_options());
    const std::array<Mat4, 2U> mvps{Mat4::identity(), Mat4::identity()};

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.375F, 0.25F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_prepared_model_instances(framebuffer, prepared, std::span<const Mat4>{mvps});
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw, "non-empty MVP instance batch rejects directional lighting");
    check_unchanged(framebuffer, before, "MVP instance lighting rejection");
}

void test_prepared_scene_plan_reuses_owned_geometry_across_cameras() {
    std::vector<PreparedScenePlanEntry> entries;
    entries.push_back({
        prepare_spatial_model(scene_triangle_asset({0.9F, 0.2F, 0.1F})),
        Mat4::identity(),
        PreparedScenePhase::CallerOrder,
    });
    entries.push_back({
        prepare_spatial_model(
            scene_triangle_asset({0.1F, 0.8F, 0.3F}, 0.5F),
            source_alpha_options()),
        Mat4::translation({4.0F, 0.0F, 0.0F}),
        PreparedScenePhase::BackToFront,
    });
    const PreparedScenePlan plan{std::move(entries)};

    const Mat4 projection = Mat4::perspective(radians(60.0F), 1.0F, 0.1F, 20.0F);
    const Mat4 origin_view = Mat4::look_at(
        {0.0F, 0.0F, 3.0F},
        {0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F});
    const Mat4 shifted_view = Mat4::look_at(
        {4.0F, 0.0F, 3.0F},
        {4.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F});

    const PreparedSceneEvaluation origin =
        evaluate_prepared_scene_plan(plan, origin_view, projection);
    const PreparedSceneEvaluation shifted =
        evaluate_prepared_scene_plan(plan, shifted_view, projection);

    check(origin.caller_order_draws().size() == 1U
              && origin.back_to_front_draws().size() == 1U
              && shifted.caller_order_draws().size() == 1U
              && shifted.back_to_front_draws().size() == 1U,
          "reusable prepared scene preserves complete phase plans across camera evaluations");
    check(origin.visible_caller_order_draws().size() == 1U
              && origin.visible_back_to_front_draws().empty(),
          "origin camera retains only the origin-side prepared draw");
    check(shifted.visible_caller_order_draws().empty()
              && shifted.visible_back_to_front_draws().size() == 1U,
          "shifted camera re-evaluates visibility without rebuilding scene ownership");

    Framebuffer origin_fb(65U, 65U, SampleCount::Four);
    Framebuffer shifted_fb(65U, 65U, SampleCount::Four);
    origin_fb.clear({0.125F, 0.25F, 0.375F});
    shifted_fb.clear({0.125F, 0.25F, 0.375F});
    draw_prepared_scene_evaluation(origin_fb, origin);
    draw_prepared_scene_evaluation(shifted_fb, shifted);
    check(origin_fb.fnv1a64() != shifted_fb.fnv1a64(),
          "different camera evaluations execute different visible subsets from one owned scene plan");
}

void test_prepared_scene_combined_executor_matches_manual_phase_execution() {
    std::vector<PreparedScenePlanEntry> entries;
    entries.push_back({
        prepare_spatial_model(scene_triangle_asset({0.1F, 0.25F, 0.8F})),
        Mat4::translation({0.0F, 0.0F, 0.5F}),
        PreparedScenePhase::CallerOrder,
    });
    entries.push_back({
        prepare_spatial_model(
            scene_triangle_asset({0.9F, 0.1F, 0.2F}, 0.5F),
            source_alpha_options()),
        Mat4::translation({0.0F, 0.0F, -0.2F}),
        PreparedScenePhase::BackToFront,
    });
    entries.push_back({
        prepare_spatial_model(
            scene_triangle_asset({0.1F, 0.9F, 0.3F}, 0.5F),
            source_alpha_options()),
        Mat4::translation({0.0F, 0.0F, 0.2F}),
        PreparedScenePhase::BackToFront,
    });
    const PreparedScenePlan plan{std::move(entries)};
    const PreparedSceneEvaluation evaluation =
        evaluate_prepared_scene_plan(plan, Mat4::identity(), Mat4::identity());

    check(evaluation.caller_order_draws().size() == 1U,
          "prepared scene evaluation emits the caller-order phase");
    check(evaluation.back_to_front_draws().size() == 2U,
          "prepared scene evaluation emits every back-to-front draw");
    if (evaluation.back_to_front_draws().size() == 2U) {
        check(evaluation.back_to_front_draws()[0].view_depth
                  < evaluation.back_to_front_draws()[1].view_depth,
              "prepared scene back-to-front phase uses stable increasing view-depth order");
    }

    Framebuffer combined(65U, 65U, SampleCount::Four);
    Framebuffer manual(65U, 65U, SampleCount::Four);
    combined.clear({0.125F, 0.25F, 0.375F}, 1.0F, 17U);
    manual.clear({0.125F, 0.25F, 0.375F}, 1.0F, 17U);

    draw_prepared_scene_evaluation(combined, evaluation);

    preflight_prepared_draw_order(manual, evaluation.caller_order_draws());
    preflight_prepared_draw_order(manual, evaluation.back_to_front_draws());
    draw_prepared_draw_order(
        manual,
        evaluation.visible_caller_order_draws(),
        evaluation.view(),
        evaluation.projection());
    draw_prepared_draw_order(
        manual,
        evaluation.visible_back_to_front_draws(),
        evaluation.view(),
        evaluation.projection());

    check(combined.rgb8() == manual.rgb8()
              && combined.fnv1a64() == manual.fnv1a64(),
          "prepared scene combined executor is byte/hash equivalent to explicit phase execution");
    check_framebuffers_equal(
        combined,
        manual,
        "prepared scene combined executor");
}

void test_prepared_scene_visibility_cannot_bypass_full_target_preflight() {
    ModelRenderOptions invalid_target_options;
    invalid_target_options.viewport_state.viewport = RasterRect{0U, 0U, 4096U, 4096U};

    std::vector<PreparedScenePlanEntry> entries;
    entries.push_back({
        prepare_spatial_model(scene_triangle_asset({0.8F, 0.2F, 0.1F})),
        Mat4::identity(),
        PreparedScenePhase::CallerOrder,
    });
    entries.push_back({
        prepare_spatial_model(
            scene_triangle_asset({0.1F, 0.8F, 0.3F}),
            invalid_target_options),
        Mat4::translation({20.0F, 0.0F, 0.0F}),
        PreparedScenePhase::CallerOrder,
    });
    const PreparedScenePlan plan{std::move(entries)};

    const Mat4 view = Mat4::look_at(
        {0.0F, 0.0F, 3.0F},
        {0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F});
    const Mat4 projection = Mat4::perspective(radians(60.0F), 1.0F, 0.1F, 20.0F);
    const PreparedSceneEvaluation evaluation =
        evaluate_prepared_scene_plan(plan, view, projection);
    check(evaluation.caller_order_draws().size() == 2U
              && evaluation.visible_caller_order_draws().size() == 1U,
          "prepared scene visibility omits the provably off-frustum invalid-target draw only from execution");

    Framebuffer framebuffer(65U, 65U, SampleCount::Four);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    bool threw = false;
    try {
        draw_prepared_scene_evaluation(framebuffer, evaluation);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(threw,
          "prepared scene full-plan preflight rejects target-invalid work even when visibility removes it");
    check(framebuffer.rgb8() == before,
          "prepared scene full-plan rejection occurs before any color mutation");
    check(std::isinf(framebuffer.sample_depth_at(32U, 32U, 0U)),
          "prepared scene full-plan rejection occurs before any depth mutation");
}

}  // namespace

int main() {
    try {
        test_model_transform_batch_matches_sequential_submission();
        test_mvp_batch_matches_sequential_submission();
        test_instance_order_is_observable_and_deterministic();
        test_later_singular_instance_fails_before_any_batch_write();
        test_empty_instance_batches_are_noops();
        test_nonempty_mvp_batch_preserves_lighting_restriction();
        test_prepared_scene_plan_reuses_owned_geometry_across_cameras();
        test_prepared_scene_combined_executor_matches_manual_phase_execution();
        test_prepared_scene_visibility_cannot_bypass_full_target_preflight();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " model instance test(s) failed\n";
        return 1;
    }
    std::cout << "all model instance tests passed\n";
    return 0;
}
