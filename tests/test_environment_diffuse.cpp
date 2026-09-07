#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/environment_lighting.hpp"
#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/image_loader.hpp"
#include "tiny_renderer/model_renderer.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check_near(float actual, float expected, const std::string& message, float epsilon = 7.0e-3F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

void check_vec3_near(
    const Vec3& actual,
    const Vec3& expected,
    const std::string& message,
    float epsilon = 7.0e-3F) {
    check_near(actual.x, expected.x, message + " red", epsilon);
    check_near(actual.y, expected.y, message + " green", epsilon);
    check_near(actual.z, expected.z, message + " blue", epsilon);
}

void check_vec3_different(
    const Vec3& a,
    const Vec3& b,
    const std::string& message,
    float minimum_delta = 0.1F) {
    const float delta = std::fabs(a.x - b.x)
        + std::fabs(a.y - b.y)
        + std::fabs(a.z - b.z);
    check(delta > minimum_delta, message);
}

EnvironmentDiffuseState environment_state(
    const Texture2D& texture,
    float intensity = 1.0F,
    float yaw = 0.0F) {
    EnvironmentDiffuseState state;
    state.texture = &texture;
    state.sampler.address_u = AddressMode::Repeat;
    state.sampler.address_v = AddressMode::Clamp;
    state.sampler.filter = FilterMode::Bilinear;
    state.sampler.mip_filter = MipFilterMode::Disabled;
    state.intensity = intensity;
    state.yaw_radians = yaw;
    return state;
}

EnvironmentReflectionState reflection_state(
    const Texture2D& texture,
    float intensity = 1.0F,
    float yaw = 0.0F) {
    EnvironmentReflectionState state;
    state.texture = &texture;
    state.sampler.address_u = AddressMode::Repeat;
    state.sampler.address_v = AddressMode::Clamp;
    state.sampler.filter = FilterMode::Nearest;
    state.sampler.mip_filter = MipFilterMode::Disabled;
    state.intensity = intensity;
    state.yaw_radians = yaw;
    return state;
}

EnvironmentDiffuseLight environment_light(
    const Texture2D& texture,
    float intensity = 1.0F,
    float yaw = 0.0F) {
    EnvironmentDiffuseLight light;
    light.normal = {0U, 1U, 2U};
    light.environment = environment_state(texture, intensity, yaw);
    return light;
}

EnvironmentReflectionLight reflection_light(
    const Texture2D& texture,
    const Vec3& viewer_position = {0.0F, 0.0F, 4.0F},
    float intensity = 1.0F,
    float yaw = 0.0F) {
    EnvironmentReflectionLight light;
    light.normal = {0U, 1U, 2U};
    light.viewer_position = viewer_position;
    light.environment = reflection_state(texture, intensity, yaw);
    return light;
}

Vertex normal_vertex(
    const Vec3& position,
    const Vec3& normal = {0.0F, 0.0F, 1.0F}) {
    return Vertex::with_varyings(
        position,
        VaryingPack{normal.x, normal.y, normal.z});
}

ModelAsset model_from_triangle(
    Vec3 albedo = {1.0F, 1.0F, 1.0F},
    Vec3 specular = {0.0F, 0.0F, 0.0F},
    Vec3 normal = {0.0F, 0.0F, 1.0F}) {
    ModelAsset asset;
    asset.mesh.vertices = {
        normal_vertex({-0.8F, -0.8F, 0.0F}, normal),
        normal_vertex({0.8F, -0.8F, 0.0F}, normal),
        normal_vertex({0.0F, 0.8F, 0.0F}, normal),
    };
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "environment";
    draw.material.albedo = albedo;
    draw.material.specular = specular;
    asset.draws.push_back(draw);
    return asset;
}

DirectionalLight directional(float diffuse) {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.direction_to_light = {0.0F, 0.0F, 1.0F};
    light.ambient = 0.0F;
    light.diffuse = diffuse;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    return light;
}

FixedLight fixed_directional(float diffuse) {
    FixedLight result;
    result.type = FixedLightType::Directional;
    result.directional = directional(diffuse);
    return result;
}

