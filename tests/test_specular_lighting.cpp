#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <string>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/model_fingerprint.hpp"
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

void check_near(
    float actual,
    float expected,
    const std::string& message,
    float epsilon = 3.0e-3F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

Vertex normal_vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.0F, 0.0F, 1.0F});
}

Triangle canonical_triangle() {
    return Triangle{{
        normal_vertex({-0.7F, -0.7F, 0.0F}),
        normal_vertex({0.7F, -0.7F, 0.0F}),
        normal_vertex({0.0F, 0.7F, 0.0F}),
    }};
}

DirectionalLight specular_light(const Vec3& viewer_position) {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.direction_to_light = {0.0F, 0.0F, 1.0F};
    light.ambient = 0.0F;
    light.diffuse = 1.0F;
    light.viewer_position = viewer_position;
    return light;
}

MaterialState glossy_material() {
    MaterialState material;
    material.albedo = {0.0F, 0.0F, 0.0F};
    material.opacity = 1.0F;
    material.specular = {1.0F, 1.0F, 1.0F};
    material.shininess = 32.0F;
    return material;
}

float render_center_red(const MaterialState& material, const Vec3& viewer_position) {
    Framebuffer framebuffer(65U, 65U);
    Rasterizer rasterizer(
        framebuffer,
        {},
        {},
        specular_light(viewer_position),
        material,
        BaseColorSource::ConstantWhite);
    rasterizer.draw_triangle(
        canonical_triangle(),
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    return framebuffer.color_at(32U, 32U).x;
}

ModelAsset model_from_triangle(const MaterialState& material) {
    const Triangle triangle = canonical_triangle();
    ModelAsset asset;
    asset.mesh.vertices = {triangle[0], triangle[1], triangle[2]};
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "glossy";
    draw.material = material;
    asset.draws.push_back(std::move(draw));
    return asset;
}

ModelRenderOptions glossy_options(const Vec3& viewer_position) {
    ModelRenderOptions options;
    options.directional_light = specular_light(viewer_position);
    return options;
}

void test_viewer_position_changes_blinn_phong_highlight() {
    const MaterialState material = glossy_material();
    const float aligned = render_center_red(material, {0.0F, 0.0F, 4.0F});
    const float off_axis = render_center_red(material, {4.0F, 0.0F, 4.0F});

    check(aligned > 0.9F,
          "viewer aligned with the reflection axis produces a strong specular highlight");
    check(off_axis < aligned * 0.5F,
          "moving the viewer off axis reduces the Blinn-Phong highlight");
}

void test_zero_specular_preserves_lambert_output() {
    MaterialState material;
    material.albedo = {0.4F, 0.2F, 0.1F};
    material.specular = {0.0F, 0.0F, 0.0F};

    Framebuffer first(65U, 65U);
    Rasterizer first_rasterizer(
        first,
        {},
        {},
        specular_light({0.0F, 0.0F, 4.0F}),
        material,
        BaseColorSource::ConstantWhite);
    first_rasterizer.draw_triangle(
        canonical_triangle(),
        Mat4::identity(), Mat4::identity(), Mat4::identity());

    Framebuffer second(65U, 65U);
    Rasterizer second_rasterizer(
        second,
        {},
        {},
        specular_light({4.0F, 2.0F, 1.0F}),
        material,
        BaseColorSource::ConstantWhite);
    second_rasterizer.draw_triangle(
        canonical_triangle(),
        Mat4::identity(), Mat4::identity(), Mat4::identity());

    check(first.rgb8() == second.rgb8(),
          "zero specular reflectance preserves Lambert output independent of viewer position");
}

void test_bounded_mtl_ks_ns_contract() {
    {
        std::istringstream input(
            "newmtl glossy\n"
            "Kd 0.2 0.3 0.4\n"
            "Ks 0.5 0.6 0.7\n"
            "Ns 64\n");
        const MaterialLibrary library = load_mtl(input);
        const MaterialState& material = library.at("glossy");
        check_near(material.specular.x, 0.5F, "MTL Ks red is imported");
        check_near(material.specular.y, 0.6F, "MTL Ks green is imported");
        check_near(material.specular.z, 0.7F, "MTL Ks blue is imported");
        check_near(material.shininess, 64.0F, "MTL Ns shininess is imported");
    }

    {
        std::istringstream input(
            "newmtl matte\n"
            "Kd 0.2 0.3 0.4\n");
        const MaterialLibrary library = load_mtl(input);
        const MaterialState& material = library.at("matte");
        check(material.specular.x == 0.0F
                  && material.specular.y == 0.0F
                  && material.specular.z == 0.0F,
              "missing Ks preserves zero-specular compatibility");
        check_near(material.shininess, 32.0F,
                   "missing Ns preserves the bounded default exponent");
    }

    const auto expect_failure = [](const std::string& text) {
        std::istringstream input(text);
        try {
            (void)load_mtl(input);
        } catch (const MtlParseError&) {
            return true;
        }
        return false;
    };

    check(expect_failure(
              "newmtl x\nKd 1 1 1\nKs 0.1 0.2 0.3\nKs 0.2 0.3 0.4\n"),
          "duplicate Ks is rejected deterministically");
    check(expect_failure(
              "newmtl x\nKd 1 1 1\nKs 1.1 0 0\n"),
          "out-of-range Ks is rejected");
    check(expect_failure(
              "newmtl x\nKd 1 1 1\nNs 16\nNs 32\n"),
          "duplicate Ns is rejected deterministically");
    check(expect_failure(
              "newmtl x\nKd 1 1 1\nNs 0\n"),
          "shininess below the bounded subset is rejected");
    check(expect_failure(
              "newmtl x\nKd 1 1 1\nNs 1001\n"),
          "shininess above the bounded subset is rejected");
}

void test_file_driven_specular_material_matches_programmatic_state() {
    const std::filesystem::path fixture =
        std::filesystem::path(TINY_RENDERER_SOURCE_DIR)
        / "tests" / "fixtures" / "specular_material.obj";

    ModelAsset imported = load_obj_model_asset_file(fixture);
    check(imported.draws.size() == 1U,
          "specular fixture produces one canonical material draw");
    check_near(imported.draws[0].material.specular.x, 1.0F,
               "file-driven model retains Ks red");
    check_near(imported.draws[0].material.specular.y, 0.5F,
               "file-driven model retains Ks green");
    check_near(imported.draws[0].material.specular.z, 0.25F,
               "file-driven model retains Ks blue");
    check_near(imported.draws[0].material.shininess, 32.0F,
               "file-driven model retains Ns shininess");

    ModelAsset manual = imported;
    manual.draws[0].material = MaterialState{};
    manual.draws[0].material.albedo = {0.0F, 0.0F, 0.0F};
    manual.draws[0].material.specular = {1.0F, 0.5F, 0.25F};
    manual.draws[0].material.shininess = 32.0F;

    ModelRenderOptions options = glossy_options({0.0F, 0.0F, 4.0F});
    options.directional_light.normal = {2U, 3U, 4U};
    Framebuffer imported_framebuffer(65U, 65U);
    draw_model_asset(
        imported_framebuffer,
        imported,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);

    Framebuffer manual_framebuffer(65U, 65U);
    draw_model_asset(
        manual_framebuffer,
        manual,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);

    check(imported_framebuffer.rgb8() == manual_framebuffer.rgb8(),
          "imported Ks/Ns render byte-identically to equivalent programmatic material state");
}

void test_prepared_static_specular_validation() {
    {
        MaterialState invalid = glossy_material();
        invalid.specular.x = 1.1F;
        bool threw = false;
        try {
            (void)prepare_model_asset(model_from_triangle(invalid));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "prepared model construction rejects out-of-range specular reflectance");
    }

    {
        MaterialState invalid = glossy_material();
        invalid.shininess = 0.0F;
        bool threw = false;
        try {
            (void)prepare_model_asset(model_from_triangle(invalid));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "prepared model construction rejects invalid shininess");
    }

    {
        ModelRenderOptions options = glossy_options({
            std::numeric_limits<float>::quiet_NaN(), 0.0F, 4.0F});
        bool threw = false;
        try {
            (void)prepare_model_asset(model_from_triangle(glossy_material()), options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "prepared specular model rejects a non-finite viewer position");
    }
}

void test_prepared_list_preflights_later_bad_world_transform_before_writes() {
    const PreparedModelSubmission prepared = prepare_model_asset(
        model_from_triangle(glossy_material()),
        glossy_options({0.0F, 0.0F, 4.0F}));

    const Mat4 invalid_projective({
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 0.0F,
    });
    const PreparedModelListEntry entries[] = {
        {&prepared, Mat4::identity()},
        {&prepared, invalid_projective},
    };

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 11U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);

    bool threw = false;
    try {
        draw_prepared_model_list(
            framebuffer,
            entries,
            Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw,
          "later prepared-list specular world transform is rejected");
    check(framebuffer.rgb8() == before,
          "later specular transform rejection happens before earlier color writes");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "later specular transform rejection happens before earlier depth writes");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "later specular transform rejection happens before earlier stencil writes");
}

