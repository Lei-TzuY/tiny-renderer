#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/fragment_program.hpp"
#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/model_fingerprint.hpp"
#include "tiny_renderer/model_inspection.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/mtl_loader.hpp"
#include "tiny_renderer/obj_loader.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 3.0e-3F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

void check_color(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check_near(actual.x, expected.x, message + " red");
    check_near(actual.y, expected.y, message + " green");
    check_near(actual.z, expected.z, message + " blue");
}

std::filesystem::path fixture_path(const char* name) {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for emissive texture tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

VaryingPack uv_varyings(float u, float v) {
    VaryingPack varyings;
    varyings.count = 2U;
    varyings.values[0] = u;
    varyings.values[1] = v;
    return varyings;
}

ModelAsset emissive_model(
    std::shared_ptr<const Texture2D> texture,
    Vec3 emissive = {0.5F, 0.25F, 1.0F},
    float u_scale = 1.0F,
    float v_scale = 1.0F) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.8F, -0.8F, 0.0F}, uv_varyings(0.0F, 0.0F)),
        Vertex::with_varyings({0.8F, -0.8F, 0.0F}, uv_varyings(u_scale, 0.0F)),
        Vertex::with_varyings({0.0F, 0.8F, 0.0F}, uv_varyings(0.0F, v_scale)),
    };
    asset.mesh.triangles.push_back({0U, 1U, 2U});
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "emissive";
    draw.material.albedo = {0.0F, 0.0F, 0.0F};
    draw.material.emissive = emissive;
    draw.emissive_texture = std::move(texture);
    asset.draws.push_back(std::move(draw));
    return asset;
}

ModelAsset diffuse_model(
    std::shared_ptr<const Texture2D> texture,
    float u_scale,
    float v_scale) {
    ModelAsset asset = emissive_model({}, {0.0F, 0.0F, 0.0F}, u_scale, v_scale);
    asset.draws[0].material.albedo = {1.0F, 1.0F, 1.0F};
    asset.draws[0].diffuse_texture = std::move(texture);
    return asset;
}

Framebuffer render_model(const ModelAsset& asset, ModelRenderOptions options = {}) {
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(framebuffer, asset, Mat4::identity(), options);
    return framebuffer;
}

class HalfFixedProgram final : public FragmentProgram {
public:
    FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
        return {input.fixed_rgb * 0.5F, input.fixed_opacity, false};
    }
};

void test_programmatic_hdr_emission_and_fragment_program_order() {
    auto texture = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{2.0F, 0.5F, 0.25F}});
    check(texture->texels_nonnegative(),
          "finite HDR emissive texture records the non-negative invariant");
    check(!texture->texels_within_unit_range(),
          "HDR emissive texture may intentionally exceed unit reflectance range");

    const ModelAsset asset = emissive_model(texture);
    const Framebuffer fixed = render_model(asset);
    check_color(fixed.color_at(32U, 32U), {1.0F, 0.125F, 0.25F},
                "map_Ke multiplies bounded Ke component-wise before fixed output");

    ModelRenderOptions options;
    options.fragment_program = std::make_shared<const HalfFixedProgram>();
    const Framebuffer programmed = render_model(asset, options);
    check_color(programmed.color_at(32U, 32U), {0.5F, 0.0625F, 0.125F},
                "fragment program observes fixed RGB after emissive texture contribution");
}

void test_rich_mtl_map_ke_contract() {
    {
        std::istringstream input(
            "newmtl glow\n"
            "Kd 0 0 0\n"
            "Ke 0.5 0.25 1\n"
            "map_Ke emissive_map.ppm\n");
        const MaterialAssetLibrary library = load_mtl_assets(input);
        check(library.at("glow").emissive_map_filename == std::optional<std::string>{"emissive_map.ppm"},
              "rich MTL captures one map_Ke filename");
    }

    const auto rich_failure = [](const std::string& text) {
        std::istringstream input(text);
        try {
            (void)load_mtl_assets(input);
        } catch (const MtlParseError&) {
            return true;
        }
        return false;
    };
    check(rich_failure(
              "newmtl glow\nKd 0 0 0\nmap_Ke emissive_map.ppm\nmap_Ke emissive_map.ppm\n"),
          "duplicate map_Ke is rejected deterministically");
    check(rich_failure(
              "newmtl glow\nKd 0 0 0\nmap_Ke ../escape.pfm\n"),
          "map_Ke rejects parent-path escape");
    check(rich_failure(
              "newmtl glow\nKd 0 0 0\nmap_Ke emissive_map.ppm option\n"),
          "map_Ke rejects options in the bounded subset");

    std::istringstream legacy(
        "newmtl glow\nKd 0 0 0\nmap_Ke emissive_map.ppm\n");
    bool legacy_threw = false;
    try {
        (void)load_mtl(legacy);
    } catch (const MtlParseError&) {
        legacy_threw = true;
    }
    check(legacy_threw,
          "legacy strict load_mtl continues to reject mapped-asset directives");
}

