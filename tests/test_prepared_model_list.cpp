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
#error TINY_RENDERER_SOURCE_DIR must be provided for prepared model list tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

ModelRenderOptions lit_options() {
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

ModelAsset one_triangle_asset(const Vec3& albedo) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex{{-0.8F, -0.8F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        Vertex{{0.8F, -0.8F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        Vertex{{0.0F, 0.8F, 0.0F}, {1.0F, 1.0F, 1.0F}},
    };
    asset.mesh.triangles = {{0U, 1U, 2U}};

    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material.albedo = albedo;
    asset.draws = {draw};
    return asset;
}

void check_unchanged(
    const Framebuffer& framebuffer,
    const std::vector<std::uint8_t>& before,
    const std::string& context) {
    check(framebuffer.rgb8() == before, context + " preserves framebuffer color");
    check(std::isinf(framebuffer.depth_at(32U, 32U)), context + " preserves framebuffer depth");
}

void test_heterogeneous_list_matches_sequential_submission() {
    PreparedModelSubmission kd = prepare_model_asset(
        load_obj_model_asset_file(fixture_path("material_sequence.obj")),
        lit_options());
    PreparedModelSubmission mapped = prepare_model_asset(
        load_obj_model_asset_file(fixture_path("material_texture_sequence.obj")),
        lit_options());

    const Mat4 left = Mat4::translation({-0.45F, 0.0F, 0.0F}) * Mat4::scale({0.45F, 0.45F, 1.0F});
    const Mat4 right = Mat4::translation({0.45F, 0.0F, 0.0F}) * Mat4::scale({0.45F, 0.45F, 1.0F});
    const std::array<PreparedModelListEntry, 2U> entries{{
        {&kd, left},
        {&mapped, right},
    }};

    Framebuffer sequential(97U, 65U);
    draw_prepared_model(sequential, kd, left, Mat4::identity(), Mat4::identity());
    draw_prepared_model(sequential, mapped, right, Mat4::identity(), Mat4::identity());

    Framebuffer listed(97U, 65U);
    draw_prepared_model_list(
        listed,
        std::span<const PreparedModelListEntry>{entries},
        Mat4::identity(),
        Mat4::identity());

    check(listed.rgb8() == sequential.rgb8(),
          "heterogeneous prepared list is byte-identical to equivalent sequential submissions");
    check(listed.fnv1a64() == sequential.fnv1a64(),
          "heterogeneous prepared list preserves sequential deterministic hashing");
}

void test_caller_entry_order_is_observable() {
    PreparedModelSubmission red = prepare_model_asset(one_triangle_asset({1.0F, 0.0F, 0.0F}));
    PreparedModelSubmission green = prepare_model_asset(one_triangle_asset({0.0F, 1.0F, 0.0F}));

    const std::array<PreparedModelListEntry, 2U> red_first{{
        {&red, Mat4::identity()},
        {&green, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 2U> green_first{{
        {&green, Mat4::identity()},
        {&red, Mat4::identity()},
    }};

    Framebuffer red_first_fb(65U, 65U);
    draw_prepared_model_list(
        red_first_fb,
        std::span<const PreparedModelListEntry>{red_first},
        Mat4::identity(),
        Mat4::identity());

    Framebuffer green_first_fb(65U, 65U);
    draw_prepared_model_list(
        green_first_fb,
        std::span<const PreparedModelListEntry>{green_first},
        Mat4::identity(),
        Mat4::identity());

    check(red_first_fb.rgb8() != green_first_fb.rgb8(),
          "reversing equal-depth heterogeneous entries changes ownership, proving caller order is preserved");
}

void test_later_singular_entry_fails_before_earlier_write() {
    PreparedModelSubmission first = prepare_model_asset(
        load_obj_model_asset_file(fixture_path("material_sequence.obj")),
        lit_options());
    PreparedModelSubmission second = prepare_model_asset(
        load_obj_model_asset_file(fixture_path("material_texture_sequence.obj")),
        lit_options());

    const std::array<PreparedModelListEntry, 2U> entries{{
        {&first, Mat4::scale({0.5F, 0.5F, 1.0F})},
        {&second, Mat4::scale({0.0F, 0.5F, 1.0F})},
    }};

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_prepared_model_list(
            framebuffer,
            std::span<const PreparedModelListEntry>{entries},
            Mat4::identity(),
            Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw, "later singular heterogeneous entry is rejected during whole-list preflight");
    check_unchanged(framebuffer, before, "later singular heterogeneous entry rejection");
}

void test_null_entry_fails_before_any_write() {
    PreparedModelSubmission valid = prepare_model_asset(one_triangle_asset({0.8F, 0.2F, 0.2F}));
    const std::array<PreparedModelListEntry, 2U> entries{{
        {&valid, Mat4::identity()},
        {nullptr, Mat4::identity()},
    }};

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.25F, 0.125F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_prepared_model_list(
            framebuffer,
            std::span<const PreparedModelListEntry>{entries},
            Mat4::identity(),
            Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw, "null prepared-plan list entry is rejected rather than skipped");
    check_unchanged(framebuffer, before, "null prepared-plan entry rejection");
}

void test_empty_list_is_noop() {
    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.375F, 0.125F, 0.25F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    draw_prepared_model_list(
        framebuffer,
        std::span<const PreparedModelListEntry>{},
        Mat4::identity(),
        Mat4::identity());

    check_unchanged(framebuffer, before, "empty heterogeneous prepared list");
}

}  // namespace

int main() {
    try {
        test_heterogeneous_list_matches_sequential_submission();
        test_caller_entry_order_is_observable();
        test_later_singular_entry_fails_before_earlier_write();
        test_null_entry_fails_before_any_write();
        test_empty_list_is_noop();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " prepared model list test(s) failed\n";
        return 1;
    }
    std::cout << "all prepared model list tests passed\n";
    return 0;
}