VaryingPack uv_normal_varyings(float u, float v) {
    VaryingPack varyings;
    varyings.count = 5U;
    varyings.values[0] = u;
    varyings.values[1] = v;
    varyings.values[2] = 0.0F;
    varyings.values[3] = 0.0F;
    varyings.values[4] = 1.0F;
    return varyings;
}

ModelAsset specular_textured_model(std::shared_ptr<const Texture2D> texture) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.7F, -0.7F, 0.0F}, uv_normal_varyings(0.0F, 0.0F)),
        Vertex::with_varyings({0.7F, -0.7F, 0.0F}, uv_normal_varyings(1.0F, 0.0F)),
        Vertex::with_varyings({0.0F, 0.7F, 0.0F}, uv_normal_varyings(0.5F, 1.0F)),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "specular-textured";
    draw.material.albedo = {0.0F, 0.0F, 0.0F};
    draw.material.specular = {1.0F, 1.0F, 1.0F};
    draw.material.shininess = 32.0F;
    draw.specular_texture = std::move(texture);
    asset.draws.push_back(std::move(draw));
    return asset;
}

ModelRenderOptions specular_texture_options() {
    ModelRenderOptions options;
    options.directional_light = specular_light({0.0F, 0.0F, 4.0F});
    options.directional_light.normal = {2U, 3U, 4U};
    return options;
}

