#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
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

bool exact_matrix_equal(const Mat4& left, const Mat4& right) {
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            if (left(row, column) != right(row, column)) {
                return false;
            }
        }
    }
    return true;
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

void test_programmatic_hierarchy_matches_explicit_world_frames() {
    OfflineRenderSettings settings;
    settings.width = 61U;
    settings.height = 47U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};

    const ModelAsset red = triangle_asset(
        {0.85F, 0.15F, 0.10F},
        {0.0F, 0.0F, 0.0F});
    const ModelAsset green = triangle_asset(
        {0.10F, 0.75F, 0.20F},
        {0.0F, 0.0F, 0.0F});
    const ModelAsset blue = triangle_asset(
        {0.10F, 0.25F, 0.85F},
        {0.0F, 0.0F, 0.0F});

    const std::array<OfflineSceneEntry, 3> entries{{
        OfflineSceneEntry{
            &red,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::Opaque},
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
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});

    const OfflineSceneHierarchy roots({
        std::nullopt,
        std::nullopt,
        std::nullopt,
    });
    const std::vector<Mat4> root_world_a{
        Mat4::translation({-0.45F, 0.0F, -0.1F}),
        Mat4::translation({0.20F, 0.0F, -0.35F}),
        Mat4::translation({0.45F, 0.0F, -0.7F}),
    };
    const std::vector<Mat4> root_world_b{
        Mat4::translation({0.45F, 0.0F, -0.1F}),
        Mat4::translation({-0.20F, 0.0F, -0.7F}),
        Mat4::translation({-0.45F, 0.0F, -0.35F}),
    };
    const std::array<OfflineSceneHierarchicalFrameState, 2> root_frames{{
        OfflineSceneHierarchicalFrameState{camera, root_world_a},
        OfflineSceneHierarchicalFrameState{camera, root_world_b},
    }};
    const std::array<OfflineSceneFrameState, 2> explicit_root_frames{{
        OfflineSceneFrameState{camera, root_world_a},
        OfflineSceneFrameState{camera, root_world_b},
    }};

    const PreparedOfflineCameraSequence hierarchical_roots =
        prepare_offline_hierarchy_sequence(
            reusable,
            roots,
            root_frames);
    const PreparedOfflineCameraSequence explicit_roots =
        prepare_offline_frame_sequence(
            reusable,
            explicit_root_frames);
    check(
        hierarchical_roots.frame_count() == explicit_roots.frame_count(),
        "root-only hierarchy preserves the explicit M92 frame count");
    for (std::size_t index = 0U;
         index < hierarchical_roots.frame_count()
             && index < explicit_roots.frame_count();
         ++index) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(hierarchical_roots, index),
                render_prepared_camera_sequence_frame(explicit_roots, index)),
            "root-only hierarchy is exact resolved/hash and 4x per-sample equivalent to explicit M92 world transforms");
    }

    // Parent indices deliberately point forward in entry order:
    // entry 2 is the root, entry 0 is its child, entry 1 is the grandchild.
    const OfflineSceneHierarchy hierarchy({
        std::optional<std::size_t>{2U},
        std::optional<std::size_t>{0U},
        std::nullopt,
    });
    const std::vector<Mat4> local_a{
        Mat4::translation({0.25F, 0.0F, -0.15F}),
        Mat4::translation({0.20F, 0.0F, -0.15F}),
        Mat4::translation({-0.55F, 0.0F, -0.10F}),
    };
    const std::vector<Mat4> local_b{
        Mat4::translation({0.25F, 0.0F, -0.15F}),
        Mat4::translation({0.20F, 0.0F, -0.15F}),
        Mat4::translation({0.35F, 0.0F, -0.45F}),
    };
    const std::vector<Mat4> world_a{
        local_a[2] * local_a[0],
        local_a[2] * local_a[0] * local_a[1],
        local_a[2],
    };
    const std::vector<Mat4> world_b{
        local_b[2] * local_b[0],
        local_b[2] * local_b[0] * local_b[1],
        local_b[2],
    };
    const std::array<OfflineSceneHierarchicalFrameState, 2> local_frames{{
        OfflineSceneHierarchicalFrameState{camera, local_a},
        OfflineSceneHierarchicalFrameState{camera, local_b},
    }};
    const std::array<OfflineSceneFrameState, 2> manual_world_frames{{
        OfflineSceneFrameState{camera, world_a},
        OfflineSceneFrameState{camera, world_b},
    }};

    const PreparedOfflineCameraSequence hierarchical =
        prepare_offline_hierarchy_sequence(
            reusable,
            hierarchy,
            local_frames);
    const PreparedOfflineCameraSequence manual =
        prepare_offline_frame_sequence(
            reusable,
            manual_world_frames);
    for (std::size_t index = 0U;
         index < hierarchical.frame_count() && index < manual.frame_count();
         ++index) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(hierarchical, index),
                render_prepared_camera_sequence_frame(manual, index)),
            "arbitrary-order hierarchy resolves parent_world * local exactly before M92 preparation");
    }
    if (hierarchical.frame_count() == 2U) {
        check(
            !exact_frame_equal(
                render_prepared_camera_sequence_frame(hierarchical, 0U),
                render_prepared_camera_sequence_frame(hierarchical, 1U)),
            "moving a later-index root observably moves its earlier-index child chain");
    }
}

void test_programmatic_hierarchy_validation_contract() {
    check_throws<std::out_of_range>(
        [] {
            (void)OfflineSceneHierarchy({
                std::optional<std::size_t>{1U},
            });
        },
        "hierarchy rejects out-of-range parent references");
    check_throws<std::invalid_argument>(
        [] {
            (void)OfflineSceneHierarchy({
                std::optional<std::size_t>{0U},
            });
        },
        "hierarchy rejects self-parenting");
    check_throws<std::invalid_argument>(
        [] {
            (void)OfflineSceneHierarchy({
                std::optional<std::size_t>{1U},
                std::optional<std::size_t>{0U},
            });
        },
        "hierarchy rejects parent cycles independent of entry order");

    std::size_t shade_calls = 0U;
    const ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F});
    ModelRenderOptions options;
    options.fragment_program =
        std::make_shared<CountingFragmentProgram>(&shade_calls);
    OfflineRenderSettings settings;
    settings.width = 31U;
    settings.height = 31U;
    settings.sample_count = SampleCount::Four;
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});

    const OfflineSceneHierarchy wrong_size({
        std::nullopt,
    });
    const std::array<OfflineSceneHierarchicalFrameState, 1> wrong_size_frame{{
        OfflineSceneHierarchicalFrameState{
            camera,
            {Mat4::identity()}},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_hierarchy_sequence(
                reusable,
                wrong_size,
                wrong_size_frame);
        },
        "hierarchy entry ownership must match the prepared scene before frame resolution");

    const OfflineSceneHierarchy hierarchy({
        std::nullopt,
        std::optional<std::size_t>{0U},
    });
    const std::array<OfflineSceneHierarchicalFrameState, 1> missing_local{{
        OfflineSceneHierarchicalFrameState{
            camera,
            {Mat4::identity()}},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_hierarchy_sequence(
                reusable,
                hierarchy,
                missing_local);
        },
        "hierarchical frame local-transform count must match hierarchy entry count");

    const std::array<OfflineSceneHierarchicalFrameState, 2> later_projective{{
        OfflineSceneHierarchicalFrameState{
            camera,
            {Mat4::identity(), Mat4::identity()}},
        OfflineSceneHierarchicalFrameState{
            camera,
            {
                Mat4::identity(),
                Mat4::perspective(
                    radians(55.0F),
                    1.0F,
                    0.1F,
                    20.0F),
            }},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_hierarchy_sequence(
                reusable,
                hierarchy,
                later_projective);
        },
        "later projective local transform rejects the complete hierarchy transaction");
    check(
        shade_calls == 0U,
        "later invalid hierarchical frame rejects before any earlier fragment execution");

    Mat4 huge_parent = Mat4::identity();
    Mat4 huge_child = Mat4::identity();
    huge_parent(0U, 0U) = std::numeric_limits<float>::max();
    huge_child(0U, 0U) = 2.0F;
    const std::array<OfflineSceneHierarchicalFrameState, 1> overflow_composition{{
        OfflineSceneHierarchicalFrameState{
            camera,
            {huge_parent, huge_child}},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_hierarchy_sequence(
                reusable,
                hierarchy,
                overflow_composition);
        },
        "finite affine locals whose composition overflows are rejected after world-transform composition");
    check(
        shade_calls == 0U,
        "composed-world overflow remains fail-closed before fragment execution");

    std::vector<OfflineSceneHierarchicalFrameState> too_many_frames(
        detail::kMaxOfflineSequenceCameras + 1U,
        OfflineSceneHierarchicalFrameState{
            camera,
            {Mat4::identity(), Mat4::identity()}});
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_hierarchy_sequence(
                reusable,
                hierarchy,
                too_many_frames);
        },
        "hierarchy sequence enforces the established bounded frame count before world-frame allocation");
}



