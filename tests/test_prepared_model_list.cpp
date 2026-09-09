#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
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

ModelRenderOptions transparent_options() {
    ModelRenderOptions options;
    options.depth_state.write_enabled = false;
    options.blend_state.enabled = true;
    options.blend_state.source_factor = BlendFactor::SourceAlpha;
    options.blend_state.destination_factor = BlendFactor::OneMinusSourceAlpha;
    return options;
}

class ShiftZProgram final : public VertexProgram {
public:
    explicit ShiftZProgram(float delta) : delta_(delta) {}

    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        VertexProgramOutput output{input.position, input.varyings};
        output.position.z += delta_;
        return output;
    }

private:
    float delta_{};
};

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

ModelAsset transparent_triangle_asset(const Vec3& albedo) {
    ModelAsset asset = one_triangle_asset(albedo);
    asset.draws[0].material.opacity = 0.5F;
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

void test_back_to_front_list_matches_manual_transparent_order() {
    const ModelRenderOptions options = transparent_options();
    PreparedModelSubmission near_red = prepare_model_asset(
        transparent_triangle_asset({1.0F, 0.0F, 0.0F}), options);
    PreparedModelSubmission far_blue = prepare_model_asset(
        transparent_triangle_asset({0.0F, 0.0F, 1.0F}), options);

    const Mat4 near_model = Mat4::translation({0.0F, 0.0F, -2.0F});
    const Mat4 far_model = Mat4::translation({0.0F, 0.0F, -4.0F});
    const Mat4 view = Mat4::identity();
    const Mat4 projection = Mat4::perspective(1.0F, 1.0F, 0.1F, 10.0F);

    const std::array<PreparedModelListEntry, 2U> near_first{{
        {&near_red, near_model},
        {&far_blue, far_model},
    }};
    const std::array<PreparedModelListEntry, 2U> manual_far_first{{
        {&far_blue, far_model},
        {&near_red, near_model},
    }};

    Framebuffer unsorted(65U, 65U);
    draw_prepared_model_list(
        unsorted,
        std::span<const PreparedModelListEntry>{near_first},
        view,
        projection);

    Framebuffer manual(65U, 65U);
    draw_prepared_model_list(
        manual,
        std::span<const PreparedModelListEntry>{manual_far_first},
        view,
        projection);

    Framebuffer sorted(65U, 65U);
    draw_prepared_model_list_back_to_front(
        sorted,
        std::span<const PreparedModelListEntry>{near_first},
        view,
        projection);

    check(sorted.rgb8() == manual.rgb8(),
          "back-to-front prepared list matches explicit far-then-near transparent submission");
    check(sorted.fnv1a64() == manual.fnv1a64(),
          "back-to-front prepared list preserves deterministic manual-order hash");
    check(sorted.rgb8() != unsorted.rgb8(),
          "back-to-front prepared list changes non-commutative source-alpha composition from near-first caller order");
}

void test_back_to_front_equal_depth_is_stable() {
    const ModelRenderOptions options = transparent_options();
    PreparedModelSubmission red = prepare_model_asset(
        transparent_triangle_asset({1.0F, 0.0F, 0.0F}), options);
    PreparedModelSubmission green = prepare_model_asset(
        transparent_triangle_asset({0.0F, 1.0F, 0.0F}), options);
    const Mat4 model = Mat4::translation({0.0F, 0.0F, -3.0F});
    const Mat4 projection = Mat4::perspective(1.0F, 1.0F, 0.1F, 10.0F);
    const std::array<PreparedModelListEntry, 2U> entries{{
        {&red, model},
        {&green, model},
    }};

    Framebuffer sequential(65U, 65U);
    draw_prepared_model_list(
        sequential,
        std::span<const PreparedModelListEntry>{entries},
        Mat4::identity(),
        projection);

    Framebuffer sorted(65U, 65U);
    draw_prepared_model_list_back_to_front(
        sorted,
        std::span<const PreparedModelListEntry>{entries},
        Mat4::identity(),
        projection);

    check(sorted.rgb8() == sequential.rgb8(),
          "equal-depth back-to-front sort preserves caller order stably");
}

void test_back_to_front_nonfinite_sort_key_fails_before_write() {
    PreparedModelSubmission valid = prepare_model_asset(
        transparent_triangle_asset({0.8F, 0.2F, 0.2F}), transparent_options());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<PreparedModelListEntry, 2U> entries{{
        {&valid, Mat4::translation({0.0F, 0.0F, -2.0F})},
        {&valid, Mat4::translation({0.0F, 0.0F, nan})},
    }};

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.25F, 0.125F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_prepared_model_list_back_to_front(
            framebuffer,
            std::span<const PreparedModelListEntry>{entries},
            Mat4::identity(),
            Mat4::perspective(1.0F, 1.0F, 0.1F, 10.0F));
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw, "non-finite back-to-front sort transform is rejected");
    check_unchanged(framebuffer, before, "non-finite back-to-front sort rejection");
}

void test_back_to_front_vertex_program_fails_before_write() {
    ModelRenderOptions options = transparent_options();
    options.vertex_program = std::make_shared<ShiftZProgram>(1.0F);
    PreparedModelSubmission programmed = prepare_model_asset(
        transparent_triangle_asset({0.2F, 0.8F, 0.2F}),
        options);
    const std::array<PreparedModelListEntry, 1U> entries{{
        {&programmed, Mat4::translation({0.0F, 0.0F, -3.0F})},
    }};

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.25F, 0.125F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_prepared_model_list_back_to_front(
            framebuffer,
            std::span<const PreparedModelListEntry>{entries},
            Mat4::identity(),
            Mat4::perspective(1.0F, 1.0F, 0.1F, 10.0F));
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw, "back-to-front prepared list rejects vertex-program entries");
    check_unchanged(framebuffer, before, "vertex-program back-to-front rejection");
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
    draw_prepared_model_list_back_to_front(
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
        test_back_to_front_list_matches_manual_transparent_order();
        test_back_to_front_equal_depth_is_stable();
        test_back_to_front_nonfinite_sort_key_fails_before_write();
        test_back_to_front_vertex_program_fails_before_write();
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