Framebuffer render_environment_model(
    const Texture2D& texture,
    SampleCount samples,
    Vec3 albedo = {1.0F, 1.0F, 1.0F},
    float intensity = 1.0F) {
    ModelRenderOptions options;
    options.fixed_lights.environment_diffuse = environment_light(texture, intensity);
    Framebuffer framebuffer(65U, 65U, samples);
    draw_model_asset(
        framebuffer,
        model_from_triangle(albedo),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return framebuffer;
}

Framebuffer render_reflection_model(
    const Texture2D& texture,
    SampleCount samples,
    const Vec3& viewer_position,
    const Vec3& albedo,
    const Vec3& specular,
    const Vec3& normal = {0.0F, 0.0F, 1.0F},
    float intensity = 1.0F,
    float yaw = 0.0F) {
    ModelRenderOptions options;
    options.fixed_lights.environment_reflection =
        reflection_light(texture, viewer_position, intensity, yaw);
    Framebuffer framebuffer(65U, 65U, samples);
    draw_model_asset(
        framebuffer,
        model_from_triangle(albedo, specular, normal),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return framebuffer;
}

void test_constant_environment_has_analytic_lambert_result() {
    const Texture2D environment(1U, 1U, {{2.0F, 4.0F, 6.0F}});
    const EnvironmentDiffuseState state = environment_state(environment, 0.5F);

    const Vec3 irradiance = diffuse_environment_irradiance(state, {0.2F, 0.9F, -0.3F});
    check_vec3_near(
        irradiance,
        {kPi * 1.0F, kPi * 2.0F, kPi * 3.0F},
        "constant environment integrates to pi times scaled radiance",
        1.5e-2F);

    const Vec3 lambert = diffuse_environment_lambert_factor(state, {-0.4F, 0.1F, 0.8F});
    check_vec3_near(
        lambert,
        {1.0F, 2.0F, 3.0F},
        "Lambert factor divides irradiance by pi for an exact constant-environment result",
        5.0e-3F);
}

void test_directional_environment_and_yaw_are_observable_and_deterministic() {
    std::vector<Vec3> texels(8U * 4U, {0.0F, 0.0F, 0.0F});
    for (std::size_t y = 0U; y < 4U; ++y) {
        texels[y * 8U + 0U] = {8.0F, 1.0F, 0.0F};
        texels[y * 8U + 1U] = {4.0F, 0.5F, 0.0F};
    }
    const Texture2D environment(8U, 4U, texels);

    EnvironmentDiffuseState state = environment_state(environment);
    const Vec3 zero_yaw = diffuse_environment_lambert_factor(state, {0.0F, 0.0F, 1.0F});
    const Vec3 repeat = diffuse_environment_lambert_factor(state, {0.0F, 0.0F, 1.0F});
    check_vec3_near(repeat, zero_yaw, "repeated hemisphere integration is deterministic", 1.0e-6F);

    state.yaw_radians = kPi * 0.5F;
    const Vec3 rotated = diffuse_environment_lambert_factor(state, {0.0F, 0.0F, 1.0F});
    check(
        std::fabs(rotated.x - zero_yaw.x) > 0.1F,
        "environment yaw changes a directional/high-contrast diffuse integral");
}

void test_environment_only_raster_path_on_1x_and_4x() {
    const Texture2D environment(1U, 1U, {{0.2F, 0.4F, 0.6F}});
    const Vec3 albedo{0.5F, 0.25F, 1.0F};
    const Vec3 expected{0.1F, 0.1F, 0.6F};

    const Framebuffer single = render_environment_model(
        environment, SampleCount::One, albedo);
    check_vec3_near(
        single.color_at(32U, 32U),
        expected,
        "environment-only lighting uses the existing material/raster path on 1x");

    const Framebuffer four = render_environment_model(
        environment, SampleCount::Four, albedo);
    check_vec3_near(
        four.color_at(32U, 32U),
        expected,
        "environment-only lighting resolves identically at a fully covered 4x sample set");
    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        check_vec3_near(
            four.sample_color_at(32U, 32U, sample),
            expected,
            "environment diffuse shading executes at each covered 4x sample");
    }
}

void test_environment_composes_with_fixed_directional_light() {
    const Texture2D environment(1U, 1U, {{0.25F, 0.25F, 0.25F}});
    ModelRenderOptions options;
    options.fixed_lights.count = 1U;
    options.fixed_lights.lights[0] = fixed_directional(0.5F);
    options.fixed_lights.environment_diffuse = environment_light(environment);

    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        model_from_triangle(),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    check_vec3_near(
        framebuffer.color_at(32U, 32U),
        {0.75F, 0.75F, 0.75F},
        "environment diffuse contribution composes additively with fixed directional lighting");
}

void test_imported_and_programmatic_hdr_environment_equivalence() {
    const std::filesystem::path source =
        std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests/fixtures/environment_hdr.pfm";
    const Texture2D imported = load_texture_image_file(source);
    std::vector<Vec3> texels;
    texels.reserve(imported.width() * imported.height());
    for (std::size_t y = 0U; y < imported.height(); ++y) {
        for (std::size_t x = 0U; x < imported.width(); ++x) {
            texels.push_back(imported.texel(x, y));
        }
    }
    const Texture2D programmatic(imported.width(), imported.height(), texels);

    const EnvironmentDiffuseState imported_state = environment_state(imported, 1.25F, 0.3F);
    const EnvironmentDiffuseState programmatic_state = environment_state(programmatic, 1.25F, 0.3F);
    check_vec3_near(
        diffuse_environment_lambert_factor(imported_state, {0.3F, 0.8F, -0.2F}),
        diffuse_environment_lambert_factor(programmatic_state, {0.3F, 0.8F, -0.2F}),
        "PFM-imported and equivalent programmatic HDR environments produce equal irradiance",
        1.0e-6F);
}

void test_prepared_and_list_equivalence() {
    const Texture2D environment(1U, 1U, {{0.3F, 0.2F, 0.1F}});
    const ModelAsset asset = model_from_triangle({0.5F, 1.0F, 0.25F});
    ModelRenderOptions options;
    options.fixed_lights.environment_diffuse = environment_light(environment, 1.5F);
    const PreparedModelSubmission prepared = prepare_model_asset(asset, options);

    Framebuffer direct(65U, 65U, SampleCount::Four);
    draw_model_asset(
        direct, asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(), options);

    Framebuffer listed(65U, 65U, SampleCount::Four);
    const PreparedModelListEntry entries[] = {{&prepared, Mat4::identity()}};
    draw_prepared_model_list(listed, entries, Mat4::identity(), Mat4::identity());
    check(
        listed.rgb8() == direct.rgb8(),
        "prepared-list environment lighting is byte-equivalent to direct model execution");
    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        check_vec3_near(
            listed.sample_color_at(32U, 32U, sample),
            direct.sample_color_at(32U, 32U, sample),
            "prepared-list environment lighting preserves exact 4x sample shading",
            1.0e-6F);
    }
}

