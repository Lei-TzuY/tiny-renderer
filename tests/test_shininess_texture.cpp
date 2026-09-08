#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_fingerprint.hpp"
#include "tiny_renderer/model_inspection.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/mtl_loader.hpp"
#include "tiny_renderer/obj_loader.hpp"
#include "tiny_renderer/rasterizer.hpp"

using namespace tiny_renderer;

namespace {
int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

VaryingPack uv_normal(float u, float v) {
    VaryingPack varyings;
    varyings.count = 5U;
    varyings.values[0] = u;
    varyings.values[1] = v;
    varyings.values[2] = 0.0F;
    varyings.values[3] = 0.0F;
    varyings.values[4] = 1.0F;
    return varyings;
}

ModelAsset shininess_model(
    std::shared_ptr<const Texture2D> texture,
    float uniform_shininess = 32.0F) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.7F, -0.7F, 0.0F}, uv_normal(0.0F, 0.0F)),
        Vertex::with_varyings({0.7F, -0.7F, 0.0F}, uv_normal(1.0F, 0.0F)),
        Vertex::with_varyings({0.0F, 0.7F, 0.0F}, uv_normal(0.5F, 1.0F)),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "glossy";
    draw.material.albedo = {0.0F, 0.0F, 0.0F};
    draw.material.specular = {1.0F, 1.0F, 1.0F};
    draw.material.shininess = uniform_shininess;
    draw.shininess_texture = std::move(texture);
    asset.draws.push_back(std::move(draw));
    return asset;
}

DirectionalLight direct_light(Vec3 viewer) {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {2U, 3U, 4U};
    light.direction_to_light = {0.0F, 0.0F, 1.0F};
    light.ambient = 0.0F;
    light.diffuse = 1.0F;
    light.viewer_position = viewer;
    return light;
}

ModelRenderOptions direct_options(Vec3 viewer = {0.75F, 0.0F, 4.0F}) {
    ModelRenderOptions options;
    options.directional_light = direct_light(viewer);
    return options;
}

Framebuffer render(const ModelAsset& asset, ModelRenderOptions options) {
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return framebuffer;
}

Texture2D checker_environment() {
    std::vector<Vec3> texels;
    texels.reserve(32U);
    for (std::size_t y = 0U; y < 4U; ++y) {
        for (std::size_t x = 0U; x < 8U; ++x) {
            const float value = ((x + y) & 1U) == 0U ? 0.0F : 1.0F;
            texels.push_back({value, 1.0F - value, value * 0.25F});
        }
    }
    return Texture2D(8U, 4U, std::move(texels));
}

ModelRenderOptions reflection_options(Texture2D& environment) {
    ModelRenderOptions options;
    EnvironmentReflectionLight reflection;
    reflection.normal = {2U, 3U, 4U};
    reflection.viewer_position = {0.0F, 0.0F, 4.0F};
    reflection.environment.texture = &environment;
    reflection.environment.sampler.filter = FilterMode::Bilinear;
    reflection.environment.sampler.mip_filter = MipFilterMode::Linear;
    reflection.environment.mip_policy = EnvironmentReflectionMipPolicy::MaterialShininess;
    reflection.environment.angular_footprint_radians = 0.8F;
    options.fixed_lights.environment_reflection = reflection;
    return options;
}