void test_programmatic_transform_graph_matches_hierarchy_and_group_reference() {
    OfflineRenderSettings settings;
    settings.width = 61U;
    settings.height = 47U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};

    const ModelAsset red = triangle_asset(
        {0.85F, 0.15F, 0.10F},
        {0.0F, 0.0F, 0.0F});
    const ModelAsset green = triangle_asset(
        {0.10F, 0.75F, 0.20F},
        {0.0F, 0.0F, 0.0F});
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &red,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &green,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});

    // A 1:1 graph is an exact semantic replacement for the M96 hierarchy.
    // The child points forward to node/entry 1 to retain arbitrary parent order.
    const OfflineSceneHierarchy hierarchy({
        std::optional<std::size_t>{1U},
        std::nullopt,
    });
    const OfflineSceneTransformGraph one_to_one_graph(
        {
            std::optional<std::size_t>{1U},
            std::nullopt,
        },
        {0U, 1U});
    const std::vector<Mat4> local_a{
        Mat4::translation({0.30F, 0.0F, -0.15F}),
        Mat4::translation({-0.45F, 0.0F, -0.20F}),
    };
    const std::vector<Mat4> local_b{
        Mat4::translation({-0.25F, 0.0F, -0.10F}),
        Mat4::translation({0.35F, 0.0F, -0.45F}),
    };
    const std::array<OfflineSceneHierarchicalFrameState, 2> hierarchy_frames{{
        OfflineSceneHierarchicalFrameState{camera, local_a},
        OfflineSceneHierarchicalFrameState{camera, local_b},
    }};
    const std::array<OfflineSceneTransformGraphFrameState, 2> graph_frames{{
        OfflineSceneTransformGraphFrameState{camera, local_a},
        OfflineSceneTransformGraphFrameState{camera, local_b},
    }};

    const PreparedOfflineCameraSequence hierarchical =
        prepare_offline_hierarchy_sequence(
            reusable,
            hierarchy,
            hierarchy_frames);
    const PreparedOfflineCameraSequence graph_sequence =
        prepare_offline_transform_graph_sequence(
            reusable,
            one_to_one_graph,
            graph_frames);
    check(
        hierarchical.frame_count() == graph_sequence.frame_count()
            && graph_sequence.frame_count() == 2U,
        "1:1 transform graph preserves M96 frame ownership");
    for (std::size_t index = 0U;
         index < hierarchical.frame_count()
             && index < graph_sequence.frame_count();
         ++index) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(hierarchical, index),
                render_prepared_camera_sequence_frame(graph_sequence, index)),
            "1:1 transform graph is exact resolved/hash and 4x per-sample equivalent to M96 hierarchy execution");
    }

    // Node 0 is a non-renderable pivot. Nodes 1 and 2 are independently
    // bound to prepared entries 0 and 1. Moving/scaling the pivot must move
    // both render entries without changing M92 ownership or execution order.
    const OfflineSceneTransformGraph grouped_graph(
        {
            std::nullopt,
            std::optional<std::size_t>{0U},
            std::optional<std::size_t>{0U},
        },
        {1U, 2U});
    const Mat4 pivot_a =
        Mat4::translation({-0.20F, 0.0F, -0.30F})
        * Mat4::scale({1.0F, 1.0F, 1.0F});
    const Mat4 pivot_b =
        Mat4::translation({0.25F, 0.0F, -0.40F})
        * Mat4::scale({1.4F, 1.0F, 1.0F});
    const Mat4 child_red =
        Mat4::translation({-0.30F, 0.0F, 0.0F});
    const Mat4 child_green =
        Mat4::translation({0.30F, 0.0F, -0.15F});
    const std::array<OfflineSceneTransformGraphFrameState, 2> grouped_frames{{
        OfflineSceneTransformGraphFrameState{
            camera,
            {pivot_a, child_red, child_green}},
        OfflineSceneTransformGraphFrameState{
            camera,
            {pivot_b, child_red, child_green}},
    }};
    const std::array<OfflineSceneFrameState, 2> manual_world_frames{{
        OfflineSceneFrameState{
            camera,
            {pivot_a * child_red, pivot_a * child_green}},
        OfflineSceneFrameState{
            camera,
            {pivot_b * child_red, pivot_b * child_green}},
    }};

    const PreparedOfflineCameraSequence grouped =
        prepare_offline_transform_graph_sequence(
            reusable,
            grouped_graph,
            grouped_frames);
    const PreparedOfflineCameraSequence manual =
        prepare_offline_frame_sequence(
            reusable,
            manual_world_frames);
    check(
        grouped.frame_count() == manual.frame_count()
            && grouped.frame_count() == 2U,
        "transform-only pivot graph preserves prepared render-entry frame count");
    for (std::size_t index = 0U;
         index < grouped.frame_count() && index < manual.frame_count();
         ++index) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(grouped, index),
                render_prepared_camera_sequence_frame(manual, index)),
            "transform-only pivot graph matches independently composed M92 render-entry world transforms exactly");
    }
    if (grouped.frame_count() == 2U) {
        check(
            !exact_frame_equal(
                render_prepared_camera_sequence_frame(grouped, 0U),
                render_prepared_camera_sequence_frame(grouped, 1U)),
            "moving one non-renderable pivot observably moves its multiple mapped render descendants");
    }
}

void test_programmatic_transform_graph_validation_contract() {
    check_throws<std::out_of_range>(
        [] {
            (void)OfflineSceneTransformGraph(
                {std::optional<std::size_t>{1U}},
                {0U});
        },
        "transform graph rejects out-of-range parent references");
    check_throws<std::invalid_argument>(
        [] {
            (void)OfflineSceneTransformGraph(
                {std::optional<std::size_t>{0U}},
                {0U});
        },
        "transform graph rejects self-parenting");
    check_throws<std::invalid_argument>(
        [] {
            (void)OfflineSceneTransformGraph(
                {
                    std::optional<std::size_t>{1U},
                    std::optional<std::size_t>{0U},
                },
                {0U});
        },
        "transform graph rejects cycles independent of node order");
    check_throws<std::out_of_range>(
        [] {
            (void)OfflineSceneTransformGraph(
                {std::nullopt},
                {1U});
        },
        "transform graph rejects render bindings outside graph ownership");
    check_throws<std::invalid_argument>(
        [] {
            (void)OfflineSceneTransformGraph(
                {std::nullopt, std::nullopt},
                {0U, 0U});
        },
        "transform graph rejects duplicate render-entry node bindings");

    std::vector<std::optional<std::size_t>> too_many_nodes(
        detail::kMaxOfflineTransformGraphNodes + 1U,
        std::nullopt);
    check_throws<std::invalid_argument>(
        [&] {
            (void)OfflineSceneTransformGraph(
                too_many_nodes,
                {});
        },
        "transform graph rejects node ownership above the bounded graph limit");

    std::size_t shade_calls = 0U;
    const ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F});
    ModelRenderOptions options;
    options.fragment_program =
        std::make_shared<CountingFragmentProgram>(&shade_calls);

    OfflineRenderSettings settings;
    settings.width = 31U;
    settings.height = 31U;
    settings.sample_count = SampleCount::Four;
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});

    const OfflineSceneTransformGraph missing_binding_graph(
        {
            std::nullopt,
            std::optional<std::size_t>{0U},
            std::optional<std::size_t>{0U},
        },
        {1U});
    const std::array<OfflineSceneTransformGraphFrameState, 1>
        missing_binding_frame{{
            OfflineSceneTransformGraphFrameState{
                camera,
                {
                    Mat4::identity(),
                    Mat4::identity(),
                    Mat4::identity(),
                }},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_transform_graph_sequence(
                reusable,
                missing_binding_graph,
                missing_binding_frame);
        },
        "transform graph requires exactly one render binding per prepared scene entry");

    const OfflineSceneTransformGraph graph(
        {
            std::nullopt,
            std::optional<std::size_t>{0U},
            std::optional<std::size_t>{0U},
        },
        {1U, 2U});

    const std::array<OfflineSceneTransformGraphFrameState, 1> missing_local{{
        OfflineSceneTransformGraphFrameState{
            camera,
            {Mat4::identity(), Mat4::identity()}},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_transform_graph_sequence(
                reusable,
                graph,
                missing_local);
        },
        "transform graph frame local-transform count must match complete graph node count");

    // Node 0 and node 1 are render-bound roots; node 2 is completely hidden
    // from rendering. Its invalid state must still reject the complete graph.
    const OfflineSceneTransformGraph hidden_group_graph(
        {
            std::nullopt,
            std::nullopt,
            std::nullopt,
        },
        {0U, 1U});
    const std::array<OfflineSceneTransformGraphFrameState, 2>
        hidden_invalid_later{{
            OfflineSceneTransformGraphFrameState{
                camera,
                {
                    Mat4::identity(),
                    Mat4::identity(),
                    Mat4::identity(),
                }},
            OfflineSceneTransformGraphFrameState{
                camera,
                {
                    Mat4::identity(),
                    Mat4::identity(),
                    Mat4::perspective(
                        radians(55.0F),
                        1.0F,
                        0.1F,
                        20.0F),
                }},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_transform_graph_sequence(
                reusable,
                hidden_group_graph,
                hidden_invalid_later);
        },
        "invalid later transform-only node rejects the complete graph transaction");
    check(
        shade_calls == 0U,
        "hidden transform-only graph failure occurs before any fragment execution");

    Mat4 huge_pivot = Mat4::identity();
    huge_pivot(0U, 0U) = std::numeric_limits<float>::max();
    Mat4 child_scale = Mat4::identity();
    child_scale(0U, 0U) = 2.0F;
    const std::array<OfflineSceneTransformGraphFrameState, 2>
        overflow_later{{
            OfflineSceneTransformGraphFrameState{
                camera,
                {
                    Mat4::identity(),
                    Mat4::identity(),
                    Mat4::identity(),
                }},
            OfflineSceneTransformGraphFrameState{
                camera,
                {
                    huge_pivot,
                    child_scale,
                    Mat4::identity(),
                }},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_transform_graph_sequence(
                reusable,
                graph,
                overflow_later);
        },
        "finite transform-only ancestor whose child composition overflows rejects the complete graph transaction");
    check(
        shade_calls == 0U,
        "later transform-graph composition failure leaves earlier frames unexecutable");

    std::vector<OfflineSceneTransformGraphFrameState> too_many_frames(
        detail::kMaxOfflineSequenceCameras + 1U,
        OfflineSceneTransformGraphFrameState{
            camera,
            {
                Mat4::identity(),
                Mat4::identity(),
                Mat4::identity(),
            }});
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_transform_graph_sequence(
                reusable,
                graph,
                too_many_frames);
        },
        "transform graph sequence enforces the established bounded frame count");
}