void test_invalid_environment_state_fails_before_framebuffer_mutation() {
    const Texture2D negative(1U, 1U, {{-1.0F, 0.2F, 0.3F}});
    ModelRenderOptions options;
    options.fixed_lights.environment_diffuse = environment_light(negative);

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.7F, 11U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);

    bool threw = false;
    try {
        draw_model_asset(
            framebuffer,
            model_from_triangle(),
            Mat4::identity(), Mat4::identity(), Mat4::identity(),
            options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "negative environment radiance is rejected before execution");
    check(framebuffer.rgb8() == before, "invalid environment state leaves color untouched");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "invalid environment state leaves depth untouched");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "invalid environment state leaves stencil untouched");

    const Texture2D srgb(
        1U, 1U, {{0.5F, 0.5F, 0.5F}}, TextureTransferFunction::Srgb);
    EnvironmentDiffuseState invalid_domain = environment_state(srgb);
    bool domain_threw = false;
    try {
        validate_environment_diffuse_state(invalid_domain);
    } catch (const std::invalid_argument&) {
        domain_threw = true;
    }
    check(domain_threw, "environment diffuse lighting rejects non-linear texture domains");
}

void test_constant_environment_reflection_is_specular_only_on_1x_and_4x() {
    const Texture2D environment(1U, 1U, {{2.0F, 4.0F, 6.0F}});
    const Vec3 albedo{0.1F, 0.7F, 0.3F};
    const Vec3 specular{0.25F, 0.5F, 0.75F};
    const Vec3 expected{0.5F, 2.0F, 4.5F};

    const Framebuffer single = render_reflection_model(
        environment, SampleCount::One, {0.0F, 0.0F, 4.0F}, albedo, specular);
    check_vec3_near(
        single.color_at(32U, 32U),
        expected,
        "constant mirror reflection multiplies HDR radiance by material specular without diffuse albedo");

    const Framebuffer four = render_reflection_model(
        environment, SampleCount::Four, {0.0F, 0.0F, 4.0F}, albedo, specular);
    check_vec3_near(
        four.color_at(32U, 32U),
        expected,
        "constant mirror reflection resolves identically on a fully covered 4x pixel");
    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        check_vec3_near(
            four.sample_color_at(32U, 32U, sample),
            expected,
            "environment reflection shades every covered 4x sample");
    }
}

