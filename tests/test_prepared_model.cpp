#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
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
#error TINY_RENDERER_SOURCE_DIR must be provided for prepared model tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

ModelRenderOptions fixture_options() {
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

ModelAsset make_two_draw_asset() {
    ModelAsset asset;
    asset.mesh = make_two_triangle_mesh();
    MaterialDraw first;
    first.range = {0U, 1U};
    first.material.albedo = {0.8F, 0.2F, 0.2F};
    MaterialDraw second;
    second.range = {1U, 1U};
    second.material.albedo = {0.2F, 0.8F, 0.2F};
    asset.draws = {first, second};
    return asset;
}

void check_unchanged(
    const Framebuffer& framebuffer,
    const std::vector<std::uint8_t>& before,
    const std::string& context) {
    check(framebuffer.rgb8() == before, context + " preserves framebuffer color");
    check(std::isinf(framebuffer.depth_at(32U, 32U)), context + " preserves framebuffer depth");
}

void test_prepared_render_matches_direct_and_is_reusable() {
    const ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
    const ModelRenderOptions options = fixture_options();
    const PreparedModelSubmission prepared = prepare_model_asset(asset, options);

    Framebuffer direct(65U, 65U);
    draw_model_asset(
        direct,
        asset,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        options);

    Framebuffer first(65U, 65U);
    draw_prepared_model(
        first,
        prepared,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    Framebuffer second(65U, 65U);
    draw_prepared_model(
        second,
        prepared,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());

    check(first.rgb8() == direct.rgb8(),
          "prepared model execution is byte-identical to first-class direct submission");
    check(first.fnv1a64() == direct.fnv1a64(),
          "prepared model execution preserves the direct deterministic framebuffer hash");
    check(second.rgb8() == first.rgb8(),
          "one prepared model plan can be executed repeatedly with deterministic bytes");
    check(second.fnv1a64() == first.fnv1a64(),
          "repeated prepared execution preserves deterministic hashing");
}

void test_prepared_plan_owns_snapshot_and_texture_lifetime() {
    PreparedModelSubmission prepared = [&]() {
        ModelAsset source = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
        PreparedModelSubmission plan = prepare_model_asset(source, fixture_options());
        check(!plan.asset().draws.empty() && plan.asset().draws.front().diffuse_texture != nullptr,
              "prepared plan retains imported diffuse texture ownership");

        source.mesh.triangles.clear();
        source.draws.clear();
        return plan;
    }();

    check(prepared.asset().mesh.triangles.size() == 3U,
          "prepared plan owns canonical topology independently of the source ModelAsset");
    check(prepared.asset().draws.size() == 3U,
          "prepared plan owns material draw records independently of the source ModelAsset");

    Framebuffer framebuffer(65U, 65U);
    draw_prepared_model(
        framebuffer,
        prepared,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    check(framebuffer.fnv1a64() != Framebuffer(65U, 65U).fnv1a64(),
          "prepared plan remains executable after the source ModelAsset is destroyed or mutated");
}

void test_prepare_rejects_invalid_static_state() {
    {
        ModelAsset asset = make_two_draw_asset();
        asset.draws[1].material.albedo.x = 2.0F;
        bool threw = false;
        try {
            (void)prepare_model_asset(std::move(asset));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "prepare rejects an invalid later material without needing a framebuffer");
    }

    {
        ModelAsset asset = make_two_draw_asset();
        asset.draws[1].range.first_triangle = 0U;
        bool threw = false;
        try {
            (void)prepare_model_asset(std::move(asset));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "prepare rejects non-contiguous model draw coverage");
    }

    {
        ModelAsset asset = make_two_draw_asset();
        asset.mesh.triangles[1][2] = 999U;
        bool threw = false;
        try {
            (void)prepare_model_asset(std::move(asset));
        } catch (const std::out_of_range&) {
            threw = true;
        }
        check(threw, "prepare rejects invalid canonical mesh indices");
    }

    {
        ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
        ModelRenderOptions options = fixture_options();
        options.sampler.filter = static_cast<FilterMode>(99);
        bool threw = false;
        try {
            (void)prepare_model_asset(std::move(asset), options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "prepare rejects invalid sampler state when mapped textures consume it");
    }

    {
        ModelAsset asset = load_obj_model_asset_file(fixture_path("material_sequence.obj"));
        ModelRenderOptions options;
        options.sampler.filter = static_cast<FilterMode>(99);
        bool threw = false;
        try {
            (void)prepare_model_asset(std::move(asset), options);
        } catch (const std::exception&) {
            threw = true;
        }
        check(!threw, "prepare ignores sampler state when no material draw consumes a texture");
    }
}

void test_dynamic_execution_contract_remains_fail_closed() {
    ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
    const PreparedModelSubmission prepared = prepare_model_asset(std::move(asset), fixture_options());

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_prepared_model(
            framebuffer,
            prepared,
            Mat4::scale({0.0F, 1.0F, 1.0F}),
            Mat4::identity(),
            Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "prepared execution rejects a singular dynamic normal transform");
    check_unchanged(framebuffer, before, "singular prepared-model normal transform");

    Framebuffer mvp_framebuffer(65U, 65U);
    mvp_framebuffer.clear({0.25F, 0.125F, 0.375F});
    const std::vector<std::uint8_t> mvp_before = mvp_framebuffer.rgb8();
    threw = false;
    try {
        draw_prepared_model(mvp_framebuffer, prepared, Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "prepared MVP-only execution preserves the lighting transform restriction");
    check_unchanged(mvp_framebuffer, mvp_before, "prepared MVP-only lighting rejection");
}

}  // namespace

int main() {
    try {
        test_prepared_render_matches_direct_and_is_reusable();
        test_prepared_plan_owns_snapshot_and_texture_lifetime();
        test_prepare_rejects_invalid_static_state();
        test_dynamic_execution_contract_remains_fail_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " prepared model test(s) failed\n";
        return 1;
    }
    std::cout << "all prepared model tests passed\n";
    return 0;
}
