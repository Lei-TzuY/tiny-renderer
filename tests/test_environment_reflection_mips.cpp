#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/environment_lighting.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/offline_render.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check_vec3_near(
    const Vec3& actual,
    const Vec3& expected,
    const std::string& message,
    float epsilon = 2.0e-4F) {
    check(
        std::fabs(actual.x - expected.x) <= epsilon
            && std::fabs(actual.y - expected.y) <= epsilon
            && std::fabs(actual.z - expected.z) <= epsilon,
        message);
}

template <typename Exception, typename Function>
void check_throws(Function&& function, const std::string& message) {
    bool threw_expected = false;
    try {
        function();
    } catch (const Exception&) {
        threw_expected = true;
    } catch (...) {
    }
    check(threw_expected, message);
}

Texture2D checker_texture(std::size_t width = 64U, std::size_t height = 32U) {
    std::vector<Vec3> texels;
    texels.reserve(width * height);
    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            const float value = ((x + y) & 1U) == 0U ? 0.0F : 1.0F;
            texels.push_back({value, 1.0F - value, value});
        }
    }
    return Texture2D(width, height, std::move(texels));
}

float wrapped_u_delta(float from, float to) {
    float delta = to - from;
    if (delta > 0.5F) {
        delta -= 1.0F;
    } else if (delta < -0.5F) {
        delta += 1.0F;
    }
    return delta;
}

TextureGradients expected_angular_gradients(
    const Vec3& input_direction,
    float yaw,
    float angle) {
    const Vec3 direction = normalize(input_direction);
    const Vec3 helper = std::fabs(direction.y) < 0.999F
        ? Vec3{0.0F, 1.0F, 0.0F}
        : Vec3{1.0F, 0.0F, 0.0F};
    const Vec3 tangent = normalize(cross(helper, direction));
    const Vec3 bitangent = cross(direction, tangent);
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const Vec2 center = equirectangular_uv(direction, yaw);
    const Vec2 dx = equirectangular_uv(direction * cosine + tangent * sine, yaw);
    const Vec2 dy = equirectangular_uv(direction * cosine + bitangent * sine, yaw);
    return {
        {wrapped_u_delta(center.x, dx.x), dx.y - center.y},
        {wrapped_u_delta(center.x, dy.x), dy.y - center.y},
    };
}

EnvironmentReflectionState base_state(const Texture2D& texture) {
    EnvironmentReflectionState state;
    state.texture = &texture;
    state.sampler.address_u = AddressMode::Repeat;
    state.sampler.address_v = AddressMode::Clamp;
    state.sampler.filter = FilterMode::Nearest;
    state.sampler.mip_filter = MipFilterMode::Disabled;
    state.intensity = 1.0F;
    state.yaw_radians = 0.0F;
    state.mip_policy = EnvironmentReflectionMipPolicy::BaseLevel;
    state.angular_footprint_radians = 0.0F;
    return state;
}

EnvironmentReflectionState angular_state(
    const Texture2D& texture,
    MipFilterMode mip_filter = MipFilterMode::Linear,
    float angle = 0.35F,
    std::size_t max_anisotropy = 1U) {
    EnvironmentReflectionState state = base_state(texture);
    state.sampler.mip_filter = mip_filter;
    state.sampler.max_anisotropy = max_anisotropy;
    state.mip_policy = EnvironmentReflectionMipPolicy::AngularFootprint;
    state.angular_footprint_radians = angle;
    return state;
}

EnvironmentReflectionState material_shininess_state(
    const Texture2D& texture,
    MipFilterMode mip_filter = MipFilterMode::Linear,
    float max_angle = 0.60F,
    std::size_t max_anisotropy = 1U) {
    EnvironmentReflectionState state = angular_state(
        texture, mip_filter, max_angle, max_anisotropy);
    state.mip_policy = EnvironmentReflectionMipPolicy::MaterialShininess;
    return state;
}

VaryingPack normal_varyings() {
    VaryingPack varyings;
    varyings.count = 3U;
    varyings.values[0] = 0.0F;
    varyings.values[1] = 0.0F;
    varyings.values[2] = 1.0F;
    return varyings;
}

ModelAsset reflective_triangle_asset() {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.8F, -0.8F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.8F, -0.8F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.0F, 0.8F, 0.0F}, normal_varyings()),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "reflective";
    draw.material.albedo = {0.0F, 0.0F, 0.0F};
    draw.material.specular = {1.0F, 0.8F, 0.6F};
    draw.material.shininess = 32.0F;
    asset.draws.push_back(draw);
    return asset;
}