void test_file_driven_map_ke_cache_equivalence_and_inspection() {
    const ModelAsset imported = load_obj_model_asset_file(
        fixture_path("emissive_texture_sequence.obj"));
    check(imported.draws.size() == 3U,
          "file-driven emissive fixture preserves A-B-A draw order");
    if (imported.draws.size() != 3U) {
        return;
    }
    check(imported.draws[0].emissive_texture != nullptr
              && imported.draws[1].emissive_texture != nullptr
              && imported.draws[2].emissive_texture != nullptr,
          "every mapped emissive draw owns its texture");
    check(imported.draws[0].emissive_texture == imported.draws[1].emissive_texture
              && imported.draws[0].emissive_texture == imported.draws[2].emissive_texture,
          "same-file linear map_Ke roles share decoded cache ownership");

    ModelAsset manual = imported;
    auto programmatic = std::make_shared<const Texture2D>(
        1U,
        1U,
        std::vector<Vec3>{{128.0F / 255.0F, 64.0F / 255.0F, 1.0F}});
    for (MaterialDraw& draw : manual.draws) {
        draw.emissive_texture = programmatic;
    }

    const Framebuffer imported_framebuffer = render_model(imported);
    const Framebuffer manual_framebuffer = render_model(manual);
    check(imported_framebuffer.rgb8() == manual_framebuffer.rgb8(),
          "file-driven map_Ke renders byte-identically to equivalent programmatic texture content");
    check(imported_framebuffer.fnv1a64() == manual_framebuffer.fnv1a64(),
          "file-driven/programmatic map_Ke framebuffer hashes match");

    const std::uint64_t mapped_hash = model_asset_fnv1a64(imported);
    ModelAsset unmapped = imported;
    for (MaterialDraw& draw : unmapped.draws) {
        draw.emissive_texture.reset();
    }
    check(mapped_hash != model_asset_fnv1a64(unmapped),
          "emissive texture content participates in model fingerprint only when mapped");
    ModelAsset deep_copy = imported;
    for (MaterialDraw& draw : deep_copy.draws) {
        draw.emissive_texture = programmatic;
    }
    check(mapped_hash == model_asset_fnv1a64(deep_copy),
          "emissive texture fingerprint depends on content rather than shared_ptr identity");

    const std::string summary = inspect_model_asset(imported);
    check(summary.find("draw[0].emissive_texture=1x1\n") != std::string::npos,
          "asset inspection exposes map_Ke dimensions");
}

void test_file_driven_hdr_pfm_preserves_radiance_above_one() {
    const ModelAsset asset = load_obj_model_asset_file(fixture_path("emissive_hdr.obj"));
    check(asset.draws.size() == 1U && asset.draws[0].emissive_texture != nullptr,
          "map_Ke PFM fixture owns one emissive texture");
    if (asset.draws.empty() || !asset.draws[0].emissive_texture) {
        return;
    }
    const Texture2D& texture = *asset.draws[0].emissive_texture;
    check(texture.texels_nonnegative(),
          "linear PFM emissive radiance satisfies non-negative resource contract");
    check(!texture.texels_within_unit_range(),
          "linear PFM map_Ke preserves HDR components above one");
    check_color(texture.texel(0U, 0U), {2.0F, 0.5F, 4.0F},
                "PFM map_Ke preserves source linear float texel exactly");

    const Framebuffer framebuffer = render_model(asset);
    check_color(framebuffer.color_at(32U, 32U), {1.0F, 0.5F, 1.0F},
                "HDR PFM map_Ke modulates bounded Ke without unit-range clipping");
}

void test_emissive_role_reuses_mip_anisotropic_gradient_sampler() {
    std::vector<Vec3> texels;
    texels.reserve(16U);
    for (std::size_t y = 0U; y < 4U; ++y) {
        for (std::size_t x = 0U; x < 4U; ++x) {
            texels.push_back({
                static_cast<float>(x) / 3.0F,
                static_cast<float>(y) / 3.0F,
                static_cast<float>(x + y) / 6.0F,
            });
        }
    }
    auto texture = std::make_shared<const Texture2D>(4U, 4U, texels);

    ModelRenderOptions options;
    options.sampler.address_u = AddressMode::Repeat;
    options.sampler.address_v = AddressMode::Repeat;
    options.sampler.filter = FilterMode::Bilinear;
    options.sampler.mip_filter = MipFilterMode::Linear;
    options.sampler.max_anisotropy = 4U;

    const ModelAsset emissive = emissive_model(texture, {1.0F, 1.0F, 1.0F}, 24.0F, 1.0F);
    const ModelAsset diffuse = diffuse_model(texture, 24.0F, 1.0F);
    const Framebuffer emissive_framebuffer = render_model(emissive, options);
    const Framebuffer diffuse_framebuffer = render_model(diffuse, options);
    check(emissive_framebuffer.rgb8() == diffuse_framebuffer.rgb8(),
          "map_Ke reuses the same mip/anisotropic gradient sampler as diffuse texture lookup");
}

