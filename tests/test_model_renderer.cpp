#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
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
#error TINY_RENDERER_SOURCE_DIR must be provided for model renderer tests
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

SamplerState fixture_sampler() {
    return SamplerState{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear};
}

ModelRenderOptions fixture_options() {
    ModelRenderOptions options;
    options.u_channel = 0U;
    options.v_channel = 1U;
    options.sampler = fixture_sampler();
    options.directional_light = fixture_light();
    return options;
}

void render_manual(const ModelAsset& asset, Framebuffer& framebuffer) {
    const ModelRenderOptions options = fixture_options();
    for (const MaterialDraw& draw : asset.draws) {
        TextureBinding texture_binding{};
        BaseColorSource source = BaseColorSource::ConstantWhite;
        if (draw.diffuse_texture) {
            texture_binding = TextureBinding{
                draw.diffuse_texture.get(),
                options.u_channel,
                options.v_channel,
                options.sampler,
            };
            source = BaseColorSource::Texture;
        }
        Rasterizer rasterizer(
            framebuffer,
            {},
            texture_binding,
            options.directional_light,
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

void check_unchanged(const Framebuffer& framebuffer, const std::vector<std::uint8_t>& before, const std::string& context) {
    check(framebuffer.rgb8() == before, context + " preserves framebuffer color");
    check(std::isinf(framebuffer.depth_at(32U, 32U)), context + " preserves framebuffer depth");
}

void test_first_class_submission_matches_manual_loop() {
    const ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));

    Framebuffer direct_fb(65U, 65U);
    draw_model_asset(
        direct_fb,
        asset,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        fixture_options());

    Framebuffer manual_fb(65U, 65U);
    render_manual(asset, manual_fb);

    check(direct_fb.rgb8() == manual_fb.rgb8(),
          "first-class model submission is byte-identical to the established manual MaterialDraw loop");
    check(direct_fb.fnv1a64() == manual_fb.fnv1a64(),
          "first-class model submission preserves the established deterministic framebuffer hash");

    const ModelAsset kd_asset = load_obj_model_asset_file(fixture_path("material_sequence.obj"));
    Framebuffer direct_kd(65U, 65U);
    Framebuffer manual_kd(65U, 65U);
    draw_model_asset(
        direct_kd,
        kd_asset,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        fixture_options());
    render_manual(kd_asset, manual_kd);
    check(direct_kd.rgb8() == manual_kd.rgb8(),
          "Kd-only model submission is byte-identical to the manual MaterialDraw loop");
    check(direct_kd.fnv1a64() == manual_kd.fnv1a64(),
          "Kd-only model submission preserves deterministic hashing");
}

void test_later_invalid_draw_state_fails_before_any_write() {
    ModelAsset asset = make_two_draw_asset();
    asset.draws[1].material.albedo.x = 2.0F;

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_model_asset(framebuffer, asset, Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "invalid later model material is rejected");
    check_unchanged(framebuffer, before, "invalid later model material");
}

void test_invalid_structure_fails_before_any_write() {
    {
        ModelAsset asset = make_two_draw_asset();
        asset.mesh.triangles[1][2] = 999U;
        Framebuffer framebuffer(65U, 65U);
        framebuffer.clear({0.25F, 0.125F, 0.375F});
        const std::vector<std::uint8_t> before = framebuffer.rgb8();
        bool threw = false;
        try {
            draw_model_asset(framebuffer, asset, Mat4::identity());
        } catch (const std::out_of_range&) {
            threw = true;
        }
        check(threw, "invalid later model triangle index is rejected");
        check_unchanged(framebuffer, before, "invalid later model triangle index");
    }

    {
        ModelAsset asset = make_two_draw_asset();
        asset.draws[1].range.first_triangle = 0U;
        Framebuffer framebuffer(65U, 65U);
        const std::vector<std::uint8_t> before = framebuffer.rgb8();
        bool threw = false;
        try {
            draw_model_asset(framebuffer, asset, Mat4::identity());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "overlapping/non-contiguous model draw ranges are rejected");
        check_unchanged(framebuffer, before, "invalid model draw-range topology");
    }
}

void test_invalid_sampler_and_mvp_lighting_fail_before_any_write() {
    const ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));

    {
        ModelRenderOptions options = fixture_options();
        options.sampler.filter = static_cast<FilterMode>(99);
        Framebuffer framebuffer(65U, 65U);
        framebuffer.clear({0.1F, 0.2F, 0.3F});
        const std::vector<std::uint8_t> before = framebuffer.rgb8();
        bool threw = false;
        try {
            draw_model_asset(
                framebuffer,
                asset,
                Mat4::identity(),
                Mat4::identity(),
                Mat4::identity(),
                options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "unknown model texture filter is rejected");
        check_unchanged(framebuffer, before, "invalid model texture sampler");
    }

    {
        Framebuffer framebuffer(65U, 65U);
        framebuffer.clear({0.2F, 0.1F, 0.3F});
        const std::vector<std::uint8_t> before = framebuffer.rgb8();
        bool threw = false;
        try {
            draw_model_asset(framebuffer, asset, Mat4::identity(), fixture_options());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "model MVP-only submission rejects enabled directional lighting");
        check_unchanged(framebuffer, before, "model MVP-only lighting rejection");
    }
}

void test_empty_model_is_a_noop_but_nonempty_mesh_requires_draws() {
    Framebuffer framebuffer(33U, 33U);
    framebuffer.clear({0.125F, 0.125F, 0.125F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    draw_model_asset(framebuffer, ModelAsset{}, Mat4::identity());
    check(framebuffer.rgb8() == before, "empty model submission is a deterministic no-op");

    ModelAsset missing_draws;
    missing_draws.mesh = make_two_triangle_mesh();
    bool threw = false;
    try {
        draw_model_asset(framebuffer, missing_draws, Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "non-empty canonical mesh without draw coverage is rejected");
    check(framebuffer.rgb8() == before, "missing model draw coverage fails before color mutation");
}

}  // namespace

int main() {
    try {
        test_first_class_submission_matches_manual_loop();
        test_later_invalid_draw_state_fails_before_any_write();
        test_invalid_structure_fails_before_any_write();
        test_invalid_sampler_and_mvp_lighting_fail_before_any_write();
        test_empty_model_is_a_noop_but_nonempty_mesh_requires_draws();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " model renderer test(s) failed\n";
        return 1;
    }
    std::cout << "all model renderer tests passed\n";
    return 0;
}
