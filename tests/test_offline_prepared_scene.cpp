#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/offline_sequence.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
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

VaryingPack normal_varyings() {
    VaryingPack varyings;
    varyings.count = 3U;
    varyings.values[0] = 0.0F;
    varyings.values[1] = 0.0F;
    varyings.values[2] = 1.0F;
    return varyings;
}

ModelAsset triangle_asset(
    Vec3 albedo,
    Vec3 specular,
    float opacity = 1.0F) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.9F, -0.9F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.9F, -0.9F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.0F, 0.9F, 0.0F}, normal_varyings()),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};

    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "scene";
    draw.material.albedo = albedo;
    draw.material.specular = specular;
    draw.material.shininess = 32.0F;
    draw.material.opacity = opacity;
    asset.draws.push_back(draw);
    return asset;
}

Texture2D directional_environment_texture() {
    return Texture2D(
        4U,
        2U,
        {
            {7.0F, 0.2F, 0.1F},
            {0.2F, 6.0F, 0.1F},
            {0.1F, 0.2F, 5.0F},
            {3.5F, 1.0F, 0.25F},
            {0.5F, 3.0F, 0.25F},
            {2.0F, 0.5F, 4.0F},
            {5.0F, 2.0F, 0.5F},
            {0.25F, 4.0F, 2.0F},
        });
}

OfflineEnvironmentReflectionState offline_reflection(const Texture2D& texture) {
    OfflineEnvironmentReflectionState state;
    state.normal = {0U, 1U, 2U};
    state.environment.texture = &texture;
    state.environment.sampler.address_u = AddressMode::Repeat;
    state.environment.sampler.address_v = AddressMode::Clamp;
    state.environment.sampler.filter = FilterMode::Nearest;
    state.environment.sampler.mip_filter = MipFilterMode::Disabled;
    state.environment.intensity = 0.7F;
    state.environment.yaw_radians = 0.2F;
    return state;
}

OfflineSceneCamera camera_at(const Vec3& eye) {
    OfflineSceneCamera camera;
    camera.eye = eye;
    camera.target = {0.0F, 0.0F, 0.0F};
    camera.up = {0.0F, 1.0F, 0.0F};
    camera.vertical_fov_radians = radians(55.0F);
    camera.near_plane = 0.1F;
    camera.far_plane = 20.0F;
    return camera;
}

bool exact_frame_equal(const Framebuffer& left, const Framebuffer& right) {
    if (left.width() != right.width()
        || left.height() != right.height()
        || left.samples_per_pixel() != right.samples_per_pixel()
        || left.rgb8() != right.rgb8()
        || left.fnv1a64() != right.fnv1a64()) {
        return false;
    }
    for (std::size_t y = 0U; y < left.height(); ++y) {
        for (std::size_t x = 0U; x < left.width(); ++x) {
            for (std::size_t sample = 0U; sample < left.samples_per_pixel(); ++sample) {
                const Vec3 a = left.sample_color_at(x, y, sample);
                const Vec3 b = right.sample_color_at(x, y, sample);
                if (a.x != b.x || a.y != b.y || a.z != b.z
                    || left.sample_depth_at(x, y, sample) != right.sample_depth_at(x, y, sample)
                    || left.sample_stencil_at(x, y, sample) != right.sample_stencil_at(x, y, sample)) {
                    return false;
                }
            }
        }
    }
    return true;
}