ModelAsset reflective_pair_asset(float left_shininess, float right_shininess) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.9F, -0.8F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({-0.1F, -0.8F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({-0.5F, 0.8F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.1F, -0.8F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.9F, -0.8F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.5F, 0.8F, 0.0F}, normal_varyings()),
    };
    asset.mesh.triangles = {
        {{0U, 1U, 2U}},
        {{3U, 4U, 5U}},
    };

    MaterialDraw left;
    left.range = {0U, 1U};
    left.material_name = "broad";
    left.material.albedo = {0.0F, 0.0F, 0.0F};
    left.material.specular = {1.0F, 1.0F, 1.0F};
    left.material.shininess = left_shininess;
    asset.draws.push_back(left);

    MaterialDraw right;
    right.range = {1U, 1U};
    right.material_name = "sharp";
    right.material.albedo = {0.0F, 0.0F, 0.0F};
    right.material.specular = {1.0F, 1.0F, 1.0F};
    right.material.shininess = right_shininess;
    asset.draws.push_back(right);
    return asset;
}

ModelRenderOptions reflection_options(const Texture2D& texture) {
    ModelRenderOptions options;
    EnvironmentReflectionLight light;
    light.normal = {0U, 1U, 2U};
    light.viewer_position = {0.0F, 0.0F, 2.0F};
    light.environment = angular_state(texture, MipFilterMode::Linear, 0.30F, 4U);
    options.fixed_lights.environment_reflection = light;
    return options;
}

ModelRenderOptions material_reflection_options(const Texture2D& texture) {
    ModelRenderOptions options;
    EnvironmentReflectionLight light;
    light.normal = {0U, 1U, 2U};
    light.viewer_position = {0.0F, 0.0F, 2.0F};
    light.environment = material_shininess_state(
        texture, MipFilterMode::Nearest, 0.60F, 1U);
    options.fixed_lights.environment_reflection = light;
    return options;
}

bool exact_samples_equal(const Framebuffer& a, const Framebuffer& b) {
    if (a.width() != b.width()
        || a.height() != b.height()
        || a.samples_per_pixel() != b.samples_per_pixel()) {
        return false;
    }
    for (std::size_t y = 0U; y < a.height(); ++y) {
        for (std::size_t x = 0U; x < a.width(); ++x) {
            for (std::size_t sample = 0U; sample < a.samples_per_pixel(); ++sample) {
                const Vec3 av = a.sample_color_at(x, y, sample);
                const Vec3 bv = b.sample_color_at(x, y, sample);
                if (av.x != bv.x || av.y != bv.y || av.z != bv.z
                    || a.sample_depth_at(x, y, sample) != b.sample_depth_at(x, y, sample)
                    || a.sample_stencil_at(x, y, sample) != b.sample_stencil_at(x, y, sample)) {
                    return false;
                }
            }
        }
    }
    return true;
}

void test_base_level_preserves_reference() {
    const Texture2D texture = checker_texture();
    const EnvironmentReflectionState state = base_state(texture);
    const Vec3 direction{0.35F, -0.2F, -1.0F};
    const Vec2 uv = equirectangular_uv(direction, state.yaw_radians);
    check_vec3_near(
        reflection_environment_radiance(state, direction),
        texture.sample(uv, state.sampler),
        "base-level reflection preserves direct environment sampling");
}

void test_angular_footprint_reuses_sample_grad() {
    const Texture2D texture = checker_texture();
    const EnvironmentReflectionState state = angular_state(
        texture, MipFilterMode::Nearest, 0.40F, 1U);
    const Vec3 direction{0.32F, 0.18F, -1.0F};
    const Vec2 uv = equirectangular_uv(direction, state.yaw_radians);
    const TextureGradients gradients = expected_angular_gradients(
        direction, state.yaw_radians, state.angular_footprint_radians);
    const Vec3 expected = texture.sample_grad(uv, gradients, state.sampler);
    const Vec3 actual = reflection_environment_radiance(state, direction);
    check_vec3_near(
        actual,
        expected,
        "angular reflection footprint delegates mip selection to Texture2D::sample_grad");

    SamplerState base_sampler = state.sampler;
    base_sampler.mip_filter = MipFilterMode::Disabled;
    base_sampler.max_anisotropy = 1U;
    const Vec3 base = texture.sample(uv, base_sampler);
    check(
        !nearly_equal(actual.x, base.x)
            || !nearly_equal(actual.y, base.y)
            || !nearly_equal(actual.z, base.z),
        "angular reflection regression exercises a mip-filtered result");
}