void test_map_ns_import_cache_fingerprint_and_inspection() {
    const auto fixture = std::filesystem::path(TINY_RENDERER_SOURCE_DIR)
        / "tests" / "fixtures" / "shininess_textured.obj";
    const ModelAsset imported = load_obj_model_asset_file(fixture);
    check(imported.draws.size() == 1U, "map_Ns fixture produces one material draw");
    if (imported.draws.empty()) {
        return;
    }
    const MaterialDraw& draw = imported.draws[0];
    check(draw.shininess_texture != nullptr, "map_Ns owns a decoded shininess texture");
    check(draw.specular_texture != nullptr, "fixture also owns map_Ks");
    check(draw.shininess_texture.get() == draw.specular_texture.get(),
          "same linear file shared by map_Ks/map_Ns deduplicates in the owned texture cache");
    check(draw.shininess_texture->source_transfer_function() == TextureTransferFunction::Linear,
          "map_Ns is imported as linear data");

    ModelAsset without = imported;
    const std::uint64_t with_fingerprint = model_asset_fnv1a64(imported);
    without.draws[0].shininess_texture.reset();
    check(model_asset_fnv1a64(without) != with_fingerprint,
          "model fingerprint includes map_Ns semantic content");
    check(inspect_model_asset(imported).find("shininess_texture=1x1") != std::string::npos,
          "asset inspection exposes map_Ns dimensions");

    std::istringstream rich(
        "newmtl x\nKd 1 1 1\nmap_Ns shininess_map.ppm\n");
    const MaterialAssetLibrary library = load_mtl_assets(rich);
    check(library.at("x").shininess_map_filename
              == std::optional<std::string>{"shininess_map.ppm"},
          "rich MTL parser retains map_Ns filename");

    const auto expect_rich_failure = [](const std::string& text) {
        std::istringstream input(text);
        try {
            (void)load_mtl_assets(input);
        } catch (const MtlParseError&) {
            return true;
        }
        return false;
    };
    check(expect_rich_failure(
              "newmtl x\nKd 1 1 1\nmap_Ns shininess_map.ppm\nmap_Ns shininess_map.ppm\n"),
          "duplicate map_Ns is rejected deterministically");
    check(expect_rich_failure(
              "newmtl x\nKd 1 1 1\nmap_Ns ../shininess_map.ppm\n"),
          "map_Ns rejects parent-path traversal");

    std::istringstream legacy(
        "newmtl x\nKd 1 1 1\nmap_Ns shininess_map.ppm\n");
    bool legacy_threw = false;
    try {
        (void)load_mtl(legacy);
    } catch (const MtlParseError&) {
        legacy_threw = true;
    }
    check(legacy_threw,
          "legacy strict MTL loader continues to reject map_Ns directives");
}

void test_map_ns_scalar_mapping_drives_direct_specular() {
    const auto map = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{0.0F, 0.5F, 1.0F}});
    const ModelAsset mapped = shininess_model(map, 7.0F);
    ModelAsset equivalent = mapped;
    equivalent.draws[0].shininess_texture.reset();
    equivalent.draws[0].material.shininess = 500.5F;

    const Framebuffer mapped_fb = render(mapped, direct_options());
    const Framebuffer equivalent_fb = render(equivalent, direct_options());
    check(mapped_fb.rgb8() == equivalent_fb.rgb8(),
          "map_Ns arithmetic-mean RGB maps exactly to equivalent uniform Ns=500.5 direct shading");

    ModelAsset different_uniform = mapped;
    different_uniform.draws[0].material.shininess = 900.0F;
    const Framebuffer different_uniform_fb = render(different_uniform, direct_options());
    check(mapped_fb.rgb8() == different_uniform_fb.rgb8(),
          "present map_Ns replaces the uniform Ns fallback rather than multiplying it");

    ModelAsset fallback = mapped;
    fallback.draws[0].shininess_texture.reset();
    fallback.draws[0].material.shininess = 32.0F;
    const Framebuffer fallback_fb = render(fallback, direct_options());
    check(mapped_fb.rgb8() != fallback_fb.rgb8(),
          "map_Ns changes the direct Blinn-Phong exponent when mapped value differs from uniform Ns");
}

void test_map_ns_and_uniform_ns_share_environment_material_shininess_policy() {
    Texture2D environment = checker_environment();
    const auto zero_map = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{0.0F, 0.0F, 0.0F}});
    const ModelAsset mapped = shininess_model(zero_map, 32.0F);
    ModelAsset equivalent = mapped;
    equivalent.draws[0].shininess_texture.reset();
    equivalent.draws[0].material.shininess = 1.0F;
    ModelAsset fallback = mapped;
    fallback.draws[0].shininess_texture.reset();
    fallback.draws[0].material.shininess = 32.0F;

    const Framebuffer mapped_fb = render(mapped, reflection_options(environment));
    const Framebuffer equivalent_fb = render(equivalent, reflection_options(environment));
    const Framebuffer fallback_fb = render(fallback, reflection_options(environment));
    check(mapped_fb.rgb8() == equivalent_fb.rgb8(),
          "map_Ns resolved exponent and uniform Ns use one environment MaterialShininess footprint rule");
    check(mapped_fb.rgb8() != fallback_fb.rgb8(),
          "per-fragment map_Ns changes material-coupled environment reflection filtering");
}

