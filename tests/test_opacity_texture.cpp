#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 1.0e-5F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

void check_vec3(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check_near(actual.x, expected.x, message + " red");
    check_near(actual.y, expected.y, message + " green");
    check_near(actual.z, expected.z, message + " blue");
}

std::filesystem::path fixture_path(const char* name) {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for opacity texture fixture tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

MaterialAssetLibrary parse_assets(std::string_view text) {
    std::istringstream input{std::string(text)};
    return load_mtl_assets(input);
}

void expect_asset_error(std::string_view text, const std::string& message) {
    bool threw = false;
    try {
        (void)parse_assets(text);
    } catch (const MtlParseError&) {
        threw = true;
    }
    check(threw, message);
}

ModelRenderOptions alpha_options() {
    ModelRenderOptions options;
    options.u_channel = 0U;
    options.v_channel = 1U;
    options.sampler = {AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear};
    options.depth_state.write_enabled = false;
    options.blend_state.enabled = true;
    options.blend_state.source_factor = BlendFactor::SourceAlpha;
    options.blend_state.destination_factor = BlendFactor::OneMinusSourceAlpha;
    return options;
}

ModelAsset manual_fixture_asset() {
    ModelAsset asset;
    asset.mesh = load_obj_file(fixture_path("opacity_texture_sequence.obj"));
    auto opacity = std::make_shared<const Texture2D>(load_ppm_file(fixture_path("checker.ppm")));

    MaterialDraw warm_first;
    warm_first.range = {0U, 1U};
    warm_first.material_name = "warm";
    warm_first.material = {{1.0F, 0.5F, 0.25F}, 0.5F};
    warm_first.opacity_texture = opacity;

    MaterialDraw cool;
    cool.range = {1U, 1U};
    cool.material_name = "cool";
    cool.material = {{0.25F, 0.5F, 1.0F}, 0.75F};
    cool.opacity_texture = opacity;

    MaterialDraw warm_last = warm_first;
    warm_last.range = {2U, 1U};
    asset.draws = {warm_first, cool, warm_last};
    return asset;
}

void check_sample_storage_equal(
    const Framebuffer& actual,
    const Framebuffer& expected,
    const std::string& context) {
    check(actual.width() == expected.width(), context + " width");
    check(actual.height() == expected.height(), context + " height");
    check(actual.sample_count() == expected.sample_count(), context + " sample count");
    if (actual.width() != expected.width()
        || actual.height() != expected.height()
        || actual.samples_per_pixel() != expected.samples_per_pixel()) {
        return;
    }
    for (std::size_t y = 0U; y < actual.height(); ++y) {
        for (std::size_t x = 0U; x < actual.width(); ++x) {
            for (std::size_t sample = 0U; sample < actual.samples_per_pixel(); ++sample) {
                const Vec3& a = actual.sample_color_at(x, y, sample);
                const Vec3& b = expected.sample_color_at(x, y, sample);
                check(a.x == b.x && a.y == b.y && a.z == b.z,
                      context + " sample RGB");
                const float ad = actual.sample_depth_at(x, y, sample);
                const float bd = expected.sample_depth_at(x, y, sample);
                check(ad == bd || (std::isinf(ad) && std::isinf(bd)),
                      context + " sample depth");
                check(actual.sample_stencil_at(x, y, sample)
                          == expected.sample_stencil_at(x, y, sample),
                      context + " sample stencil");
            }
        }
    }
}

void test_rich_map_d_parsing_and_legacy_strictness() {
    const std::string mapped =
        "newmtl cutout\n"
        "Kd 1 0.5 0.25\n"
        "d 0.6\n"
        "map_d checker.ppm\n";
    const MaterialAssetLibrary library = parse_assets(mapped);
    const auto entry = library.find("cutout");
    check(entry != library.end(), "rich MTL loader accepts map_d material");
    if (entry != library.end()) {
        check(entry->second.opacity_map_filename.has_value(), "rich material retains map_d filename");
        if (entry->second.opacity_map_filename) {
            check(*entry->second.opacity_map_filename == "checker.ppm", "map_d filename is preserved exactly");
        }
        check_near(entry->second.material.opacity, 0.6F, "uniform d remains independent from map_d");
    }

    bool legacy_threw = false;
    try {
        std::istringstream input(mapped);
        (void)load_mtl(input);
    } catch (const MtlParseError&) {
        legacy_threw = true;
    }
    check(legacy_threw, "legacy strict MTL loader still rejects map_d");

    expect_asset_error(
        "map_d checker.ppm\nnewmtl a\nKd 1 1 1\n",
        "map_d before newmtl is rejected");
    expect_asset_error(
        "newmtl a\nKd 1 1 1\nmap_d checker.ppm\nmap_d checker.ppm\n",
        "duplicate map_d is rejected");
    expect_asset_error(
        "newmtl a\nKd 1 1 1\nmap_d ../checker.ppm\n",
        "map_d parent path is rejected");
    expect_asset_error(
        "newmtl a\nKd 1 1 1\nmap_d textures/checker.ppm\n",
        "map_d nested path is rejected");
    expect_asset_error(
        "newmtl a\nKd 1 1 1\nmap_d -clamp on checker.ppm\n",
        "map_d options are rejected by the bounded subset");
}

void test_owned_opacity_texture_dedup_and_lifetime() {
    ModelAsset asset = load_obj_model_asset_file(fixture_path("opacity_texture_sequence.obj"));
    check(asset.draws.size() == 3U, "map_d A-B-A fixture preserves three ordered draw ranges");
    if (asset.draws.size() != 3U) {
        return;
    }
    for (const MaterialDraw& draw : asset.draws) {
        check(!draw.diffuse_texture, "map_d-only material does not synthesize a diffuse texture");
        check(static_cast<bool>(draw.opacity_texture), "each map_d draw owns an opacity texture");
    }
    if (asset.draws[0].opacity_texture && asset.draws[1].opacity_texture && asset.draws[2].opacity_texture) {
        check(asset.draws[0].opacity_texture.get() == asset.draws[1].opacity_texture.get(),
              "materials referencing the same map_d share one decoded texture");
        check(asset.draws[0].opacity_texture.get() == asset.draws[2].opacity_texture.get(),
              "repeated material draw shares the same map_d ownership object");
    }

    std::shared_ptr<const Texture2D> retained = asset.draws[0].opacity_texture;
    asset = {};
    check(static_cast<bool>(retained), "opacity texture lifetime survives source ModelAsset destruction");
    if (retained) {
        const Vec3 sample = retained->sample({0.25F, 0.25F}, {});
        check(std::isfinite(sample.x) && std::isfinite(sample.y) && std::isfinite(sample.z),
              "retained opacity texture remains sampleable");
    }
}

void test_fragment_opacity_scalar_rule() {
    Texture2D opacity(1U, 1U, {{0.0F, 0.3F, 0.6F}});
    TextureBinding binding;
    binding.u_channel = 0U;
    binding.v_channel = 1U;
    binding.opacity_texture = &opacity;

    MaterialState material{{1.0F, 0.0F, 0.0F}, 0.5F};
    BlendState blend;
    blend.enabled = true;
    blend.source_factor = BlendFactor::SourceAlpha;
    blend.destination_factor = BlendFactor::OneMinusSourceAlpha;
    DepthState depth;
    depth.write_enabled = false;

    Triangle triangle{{
        {{-0.8F, -0.8F, 0.0F}, {0.5F, 0.5F}},
        {{0.8F, -0.8F, 0.0F}, {0.5F, 0.5F}},
        {{0.0F, 0.8F, 0.0F}, {0.5F, 0.5F}},
    }};
    Framebuffer framebuffer(33U, 33U);
    framebuffer.clear({0.0F, 0.0F, 1.0F});
    Rasterizer rasterizer(
        framebuffer,
        ColorBinding{99U, 99U, 99U},
        binding,
        {},
        material,
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        depth,
        {},
        {},
        blend);
    rasterizer.draw_triangle(triangle, Mat4::identity());

    // mean(0, 0.3, 0.6) = 0.3; material d=0.5 -> source alpha 0.15.
    check_vec3(framebuffer.color_at(16U, 16U), {0.15F, 0.0F, 0.85F},
               "opacity texture mean multiplies uniform d before source-alpha blending");
}

void render_file_and_manual_equivalence(SampleCount samples) {
    const ModelAsset imported = load_obj_model_asset_file(fixture_path("opacity_texture_sequence.obj"));
    const ModelAsset manual = manual_fixture_asset();
    const ModelRenderOptions options = alpha_options();

    Framebuffer imported_fb(65U, 65U, samples);
    imported_fb.clear({0.1F, 0.2F, 0.3F});
    draw_model_asset(imported_fb, imported, Mat4::identity(), options);

    Framebuffer manual_fb(65U, 65U, samples);
    manual_fb.clear({0.1F, 0.2F, 0.3F});
    draw_model_asset(manual_fb, manual, Mat4::identity(), options);

    check(imported_fb.rgb8() == manual_fb.rgb8(),
          samples == SampleCount::Four
              ? "file-driven map_d matches programmatic opacity texture on 4x target"
              : "file-driven map_d matches programmatic opacity texture on 1x target");
    check(imported_fb.fnv1a64() == manual_fb.fnv1a64(),
          "file-driven and programmatic opacity textures preserve resolved deterministic hash");
    if (samples == SampleCount::Four) {
        check_sample_storage_equal(imported_fb, manual_fb, "file/programmatic 4x map_d equivalence");
    }
}

void test_invalid_opacity_uv_preflight_is_fail_closed() {
    const ModelAsset imported = load_obj_model_asset_file(fixture_path("opacity_texture_sequence.obj"));
    ModelRenderOptions options = alpha_options();
    options.u_channel = 99U;

    Framebuffer framebuffer(33U, 33U, SampleCount::Four);
    framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.75F, 9U);
    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    const float before_depth = framebuffer.sample_depth_at(16U, 16U, 0U);
    const std::uint8_t before_stencil = framebuffer.sample_stencil_at(16U, 16U, 0U);

    bool threw = false;
    try {
        draw_model_asset(framebuffer, imported, Mat4::identity(), options);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(threw, "opacity map with invalid UV channel rejects before submission");
    check(framebuffer.rgb8() == before, "invalid opacity UV preserves all resolved RGB");
    check(framebuffer.sample_depth_at(16U, 16U, 0U) == before_depth,
          "invalid opacity UV preserves sample depth");
    check(framebuffer.sample_stencil_at(16U, 16U, 0U) == before_stencil,
          "invalid opacity UV preserves sample stencil");
}

void test_prepared_list_preserves_opacity_texture_on_4x() {
    ModelAsset source = load_obj_model_asset_file(fixture_path("opacity_texture_sequence.obj"));
    const Texture2D* original_texture = source.draws.front().opacity_texture.get();
    PreparedModelSubmission prepared = prepare_model_asset(source, alpha_options());
    source = {};
    check(prepared.asset().draws.front().opacity_texture.get() == original_texture,
          "prepared plan retains the imported opacity texture ownership object");

    const Mat4 left = Mat4::translation({-0.3F, 0.0F, 0.0F}) * Mat4::scale({0.6F, 0.6F, 1.0F});
    const Mat4 right = Mat4::translation({0.3F, 0.0F, 0.0F}) * Mat4::scale({0.6F, 0.6F, 1.0F});
    const std::array<PreparedModelListEntry, 2U> entries{{
        {&prepared, left},
        {&prepared, right},
    }};

    Framebuffer sequential(65U, 65U, SampleCount::Four);
    sequential.clear({0.05F, 0.1F, 0.15F});
    draw_prepared_model(sequential, prepared, left, Mat4::identity(), Mat4::identity());
    draw_prepared_model(sequential, prepared, right, Mat4::identity(), Mat4::identity());

    Framebuffer listed(65U, 65U, SampleCount::Four);
    listed.clear({0.05F, 0.1F, 0.15F});
    draw_prepared_model_list(
        listed,
        std::span<const PreparedModelListEntry>{entries},
        Mat4::identity(),
        Mat4::identity());

    check(listed.rgb8() == sequential.rgb8(),
          "4x prepared list with map_d resolves byte-identically to sequential prepared submission");
    check_sample_storage_equal(listed, sequential, "4x prepared list map_d sample storage");
}

}  // namespace

int main() {
    try {
        test_rich_map_d_parsing_and_legacy_strictness();
        test_owned_opacity_texture_dedup_and_lifetime();
        test_fragment_opacity_scalar_rule();
        render_file_and_manual_equivalence(SampleCount::One);
        render_file_and_manual_equivalence(SampleCount::Four);
        test_invalid_opacity_uv_preflight_is_fail_closed();
        test_prepared_list_preserves_opacity_texture_on_4x();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " opacity texture test(s) failed\n";
        return 1;
    }
    std::cout << "all opacity texture tests passed\n";
    return 0;
}