void test_map_ks_import_and_shared_linear_cache() {
    const std::filesystem::path fixture =
        std::filesystem::path(TINY_RENDERER_SOURCE_DIR)
        / "tests" / "fixtures" / "specular_textured.obj";
    const ModelAsset imported = load_obj_model_asset_file(fixture);
    check(imported.draws.size() == 1U,
          "map_Ks fixture produces one material draw");
    if (imported.draws.empty()) {
        return;
    }
    const MaterialDraw& draw = imported.draws[0];
    check(draw.specular_texture != nullptr,
          "map_Ks fixture owns a decoded specular texture");
    check(draw.opacity_texture != nullptr,
          "fixture owns its sibling linear opacity texture");
    check(draw.specular_texture.get() == draw.opacity_texture.get(),
          "linear map_Ks and map_d references share the decoded texture cache");
    if (draw.specular_texture) {
        check(draw.specular_texture->source_transfer_function()
                  == TextureTransferFunction::Linear,
              "map_Ks enters the linear material texture domain");
    }

    std::istringstream duplicate(
        "newmtl x\nKd 1 1 1\nmap_Ks checker.ppm\nmap_Ks checker.ppm\n");
    bool duplicate_threw = false;
    try {
        (void)load_mtl_assets(duplicate);
    } catch (const MtlParseError&) {
        duplicate_threw = true;
    }
    check(duplicate_threw,
          "duplicate map_Ks is rejected deterministically");

    std::istringstream legacy(
        "newmtl x\nKd 1 1 1\nmap_Ks checker.ppm\n");
    bool legacy_threw = false;
    try {
        (void)load_mtl(legacy);
    } catch (const MtlParseError&) {
        legacy_threw = true;
    }
    check(legacy_threw,
          "legacy strict MTL loader continues to reject map_Ks directives");
}

void test_specular_texture_modulates_direct_and_environment_specular() {
    const auto specular_texture = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{0.25F, 0.5F, 1.0F}});
    const ModelAsset asset = specular_textured_model(specular_texture);

    Framebuffer direct(65U, 65U);
    draw_model_asset(
        direct,
        asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        specular_texture_options());
    const Vec3 direct_center = direct.color_at(32U, 32U);
    check_near(direct_center.x, 0.25F,
               "map_Ks modulates direct specular red once");
    check_near(direct_center.y, 0.5F,
               "map_Ks modulates direct specular green once");
    check_near(direct_center.z, 1.0F,
               "map_Ks modulates direct specular blue once");

    Texture2D environment(
        1U, 1U, std::vector<Vec3>{{0.8F, 0.4F, 0.2F}});
    ModelRenderOptions reflection_options;
    EnvironmentReflectionLight reflection;
    reflection.normal = {2U, 3U, 4U};
    reflection.viewer_position = {0.0F, 0.0F, 4.0F};
    reflection.environment.texture = &environment;
    reflection_options.fixed_lights.environment_reflection = reflection;

    Framebuffer reflected(65U, 65U);
    draw_model_asset(
        reflected,
        asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        reflection_options);
    const Vec3 reflected_center = reflected.color_at(32U, 32U);
    check_near(reflected_center.x, 0.2F,
               "map_Ks resolved reflectance feeds environment reflection red");
    check_near(reflected_center.y, 0.2F,
               "map_Ks resolved reflectance feeds environment reflection green");
    check_near(reflected_center.z, 0.2F,
               "map_Ks resolved reflectance feeds environment reflection blue");
}