void test_reflection_lookup_depends_on_view_normal_and_yaw() {
    const Texture2D environment(
        4U,
        1U,
        {
            {8.0F, 0.0F, 0.0F},
            {0.0F, 8.0F, 0.0F},
            {0.0F, 0.0F, 8.0F},
            {8.0F, 8.0F, 0.0F},
        });
    const Vec3 white{1.0F, 1.0F, 1.0F};

    const Vec3 front = render_reflection_model(
        environment, SampleCount::One, {0.0F, 0.0F, 4.0F}, white, white)
        .color_at(32U, 32U);
    const Vec3 side = render_reflection_model(
        environment, SampleCount::One, {4.0F, 0.0F, 0.0F}, white, white)
        .color_at(32U, 32U);
    check_vec3_different(front, side, "viewer position changes the reflected environment lookup");

    const Vec3 tilted_normal = render_reflection_model(
        environment,
        SampleCount::One,
        {0.0F, 0.0F, 4.0F},
        white,
        white,
        {1.0F, 0.0F, 0.0F})
        .color_at(32U, 32U);
    check_vec3_different(front, tilted_normal, "world-space normal changes the reflected environment lookup");

    const Vec3 yawed = render_reflection_model(
        environment,
        SampleCount::One,
        {0.0F, 0.0F, 4.0F},
        white,
        white,
        {0.0F, 0.0F, 1.0F},
        1.0F,
        kPi * 0.5F)
        .color_at(32U, 32U);
    check_vec3_different(front, yawed, "environment reflection yaw changes the lookup deterministically");

    const EnvironmentReflectionState direct = reflection_state(environment);
    check_vec3_near(
        reflection_environment_radiance(direct, {0.0F, 0.0F, 1.0F}),
        {8.0F, 0.0F, 0.0F},
        "reflection helper reuses the canonical +Z equirectangular seam convention",
        1.0e-6F);
}

void test_zero_specular_preserves_m57_output_exactly() {
    const Texture2D environment(1U, 1U, {{9.0F, 7.0F, 5.0F}});
    const ModelAsset asset = model_from_triangle({0.2F, 0.4F, 0.6F});

    Framebuffer baseline(65U, 65U, SampleCount::Four);
    draw_model_asset(
        baseline,
        asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity());

    ModelRenderOptions reflected_options;
    reflected_options.fixed_lights.environment_reflection = reflection_light(environment);
    Framebuffer reflected(65U, 65U, SampleCount::Four);
    draw_model_asset(
        reflected,
        asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        reflected_options);

    check(
        reflected.rgb8() == baseline.rgb8(),
        "zero material specular preserves pre-reflection M57 RGB output byte-for-byte");
    check(
        reflected.fnv1a64() == baseline.fnv1a64(),
        "zero material specular preserves the deterministic framebuffer hash");
}