void test_programmatic_sparse_transform_graph_clip_matches_dense_m100() {
    OfflineRenderSettings settings;
    settings.width = 61U;
    settings.height = 47U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};

    const ModelAsset red = triangle_asset(
        {0.85F, 0.15F, 0.10F},
        {0.0F, 0.0F, 0.0F});
    const ModelAsset green = triangle_asset(
        {0.10F, 0.75F, 0.20F},
        {0.0F, 0.0F, 0.0F});
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &red,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &green,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);

    const OfflineSceneTransformGraph graph(
        {
            std::nullopt,
            std::optional<std::size_t>{0U},
            std::optional<std::size_t>{0U},
        },
        {1U, 2U});

    const OfflineSceneCamera camera_a =
        camera_at({0.0F, 0.0F, 3.0F});
    OfflineSceneCamera camera_b =
        camera_at({0.45F, 0.15F, 3.0F});
    camera_b.target = {0.10F, 0.0F, -0.10F};

    const Mat4 pivot_a =
        Mat4::translation({-0.20F, 0.0F, -0.30F})
        * Mat4::scale({1.0F, 1.0F, 1.0F});
    const Mat4 pivot_b =
        Mat4::translation({0.20F, 0.0F, -0.30F})
        * Mat4::scale({1.5F, 1.0F, 1.0F});
    const Mat4 red_a = Mat4::translation({-0.25F, 0.0F, 0.0F});
    const Mat4 red_b = Mat4::translation({0.15F, 0.0F, -0.05F});
    const Mat4 green_a = Mat4::translation({0.30F, 0.0F, -0.10F});
    const Mat4 green_b = Mat4::translation({-0.20F, 0.0F, -0.15F});

    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 2>
        dense_keyframes{{
            OfflineSceneTransformGraphTimelineKeyframe{
                0.0F,
                OfflineSceneTransformGraphFrameState{
                    camera_a,
                    {pivot_a, red_a, green_a}}},
            OfflineSceneTransformGraphTimelineKeyframe{
                2.0F,
                OfflineSceneTransformGraphFrameState{
                    camera_b,
                    {pivot_b, red_b, green_b}}},
        }};

    std::optional<std::vector<OfflineSceneSparseCameraKeyframe>>
        camera_track{std::vector<OfflineSceneSparseCameraKeyframe>{
            {0.0F, camera_a},
            {2.0F, camera_b},
        }};
    std::vector<OfflineSceneSparseTransformTrack> tracks{
        OfflineSceneSparseTransformTrack{
            0U,
            {
                {0.0F, pivot_a},
                {2.0F, pivot_b},
            }},
        OfflineSceneSparseTransformTrack{
            1U,
            {
                {0.0F, red_a},
                {2.0F, red_b},
            }},
        OfflineSceneSparseTransformTrack{
            2U,
            {
                {0.0F, green_a},
                {2.0F, green_b},
            }},
    };
    const OfflineSceneSparseTransformGraphClip sparse_clip(
        0.0F,
        2.0F,
        camera_a,
        {pivot_a, red_a, green_a},
        std::move(camera_track),
        std::move(tracks));

    const std::array<float, 4> samples{{2.0F, 1.0F, 0.0F, 1.0F}};
    const std::vector<OfflineSceneTransformGraphFrameState> sparse_frames =
        sample_offline_sparse_transform_graph_clip(
            sparse_clip,
            samples);
    check(
        sparse_frames.size() == samples.size(),
        "sparse clip preserves caller sample count and out-of-order request order");
    if (sparse_frames.size() == samples.size()) {
        check(
            exact_matrix_equal(
                sparse_frames[0].local_transforms[0],
                pivot_b)
                && exact_matrix_equal(
                    sparse_frames[2].local_transforms[0],
                    pivot_a),
            "sparse clip exact endpoint requests preserve stored local state without interpolation arithmetic");
        check(
            exact_matrix_equal(
                sparse_frames[1].local_transforms[0],
                sparse_frames[3].local_transforms[0]),
            "sparse clip repeated interior requests are deterministic");
        check(
            sparse_frames[0].camera.eye.x == camera_b.eye.x
                && sparse_frames[2].camera.eye.x == camera_a.eye.x,
            "sparse clip exact camera track endpoints preserve stored camera state");
    }

    const PreparedOfflineCameraSequence sparse_sequence =
        prepare_offline_sparse_transform_graph_clip_sequence(
            reusable,
            graph,
            sparse_clip,
            samples);
    const PreparedOfflineCameraSequence dense_sequence =
        prepare_offline_transform_graph_timeline_sequence(
            reusable,
            graph,
            dense_keyframes,
            samples);

    check(
        sparse_sequence.frame_count() == dense_sequence.frame_count()
            && sparse_sequence.frame_count() == samples.size(),
        "aligned sparse and dense graph timelines prepare identical bounded sample ownership");
    for (std::size_t index = 0U;
         index < sparse_sequence.frame_count()
             && index < dense_sequence.frame_count();
         ++index) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(sparse_sequence, index),
                render_prepared_camera_sequence_frame(dense_sequence, index)),
            "aligned sparse clip is exact resolved/hash and 4x per-sample equivalent to dense M100");
    }

    // Only the transform-only pivot animates here. Render-bound descendants
    // have no tracks and therefore must remain bit-exact in local space.
    const Mat4 static_red = Mat4::translation({-0.30F, 0.0F, 0.0F});
    const Mat4 static_green = Mat4::translation({0.30F, 0.0F, -0.15F});
    const Mat4 moving_pivot_a =
        Mat4::translation({-0.30F, 0.0F, -0.35F});
    const Mat4 moving_pivot_b =
        Mat4::translation({0.30F, 0.0F, -0.35F});
    const OfflineSceneSparseTransformGraphClip pivot_only_clip(
        0.0F,
        2.0F,
        camera_a,
        {moving_pivot_a, static_red, static_green},
        std::nullopt,
        {
            OfflineSceneSparseTransformTrack{
                0U,
                {
                    {0.0F, moving_pivot_a},
                    {2.0F, moving_pivot_b},
                }},
        });
    const std::array<float, 3> pivot_samples{{0.0F, 1.0F, 2.0F}};
    const auto pivot_sparse_frames =
        sample_offline_sparse_transform_graph_clip(
            pivot_only_clip,
            pivot_samples);
    check(
        pivot_sparse_frames.size() == pivot_samples.size(),
        "pivot-only sparse clip materializes every requested complete graph-local frame");
    for (const OfflineSceneTransformGraphFrameState& frame :
         pivot_sparse_frames) {
        check(
            frame.local_transforms.size() == 3U
                && exact_matrix_equal(frame.local_transforms[1], static_red)
                && exact_matrix_equal(frame.local_transforms[2], static_green),
            "untracked sparse clip graph nodes remain bit-exact at every sample");
    }

    const Mat4 moving_pivot_mid =
        Mat4::translation({0.0F, 0.0F, -0.35F});
    const std::array<OfflineSceneTransformGraphFrameState, 3>
        explicit_graph_frames{{
            OfflineSceneTransformGraphFrameState{
                camera_a,
                {moving_pivot_a, static_red, static_green}},
            OfflineSceneTransformGraphFrameState{
                camera_a,
                {moving_pivot_mid, static_red, static_green}},
            OfflineSceneTransformGraphFrameState{
                camera_a,
                {moving_pivot_b, static_red, static_green}},
        }};
    const PreparedOfflineCameraSequence pivot_sparse_sequence =
        prepare_offline_sparse_transform_graph_clip_sequence(
            reusable,
            graph,
            pivot_only_clip,
            pivot_samples);
    const PreparedOfflineCameraSequence explicit_graph_sequence =
        prepare_offline_transform_graph_sequence(
            reusable,
            graph,
            explicit_graph_frames);
    for (std::size_t index = 0U;
         index < pivot_sparse_sequence.frame_count()
             && index < explicit_graph_sequence.frame_count();
         ++index) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(
                    pivot_sparse_sequence, index),
                render_prepared_camera_sequence_frame(
                    explicit_graph_sequence, index)),
            "pivot-only sparse clip exactly matches independently materialized M99 graph-local frames");
    }
    if (pivot_sparse_sequence.frame_count() == 3U) {
        check(
            !exact_frame_equal(
                render_prepared_camera_sequence_frame(
                    pivot_sparse_sequence, 0U),
                render_prepared_camera_sequence_frame(
                    pivot_sparse_sequence, 2U)),
            "one animated transform-only sparse track observably moves multiple render descendants");
    }
}