void test_invalid_emissive_resource_and_later_list_uv_fail_before_write() {
    auto negative = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{-0.1F, 0.5F, 1.0F}});
    check(!negative->texels_nonnegative(),
          "finite negative linear texture remains constructible but records invalid emissive domain");

    {
        Framebuffer framebuffer(65U, 65U);
        framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 7U);
        const auto before = framebuffer.rgb8();
        TextureBinding binding;
        binding.emissive_texture = negative.get();
        MaterialState material;
        material.albedo = {0.0F, 0.0F, 0.0F};
        material.emissive = {1.0F, 1.0F, 1.0F};
        Rasterizer rasterizer(
            framebuffer, {}, binding, {}, material, BaseColorSource::ConstantWhite);
        bool threw = false;
        try {
            const ModelAsset source = emissive_model(negative, material.emissive);
            Triangle triangle{{
                source.mesh.vertices[0], source.mesh.vertices[1], source.mesh.vertices[2],
            }};
            rasterizer.draw_triangle(triangle, Mat4::identity());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "direct raster rejects negative emissive texture before shading");
        check(framebuffer.rgb8() == before,
              "direct negative emissive texture rejection precedes framebuffer mutation");
    }

    bool prepare_threw = false;
    try {
        (void)prepare_model_asset(emissive_model(negative));
    } catch (const std::invalid_argument&) {
        prepare_threw = true;
    }
    check(prepare_threw,
          "prepared model construction rejects negative emissive texture resources");

    auto valid_texture = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{1.0F, 1.0F, 1.0F}});
    const PreparedModelSubmission valid = prepare_model_asset(emissive_model(valid_texture));
    ModelAsset invalid_asset = emissive_model(valid_texture);
    invalid_asset.mesh.vertices[0].varyings.values[0] =
        std::numeric_limits<float>::quiet_NaN();
    const PreparedModelSubmission invalid = prepare_model_asset(std::move(invalid_asset));
    const PreparedModelListEntry entries[] = {
        {&valid, Mat4::identity()},
        {&invalid, Mat4::identity()},
    };

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.15F, 0.25F, 0.35F}, 0.8F, 9U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);
    bool list_threw = false;
    try {
        draw_prepared_model_list(
            framebuffer,
            std::span<const PreparedModelListEntry>{entries},
            Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        list_threw = true;
    }
    check(list_threw,
          "later prepared-list map_Ke invalid UV is rejected");
    check(framebuffer.rgb8() == before,
          "later map_Ke UV rejection happens before earlier list color writes");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "later map_Ke UV rejection happens before earlier list depth writes");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "later map_Ke UV rejection happens before earlier list stencil writes");
}

void test_prepared_plan_retains_emissive_texture_lifetime() {
    std::weak_ptr<const Texture2D> weak;
    std::optional<PreparedModelSubmission> prepared;
    {
        ModelAsset source = load_obj_model_asset_file(
            fixture_path("emissive_texture_sequence.obj"));
        weak = source.draws[0].emissive_texture;
        prepared.emplace(prepare_model_asset(std::move(source)));
    }
    check(!weak.expired(),
          "prepared model retains owned map_Ke texture after source asset destruction");
    check(prepared->asset().draws[0].emissive_texture != nullptr,
          "prepared model exposes retained emissive texture ownership");

    Framebuffer framebuffer(65U, 65U);
    draw_prepared_model(framebuffer, *prepared, Mat4::identity());
    check(framebuffer.color_at(32U, 32U).x > 0.0F,
          "prepared retained map_Ke remains executable after source lifetime ends");
}

}  // namespace

int main() {
    try {
        test_programmatic_hdr_emission_and_fragment_program_order();
        test_rich_mtl_map_ke_contract();
        test_file_driven_map_ke_cache_equivalence_and_inspection();
        test_file_driven_hdr_pfm_preserves_radiance_above_one();
        test_emissive_role_reuses_mip_anisotropic_gradient_sampler();
        test_invalid_emissive_resource_and_later_list_uv_fail_before_write();
        test_prepared_plan_retains_emissive_texture_lifetime();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " emissive texture test(s) failed\n";
        return 1;
    }
    std::cout << "all emissive texture tests passed\n";
    return 0;
}
