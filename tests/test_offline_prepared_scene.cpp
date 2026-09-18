#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <limits>
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

void test_prepared_camera_sequence_plan_matches_existing_execution() {
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

    const PreparedOfflineCameraSequence prepared =
        prepare_offline_camera_sequence(reusable, cameras);
    check(prepared.frame_count() == cameras.size(),
          "prepared camera sequence exposes exactly one indexed frame per camera");

    const std::vector<Framebuffer> materialized =
        render_prepared_scene_sequence(reusable, cameras);
    check(materialized.size() == cameras.size(),
          "materialized compatibility sequence preserves camera count");

    for (std::size_t i = 0U; i < cameras.size(); ++i) {
        const Framebuffer indexed =
            render_prepared_camera_sequence_frame(prepared, i);
        const Framebuffer individual =
            render_prepared_scene_preview(reusable, cameras[i]);
        check(
            exact_frame_equal(indexed, individual),
            "prepared indexed frame is exact per-sample equivalent to individual reusable rendering");
        if (i < materialized.size()) {
            check(
                exact_frame_equal(indexed, materialized[i]),
                "prepared indexed frame is exact-equivalent to compatibility vector execution");
        }
    }

    const Framebuffer first =
        render_prepared_camera_sequence_frame(prepared, 0U);
    const Framebuffer repeated =
        render_prepared_camera_sequence_frame(prepared, 2U);
    check(
        exact_frame_equal(first, repeated),
        "prepared A-B-A sequence preserves exact repeated-camera determinism");

    const Framebuffer first_repeat =
        render_prepared_camera_sequence_frame(prepared, 0U);
    check(
        exact_frame_equal(first, first_repeat),
        "one prepared frame can be executed repeatedly without rebuilding sequence state");

    check_throws<std::out_of_range>(
        [&] {
            (void)render_prepared_camera_sequence_frame(
                prepared,
                prepared.frame_count());
        },
        "prepared camera sequence rejects an out-of-range frame index");
}

void test_prepared_camera_sequence_retains_plan_after_source_destruction() {
    OfflineRenderSettings settings;
    settings.width = 47U;
    settings.height = 35U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.015F, 0.02F, 0.025F};

    const OfflineSceneCamera camera = camera_at({0.45F, 0.1F, 3.0F});
    const std::array<OfflineSceneCamera, 1> cameras{{camera}};

    const PreparedOfflineCameraSequence prepared = [&] {
        PreparedOfflineMixedScene source =
            make_reflective_mixed_scene(settings);
        return prepare_offline_camera_sequence(source, cameras);
    }();

    const PreparedOfflineMixedScene reference_scene =
        make_reflective_mixed_scene(settings);
    const Framebuffer expected =
        render_prepared_scene_preview(reference_scene, camera);
    const Framebuffer actual =
        render_prepared_camera_sequence_frame(prepared, 0U);

    check(
        exact_frame_equal(actual, expected),
        "prepared camera sequence retains immutable scene-plan ownership after the source scene is destroyed");
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
        [&] { (void)prepare_offline_camera_sequence(reusable, cameras); },
        "later invalid camera rejects prepared sequence construction");
    check(
        shade_calls == 0U,
        "later invalid camera rejects prepared sequence before any fragment execution");

    check_throws<std::invalid_argument>(
        [&] { (void)render_prepared_scene_sequence(reusable, cameras); },
        "later invalid camera rejects the complete reusable compatibility sequence");
    check(
        shade_calls == 0U,
        "later invalid camera rejects compatibility sequence before any earlier frame fragment execution");
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

    const PreparedOfflineCameraSequence prepared =
        prepare_offline_camera_sequence(reusable, cameras);
    check(
        prepared.frame_count() == cameras.size(),
        "frame-at-a-time prepared sequence is bounded by camera metadata rather than aggregate framebuffer ownership");
    const Framebuffer one_frame =
        render_prepared_camera_sequence_frame(prepared, 64U);
    check(
        one_frame.width() == settings.width
            && one_frame.height() == settings.height
            && one_frame.samples_per_pixel() == 4U,
        "prepared sequence can execute one frame without materializing the full 65-frame result");

    check_throws<std::invalid_argument>(
        [&] { (void)render_prepared_scene_sequence(reusable, cameras); },
        "compatibility vector sequence retains the historical total resolved-pixel bound");
}