void test_material_shininess_reuses_angular_sampler() {
    const Texture2D texture = checker_texture(128U, 64U);
    const EnvironmentReflectionState state = material_shininess_state(
        texture, MipFilterMode::Nearest, 0.60F, 1U);
    const Vec3 direction{0.32F, 0.18F, -1.0F};
    const Vec2 uv = equirectangular_uv(direction, state.yaw_radians);

    for (const float shininess : {1.0F, 4.0F, 1000.0F}) {
        const float effective_angle = state.angular_footprint_radians / shininess;
        const TextureGradients gradients = expected_angular_gradients(
            direction, state.yaw_radians, effective_angle);
        const Vec3 expected = texture.sample_grad(uv, gradients, state.sampler);
        const Vec3 actual = reflection_environment_radiance(
            state, direction, shininess);
        check_vec3_near(
            actual,
            expected,
            "material shininess resolves to the documented max-angle/Ns footprint");
    }

    const Vec3 broad = reflection_environment_radiance(state, direction, 1.0F);
    const Vec3 sharp = reflection_environment_radiance(state, direction, 1000.0F);
    check(
        !nearly_equal(broad.x, sharp.x)
            || !nearly_equal(broad.y, sharp.y)
            || !nearly_equal(broad.z, sharp.z),
        "material shininess changes reflection sharpness under one environment state");

    check_throws<std::invalid_argument>(
        [&] { (void)reflection_environment_radiance(state, direction); },
        "material-shininess reflection cannot be sampled without material state");
    check_throws<std::invalid_argument>(
        [&] { (void)reflection_environment_radiance(state, direction, 0.0F); },
        "material-shininess reflection rejects shininess below the material bound");
    check_throws<std::invalid_argument>(
        [&] { (void)reflection_environment_radiance(state, direction, 1001.0F); },
        "material-shininess reflection rejects shininess above the material bound");
    check_throws<std::invalid_argument>(
        [&] {
            (void)reflection_environment_radiance(
                state,
                direction,
                std::numeric_limits<float>::quiet_NaN());
        },
        "material-shininess reflection rejects non-finite shininess");
}

void test_angular_footprint_wraps_environment_seam() {
    const Texture2D texture = checker_texture(128U, 32U);
    const EnvironmentReflectionState state = angular_state(
        texture, MipFilterMode::Linear, 0.30F, 2U);
    const Vec3 direction{0.0F, 0.0F, 1.0F};
    const Vec3 normalized = normalize(direction);
    const Vec3 tangent = normalize(cross(Vec3{0.0F, 1.0F, 0.0F}, normalized));
    const Vec2 center = equirectangular_uv(normalized, 0.0F);
    const Vec2 neighbor = equirectangular_uv(
        normalized * std::cos(state.angular_footprint_radians)
            + tangent * std::sin(state.angular_footprint_radians),
        0.0F);
    const float raw_delta = neighbor.x - center.x;
    const TextureGradients gradients = expected_angular_gradients(
        direction, state.yaw_radians, state.angular_footprint_radians);
    check(std::fabs(raw_delta) > 0.5F, "reflection footprint regression crosses the canonical longitude seam");
    check(std::fabs(gradients.dx.x) < 0.5F, "reflection footprint uses shortest wrapped longitude delta");
    check_vec3_near(
        reflection_environment_radiance(state, direction),
        texture.sample_grad(center, gradients, state.sampler),
        "seam-aware angular reflection matches wrapped-gradient sampling");
}