void test_reflection_composes_additively_with_diffuse_environment_and_fixed_light() {
    const Texture2D diffuse_environment(1U, 1U, {{0.1F, 0.2F, 0.3F}});
    const Texture2D reflection_environment(1U, 1U, {{0.4F, 0.5F, 0.6F}});
    const Vec3 albedo{0.5F, 0.25F, 0.75F};
    const Vec3 specular{0.2F, 0.4F, 0.6F};
    const ModelAsset asset = model_from_triangle(albedo, specular);

    auto render = [&](const ModelRenderOptions& options) {
        Framebuffer framebuffer(65U, 65U);
        draw_model_asset(
            framebuffer,
            asset,
            Mat4::identity(), Mat4::identity(), Mat4::identity(),
            options);
        return framebuffer.color_at(32U, 32U);
    };

    ModelRenderOptions reflection_only;
    reflection_only.fixed_lights.environment_reflection = reflection_light(reflection_environment);
    const Vec3 reflection = render(reflection_only);

    ModelRenderOptions diffuse_only;
    diffuse_only.fixed_lights.environment_diffuse = environment_light(diffuse_environment);
    const Vec3 diffuse = render(diffuse_only);

    ModelRenderOptions fixed_only;
    fixed_only.fixed_lights.count = 1U;
    fixed_only.fixed_lights.lights[0] = fixed_directional(0.25F);
    const Vec3 fixed = render(fixed_only);

    ModelRenderOptions combined = fixed_only;
    combined.fixed_lights.environment_diffuse = environment_light(diffuse_environment);
    combined.fixed_lights.environment_reflection = reflection_light(reflection_environment);
    const Vec3 actual = render(combined);
    const Vec3 expected{
        reflection.x + diffuse.x + fixed.x,
        reflection.y + diffuse.y + fixed.y,
        reflection.z + diffuse.z + fixed.z,
    };
    check_vec3_near(
        actual,
        expected,
        "environment reflection composes additively with diffuse environment and fixed lighting",
        1.0e-5F);
}

void test_reflection_imported_and_programmatic_hdr_equivalence() {
    const std::filesystem::path source =
        std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests/fixtures/environment_hdr.pfm";
    const Texture2D imported = load_texture_image_file(source);
    std::vector<Vec3> texels;
    texels.reserve(imported.width() * imported.height());
    for (std::size_t y = 0U; y < imported.height(); ++y) {
        for (std::size_t x = 0U; x < imported.width(); ++x) {
            texels.push_back(imported.texel(x, y));
        }
    }
    const Texture2D programmatic(imported.width(), imported.height(), texels);

    const EnvironmentReflectionState imported_state = reflection_state(imported, 1.25F, 0.3F);
    const EnvironmentReflectionState programmatic_state = reflection_state(programmatic, 1.25F, 0.3F);
    const Vec3 direction{0.3F, 0.2F, 0.9F};
    check_vec3_near(
        reflection_environment_radiance(imported_state, direction),
        reflection_environment_radiance(programmatic_state, direction),
        "PFM-imported and equivalent programmatic HDR environments produce equal reflection radiance",
        1.0e-6F);

    const Vec3 white{1.0F, 1.0F, 1.0F};
    const Framebuffer imported_render = render_reflection_model(
        imported, SampleCount::One, {0.0F, 0.0F, 4.0F}, white, white, {0.0F, 0.0F, 1.0F}, 1.25F, 0.3F);
    const Framebuffer programmatic_render = render_reflection_model(
        programmatic, SampleCount::One, {0.0F, 0.0F, 4.0F}, white, white, {0.0F, 0.0F, 1.0F}, 1.25F, 0.3F);
    check(
        imported_render.rgb8() == programmatic_render.rgb8(),
        "PFM-imported and programmatic HDR reflection render byte-equivalently");
}

void test_reflection_prepared_and_list_equivalence() {
    const Texture2D environment(1U, 1U, {{1.2F, 0.7F, 0.3F}});
    const ModelAsset asset = model_from_triangle(
        {0.4F, 0.5F, 0.6F},
        {0.25F, 0.5F, 0.75F});
    ModelRenderOptions options;
    options.fixed_lights.environment_reflection = reflection_light(environment, {0.0F, 0.0F, 3.0F});
    const PreparedModelSubmission prepared = prepare_model_asset(asset, options);

    Framebuffer direct(65U, 65U, SampleCount::Four);
    draw_model_asset(
        direct,
        asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);

    Framebuffer listed(65U, 65U, SampleCount::Four);
    const PreparedModelListEntry entries[] = {{&prepared, Mat4::identity()}};
    draw_prepared_model_list(listed, entries, Mat4::identity(), Mat4::identity());

    check(
        listed.rgb8() == direct.rgb8(),
        "prepared-list environment reflection is byte-equivalent to direct model execution");
    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        check_vec3_near(
            listed.sample_color_at(32U, 32U, sample),
            direct.sample_color_at(32U, 32U, sample),
            "prepared-list environment reflection preserves exact 4x sample shading",
            1.0e-6F);
    }
}