PreparedOfflineMixedScene make_reflective_mixed_scene(
    const OfflineRenderSettings& settings) {
    ModelAsset reflective = triangle_asset(
        {0.0F, 0.0F, 0.0F},
        {0.9F, 0.65F, 0.35F});
    ModelAsset transparent = triangle_asset(
        {0.1F, 0.7F, 0.25F},
        {0.0F, 0.0F, 0.0F},
        0.45F);
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &reflective,
            Mat4::translation({-0.2F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &transparent,
            Mat4::translation({0.25F, 0.0F, -0.25F}),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    return prepare_offline_mixed_scene(entries, settings);
}

void test_reusable_mixed_scene_matches_one_shot_across_cameras() {
    const Texture2D environment = directional_environment_texture();
    OfflineRenderSettings settings;
    settings.width = 57U;
    settings.height = 43U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};
    settings.environment_reflection = offline_reflection(environment);

    const PreparedOfflineMixedScene reusable = make_reflective_mixed_scene(settings);

    const ModelAsset reflective_reference = triangle_asset(
        {0.0F, 0.0F, 0.0F},
        {0.9F, 0.65F, 0.35F});
    const ModelAsset transparent_reference = triangle_asset(
        {0.1F, 0.7F, 0.25F},
        {0.0F, 0.0F, 0.0F},
        0.45F);
    const std::array<OfflineSceneEntry, 2> reference_entries{{
        OfflineSceneEntry{
            &reflective_reference,
            Mat4::translation({-0.2F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &transparent_reference,
            Mat4::translation({0.25F, 0.0F, -0.25F}),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};

    const OfflineSceneCamera camera_a = camera_at({0.0F, 0.0F, 3.0F});
    const OfflineSceneCamera camera_b = camera_at({1.35F, 0.15F, 3.0F});

    const Framebuffer reusable_a = render_prepared_scene_preview(reusable, camera_a);
    const Framebuffer reference_a = render_scene_preview(
        reference_entries,
        settings,
        OfflineSceneOrdering::MixedTransparency,
        camera_a);
    check(
        exact_frame_equal(reusable_a, reference_a),
        "reusable prepared mixed scene is exact 4x attachment-equivalent to one-shot camera A rendering");

    const Framebuffer reusable_b = render_prepared_scene_preview(reusable, camera_b);
    const Framebuffer reference_b = render_scene_preview(
        reference_entries,
        settings,
        OfflineSceneOrdering::MixedTransparency,
        camera_b);
    check(
        exact_frame_equal(reusable_b, reference_b),
        "reusable prepared mixed scene rebinds reflection viewer and is exact-equivalent to one-shot camera B rendering");
    check(
        reusable_a.rgb8() != reusable_b.rgb8(),
        "distinct active cameras produce observably distinct reusable scene output");

    const Framebuffer reusable_b_repeat = render_prepared_scene_preview(reusable, camera_b);
    check(
        exact_frame_equal(reusable_b, reusable_b_repeat),
        "reusing one owned prepared scene for the same camera is exactly deterministic");
}

void test_reusable_camera_sequence_matches_individual_execution() {
    const Texture2D environment = directional_environment_texture();
    OfflineRenderSettings settings;
    settings.width = 57U;
    settings.height = 43U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};
    settings.environment_reflection = offline_reflection(environment);

    const PreparedOfflineMixedScene reusable = make_reflective_mixed_scene(settings);
    const OfflineSceneCamera camera_a = camera_at({0.0F, 0.0F, 3.0F});
    const OfflineSceneCamera camera_b = camera_at({1.35F, 0.15F, 3.0F});
    const std::array<OfflineSceneCamera, 3> cameras{{camera_a, camera_b, camera_a}};

    const std::vector<Framebuffer> sequence =
        render_prepared_scene_sequence(reusable, cameras);
    check(sequence.size() == cameras.size(),
        "camera sequence returns exactly one framebuffer per input camera");
    if (sequence.size() == cameras.size()) {
        for (std::size_t i = 0U; i < cameras.size(); ++i) {
            const Framebuffer individual =
                render_prepared_scene_preview(reusable, cameras[i]);
            check(
                exact_frame_equal(sequence[i], individual),
                "camera sequence frame is exact per-sample equivalent to individual reusable rendering");
        }
        check(
            exact_frame_equal(sequence[0], sequence[2]),
            "repeated camera positions remain exactly deterministic inside one sequence");
        check(
            sequence[0].rgb8() != sequence[1].rgb8(),
            "camera sequence rebinds camera-dependent reflection between adjacent frames");
    }

    const std::span<const OfflineSceneCamera> empty_cameras{};
    check(
        render_prepared_scene_sequence(reusable, empty_cameras).empty(),
        "empty reusable camera sequence is a deterministic no-op");
}

class CountingFragmentProgram final : public FragmentProgram {
public:
    explicit CountingFragmentProgram(std::size_t* shade_calls)
        : shade_calls_(shade_calls) {}

    FragmentProgramOutput shade(
        const FragmentProgramInput& input) const noexcept override {
        ++(*shade_calls_);
        return {input.fixed_rgb, input.fixed_opacity, false};
    }

private:
    std::size_t* shade_calls_{};
};

void test_camera_sequence_preflights_every_camera_before_execution() {
    std::size_t shade_calls = 0U;
    ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F});
    ModelRenderOptions options;
    options.fragment_program = std::make_shared<CountingFragmentProgram>(&shade_calls);

    OfflineRenderSettings settings;
    settings.width = 31U;
    settings.height = 31U;
    settings.sample_count = SampleCount::Four;
    const std::array<OfflineSceneEntry, 1> entries{{
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);

    OfflineSceneCamera invalid = camera_at({1.0F, 0.0F, 3.0F});
    invalid.target = invalid.eye;
    const std::array<OfflineSceneCamera, 2> cameras{{
        camera_at({0.0F, 0.0F, 3.0F}),
        invalid,
    }};

    check_throws<std::invalid_argument>(
        [&] { (void)render_prepared_scene_sequence(reusable, cameras); },
        "later invalid camera rejects the complete reusable sequence");
    check(
        shade_calls == 0U,
        "later invalid camera rejects before any earlier frame fragment execution");
}

void test_camera_sequence_resource_bound() {
    ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F});
    OfflineRenderSettings settings;
    settings.width = 512U;
    settings.height = 512U;
    const std::array<OfflineSceneEntry, 1> entries{{
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::Opaque},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);

    std::vector<OfflineSceneCamera> cameras(
        65U,
        camera_at({0.0F, 0.0F, 3.0F}));
    check_throws<std::invalid_argument>(
        [&] { (void)render_prepared_scene_sequence(reusable, cameras); },
        "camera sequence rejects an in-memory result beyond the resolved-pixel budget");
}

void test_reusable_scene_validation_contract() {
    const ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F},
        0.5F);

    const std::array<OfflineSceneEntry, 1> missing_mode{{
        OfflineSceneEntry{&asset, Mat4::identity(), {}, std::nullopt},
    }};
    check_throws<std::invalid_argument>(
        [&] { (void)prepare_offline_mixed_scene(missing_mode); },
        "reusable mixed-scene preparation requires an explicit transparency mode");

    const std::array<OfflineSceneEntry, 1> null_asset{{
        OfflineSceneEntry{nullptr, Mat4::identity(), {}, OfflineSceneTransparencyMode::Opaque},
    }};
    check_throws<std::invalid_argument>(
        [&] { (void)prepare_offline_mixed_scene(null_asset); },
        "reusable mixed-scene preparation rejects null model assets");

    OfflineRenderSettings single_sample;
    single_sample.width = 31U;
    single_sample.height = 31U;
    single_sample.sample_count = SampleCount::One;
    const std::array<OfflineSceneEntry, 1> a2c{{
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::AlphaToCoverage},
    }};
    const PreparedOfflineMixedScene prepared = prepare_offline_mixed_scene(a2c, single_sample);
    check_throws<std::invalid_argument>(
        [&] { (void)render_prepared_scene_preview(prepared, camera_at({0.0F, 0.0F, 3.0F})); },
        "reusable scene keeps target-dependent A2C preflight and rejects a 1x render before execution");
}

}  // namespace

int main() {
    test_reusable_mixed_scene_matches_one_shot_across_cameras();
    test_reusable_camera_sequence_matches_individual_execution();
    test_camera_sequence_preflights_every_camera_before_execution();
    test_camera_sequence_resource_bound();
    test_reusable_scene_validation_contract();

    if (failures != 0) {
        std::cerr << failures << " reusable offline prepared-scene test(s) failed\n";
        return 1;
    }
    std::cout << "reusable offline prepared-scene tests passed\n";
    return 0;
}
