#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
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

EnvironmentDiffuseLight environment_light(
    const Texture2D& texture,
    float intensity = 1.0F,
    float yaw = 0.0F) {
    EnvironmentDiffuseLight light;
    light.normal = {0U, 1U, 2U};
    light.environment = environment_state(texture, intensity, yaw);
    return light;
}

Vertex normal_vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.0F, 0.0F, 1.0F});
}

ModelAsset model_from_triangle(Vec3 albedo = {1.0F, 1.0F, 1.0F}) {
    ModelAsset asset;
    asset.mesh.vertices = {
        normal_vertex({-0.8F, -0.8F, 0.0F}),
        normal_vertex({0.8F, -0.8F, 0.0F}),
        normal_vertex({0.0F, 0.8F, 0.0F}),
    };
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "environment";
    draw.material.albedo = albedo;
    draw.material.specular = {0.0F, 0.0F, 0.0F};
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
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " diffuse-environment test(s) failed\n";
        return 1;
    }
    std::cout << "all diffuse-environment tests passed\n";
    return 0;
}
