#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

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
    draw_prepared_model_instances(mvp_framebuffer, unlit_prepared, std::span<const Mat4>{});
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

}  // namespace

int main() {
    try {
        test_model_transform_batch_matches_sequential_submission();
        test_mvp_batch_matches_sequential_submission();
        test_later_singular_instance_fails_before_any_batch_write();
        test_empty_instance_batches_are_noops();
        test_nonempty_mvp_batch_preserves_lighting_restriction();
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