void test_programmatic_sparse_transform_graph_clip_validation_contract() {
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});
    const Mat4 identity = Mat4::identity();

    check_throws<std::invalid_argument>(
        [&] {
            (void)OfflineSceneSparseTransformGraphClip(
                1.0F,
                1.0F,
                camera,
                {identity},
                std::nullopt,
                {});
        },
        "sparse clip rejects a non-increasing global time domain");

    check_throws<std::invalid_argument>(
        [&] {
            (void)OfflineSceneSparseTransformGraphClip(
                0.0F,
                2.0F,
                camera,
                {
                    Mat4::perspective(
                        radians(55.0F),
                        1.0F,
                        0.1F,
                        20.0F),
                },
                std::nullopt,
                {});
        },
        "sparse clip rejects projective default local graph state");

    check_throws<std::invalid_argument>(
        [&] {
            std::optional<std::vector<OfflineSceneSparseCameraKeyframe>>
                short_camera{std::vector<OfflineSceneSparseCameraKeyframe>{
                    {0.0F, camera},
                }};
            (void)OfflineSceneSparseTransformGraphClip(
                0.0F,
                2.0F,
                camera,
                {identity},
                std::move(short_camera),
                {});
        },
        "sparse clip rejects underspecified camera tracks");

    check_throws<std::invalid_argument>(
        [&] {
            std::optional<std::vector<OfflineSceneSparseCameraKeyframe>>
                incomplete_camera{std::vector<OfflineSceneSparseCameraKeyframe>{
                    {0.5F, camera},
                    {2.0F, camera},
                }};
            (void)OfflineSceneSparseTransformGraphClip(
                0.0F,
                2.0F,
                camera,
                {identity},
                std::move(incomplete_camera),
                {});
        },
        "sparse clip camera tracks must cover the complete clip domain");

    check_throws<std::invalid_argument>(
        [&] {
            (void)OfflineSceneSparseTransformGraphClip(
                0.0F,
                2.0F,
                camera,
                {identity, identity},
                std::nullopt,
                {
                    OfflineSceneSparseTransformTrack{
                        0U,
                        {{0.0F, identity}, {2.0F, identity}}},
                    OfflineSceneSparseTransformTrack{
                        0U,
                        {{0.0F, identity}, {2.0F, identity}}},
                });
        },
        "sparse clip rejects duplicate transform-track node ownership");

    check_throws<std::out_of_range>(
        [&] {
            (void)OfflineSceneSparseTransformGraphClip(
                0.0F,
                2.0F,
                camera,
                {identity},
                std::nullopt,
                {
                    OfflineSceneSparseTransformTrack{
                        1U,
                        {{0.0F, identity}, {2.0F, identity}}},
                });
        },
        "sparse clip rejects transform tracks outside default graph-local ownership");

    check_throws<std::invalid_argument>(
        [&] {
            (void)OfflineSceneSparseTransformGraphClip(
                0.0F,
                2.0F,
                camera,
                {identity},
                std::nullopt,
                {
                    OfflineSceneSparseTransformTrack{
                        0U,
                        {{0.0F, identity}, {0.0F, identity}}},
                });
        },
        "sparse clip rejects non-increasing transform-track key times");

    check_throws<std::invalid_argument>(
        [&] {
            (void)OfflineSceneSparseTransformGraphClip(
                0.0F,
                2.0F,
                camera,
                {identity},
                std::nullopt,
                {
                    OfflineSceneSparseTransformTrack{
                        0U,
                        {{0.5F, identity}, {2.0F, identity}}},
                });
        },
        "sparse clip transform tracks must cover the complete clip domain");

    check_throws<std::invalid_argument>(
        [&] {
            const Mat4 projective = Mat4::perspective(
                radians(55.0F),
                1.0F,
                0.1F,
                20.0F);
            (void)OfflineSceneSparseTransformGraphClip(
                0.0F,
                2.0F,
                camera,
                {identity},
                std::nullopt,
                {
                    OfflineSceneSparseTransformTrack{
                        0U,
                        {{0.0F, identity}, {2.0F, projective}}},
                });
        },
        "sparse clip rejects projective animated local state");

    check_throws<std::invalid_argument>(
        [&] {
            std::vector<Mat4> defaults(17U, identity);
            std::vector<OfflineSceneSparseTransformTrack> tracks;
            tracks.reserve(17U);
            for (std::size_t node = 0U; node < 17U; ++node) {
                std::vector<OfflineSceneSparseTransformKeyframe> keys;
                keys.reserve(detail::kMaxOfflineTimelineKeyframes);
                for (std::size_t key = 0U;
                     key < detail::kMaxOfflineTimelineKeyframes;
                     ++key) {
                    const float time = key + 1U
                            == detail::kMaxOfflineTimelineKeyframes
                        ? 2.0F
                        : 2.0F * static_cast<float>(key)
                            / static_cast<float>(
                                detail::kMaxOfflineTimelineKeyframes - 1U);
                    keys.push_back({time, identity});
                }
                tracks.push_back(
                    OfflineSceneSparseTransformTrack{
                        node,
                        std::move(keys),
                    });
            }
            (void)OfflineSceneSparseTransformGraphClip(
                0.0F,
                2.0F,
                camera,
                std::move(defaults),
                std::nullopt,
                std::move(tracks));
        },
        "sparse clip aggregate animated key ownership is explicitly bounded");

    const OfflineSceneSparseTransformGraphClip valid_clip(
        0.0F,
        2.0F,
        camera,
        {identity, identity, identity},
        std::nullopt,
        {
            OfflineSceneSparseTransformTrack{
                0U,
                {
                    {0.0F, identity},
                    {2.0F, Mat4::translation({0.2F, 0.0F, 0.0F})},
                }},
        });

    const std::array<float, 1> outside{{3.0F}};
    check_throws<std::out_of_range>(
        [&] {
            (void)sample_offline_sparse_transform_graph_clip(
                valid_clip,
                outside);
        },
        "sparse clip rejects requested samples outside its global domain");

    std::vector<float> too_many_samples(
        detail::kMaxOfflineTimelineSamples + 1U,
        0.0F);
    check_throws<std::invalid_argument>(
        [&] {
            (void)sample_offline_sparse_transform_graph_clip(
                valid_clip,
                too_many_samples);
        },
        "sparse clip enforces the established bounded requested sample count");

    std::size_t shade_calls = 0U;
    const ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F});
    ModelRenderOptions options;
    options.fragment_program =
        std::make_shared<CountingFragmentProgram>(&shade_calls);

    OfflineRenderSettings settings;
    settings.width = 31U;
    settings.height = 31U;
    settings.sample_count = SampleCount::Four;
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneTransformGraph graph(
        {
            std::nullopt,
            std::optional<std::size_t>{0U},
            std::nullopt,
        },
        {1U, 2U});

    const OfflineSceneSparseTransformGraphClip wrong_node_count_clip(
        0.0F,
        2.0F,
        camera,
        {identity, identity},
        std::nullopt,
        {});
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_sparse_transform_graph_clip_sequence(
                reusable,
                graph,
                wrong_node_count_clip,
                std::span<const float>{});
        },
        "sparse clip default graph-local ownership must match immutable graph node count");

    const float large =
        std::numeric_limits<float>::max() / 4.0F;
    Mat4 large_scale = Mat4::identity();
    large_scale(0U, 0U) = large;
    const OfflineSceneSparseTransformGraphClip overflow_clip(
        0.0F,
        2.0F,
        camera,
        {large_scale, identity, identity},
        std::nullopt,
        {
            OfflineSceneSparseTransformTrack{
                0U,
                {
                    {0.0F, large_scale},
                    {2.0F, identity},
                }},
            OfflineSceneSparseTransformTrack{
                1U,
                {
                    {0.0F, identity},
                    {2.0F, large_scale},
                }},
        });
    const std::array<float, 2> exact_then_interior{{0.0F, 1.0F}};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_sparse_transform_graph_clip_sequence(
                reusable,
                graph,
                overflow_clip,
                exact_then_interior);
        },
        "later sparse clip midpoint whose transform-only parent composition overflows rejects the complete batch");
    check(
        shade_calls == 0U,
        "later sparse clip graph-composition failure occurs before any earlier sample fragment execution");

    const PreparedOfflineCameraSequence empty =
        prepare_offline_sparse_transform_graph_clip_sequence(
            reusable,
            graph,
            valid_clip,
            std::span<const float>{});
    check(
        empty.frame_count() == 0U,
        "valid sparse clip accepts an empty requested sample span after complete clip and graph validation");
}