void test_invalid_policy_combinations_fail_closed() {
    const Texture2D texture = checker_texture();

    EnvironmentReflectionState invalid = base_state(texture);
    invalid.mip_policy = EnvironmentReflectionMipPolicy::AngularFootprint;
    invalid.angular_footprint_radians = 0.2F;
    check_throws<std::invalid_argument>(
        [&] { validate_environment_reflection_state(invalid); },
        "angular reflection rejects disabled mip filtering");

    invalid = base_state(texture);
    invalid.mip_policy = EnvironmentReflectionMipPolicy::MaterialShininess;
    invalid.angular_footprint_radians = 0.2F;
    check_throws<std::invalid_argument>(
        [&] { validate_environment_reflection_state(invalid); },
        "material-shininess reflection rejects disabled mip filtering");

    invalid = base_state(texture);
    invalid.sampler.mip_filter = MipFilterMode::Nearest;
    check_throws<std::invalid_argument>(
        [&] { validate_environment_reflection_state(invalid); },
        "base reflection rejects enabled mip filtering");

    invalid = angular_state(texture);
    invalid.angular_footprint_radians = 0.0F;
    check_throws<std::invalid_argument>(
        [&] { validate_environment_reflection_state(invalid); },
        "angular reflection rejects a zero footprint");

    invalid = material_shininess_state(texture);
    invalid.angular_footprint_radians = 0.0F;
    check_throws<std::invalid_argument>(
        [&] { validate_environment_reflection_state(invalid); },
        "material-shininess reflection rejects a zero maximum footprint");

    invalid = angular_state(texture);
    invalid.angular_footprint_radians = std::numeric_limits<float>::quiet_NaN();
    check_throws<std::invalid_argument>(
        [&] { validate_environment_reflection_state(invalid); },
        "angular reflection rejects a non-finite footprint");

    invalid = angular_state(texture);
    invalid.angular_footprint_radians = kPi * 0.5F + 0.01F;
    check_throws<std::invalid_argument>(
        [&] { validate_environment_reflection_state(invalid); },
        "angular reflection rejects a footprint above pi/2");

    invalid = base_state(texture);
    invalid.angular_footprint_radians = 0.1F;
    check_throws<std::invalid_argument>(
        [&] { validate_environment_reflection_state(invalid); },
        "base reflection rejects ignored non-zero footprint state");

    invalid = angular_state(texture);
    invalid.mip_policy = static_cast<EnvironmentReflectionMipPolicy>(255);
    check_throws<std::invalid_argument>(
        [&] { validate_environment_reflection_state(invalid); },
        "reflection rejects unknown mip policy");

    invalid = angular_state(texture);
    invalid.sampler.max_anisotropy = 3U;
    check_throws<std::invalid_argument>(
        [&] { validate_environment_reflection_state(invalid); },
        "reflection inherits bounded anisotropy validation");
}