void test_specular_texture_prepared_lifetime_sampler_and_list_preflight() {
    auto owner = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{0.4F, 0.6F, 0.8F}});
    std::weak_ptr<const Texture2D> retained = owner;
    ModelAsset source = specular_textured_model(owner);
    const std::uint64_t with_texture_fingerprint = model_asset_fnv1a64(source);
    ModelAsset without_texture = source;
    without_texture.draws[0].specular_texture.reset();
    check(model_asset_fnv1a64(without_texture) != with_texture_fingerprint,
          "model fingerprint includes map_Ks semantic content");

    PreparedModelSubmission prepared = prepare_model_asset(
        std::move(source), specular_texture_options());
    owner.reset();
    check(!retained.expired(),
          "prepared plan retains map_Ks ownership after source lifetime ends");

    Framebuffer prepared_framebuffer(65U, 65U);
    draw_prepared_model(
        prepared_framebuffer,
        prepared,
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    const Vec3 center = prepared_framebuffer.color_at(32U, 32U);
    check_near(center.x, 0.4F,
               "prepared plan samples retained map_Ks red");
    check_near(center.y, 0.6F,
               "prepared plan samples retained map_Ks green");
    check_near(center.z, 0.8F,
               "prepared plan samples retained map_Ks blue");

    ModelRenderOptions invalid_sampler;
    invalid_sampler.sampler.max_anisotropy = 2U;
    bool sampler_threw = false;
    try {
        (void)prepare_model_asset(
            specular_textured_model(std::make_shared<const Texture2D>(
                1U, 1U, std::vector<Vec3>{{1.0F, 1.0F, 1.0F}})),
            invalid_sampler);
    } catch (const std::invalid_argument&) {
        sampler_threw = true;
    }
    check(sampler_threw,
          "specular-only texture participates in shared sampler validation");

    const auto invalid_texels = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{1.1F, 0.5F, 0.5F}});
    bool texel_threw = false;
    try {
        (void)prepare_model_asset(specular_textured_model(invalid_texels));
    } catch (const std::invalid_argument&) {
        texel_threw = true;
    }
    check(texel_threw,
          "prepared model rejects out-of-range map_Ks reflectance before execution");

    MaterialState visible_material;
    visible_material.albedo = {0.7F, 0.1F, 0.1F};
    const PreparedModelSubmission first = prepare_model_asset(
        model_from_triangle(visible_material));

    ModelRenderOptions invalid_uv;
    invalid_uv.u_channel = 99U;
    const PreparedModelSubmission later = prepare_model_asset(
        specular_textured_model(std::make_shared<const Texture2D>(
            1U, 1U, std::vector<Vec3>{{1.0F, 1.0F, 1.0F}})),
        invalid_uv);
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
            framebuffer,
            entries,
            Mat4::identity(), Mat4::identity());
    } catch (const std::out_of_range&) {
        uv_threw = true;
    }
    check(uv_threw,
          "later prepared-list map_Ks invalid UV binding is rejected");
    check(framebuffer.rgb8() == before,
          "later map_Ks UV rejection occurs before earlier list color writes");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "later map_Ks UV rejection occurs before earlier list depth writes");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "later map_Ks UV rejection occurs before earlier list stencil writes");
}
}  // namespace

int main() {
    try {
        test_viewer_position_changes_blinn_phong_highlight();
        test_zero_specular_preserves_lambert_output();
        test_bounded_mtl_ks_ns_contract();
        test_file_driven_specular_material_matches_programmatic_state();
        test_prepared_static_specular_validation();
        test_prepared_list_preflights_later_bad_world_transform_before_writes();
        test_map_ks_import_and_shared_linear_cache();
        test_specular_texture_modulates_direct_and_environment_specular();
        test_specular_texture_prepared_lifetime_sampler_and_list_preflight();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " specular lighting test(s) failed\n";
        return 1;
    }
    std::cout << "all specular lighting tests passed\n";
    return 0;
}