void test_programmatic_sparse_clip_blend_endpoints_and_local_reference() {
    OfflineRenderSettings settings;
    settings.width = 61U;
    settings.height = 47U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};

    const ModelAsset red = triangle_asset(
        {0.85F, 0.15F, 0.10F},
        {0.0F, 0.0F, 0.0F});
    const ModelAsset green = triangle_asset(
        {0.10F, 0.75F, 0.20F},
        {0.0F, 0.0F, 0.0F});
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &red,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &green,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneTransformGraph graph(
        {
            std::nullopt,
            std::optional<std::size_t>{0U},
            std::optional<std::size_t>{0U},
        },
        {1U, 2U});

    OfflineSceneCamera left_camera =
        camera_at({-0.30F, 0.0F, 3.0F});
    OfflineSceneCamera right_camera =
        camera_at({0.30F, 0.0F, 3.0F});

    const Mat4 red_local =
        Mat4::translation({-0.28F, 0.0F, 0.0F});
    const Mat4 green_local =
        Mat4::translation({0.28F, 0.0F, -0.12F});

    const Mat4 left_pivot_0 =
        Mat4::translation({-0.60F, 0.0F, -0.30F});
    const Mat4 left_pivot_2 =
        Mat4::translation({-0.20F, 0.0F, -0.30F});
    const Mat4 right_pivot_0 =
        Mat4::translation({0.20F, 0.0F, -0.30F});
    const Mat4 right_pivot_2 =
        Mat4::translation({0.60F, 0.0F, -0.30F});

    const OfflineSceneSparseTransformGraphClip left_clip(
        0.0F,
        2.0F,
        left_camera,
        {left_pivot_0, red_local, green_local},
        std::nullopt,
        {
            OfflineSceneSparseTransformTrack{
                0U,
                {
                    {0.0F, left_pivot_0},
                    {2.0F, left_pivot_2},
                }},
        });
    const OfflineSceneSparseTransformGraphClip right_clip(
        0.0F,
        2.0F,
        right_camera,
        {right_pivot_0, red_local, green_local},
        std::nullopt,
        {
            OfflineSceneSparseTransformTrack{
                0U,
                {
                    {0.0F, right_pivot_0},
                    {2.0F, right_pivot_2},
                }},
        });

    const std::array<float, 4> samples{{2.0F, 1.0F, 0.0F, 1.0F}};

    const PreparedOfflineCameraSequence left_direct =
        prepare_offline_sparse_transform_graph_clip_sequence(
            reusable,
            graph,
            left_clip,
            samples);
    const PreparedOfflineCameraSequence right_direct =
        prepare_offline_sparse_transform_graph_clip_sequence(
            reusable,
            graph,
            right_clip,
            samples);
    const PreparedOfflineCameraSequence weight_zero =
        prepare_offline_sparse_transform_graph_clip_blend_sequence(
            reusable,
            graph,
            left_clip,
            right_clip,
            samples,
            0.0F);
    const PreparedOfflineCameraSequence weight_one =
        prepare_offline_sparse_transform_graph_clip_blend_sequence(
            reusable,
            graph,
            left_clip,
            right_clip,
            samples,
            1.0F);

    check(
        weight_zero.frame_count() == samples.size()
            && weight_one.frame_count() == samples.size(),
        "two-clip blend endpoint weights preserve caller sample ownership");
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(weight_zero, index),
                render_prepared_camera_sequence_frame(left_direct, index)),
            "two-clip weight zero is exact source-left passthrough");
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(weight_one, index),
                render_prepared_camera_sequence_frame(right_direct, index)),
            "two-clip weight one is exact source-right passthrough");
    }

    const float weight = 0.25F;
    const std::vector<OfflineSceneTransformGraphFrameState> blended_frames =
        blend_offline_sparse_transform_graph_clips(
            left_clip,
            right_clip,
            samples,
            weight);
    check(
        blended_frames.size() == samples.size(),
        "two-clip interior blend preserves repeated and out-of-order sample count");
    if (blended_frames.size() == samples.size()) {
        const std::array<float, 4> expected_pivot_x{{
            0.0F,
            -0.20F,
            -0.40F,
            -0.20F,
        }};
        for (std::size_t index = 0U; index < samples.size(); ++index) {
            check(
                std::fabs(
                    blended_frames[index].local_transforms[0](0U, 3U)
                    - expected_pivot_x[index]) < 1.0e-6F,
                "two-clip interior blend combines transform-only pivot in local space");
            check(
                exact_matrix_equal(
                    blended_frames[index].local_transforms[1],
                    red_local)
                    && exact_matrix_equal(
                        blended_frames[index].local_transforms[2],
                        green_local),
                "identical render-bound child locals remain exact through two-clip blend");
        }
        check(
            exact_matrix_equal(
                blended_frames[1].local_transforms[0],
                blended_frames[3].local_transforms[0]),
            "two-clip repeated interior sample is deterministic");
    }

    // Build an independent M99 reference from the two already-established M102
    // source samplers plus the pre-M103 interpolation primitives. This preserves
    // their actual float semantics while independently checking that M103
    // blends complete local frames before graph composition.
    const std::vector<OfflineSceneTransformGraphFrameState> left_sampled =
        sample_offline_sparse_transform_graph_clip(left_clip, samples);
    const std::vector<OfflineSceneTransformGraphFrameState> right_sampled =
        sample_offline_sparse_transform_graph_clip(right_clip, samples);
    std::vector<OfflineSceneTransformGraphFrameState> reference_frames;
    reference_frames.reserve(samples.size());
    for (std::size_t frame_index = 0U;
         frame_index < samples.size();
         ++frame_index) {
        OfflineSceneTransformGraphFrameState reference;
        reference.camera = detail::interpolate_offline_timeline_camera(
            left_sampled[frame_index].camera,
            right_sampled[frame_index].camera,
            weight);
        reference.local_transforms.reserve(3U);
        for (std::size_t local_index = 0U; local_index < 3U; ++local_index) {
            reference.local_transforms.push_back(
                detail::interpolate_offline_timeline_affine(
                    left_sampled[frame_index].local_transforms[local_index],
                    right_sampled[frame_index].local_transforms[local_index],
                    weight));
        }
        reference_frames.push_back(std::move(reference));
    }

    const PreparedOfflineCameraSequence blended_sequence =
        prepare_offline_sparse_transform_graph_clip_blend_sequence(
            reusable,
            graph,
            left_clip,
            right_clip,
            samples,
            weight);
    const PreparedOfflineCameraSequence reference_sequence =
        prepare_offline_transform_graph_sequence(
            reusable,
            graph,
            reference_frames);

    check(
        blended_sequence.frame_count() == reference_sequence.frame_count(),
        "two-clip local blend and independent M99 reference prepare equal frame counts");
    for (std::size_t index = 0U;
         index < blended_sequence.frame_count()
             && index < reference_sequence.frame_count();
         ++index) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(
                    blended_sequence,
                    index),
                render_prepared_camera_sequence_frame(
                    reference_sequence,
                    index)),
            "two-clip interior blend is exact resolved/hash and 4x per-sample equivalent to local-blend-then-M99 reference");
    }
    if (blended_sequence.frame_count() == samples.size()) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(
                    blended_sequence,
                    1U),
                render_prepared_camera_sequence_frame(
                    blended_sequence,
                    3U)),
            "two-clip repeated interior request renders deterministically");
        check(
            !exact_frame_equal(
                render_prepared_camera_sequence_frame(
                    blended_sequence,
                    0U),
                render_prepared_camera_sequence_frame(
                    blended_sequence,
                    2U)),
            "blended transform-only pivot observably moves multiple render descendants");
    }
}

void test_programmatic_sparse_clip_blend_validation_contract() {
    const OfflineSceneCamera camera =
        camera_at({0.0F, 0.0F, 3.0F});
    const Mat4 identity = Mat4::identity();

    const OfflineSceneSparseTransformGraphClip clip_three_nodes(
        0.0F,
        2.0F,
        camera,
        {identity, identity, identity},
        std::nullopt,
        {});
    const OfflineSceneSparseTransformGraphClip clip_two_nodes(
        0.0F,
        2.0F,
        camera,
        {identity, identity},
        std::nullopt,
        {});

    const std::array<float, 1> sample_zero{{0.0F}};
    check_throws<std::invalid_argument>(
        [&] {
            (void)blend_offline_sparse_transform_graph_clips(
                clip_three_nodes,
                clip_three_nodes,
                sample_zero,
                std::numeric_limits<float>::quiet_NaN());
        },
        "two-clip blend rejects non-finite blend weights");
    check_throws<std::invalid_argument>(
        [&] {
            (void)blend_offline_sparse_transform_graph_clips(
                clip_three_nodes,
                clip_three_nodes,
                sample_zero,
                -0.01F);
        },
        "two-clip blend rejects weights below zero");
    check_throws<std::invalid_argument>(
        [&] {
            (void)blend_offline_sparse_transform_graph_clips(
                clip_three_nodes,
                clip_three_nodes,
                sample_zero,
                1.01F);
        },
        "two-clip blend rejects weights above one");
    check_throws<std::invalid_argument>(
        [&] {
            (void)blend_offline_sparse_transform_graph_clips(
                clip_three_nodes,
                clip_two_nodes,
                std::span<const float>{},
                0.5F);
        },
        "two-clip blend rejects unequal graph-local ownership even for empty requests");

    const OfflineSceneSparseTransformGraphClip shifted_domain_clip(
        1.0F,
        3.0F,
        camera,
        {identity, identity, identity},
        std::nullopt,
        {});
    const std::array<float, 1> left_only_time{{0.5F}};
    check_throws<std::out_of_range>(
        [&] {
            (void)blend_offline_sparse_transform_graph_clips(
                clip_three_nodes,
                shifted_domain_clip,
                left_only_time,
                0.0F);
        },
        "two-clip endpoint passthrough still validates requested time against both clip domains");

    OfflineRenderSettings settings;
    settings.width = 31U;
    settings.height = 31U;
    settings.sample_count = SampleCount::Four;

    std::size_t shade_calls = 0U;
    const ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F});
    ModelRenderOptions options;
    options.fragment_program =
        std::make_shared<CountingFragmentProgram>(&shade_calls);
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneTransformGraph graph(
        {
            std::nullopt,
            std::optional<std::size_t>{0U},
            std::nullopt,
        },
        {1U, 2U});

    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_sparse_transform_graph_clip_blend_sequence(
                reusable,
                graph,
                clip_two_nodes,
                clip_two_nodes,
                std::span<const float>{},
                0.5F);
        },
        "two-clip blend local ownership must match immutable graph node count");

    const float large =
        std::numeric_limits<float>::max() / 4.0F;
    Mat4 large_scale = Mat4::identity();
    large_scale(0U, 0U) = large;

    const OfflineSceneSparseTransformGraphClip left_overflow_source(
        0.0F,
        2.0F,
        camera,
        {identity, identity, identity},
        std::nullopt,
        {
            OfflineSceneSparseTransformTrack{
                0U,
                {
                    {0.0F, identity},
                    {2.0F, large_scale},
                }},
        });
    const OfflineSceneSparseTransformGraphClip right_overflow_source(
        0.0F,
        2.0F,
        camera,
        {identity, identity, identity},
        std::nullopt,
        {
            OfflineSceneSparseTransformTrack{
                1U,
                {
                    {0.0F, identity},
                    {2.0F, large_scale},
                }},
        });
    const std::array<float, 2> safe_then_overflow{{0.0F, 2.0F}};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_sparse_transform_graph_clip_blend_sequence(
                reusable,
                graph,
                left_overflow_source,
                right_overflow_source,
                safe_then_overflow,
                0.5F);
        },
        "later two-clip local blend whose parent-child composition overflows rejects the complete batch");
    check(
        shade_calls == 0U,
        "later two-clip composition failure occurs before any earlier safe sample fragment execution");

    const PreparedOfflineCameraSequence empty =
        prepare_offline_sparse_transform_graph_clip_blend_sequence(
            reusable,
            graph,
            clip_three_nodes,
            clip_three_nodes,
            std::span<const float>{},
            0.5F);
    check(
        empty.frame_count() == 0U,
        "valid two-clip blend accepts an empty requested sample span after complete ownership validation");
}