void test_prepared_ownership_validation_and_list_fail_closed() {
    auto owner = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{0.5F, 0.5F, 0.5F}});
    std::weak_ptr<const Texture2D> retained = owner;
    PreparedModelSubmission prepared = prepare_model_asset(
        shininess_model(owner), direct_options());
    owner.reset();
    check(!retained.expired(), "prepared plan retains map_Ns lifetime");
    Framebuffer retained_fb(65U, 65U);
    draw_prepared_model(
        retained_fb,
        prepared,
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    check(retained_fb.color_at(32U, 32U).x > 0.0F,
          "prepared plan samples retained map_Ns after source owner destruction");

    const auto invalid = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{1.1F, 0.5F, 0.5F}});
    bool invalid_threw = false;
    try {
        (void)prepare_model_asset(shininess_model(invalid));
    } catch (const std::invalid_argument&) {
        invalid_threw = true;
    }
    check(invalid_threw,
          "prepared model rejects out-of-range map_Ns texels");

    ModelAsset first_asset = shininess_model({}, 32.0F);
    first_asset.draws[0].material.albedo = {0.7F, 0.1F, 0.1F};
    first_asset.draws[0].material.specular = {0.0F, 0.0F, 0.0F};
    const PreparedModelSubmission first = prepare_model_asset(first_asset);

    ModelRenderOptions bad_options = direct_options();
    bad_options.u_channel = 99U;
    const PreparedModelSubmission later = prepare_model_asset(
        shininess_model(std::make_shared<const Texture2D>(
            1U, 1U, std::vector<Vec3>{{0.5F, 0.5F, 0.5F}})),
        bad_options);
    const PreparedModelListEntry entries[] = {
        {&first, Mat4::identity()},
        {&later, Mat4::identity()},
    };
    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.13F, 0.17F, 0.19F}, 0.8F, 9U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);
    bool uv_threw = false;
    try {
        draw_prepared_model_list(
            framebuffer, entries, Mat4::identity(), Mat4::identity());
    } catch (const std::out_of_range&) {
        uv_threw = true;
    }
    check(uv_threw,
          "later prepared-list map_Ns invalid UV binding is rejected");
    check(framebuffer.rgb8() == before,
          "later map_Ns UV rejection occurs before earlier list color writes");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "later map_Ns UV rejection occurs before earlier list depth writes");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "later map_Ns UV rejection occurs before earlier list stencil writes");
}

void test_emissive_only_direct_mesh_preflights_all_uv_values() {
    const auto uv = [](float u, float v) {
        VaryingPack p;
        p.count = 2U;
        p.values[0] = u;
        p.values[1] = v;
        return p;
    };
    Mesh mesh;
    mesh.vertices = {
        Vertex::with_varyings({-0.9F, -0.8F, 0.0F}, uv(0.0F, 0.0F)),
        Vertex::with_varyings({-0.1F, -0.8F, 0.0F}, uv(1.0F, 0.0F)),
        Vertex::with_varyings({-0.5F, 0.8F, 0.0F}, uv(0.5F, 1.0F)),
        Vertex::with_varyings({0.1F, -0.8F, 0.0F}, uv(0.0F, 0.0F)),
        Vertex::with_varyings({0.9F, -0.8F, 0.0F}, uv(1.0F, 0.0F)),
        Vertex::with_varyings(
            {0.5F, 0.8F, 0.0F},
            uv(std::numeric_limits<float>::quiet_NaN(), 1.0F)),
    };
    mesh.triangles = {{{0U, 1U, 2U}}, {{3U, 4U, 5U}}};

    Texture2D emissive(
        1U, 1U, std::vector<Vec3>{{1.0F, 1.0F, 1.0F}});
    TextureBinding binding;
    binding.emissive_texture = &emissive;
    MaterialState material;
    material.emissive = {1.0F, 0.0F, 0.0F};
    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.11F, 0.12F, 0.13F}, 0.7F, 4U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(16U, 32U);
    Rasterizer rasterizer(
        framebuffer, {}, binding, {}, material, BaseColorSource::ConstantWhite);

    bool threw = false;
    try {
        rasterizer.draw_mesh(mesh, Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw,
          "emissive-only direct mesh rejects non-finite UV before raster execution");
    check(framebuffer.rgb8() == before,
          "emissive-only later invalid UV cannot partially commit an earlier triangle");
    check(framebuffer.depth_at(16U, 32U) == before_depth,
          "emissive-only invalid UV rejection preserves depth before mutation");
}
}  // namespace

int main() {
    try {
        test_map_ns_import_cache_fingerprint_and_inspection();
        test_map_ns_scalar_mapping_drives_direct_specular();
        test_map_ns_and_uniform_ns_share_environment_material_shininess_policy();
        test_prepared_ownership_validation_and_list_fail_closed();
        test_emissive_only_direct_mesh_preflights_all_uv_values();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " shininess texture test(s) failed\n";
        return 1;
    }
    std::cout << "all shininess texture tests passed\n";
    return 0;
}