void test_compatibility_sequence_preserves_historical_camera_capacity() {
    OfflineRenderSettings settings;
    settings.width = 1U;
    settings.height = 1U;
    settings.sample_count = SampleCount::One;
    settings.clear_color = {0.125F, 0.25F, 0.5F};

    const std::array<OfflineSceneEntry, 0> entries{};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    std::vector<OfflineSceneCamera> cameras(
        detail::kMaxOfflineSequenceCameras + 1U,
        camera_at({0.0F, 0.0F, 3.0F}));

    check_throws<std::invalid_argument>(
        [&] { (void)prepare_offline_camera_sequence(reusable, cameras); },
        "new prepared camera-sequence API enforces its bounded metadata camera limit");

    const std::vector<Framebuffer> frames =
        render_prepared_scene_sequence(reusable, cameras);
    check(
        frames.size() == cameras.size(),
        "compatibility vector sequence preserves the historical pixel-bound contract beyond 256 cameras");
    check(
        frames.front().width() == 1U
            && frames.front().height() == 1U
            && frames.back().fnv1a64() == frames.front().fnv1a64(),
        "compatibility vector sequence remains deterministic across the preserved camera-capacity boundary");
}


void test_prepared_frame_sequence_matches_manual_per_frame_scenes() {
    OfflineRenderSettings settings;
    settings.width = 61U;
    settings.height = 47U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};

    const ModelAsset coverage = triangle_asset(
        {0.85F, 0.15F, 0.10F},
        {0.0F, 0.0F, 0.0F},
        0.55F);
    const ModelAsset green = triangle_asset(
        {0.10F, 0.75F, 0.20F},
        {0.0F, 0.0F, 0.0F},
        0.45F);
    const ModelAsset blue = triangle_asset(
        {0.10F, 0.25F, 0.85F},
        {0.0F, 0.0F, 0.0F},
        0.55F);

    // Deliberately stale/out-of-frustum base transforms prove that frame
    // overlays, not PreparedScenePlanEntry::model, drive M92 evaluation.
    const std::array<OfflineSceneEntry, 3> base_entries{{
        OfflineSceneEntry{
            &coverage,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::AlphaToCoverage},
        OfflineSceneEntry{
            &green,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
        OfflineSceneEntry{
            &blue,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(base_entries, settings);

    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});
    const std::vector<Mat4> transforms_a{
        Mat4::translation({-0.35F, 0.0F, 0.0F}),
        Mat4::translation({0.20F, 0.0F, -0.25F}),
        Mat4::translation({0.10F, 0.0F, -0.80F}),
    };
    const std::vector<Mat4> transforms_b{
        Mat4::translation({0.35F, 0.0F, 0.0F}),
        Mat4::translation({0.10F, 0.0F, -0.90F}),
        Mat4::translation({-0.20F, 0.0F, -0.20F}),
    };
    const std::vector<Mat4> transforms_c{
        Mat4::translation({12.0F, 0.0F, 0.0F}),
        Mat4::translation({12.0F, 0.0F, -0.25F}),
        Mat4::translation({12.0F, 0.0F, -0.80F}),
    };

    const std::array<OfflineSceneFrameState, 4> frames{{
        OfflineSceneFrameState{camera, transforms_a},
        OfflineSceneFrameState{camera, transforms_b},
        OfflineSceneFrameState{camera, transforms_c},
        OfflineSceneFrameState{camera, transforms_a},
    }};

    const PreparedOfflineCameraSequence prepared =
        prepare_offline_frame_sequence(reusable, frames);
    check(
        prepared.frame_count() == frames.size(),
        "prepared affine frame sequence exposes one indexed frame per transform record");

    const std::array<const std::vector<Mat4>*, 4> expected_transforms{{
        &transforms_a, &transforms_b, &transforms_c, &transforms_a,
    }};
    for (std::size_t frame_index = 0U;
         frame_index < frames.size();
         ++frame_index) {
        const std::vector<Mat4>& models = *expected_transforms[frame_index];
        const std::array<OfflineSceneEntry, 3> manual_entries{{
            OfflineSceneEntry{
                &coverage,
                models[0],
                {},
                OfflineSceneTransparencyMode::AlphaToCoverage},
            OfflineSceneEntry{
                &green,
                models[1],
                {},
                OfflineSceneTransparencyMode::SourceAlpha},
            OfflineSceneEntry{
                &blue,
                models[2],
                {},
                OfflineSceneTransparencyMode::SourceAlpha},
        }};
        const Framebuffer expected = render_scene_preview(
            manual_entries,
            settings,
            OfflineSceneOrdering::MixedTransparency,
            camera);
        const Framebuffer actual =
            render_prepared_camera_sequence_frame(prepared, frame_index);
        check(
            exact_frame_equal(actual, expected),
            "prepared affine frame is exact per-sample equivalent to manually rebuilt per-frame prepared submissions");
    }

    const Framebuffer frame_a =
        render_prepared_camera_sequence_frame(prepared, 0U);
    const Framebuffer frame_b =
        render_prepared_camera_sequence_frame(prepared, 1U);
    const Framebuffer frame_c =
        render_prepared_camera_sequence_frame(prepared, 2U);
    const Framebuffer frame_a_repeat =
        render_prepared_camera_sequence_frame(prepared, 3U);
    check(
        !exact_frame_equal(frame_a, frame_b),
        "per-frame transforms observably reevaluate caller-order placement and source-alpha depth ordering");
    check(
        exact_frame_equal(frame_a, frame_a_repeat),
        "A-B-C-A transform sequence preserves exact repeated-frame determinism");
    Framebuffer clear_reference(
        settings.width,
        settings.height,
        settings.sample_count);
    clear_reference.clear(settings.clear_color);
    check(
        exact_frame_equal(frame_c, clear_reference),
        "far-out frame is exact clear-target output and cannot reuse stale visibility from an earlier transform");
}

void test_bounded_timeline_sampling_matches_manual_frame_sequence() {
    OfflineRenderSettings settings;
    settings.width = 61U;
    settings.height = 47U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};

    const ModelAsset coverage = triangle_asset(
        {0.85F, 0.15F, 0.10F},
        {0.0F, 0.0F, 0.0F},
        0.55F);
    const ModelAsset green = triangle_asset(
        {0.10F, 0.75F, 0.20F},
        {0.0F, 0.0F, 0.0F},
        0.45F);
    const ModelAsset blue = triangle_asset(
        {0.10F, 0.25F, 0.85F},
        {0.0F, 0.0F, 0.0F},
        0.55F);

    const std::array<OfflineSceneEntry, 3> base_entries{{
        OfflineSceneEntry{
            &coverage,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::AlphaToCoverage},
        OfflineSceneEntry{
            &green,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
        OfflineSceneEntry{
            &blue,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(base_entries, settings);

    const OfflineSceneCamera camera_a = camera_at({0.0F, 0.0F, 3.0F});
    const OfflineSceneCamera camera_b = camera_at({1.0F, 0.25F, 3.0F});
    const OfflineSceneCamera camera_mid = camera_at({0.5F, 0.125F, 3.0F});

    const std::vector<Mat4> transforms_a{
        Mat4::translation({-0.5F, 0.0F, 0.0F}),
        Mat4::translation({0.25F, 0.0F, -0.25F}),
        Mat4::translation({0.125F, 0.0F, -0.75F}),
    };
    const std::vector<Mat4> transforms_b{
        Mat4::translation({0.5F, 0.0F, 0.0F}),
        Mat4::translation({-0.25F, 0.0F, -0.75F}),
        Mat4::translation({-0.125F, 0.0F, -0.25F}),
    };
    const std::vector<Mat4> transforms_mid{
        Mat4::translation({0.0F, 0.0F, 0.0F}),
        Mat4::translation({0.0F, 0.0F, -0.5F}),
        Mat4::translation({0.0F, 0.0F, -0.5F}),
    };

    const std::array<OfflineSceneTimelineKeyframe, 2> keyframes{{
        OfflineSceneTimelineKeyframe{
            0.0F,
            OfflineSceneFrameState{camera_a, transforms_a}},
        OfflineSceneTimelineKeyframe{
            2.0F,
            OfflineSceneFrameState{camera_b, transforms_b}},
    }};
    const std::array<float, 4> sample_times{{0.0F, 1.0F, 0.0F, 2.0F}};

    const std::vector<OfflineSceneFrameState> sampled =
        sample_offline_frame_timeline(keyframes, sample_times);
    check(
        sampled.size() == sample_times.size(),
        "bounded timeline preserves exact caller sample order and count");
    if (sampled.size() == sample_times.size()) {
        check(
            sampled[1].camera.eye.x == 0.5F
                && sampled[1].camera.eye.y == 0.125F
                && sampled[1].camera.eye.z == 3.0F,
            "timeline linearly interpolates camera components at the bounded interior sample");
        check(
            sampled[1].model_transforms[0](0U, 3U) == 0.0F
                && sampled[1].model_transforms[1](2U, 3U) == -0.5F
                && sampled[1].model_transforms[2](2U, 3U) == -0.5F,
            "timeline linearly interpolates affine top-3x4 coefficients");
        for (const Mat4& model : sampled[1].model_transforms) {
            check(
                model(3U, 0U) == 0.0F
                    && model(3U, 1U) == 0.0F
                    && model(3U, 2U) == 0.0F
                    && model(3U, 3U) == 1.0F,
                "timeline preserves the affine bottom row exactly");
        }
    }

    const std::array<OfflineSceneFrameState, 4> manual_frames{{
        OfflineSceneFrameState{camera_a, transforms_a},
        OfflineSceneFrameState{camera_mid, transforms_mid},
        OfflineSceneFrameState{camera_a, transforms_a},
        OfflineSceneFrameState{camera_b, transforms_b},
    }};
    const PreparedOfflineCameraSequence timeline =
        prepare_offline_timeline_sequence(
            reusable,
            keyframes,
            sample_times);
    const PreparedOfflineCameraSequence manual =
        prepare_offline_frame_sequence(reusable, manual_frames);

    check(
        timeline.frame_count() == manual.frame_count(),
        "timeline preparation delegates to the same bounded M92 frame transaction");
    for (std::size_t index = 0U;
         index < timeline.frame_count() && index < manual.frame_count();
         ++index) {
        const Framebuffer timeline_frame =
            render_prepared_camera_sequence_frame(timeline, index);
        const Framebuffer manual_frame =
            render_prepared_camera_sequence_frame(manual, index);
        check(
            exact_frame_equal(timeline_frame, manual_frame),
            "timeline sample is exact resolved and per-sample equivalent to the explicit M92 frame record");
    }

    if (timeline.frame_count() == 4U) {
        const Framebuffer first =
            render_prepared_camera_sequence_frame(timeline, 0U);
        const Framebuffer interior =
            render_prepared_camera_sequence_frame(timeline, 1U);
        const Framebuffer repeated =
            render_prepared_camera_sequence_frame(timeline, 2U);
        const Framebuffer last =
            render_prepared_camera_sequence_frame(timeline, 3U);
        check(
            exact_frame_equal(first, repeated),
            "repeated sample times preserve caller order and exact deterministic output");
        check(
            !exact_frame_equal(first, interior)
                && !exact_frame_equal(interior, last),
            "camera/model interpolation observably reevaluates mixed-transparency ordering and visibility");
    }
}

void test_bounded_timeline_validation_contract() {
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});
    const OfflineSceneFrameState frame{
        camera,
        {Mat4::identity()},
    };
    const std::array<OfflineSceneTimelineKeyframe, 2> valid{{
        OfflineSceneTimelineKeyframe{0.0F, frame},
        OfflineSceneTimelineKeyframe{2.0F, frame},
    }};
    const std::array<float, 1> one_sample{{1.0F}};

    const std::array<OfflineSceneTimelineKeyframe, 1> underspecified{{
        OfflineSceneTimelineKeyframe{0.0F, frame},
    }};
    check_throws<std::invalid_argument>(
        [&] { (void)sample_offline_frame_timeline(underspecified, one_sample); },
        "timeline rejects an underspecified one-keyframe interpolation span");

    const std::array<OfflineSceneTimelineKeyframe, 2> duplicate_times{{
        OfflineSceneTimelineKeyframe{0.0F, frame},
        OfflineSceneTimelineKeyframe{0.0F, frame},
    }};
    check_throws<std::invalid_argument>(
        [&] { (void)sample_offline_frame_timeline(duplicate_times, one_sample); },
        "timeline rejects duplicate/non-increasing keyframe times");

    const std::array<OfflineSceneTimelineKeyframe, 2> nonfinite_time{{
        OfflineSceneTimelineKeyframe{0.0F, frame},
        OfflineSceneTimelineKeyframe{
            std::numeric_limits<float>::infinity(),
            frame},
    }};
    check_throws<std::invalid_argument>(
        [&] { (void)sample_offline_frame_timeline(nonfinite_time, one_sample); },
        "timeline rejects non-finite keyframe time");

    const std::array<float, 1> out_of_domain{{2.5F}};
    check_throws<std::out_of_range>(
        [&] { (void)sample_offline_frame_timeline(valid, out_of_domain); },
        "timeline rejects sample requests outside the bounded keyframe domain");

    std::vector<float> too_many_samples(
        detail::kMaxOfflineTimelineSamples + 1U,
        1.0F);
    check_throws<std::invalid_argument>(
        [&] { (void)sample_offline_frame_timeline(valid, too_many_samples); },
        "timeline rejects more than the bounded sample count before output allocation");

    std::vector<OfflineSceneTimelineKeyframe> too_many_keyframes;
    too_many_keyframes.reserve(detail::kMaxOfflineTimelineKeyframes + 1U);
    for (std::size_t index = 0U;
         index < detail::kMaxOfflineTimelineKeyframes + 1U;
         ++index) {
        too_many_keyframes.push_back(
            OfflineSceneTimelineKeyframe{
                static_cast<float>(index),
                frame});
    }
    check_throws<std::invalid_argument>(
        [&] { (void)sample_offline_frame_timeline(too_many_keyframes, one_sample); },
        "timeline rejects more than the bounded keyframe count");

    OfflineSceneFrameState too_many_models = frame;
    too_many_models.model_transforms.assign(
        detail::kMaxOfflineSceneEntries + 1U,
        Mat4::identity());
    const std::array<OfflineSceneTimelineKeyframe, 2> oversized_models{{
        OfflineSceneTimelineKeyframe{0.0F, too_many_models},
        OfflineSceneTimelineKeyframe{2.0F, too_many_models},
    }};
    check_throws<std::invalid_argument>(
        [&] { (void)sample_offline_frame_timeline(oversized_models, one_sample); },
        "timeline rejects model-transform ownership above the bounded scene entry limit before sample allocation");

    OfflineSceneFrameState two_models = frame;
    two_models.model_transforms.push_back(Mat4::identity());
    const std::array<OfflineSceneTimelineKeyframe, 2> inconsistent_models{{
        OfflineSceneTimelineKeyframe{0.0F, frame},
        OfflineSceneTimelineKeyframe{2.0F, two_models},
    }};
    check_throws<std::invalid_argument>(
        [&] { (void)sample_offline_frame_timeline(inconsistent_models, one_sample); },
        "timeline rejects inconsistent keyframe transform counts");

    OfflineSceneFrameState projective = frame;
    projective.model_transforms[0] =
        Mat4::perspective(radians(55.0F), 1.0F, 0.1F, 20.0F);
    const std::array<OfflineSceneTimelineKeyframe, 2> projective_keyframe{{
        OfflineSceneTimelineKeyframe{0.0F, frame},
        OfflineSceneTimelineKeyframe{2.0F, projective},
    }};
    check_throws<std::invalid_argument>(
        [&] { (void)sample_offline_frame_timeline(projective_keyframe, one_sample); },
        "timeline validates every keyframe affine transform before sampling");

    check(
        sample_offline_frame_timeline(
            valid,
            std::span<const float>{}).empty(),
        "empty timeline sample requests are a deterministic no-op after keyframe validation");

    std::size_t shade_calls = 0U;
    ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F});
    ModelRenderOptions options;
    options.fragment_program =
        std::make_shared<CountingFragmentProgram>(&shade_calls);
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

    const std::array<OfflineSceneTimelineKeyframe, 2> wrong_scene_count{{
        OfflineSceneTimelineKeyframe{
            0.0F,
            OfflineSceneFrameState{camera, {}}},
        OfflineSceneTimelineKeyframe{
            2.0F,
            OfflineSceneFrameState{camera, {}}},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_timeline_sequence(
                reusable,
                wrong_scene_count,
                std::span<const float>{});
        },
        "timeline checks every keyframe against prepared scene entry count even for an empty sample request");

    OfflineSceneCamera crossing_a = camera;
    crossing_a.eye = {-1.0F, 0.0F, 3.0F};
    crossing_a.target = {1.0F, 0.0F, 3.0F};
    OfflineSceneCamera crossing_b = camera;
    crossing_b.eye = {1.0F, 0.0F, 3.0F};
    crossing_b.target = {-1.0F, 0.0F, 3.0F};
    const std::array<OfflineSceneTimelineKeyframe, 2> crossing{{
        OfflineSceneTimelineKeyframe{
            0.0F,
            OfflineSceneFrameState{
                crossing_a,
                {Mat4::identity()}}},
        OfflineSceneTimelineKeyframe{
            2.0F,
            OfflineSceneFrameState{
                crossing_b,
                {Mat4::identity()}}},
    }};
    const std::array<float, 2> crossing_samples{{0.0F, 1.0F}};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_timeline_sequence(
                reusable,
                crossing,
                crossing_samples);
        },
        "later interpolated invalid camera rejects the complete timeline transaction");
    check(
        shade_calls == 0U,
        "later invalid interpolated state rejects before any earlier timeline sample fragment execution");
}