void test_programmatic_transform_graph_timeline_matches_m97_and_local_reference() {
    OfflineRenderSettings settings;
    settings.width = 61U;
    settings.height = 47U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};

    const ModelAsset red = triangle_asset(
        {0.85F, 0.15F, 0.10F},
        {0.0F, 0.0F, 0.0F});
    const ModelAsset green = triangle_asset(
        {0.10F, 0.75F, 0.20F},
        {0.0F, 0.0F, 0.0F});
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &red,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &green,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});

    // A 1:1 graph timeline is the same semantic surface as M97: identical
    // topology, local keyframes, and sample requests must render exactly.
    const OfflineSceneHierarchy hierarchy({
        std::optional<std::size_t>{1U},
        std::nullopt,
    });
    const OfflineSceneTransformGraph one_to_one_graph(
        {
            std::optional<std::size_t>{1U},
            std::nullopt,
        },
        {0U, 1U});
    const std::vector<Mat4> local_a{
        Mat4::translation({0.10F, 0.0F, -0.10F}),
        Mat4::translation({-0.35F, 0.0F, -0.30F}),
    };
    const std::vector<Mat4> local_b{
        Mat4::translation({0.45F, 0.0F, -0.20F}),
        Mat4::translation({0.20F, 0.0F, -0.45F}),
    };
    const std::array<OfflineSceneHierarchicalTimelineKeyframe, 2>
        hierarchy_keyframes{{
            OfflineSceneHierarchicalTimelineKeyframe{
                0.0F,
                OfflineSceneHierarchicalFrameState{camera, local_a}},
            OfflineSceneHierarchicalTimelineKeyframe{
                2.0F,
                OfflineSceneHierarchicalFrameState{camera, local_b}},
        }};
    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 2>
        graph_keyframes{{
            OfflineSceneTransformGraphTimelineKeyframe{
                0.0F,
                OfflineSceneTransformGraphFrameState{camera, local_a}},
            OfflineSceneTransformGraphTimelineKeyframe{
                2.0F,
                OfflineSceneTransformGraphFrameState{camera, local_b}},
        }};
    const std::array<float, 4> sample_times{{2.0F, 1.0F, 0.0F, 1.0F}};

    const auto sampled_graph = sample_offline_transform_graph_timeline(
        one_to_one_graph,
        graph_keyframes,
        sample_times);
    check(
        sampled_graph.size() == sample_times.size(),
        "transform graph timeline preserves caller sample count and order");
    if (sampled_graph.size() == sample_times.size()) {
        check(
            sampled_graph[0].local_transforms[0](0U, 3U)
                == local_b[0](0U, 3U),
            "exact graph keyframe requests preserve stored graph-local state without interpolation arithmetic");
        check(
            sampled_graph[1].local_transforms[0](0U, 3U)
                == sampled_graph[3].local_transforms[0](0U, 3U),
            "repeated graph timeline requests are deterministic");
    }

    const PreparedOfflineCameraSequence graph_sequence =
        prepare_offline_transform_graph_timeline_sequence(
            reusable,
            one_to_one_graph,
            graph_keyframes,
            sample_times);
    const PreparedOfflineCameraSequence hierarchical_sequence =
        prepare_offline_hierarchy_timeline_sequence(
            reusable,
            hierarchy,
            hierarchy_keyframes,
            sample_times);
    check(
        graph_sequence.frame_count() == hierarchical_sequence.frame_count()
            && graph_sequence.frame_count() == sample_times.size(),
        "1:1 graph timeline preserves M97 prepared sample ownership");
    for (std::size_t index = 0U;
         index < graph_sequence.frame_count()
             && index < hierarchical_sequence.frame_count();
         ++index) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(graph_sequence, index),
                render_prepared_camera_sequence_frame(
                    hierarchical_sequence,
                    index)),
            "1:1 graph timeline is exact resolved/hash and 4x per-sample equivalent to M97");
    }

    // Node 0 is a transform-only animated pivot. Children 1 and 2 are render
    // bindings. The pivot scale and child translations both change, making
    // local-interpolate-then-compose observably different from interpolating
    // the endpoint world matrices.
    const OfflineSceneTransformGraph grouped_graph(
        {
            std::nullopt,
            std::optional<std::size_t>{0U},
            std::optional<std::size_t>{0U},
        },
        {1U, 2U});
    const Mat4 pivot_a =
        Mat4::translation({-0.15F, 0.0F, -0.30F})
        * Mat4::scale({1.0F, 1.0F, 1.0F});
    const Mat4 pivot_b =
        Mat4::translation({0.15F, 0.0F, -0.30F})
        * Mat4::scale({1.6F, 1.0F, 1.0F});
    const Mat4 red_a = Mat4::translation({0.0F, 0.0F, 0.0F});
    const Mat4 red_b = Mat4::translation({0.4F, 0.0F, 0.0F});
    const Mat4 green_a = Mat4::translation({0.30F, 0.0F, -0.10F});
    const Mat4 green_b = Mat4::translation({-0.20F, 0.0F, -0.10F});
    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 2>
        grouped_keyframes{{
            OfflineSceneTransformGraphTimelineKeyframe{
                0.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {pivot_a, red_a, green_a}}},
            OfflineSceneTransformGraphTimelineKeyframe{
                2.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {pivot_b, red_b, green_b}}},
        }};
    const std::array<float, 1> midpoint{{1.0F}};
    const auto sampled_midpoint = sample_offline_transform_graph_timeline(
        grouped_graph,
        grouped_keyframes,
        midpoint);
    check(
        sampled_midpoint.size() == 1U,
        "animated transform-only pivot produces the requested graph-local midpoint");

    Mat4 expected_pivot = Mat4::identity();
    expected_pivot(0U, 0U) = 1.3F;
    expected_pivot(0U, 3U) = 0.0F;
    expected_pivot(2U, 3U) = -0.30F;
    const Mat4 expected_red_local =
        Mat4::translation({0.20F, 0.0F, 0.0F});
    const Mat4 expected_green_local =
        Mat4::translation({0.05F, 0.0F, -0.10F});
    const Mat4 expected_red_world = expected_pivot * expected_red_local;
    const Mat4 expected_green_world = expected_pivot * expected_green_local;
    const std::array<OfflineSceneFrameState, 1> manual_midpoint{{
        OfflineSceneFrameState{
            camera,
            {expected_red_world, expected_green_world}},
    }};

    if (sampled_midpoint.size() == 1U) {
        const std::vector<Mat4> resolved =
            detail::resolve_offline_transform_graph_render_world_transforms(
                grouped_graph,
                sampled_midpoint.front().local_transforms);
        check(
            resolved.size() == 2U
                && std::fabs(
                    resolved[0](0U, 3U)
                    - expected_red_world(0U, 3U)) < 1.0e-5F,
            "graph timeline interpolates transform-only and render-bound locals before graph composition");
        const float endpoint_world_midpoint =
            0.5F * (
                (pivot_a * red_a)(0U, 3U)
                + (pivot_b * red_b)(0U, 3U));
        check(
            std::fabs(
                resolved[0](0U, 3U)
                - endpoint_world_midpoint) > 1.0e-3F,
            "graph timeline does not shortcut through endpoint world-transform interpolation");
    }

    const PreparedOfflineCameraSequence grouped_midpoint =
        prepare_offline_transform_graph_timeline_sequence(
            reusable,
            grouped_graph,
            grouped_keyframes,
            midpoint);
    const PreparedOfflineCameraSequence explicit_midpoint =
        prepare_offline_frame_sequence(
            reusable,
            manual_midpoint);
    check(
        grouped_midpoint.frame_count() == 1U
            && explicit_midpoint.frame_count() == 1U
            && exact_frame_equal(
                render_prepared_camera_sequence_frame(grouped_midpoint, 0U),
                render_prepared_camera_sequence_frame(explicit_midpoint, 0U)),
        "animated transform-only pivot midpoint is exact-equivalent to independently composed M92 world state");
}