void test_invalid_reflection_state_and_mvp_only_fail_before_mutation() {
    const Texture2D negative(1U, 1U, {{-1.0F, 0.2F, 0.3F}});
    const ModelAsset asset = model_from_triangle(
        {0.2F, 0.3F, 0.4F},
        {1.0F, 1.0F, 1.0F});
    ModelRenderOptions options;
    options.fixed_lights.environment_reflection = reflection_light(negative);

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.6F, 0.5F, 0.4F}, 0.7F, 13U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);

    bool radiance_threw = false;
    try {
        draw_model_asset(
            framebuffer,
            asset,
            Mat4::identity(), Mat4::identity(), Mat4::identity(),
            options);
    } catch (const std::invalid_argument&) {
        radiance_threw = true;
    }
    check(radiance_threw, "negative reflection radiance is rejected before execution");
    check(framebuffer.rgb8() == before, "invalid reflection radiance leaves color untouched");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "invalid reflection radiance leaves depth untouched");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "invalid reflection radiance leaves stencil untouched");

    const Texture2D valid(1U, 1U, {{0.2F, 0.3F, 0.4F}});
    ModelRenderOptions mvp_options;
    mvp_options.fixed_lights.environment_reflection = reflection_light(valid);
    bool mvp_threw = false;
    try {
        draw_model_asset(framebuffer, asset, Mat4::identity(), mvp_options);
    } catch (const std::invalid_argument&) {
        mvp_threw = true;
    }
    check(mvp_threw, "MVP-only submission rejects reflection that requires world-space fragment position");
    check(framebuffer.rgb8() == before, "MVP-only reflection rejection leaves color untouched");

    ModelRenderOptions viewer_options;
    viewer_options.fixed_lights.environment_reflection = reflection_light(valid);
    viewer_options.fixed_lights.environment_reflection->viewer_position.x =
        std::numeric_limits<float>::quiet_NaN();
    bool viewer_threw = false;
    try {
        (void)prepare_model_asset(asset, viewer_options);
    } catch (const std::invalid_argument&) {
        viewer_threw = true;
    }
    check(viewer_threw, "prepared-model construction rejects a non-finite reflection viewer position");

    const Texture2D srgb(
        1U, 1U, {{0.5F, 0.5F, 0.5F}}, TextureTransferFunction::Srgb);
    bool domain_threw = false;
    try {
        validate_environment_reflection_state(reflection_state(srgb));
    } catch (const std::invalid_argument&) {
        domain_threw = true;
    }
    check(domain_threw, "environment reflection rejects non-linear texture domains");
}

}  // namespace

int main() {
    try {
        test_constant_environment_has_analytic_lambert_result();
        test_directional_environment_and_yaw_are_observable_and_deterministic();
        test_environment_only_raster_path_on_1x_and_4x();
        test_environment_composes_with_fixed_directional_light();
        test_imported_and_programmatic_hdr_environment_equivalence();
        test_prepared_and_list_equivalence();
        test_invalid_environment_state_fails_before_framebuffer_mutation();
        test_constant_environment_reflection_is_specular_only_on_1x_and_4x();
        test_reflection_lookup_depends_on_view_normal_and_yaw();
        test_zero_specular_preserves_m57_output_exactly();
        test_reflection_composes_additively_with_diffuse_environment_and_fixed_light();
        test_reflection_imported_and_programmatic_hdr_equivalence();
        test_reflection_prepared_and_list_equivalence();
        test_invalid_reflection_state_and_mvp_only_fail_before_mutation();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " environment-lighting test(s) failed\n";
        return 1;
    }
    std::cout << "all environment-lighting tests passed\n";
    return 0;
}