void test_prepared_frame_sequence_rejects_invalid_transform_records() {
    OfflineRenderSettings settings;
    settings.width = 37U;
    settings.height = 29U;
    settings.sample_count = SampleCount::Four;

    const ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F},
        0.5F);
    const std::array<OfflineSceneEntry, 1> entries{{
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});

    const std::array<OfflineSceneFrameState, 1> missing_transform{{
        OfflineSceneFrameState{camera, {}},
    }};
    check_throws<std::invalid_argument>(
        [&] { (void)prepare_offline_frame_sequence(reusable, missing_transform); },
        "prepared affine frame sequence rejects a transform record whose entry count does not match the prepared scene");

    const std::array<OfflineSceneFrameState, 2> later_projective{{
        OfflineSceneFrameState{
            camera,
            {Mat4::translation({0.0F, 0.0F, -0.2F})}},
        OfflineSceneFrameState{
            camera,
            {Mat4::perspective(
                radians(55.0F),
                1.0F,
                0.1F,
                20.0F)}},
    }};
    check_throws<std::invalid_argument>(
        [&] { (void)prepare_offline_frame_sequence(reusable, later_projective); },
        "later projective model transform rejects the complete prepared frame transaction");

    const std::array<OfflineSceneFrameState, 2> valid_frames{{
        OfflineSceneFrameState{
            camera,
            {Mat4::translation({-0.2F, 0.0F, -0.1F})}},
        OfflineSceneFrameState{
            camera,
            {Mat4::translation({0.2F, 0.0F, -0.4F})}},
    }};
    const PreparedOfflineCameraSequence prepared =
        prepare_offline_frame_sequence(reusable, valid_frames);
    check(
        prepared.frame_count() == valid_frames.size(),
        "finite affine transforms pass the prepared frame transaction");
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
    test_prepared_camera_sequence_plan_matches_existing_execution();
    test_prepared_camera_sequence_retains_plan_after_source_destruction();
    test_camera_sequence_preflights_every_camera_before_execution();
    test_camera_sequence_resource_bound();
    test_compatibility_sequence_preserves_historical_camera_capacity();
    test_prepared_frame_sequence_matches_manual_per_frame_scenes();
    test_bounded_timeline_sampling_matches_manual_frame_sequence();
    test_bounded_timeline_validation_contract();
    test_prepared_frame_sequence_rejects_invalid_transform_records();
    test_reusable_scene_validation_contract();

    if (failures != 0) {
        std::cerr << failures << " reusable offline prepared-scene test(s) failed\n";
        return 1;
    }
    std::cout << "reusable offline prepared-scene tests passed\n";
    return 0;
}