void test_programmatic_transform_graph_timeline_validation_contract() {
    std::size_t shade_calls = 0U;
    const ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F});
    ModelRenderOptions options;
    options.fragment_program =
        std::make_shared<CountingFragmentProgram>(&shade_calls);

    OfflineRenderSettings settings;
    settings.width = 31U;
    settings.height = 31U;
    settings.sample_count = SampleCount::Four;
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});
    const OfflineSceneTransformGraph graph(
        {
            std::nullopt,
            std::optional<std::size_t>{0U},
            std::optional<std::size_t>{0U},
        },
        {1U, 2U});

    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 1>
        single_keyframe{{
            OfflineSceneTransformGraphTimelineKeyframe{
                0.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {
                        Mat4::identity(),
                        Mat4::identity(),
                        Mat4::identity(),
                    }}},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)sample_offline_transform_graph_timeline(
                graph,
                single_keyframe,
                std::span<const float>{});
        },
        "transform graph timeline requires at least two keyframes even for an empty sample request");

    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 2>
        missing_local{{
            OfflineSceneTransformGraphTimelineKeyframe{
                0.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {Mat4::identity(), Mat4::identity()}}},
            OfflineSceneTransformGraphTimelineKeyframe{
                2.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {Mat4::identity(), Mat4::identity()}}},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)sample_offline_transform_graph_timeline(
                graph,
                missing_local,
                std::span<const float>{});
        },
        "transform graph timeline validates complete graph-local ownership before considering sample count");

    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 2>
        non_increasing{{
            OfflineSceneTransformGraphTimelineKeyframe{
                1.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {
                        Mat4::identity(),
                        Mat4::identity(),
                        Mat4::identity(),
                    }}},
            OfflineSceneTransformGraphTimelineKeyframe{
                1.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {
                        Mat4::identity(),
                        Mat4::identity(),
                        Mat4::identity(),
                    }}},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)sample_offline_transform_graph_timeline(
                graph,
                non_increasing,
                std::span<const float>{});
        },
        "transform graph timeline rejects non-increasing keyframe time");

    const OfflineSceneTransformGraph wrong_binding_graph(
        {
            std::nullopt,
            std::nullopt,
        },
        {0U});
    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 2>
        wrong_binding_keyframes{{
            OfflineSceneTransformGraphTimelineKeyframe{
                0.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {Mat4::identity(), Mat4::identity()}}},
            OfflineSceneTransformGraphTimelineKeyframe{
                2.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {Mat4::identity(), Mat4::identity()}}},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_transform_graph_timeline_sequence(
                reusable,
                wrong_binding_graph,
                wrong_binding_keyframes,
                std::span<const float>{});
        },
        "transform graph timeline render binding ownership must match the prepared scene before sampling");

    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 2>
        later_projective{{
            OfflineSceneTransformGraphTimelineKeyframe{
                0.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {
                        Mat4::identity(),
                        Mat4::identity(),
                        Mat4::identity(),
                    }}},
            OfflineSceneTransformGraphTimelineKeyframe{
                2.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {
                        Mat4::identity(),
                        Mat4::identity(),
                        Mat4::perspective(
                            radians(55.0F),
                            1.0F,
                            0.1F,
                            20.0F),
                    }}},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_transform_graph_timeline_sequence(
                reusable,
                graph,
                later_projective,
                std::span<const float>{});
        },
        "transform graph timeline rejects projective graph-local keyframes before sampling");
    check(
        shade_calls == 0U,
        "invalid transform graph timeline keyframes reject before fragment execution");

    const float large_scale =
        std::numeric_limits<float>::max() / 4.0F;
    Mat4 large = Mat4::identity();
    large(0U, 0U) = large_scale;
    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 2>
        interior_composition_overflow{{
            OfflineSceneTransformGraphTimelineKeyframe{
                0.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {
                        large,
                        Mat4::identity(),
                        Mat4::identity(),
                    }}},
            OfflineSceneTransformGraphTimelineKeyframe{
                2.0F,
                OfflineSceneTransformGraphFrameState{
                    camera,
                    {
                        Mat4::identity(),
                        large,
                        Mat4::identity(),
                    }}},
        }};
    const std::array<float, 2> exact_then_interior{{0.0F, 1.0F}};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_transform_graph_timeline_sequence(
                reusable,
                graph,
                interior_composition_overflow,
                exact_then_interior);
        },
        "later finite graph-local midpoint whose composed child world overflows rejects the complete timeline transaction");
    check(
        shade_calls == 0U,
        "later graph timeline composition overflow rejects before any earlier requested sample executes fragments");

    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 2> valid{{
        OfflineSceneTransformGraphTimelineKeyframe{
            0.0F,
            OfflineSceneTransformGraphFrameState{
                camera,
                {
                    Mat4::identity(),
                    Mat4::identity(),
                    Mat4::identity(),
                }}},
        OfflineSceneTransformGraphTimelineKeyframe{
            2.0F,
            OfflineSceneTransformGraphFrameState{
                camera,
                {
                    Mat4::translation({0.1F, 0.0F, 0.0F}),
                    Mat4::translation({0.2F, 0.0F, 0.0F}),
                    Mat4::translation({-0.2F, 0.0F, 0.0F}),
                }}},
    }};

    const std::array<float, 1> outside_domain{{3.0F}};
    check_throws<std::out_of_range>(
        [&] {
            (void)sample_offline_transform_graph_timeline(
                graph,
                valid,
                outside_domain);
        },
        "transform graph timeline rejects samples outside the keyframe domain");

    std::vector<float> too_many_samples(
        detail::kMaxOfflineTimelineSamples + 1U,
        0.0F);
    check_throws<std::invalid_argument>(
        [&] {
            (void)sample_offline_transform_graph_timeline(
                graph,
                valid,
                too_many_samples);
        },
        "transform graph timeline enforces the established bounded sample count before allocation");

    const PreparedOfflineCameraSequence empty =
        prepare_offline_transform_graph_timeline_sequence(
            reusable,
            graph,
            valid,
            std::span<const float>{});
    check(
        empty.frame_count() == 0U,
        "valid graph timeline accepts an empty requested sample span after complete keyframe validation");
}

void test_programmatic_hierarchical_timeline_matches_m94_and_local_space_reference() {
    OfflineRenderSettings settings;
    settings.width = 61U;
    settings.height = 47U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};

    const ModelAsset red = triangle_asset(
        {0.85F, 0.15F, 0.10F},
        {0.0F, 0.0F, 0.0F});
    const ModelAsset green = triangle_asset(
        {0.10F, 0.75F, 0.20F},
        {0.0F, 0.0F, 0.0F});
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &red,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &green,
            Mat4::translation({8.0F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});

    const OfflineSceneHierarchy roots({
        std::nullopt,
        std::nullopt,
    });
    const std::vector<Mat4> local_a{
        Mat4::translation({-0.35F, 0.0F, -0.1F}),
        Mat4::translation({0.25F, 0.0F, -0.45F}),
    };
    const std::vector<Mat4> local_b{
        Mat4::translation({0.35F, 0.0F, -0.55F}),
        Mat4::translation({-0.25F, 0.0F, -0.2F}),
    };
    const std::array<OfflineSceneHierarchicalTimelineKeyframe, 2>
        hierarchical_keyframes{{
            OfflineSceneHierarchicalTimelineKeyframe{
                0.0F,
                OfflineSceneHierarchicalFrameState{camera, local_a}},
            OfflineSceneHierarchicalTimelineKeyframe{
                2.0F,
                OfflineSceneHierarchicalFrameState{camera, local_b}},
        }};
    const std::array<OfflineSceneTimelineKeyframe, 2> flat_keyframes{{
        OfflineSceneTimelineKeyframe{
            0.0F,
            OfflineSceneFrameState{camera, local_a}},
        OfflineSceneTimelineKeyframe{
            2.0F,
            OfflineSceneFrameState{camera, local_b}},
    }};
    const std::array<float, 4> sample_times{{2.0F, 1.0F, 0.0F, 1.0F}};

    const auto sampled_roots = sample_offline_hierarchical_timeline(
        roots,
        hierarchical_keyframes,
        sample_times);
    check(
        sampled_roots.size() == sample_times.size(),
        "hierarchical timeline preserves requested sample count");
    if (sampled_roots.size() == sample_times.size()) {
        check(
            sampled_roots[0].local_transforms[0](0U, 3U)
                == local_b[0](0U, 3U),
            "exact hierarchical keyframe sampling preserves stored local transform state without interpolation arithmetic");
        check(
            sampled_roots[1].local_transforms[0](0U, 3U)
                == sampled_roots[3].local_transforms[0](0U, 3U),
            "repeated hierarchical timeline requests are deterministic");
    }

    const PreparedOfflineCameraSequence hierarchical_roots =
        prepare_offline_hierarchy_timeline_sequence(
            reusable,
            roots,
            hierarchical_keyframes,
            sample_times);
    const PreparedOfflineCameraSequence flat =
        prepare_offline_timeline_sequence(
            reusable,
            flat_keyframes,
            sample_times);
    check(
        hierarchical_roots.frame_count() == flat.frame_count(),
        "root-only hierarchical timeline preserves M94 frame count");
    for (std::size_t index = 0U;
         index < hierarchical_roots.frame_count()
             && index < flat.frame_count();
         ++index) {
        check(
            exact_frame_equal(
                render_prepared_camera_sequence_frame(
                    hierarchical_roots,
                    index),
                render_prepared_camera_sequence_frame(flat, index)),
            "root-only hierarchical timeline is exact resolved/hash and 4x per-sample equivalent to M94");
    }

    // Parent index deliberately points forward: entry 1 is the root and
    // entry 0 is its child. The changing parent X scale makes local-space
    // interpolation observably different from interpolating endpoint worlds.
    const OfflineSceneHierarchy hierarchy({
        std::optional<std::size_t>{1U},
        std::nullopt,
    });
    Mat4 parent_a =
        Mat4::translation({-0.15F, 0.0F, -0.3F})
        * Mat4::scale({1.0F, 1.0F, 1.0F});
    Mat4 parent_b =
        Mat4::translation({0.15F, 0.0F, -0.3F})
        * Mat4::scale({1.6F, 1.0F, 1.0F});
    const std::array<OfflineSceneHierarchicalTimelineKeyframe, 2>
        parented_keyframes{{
            OfflineSceneHierarchicalTimelineKeyframe{
                0.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {
                        Mat4::translation({0.0F, 0.0F, 0.0F}),
                        parent_a,
                    }}},
            OfflineSceneHierarchicalTimelineKeyframe{
                2.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {
                        Mat4::translation({0.4F, 0.0F, 0.0F}),
                        parent_b,
                    }}},
        }};
    const std::array<float, 1> midpoint{{1.0F}};
    const auto sampled_parented = sample_offline_hierarchical_timeline(
        hierarchy,
        parented_keyframes,
        midpoint);
    check(
        sampled_parented.size() == 1U,
        "parented hierarchical timeline produces the requested interior sample");

    Mat4 expected_parent = Mat4::identity();
    expected_parent(0U, 0U) = 1.3F;
    expected_parent(0U, 3U) = 0.0F;
    expected_parent(2U, 3U) = -0.3F;
    const Mat4 expected_child_local =
        Mat4::translation({0.2F, 0.0F, 0.0F});
    const Mat4 expected_child_world =
        expected_parent * expected_child_local;
    const std::array<OfflineSceneFrameState, 1> explicit_midpoint{{
        OfflineSceneFrameState{
            camera,
            {expected_child_world, expected_parent}},
    }};

    if (sampled_parented.size() == 1U) {
        const std::vector<Mat4> resolved =
            detail::resolve_offline_hierarchy_world_transforms(
                hierarchy,
                sampled_parented.front().local_transforms);
        check(
            resolved.size() == 2U
                && std::fabs(resolved[0](0U, 3U) - 0.26F) < 1.0e-5F,
            "hierarchical timeline interpolates local state before parent-world composition");
        const float endpoint_world_midpoint =
            0.5F * (
                (parent_a * parented_keyframes[0].frame.local_transforms[0])(
                    0U, 3U)
                + (parent_b * parented_keyframes[1].frame.local_transforms[0])(
                    0U, 3U));
        check(
            std::fabs(resolved[0](0U, 3U) - endpoint_world_midpoint)
                > 1.0e-3F,
            "hierarchical timeline does not shortcut through world-transform interpolation");
    }

    const PreparedOfflineCameraSequence hierarchical_midpoint =
        prepare_offline_hierarchy_timeline_sequence(
            reusable,
            hierarchy,
            parented_keyframes,
            midpoint);
    const PreparedOfflineCameraSequence manual_midpoint =
        prepare_offline_frame_sequence(
            reusable,
            explicit_midpoint);
    check(
        hierarchical_midpoint.frame_count() == 1U
            && manual_midpoint.frame_count() == 1U
            && exact_frame_equal(
                render_prepared_camera_sequence_frame(
                    hierarchical_midpoint,
                    0U),
                render_prepared_camera_sequence_frame(
                    manual_midpoint,
                    0U)),
        "parented local-interpolate-then-compose result is exact-equivalent to an independently constructed M92 world frame");
}