void test_direct_prepared_and_headless_propagation() {
    const Texture2D texture = checker_texture();
    const ModelAsset asset = reflective_triangle_asset();
    const ModelRenderOptions options = reflection_options(texture);

    Framebuffer direct(41U, 31U, SampleCount::Four);
    Framebuffer prepared_fb(41U, 31U, SampleCount::Four);
    direct.clear({0.01F, 0.02F, 0.03F});
    prepared_fb.clear({0.01F, 0.02F, 0.03F});
    draw_model_asset(
        direct,
        asset,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        options);
    const PreparedModelSubmission prepared = prepare_model_asset(asset, options);
    draw_prepared_model(
        prepared_fb,
        prepared,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    check(
        exact_samples_equal(direct, prepared_fb),
        "angular reflection is exact-sample equivalent through direct and prepared model submission");

    OfflineRenderSettings settings;
    settings.width = 43U;
    settings.height = 33U;
    settings.sample_count = SampleCount::Four;
    OfflineEnvironmentReflectionState offline;
    offline.normal = {0U, 1U, 2U};
    offline.environment = angular_state(texture, MipFilterMode::Linear, 0.30F, 4U);
    settings.environment_reflection = offline;
    const Framebuffer headless_a = render_model_preview(asset, settings);
    const Framebuffer headless_b = render_model_preview(asset, settings);
    check(
        exact_samples_equal(headless_a, headless_b),
        "headless angular reflection is deterministic at exact sample storage");

    OfflineRenderSettings explicit_settings = settings;
    explicit_settings.environment_reflection.reset();
    ModelRenderOptions explicit_options;
    EnvironmentReflectionLight explicit_light;
    explicit_light.normal = {0U, 1U, 2U};
    explicit_light.viewer_position = {0.0F, 0.0F, 3.0F};
    explicit_light.environment = offline.environment;
    explicit_options.fixed_lights.environment_reflection = explicit_light;
    const Framebuffer explicit_headless = render_model_preview(
        asset,
        explicit_settings,
        explicit_options);
    check(
        exact_samples_equal(headless_a, explicit_headless),
        "headless angular reflection uses the authoritative preview viewer with the shared sampler path");
}

void test_material_shininess_direct_prepared_and_headless_equivalence() {
    const Texture2D texture = checker_texture(128U, 64U);
    const ModelAsset asset = reflective_pair_asset(1.0F, 1000.0F);
    const ModelRenderOptions options = material_reflection_options(texture);

    Framebuffer direct(65U, 33U, SampleCount::Four);
    Framebuffer prepared_fb(65U, 33U, SampleCount::Four);
    direct.clear({0.0F, 0.0F, 0.0F});
    prepared_fb.clear({0.0F, 0.0F, 0.0F});
    draw_model_asset(
        direct,
        asset,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        options);
    const PreparedModelSubmission prepared = prepare_model_asset(asset, options);
    draw_prepared_model(
        prepared_fb,
        prepared,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    check(
        exact_samples_equal(direct, prepared_fb),
        "material-coupled reflection is exact-sample equivalent through direct and prepared submission");

    Framebuffer uniform(65U, 33U, SampleCount::Four);
    uniform.clear({0.0F, 0.0F, 0.0F});
    draw_model_asset(
        uniform,
        reflective_pair_asset(1.0F, 1.0F),
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        options);
    check_vec3_near(
        direct.color_at(16U, 20U),
        uniform.color_at(16U, 20U),
        "unchanged left material keeps the same glossy environment result");
    const Vec3 sharp = direct.color_at(48U, 20U);
    const Vec3 broad = uniform.color_at(48U, 20U);
    check(
        !nearly_equal(sharp.x, broad.x)
            || !nearly_equal(sharp.y, broad.y)
            || !nearly_equal(sharp.z, broad.z),
        "two materials under one environment light resolve different reflection sharpness");

    OfflineRenderSettings settings;
    settings.width = 65U;
    settings.height = 49U;
    settings.sample_count = SampleCount::Four;
    OfflineEnvironmentReflectionState offline;
    offline.normal = {0U, 1U, 2U};
    offline.environment = material_shininess_state(
        texture, MipFilterMode::Nearest, 0.60F, 1U);
    settings.environment_reflection = offline;
    const Framebuffer headless = render_model_preview(asset, settings);

    OfflineRenderSettings explicit_settings = settings;
    explicit_settings.environment_reflection.reset();
    ModelRenderOptions explicit_options;
    EnvironmentReflectionLight explicit_light;
    explicit_light.normal = {0U, 1U, 2U};
    explicit_light.viewer_position = {0.0F, 0.0F, 3.0F};
    explicit_light.environment = offline.environment;
    explicit_options.fixed_lights.environment_reflection = explicit_light;
    const Framebuffer explicit_headless = render_model_preview(
        asset,
        explicit_settings,
        explicit_options);
    check(
        exact_samples_equal(headless, explicit_headless),
        "headless material-coupled reflection resolves per-material shininess through the shared viewer path");
}

void test_prepared_validation_rejects_invalid_reflection_state() {
    const Texture2D texture = checker_texture();
    ModelRenderOptions options = reflection_options(texture);
    options.fixed_lights.environment_reflection->environment.sampler.mip_filter =
        MipFilterMode::Disabled;
    check_throws<std::invalid_argument>(
        [&] { (void)prepare_model_asset(reflective_triangle_asset(), options); },
        "prepared model rejects invalid angular reflection state before framebuffer execution");

    options = material_reflection_options(texture);
    options.fixed_lights.environment_reflection->environment.sampler.mip_filter =
        MipFilterMode::Disabled;
    check_throws<std::invalid_argument>(
        [&] { (void)prepare_model_asset(reflective_triangle_asset(), options); },
        "prepared model rejects invalid material-coupled reflection state before execution");
}

}  // namespace

int main() {
    test_base_level_preserves_reference();
    test_angular_footprint_reuses_sample_grad();
    test_material_shininess_reuses_angular_sampler();
    test_angular_footprint_wraps_environment_seam();
    test_invalid_policy_combinations_fail_closed();
    test_direct_prepared_and_headless_propagation();
    test_material_shininess_direct_prepared_and_headless_equivalence();
    test_prepared_validation_rejects_invalid_reflection_state();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "environment reflection mip tests passed\n";
    return 0;
}