void test_programmatic_hierarchical_timeline_validation_contract() {
    std::size_t shade_calls = 0U;
    const ModelAsset asset = triangle_asset(
        {0.7F, 0.2F, 0.1F},
        {0.0F, 0.0F, 0.0F});
    ModelRenderOptions options;
    options.fragment_program =
        std::make_shared<CountingFragmentProgram>(&shade_calls);

    OfflineRenderSettings settings;
    settings.width = 31U;
    settings.height = 31U;
    settings.sample_count = SampleCount::Four;
    const std::array<OfflineSceneEntry, 2> entries{{
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &asset,
            Mat4::identity(),
            options,
            OfflineSceneTransparencyMode::Opaque},
    }};
    const PreparedOfflineMixedScene reusable =
        prepare_offline_mixed_scene(entries, settings);
    const OfflineSceneCamera camera = camera_at({0.0F, 0.0F, 3.0F});
    const OfflineSceneHierarchy hierarchy({
        std::optional<std::size_t>{1U},
        std::nullopt,
    });

    const std::array<OfflineSceneHierarchicalTimelineKeyframe, 2>
        missing_local{{
            OfflineSceneHierarchicalTimelineKeyframe{
                0.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {Mat4::identity()}}},
            OfflineSceneHierarchicalTimelineKeyframe{
                2.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {Mat4::identity()}}},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)sample_offline_hierarchical_timeline(
                hierarchy,
                missing_local,
                std::span<const float>{});
        },
        "hierarchical timeline validates every keyframe local count even for an empty sample request");

    const OfflineSceneHierarchy wrong_scene_hierarchy({
        std::nullopt,
    });
    const std::array<OfflineSceneHierarchicalTimelineKeyframe, 2>
        wrong_scene_keyframes{{
            OfflineSceneHierarchicalTimelineKeyframe{
                0.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {Mat4::identity()}}},
            OfflineSceneHierarchicalTimelineKeyframe{
                2.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {Mat4::identity()}}},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_hierarchy_timeline_sequence(
                reusable,
                wrong_scene_hierarchy,
                wrong_scene_keyframes,
                std::span<const float>{});
        },
        "hierarchical timeline topology ownership must match the prepared scene before sampling");

    const std::array<OfflineSceneHierarchicalTimelineKeyframe, 2>
        later_projective{{
            OfflineSceneHierarchicalTimelineKeyframe{
                0.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {Mat4::identity(), Mat4::identity()}}},
            OfflineSceneHierarchicalTimelineKeyframe{
                2.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {
                        Mat4::identity(),
                        Mat4::perspective(
                            radians(55.0F),
                            1.0F,
                            0.1F,
                            20.0F),
                    }}},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_hierarchy_timeline_sequence(
                reusable,
                hierarchy,
                later_projective,
                std::span<const float>{});
        },
        "hierarchical timeline rejects a projective local keyframe before sampling");
    check(
        shade_calls == 0U,
        "invalid hierarchical timeline keyframes reject before fragment execution");

    Mat4 positive_max = Mat4::identity();
    Mat4 negative_max = Mat4::identity();
    positive_max(0U, 0U) = std::numeric_limits<float>::max();
    negative_max(0U, 0U) = -std::numeric_limits<float>::max();
    const std::array<OfflineSceneHierarchicalTimelineKeyframe, 2>
        interpolation_overflow{{
            OfflineSceneHierarchicalTimelineKeyframe{
                0.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {Mat4::identity(), positive_max}}},
            OfflineSceneHierarchicalTimelineKeyframe{
                2.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {Mat4::identity(), negative_max}}},
        }};
    const std::array<float, 2> exact_then_interior{{0.0F, 1.0F}};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_hierarchy_timeline_sequence(
                reusable,
                hierarchy,
                interpolation_overflow,
                exact_then_interior);
        },
        "later non-finite local interpolation rejects the complete hierarchical timeline transaction");
    check(
        shade_calls == 0U,
        "later invalid local interpolation rejects before any earlier sample executes fragments");

    Mat4 huge_parent = Mat4::identity();
    huge_parent(0U, 0U) = std::numeric_limits<float>::max();
    const std::array<OfflineSceneHierarchicalTimelineKeyframe, 2>
        composition_overflow{{
            OfflineSceneHierarchicalTimelineKeyframe{
                0.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {
                        Mat4::scale({1.0F, 1.0F, 1.0F}),
                        Mat4::scale({1.0F, 1.0F, 1.0F}),
                    }}},
            OfflineSceneHierarchicalTimelineKeyframe{
                2.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {
                        Mat4::scale({4.0F, 1.0F, 1.0F}),
                        huge_parent,
                    }}},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_offline_hierarchy_timeline_sequence(
                reusable,
                hierarchy,
                composition_overflow,
                exact_then_interior);
        },
        "finite interpolated locals whose parent composition overflows reject before M92 preparation");
    check(
        shade_calls == 0U,
        "later composed-world overflow remains fail-closed before fragment execution");

    std::vector<float> too_many_samples(
        detail::kMaxOfflineTimelineSamples + 1U,
        0.0F);
    const std::array<OfflineSceneHierarchicalTimelineKeyframe, 2>
        valid{{
            OfflineSceneHierarchicalTimelineKeyframe{
                0.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {Mat4::identity(), Mat4::identity()}}},
            OfflineSceneHierarchicalTimelineKeyframe{
                2.0F,
                OfflineSceneHierarchicalFrameState{
                    camera,
                    {Mat4::identity(), Mat4::identity()}}},
        }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)sample_offline_hierarchical_timeline(
                hierarchy,
                valid,
                too_many_samples);
        },
        "hierarchical timeline enforces the established bounded sample count before allocation");
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
    test_programmatic_hierarchy_matches_explicit_world_frames();
    test_programmatic_hierarchy_validation_contract();
    test_programmatic_transform_graph_matches_hierarchy_and_group_reference();
    test_programmatic_transform_graph_validation_contract();
    test_programmatic_sparse_transform_graph_clip_matches_dense_m100();
    test_programmatic_sparse_transform_graph_clip_validation_contract();
    test_programmatic_sparse_clip_blend_endpoints_and_local_reference();
    test_programmatic_sparse_clip_blend_validation_contract();
    test_programmatic_transform_graph_timeline_matches_m97_and_local_reference();
    test_programmatic_transform_graph_timeline_validation_contract();
    test_programmatic_hierarchical_timeline_matches_m94_and_local_space_reference();
    test_programmatic_hierarchical_timeline_validation_contract();
    test_prepared_frame_sequence_rejects_invalid_transform_records();
    test_reusable_scene_validation_contract();

    if (failures != 0) {
        std::cerr << failures << " reusable offline prepared-scene test(s) failed\n";
        return 1;
    }
    std::cout << "reusable offline prepared-scene tests passed\n";
    return 0;
}
