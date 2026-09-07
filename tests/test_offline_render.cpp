#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/environment.hpp"
#include "tiny_renderer/offline_render.hpp"
#include "tiny_renderer/pfm_loader.hpp"

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

void check_vec3_near(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check(
        nearly_equal(actual.x, expected.x)
            && nearly_equal(actual.y, expected.y)
            && nearly_equal(actual.z, expected.z),
        message);
}

ModelAsset triangle_asset(float scale = 1.0F, Vec3 offset = {}) {
    const auto transform = [=](const Vec3& point) {
        return Vec3{
            point.x * scale + offset.x,
            point.y * scale + offset.y,
            point.z * scale + offset.z,
        };
    };

    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings(transform({-1.0F, -1.0F, 0.0F}), VaryingPack{}),
        Vertex::with_varyings(transform({1.0F, -1.0F, 0.0F}), VaryingPack{}),
        Vertex::with_varyings(transform({0.0F, 1.0F, 0.0F}), VaryingPack{}),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};

    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "preview";
    draw.material.albedo = {0.8F, 0.2F, 0.1F};
    asset.draws.push_back(draw);
    return asset;
}

bool differs_from_clear(const Framebuffer& rendered, const OfflineRenderSettings& settings) {
    Framebuffer clear(settings.width, settings.height, settings.sample_count);
    clear.clear(settings.clear_color);
    return rendered.fnv1a64() != clear.fnv1a64();
}

Texture2D quadrant_environment() {
    return Texture2D(
        4U,
        2U,
        {
            {1.0F, 0.0F, 0.0F},
            {2.0F, 0.0F, 0.0F},
            {0.0F, 3.0F, 0.0F},
            {0.0F, 0.0F, 4.0F},
            {5.0F, 0.0F, 0.0F},
            {0.0F, 6.0F, 0.0F},
            {0.0F, 0.0F, 7.0F},
            {8.0F, 8.0F, 0.0F},
        });
}

EnvironmentBackgroundState nearest_environment(const Texture2D& texture) {
    EnvironmentBackgroundState environment;
    environment.texture = &texture;
    environment.sampler.address_u = AddressMode::Repeat;
    environment.sampler.address_v = AddressMode::Clamp;
    environment.sampler.filter = FilterMode::Nearest;
    environment.sampler.mip_filter = MipFilterMode::Disabled;
    return environment;
}

PerspectiveCameraState canonical_environment_camera() {
    PerspectiveCameraState camera;
    camera.eye = {0.0F, 0.0F, 0.0F};
    camera.target = {0.0F, 0.0F, -1.0F};
    camera.up = {0.0F, 1.0F, 0.0F};
    camera.vertical_fov_radians = radians(90.0F);
    camera.aspect = 1.0F;
    return camera;
}

Texture2D load_constant_hdr_pfm(const Vec3& value) {
    std::array<float, 6> pixels{
        value.x, value.y, value.z,
        value.x, value.y, value.z,
    };
    std::string bytes = "PF\n2 1\n-1.0\n";
    bytes.append(
        reinterpret_cast<const char*>(pixels.data()),
        static_cast<std::size_t>(sizeof(pixels)));
    std::istringstream input(bytes, std::ios::binary);
    return load_pfm(input);
}

struct FrameSnapshot {
    std::vector<Vec3> colors;
    std::vector<float> depths;
    std::vector<unsigned int> stencils;
};

FrameSnapshot snapshot_samples(const Framebuffer& framebuffer) {
    FrameSnapshot snapshot;
    const std::size_t samples = framebuffer.samples_per_pixel();
    snapshot.colors.reserve(framebuffer.width() * framebuffer.height() * samples);
    snapshot.depths.reserve(framebuffer.width() * framebuffer.height() * samples);
    snapshot.stencils.reserve(framebuffer.width() * framebuffer.height() * samples);
    for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
            for (std::size_t sample = 0U; sample < samples; ++sample) {
                snapshot.colors.push_back(framebuffer.sample_color_at(x, y, sample));
                snapshot.depths.push_back(framebuffer.sample_depth_at(x, y, sample));
                snapshot.stencils.push_back(framebuffer.sample_stencil_at(x, y, sample));
            }
        }
    }
    return snapshot;
}

void check_snapshot_unchanged(
    const Framebuffer& framebuffer,
    const FrameSnapshot& before,
    const std::string& message) {
    const FrameSnapshot after = snapshot_samples(framebuffer);
    bool same = before.colors.size() == after.colors.size()
        && before.depths == after.depths
        && before.stencils == after.stencils;
    if (same) {
        for (std::size_t i = 0U; i < before.colors.size(); ++i) {
            if (!nearly_equal(before.colors[i].x, after.colors[i].x)
                || !nearly_equal(before.colors[i].y, after.colors[i].y)
                || !nearly_equal(before.colors[i].z, after.colors[i].z)) {
                same = false;
                break;
            }
        }
    }
    check(same, message);
}

void test_deterministic_rendering() {
    const ModelAsset asset = triangle_asset();

    OfflineRenderSettings single{};
    single.width = 96U;
    single.height = 64U;
    single.sample_count = SampleCount::One;
    const Framebuffer single_a = render_model_preview(asset, single);
    const Framebuffer single_b = render_model_preview(asset, single);
    check(single_a.rgb8() == single_b.rgb8(), "1x preview output is deterministic");
    check(differs_from_clear(single_a, single), "1x preview rasterizes visible model pixels");

    OfflineRenderSettings multisample = single;
    multisample.sample_count = SampleCount::Four;
    const Framebuffer multi_a = render_model_preview(asset, multisample);
    const Framebuffer multi_b = render_model_preview(asset, multisample);
    check(multi_a.rgb8() == multi_b.rgb8(), "4x preview output is deterministic");
    check(differs_from_clear(multi_a, multisample), "4x preview rasterizes visible model pixels");
}

void test_auto_fit_is_translation_and_uniform_scale_invariant() {
    const ModelAsset canonical = triangle_asset();
    const ModelAsset transformed = triangle_asset(4.0F, {8.0F, -12.0F, 5.0F});

    OfflineRenderSettings settings{};
    settings.width = 80U;
    settings.height = 80U;
    settings.sample_count = SampleCount::Four;
    const Framebuffer canonical_frame = render_model_preview(canonical, settings);
    const Framebuffer transformed_frame = render_model_preview(transformed, settings);
    check(
        canonical_frame.rgb8() == transformed_frame.rgb8(),
        "auto-fit normalizes uniform object scale and translation deterministically");
}

void test_limiting_axis_framing() {
    const ModelAsset asset = triangle_asset();

    OfflineRenderSettings portrait{};
    portrait.width = 32U;
    portrait.height = 96U;
    portrait.sample_count = SampleCount::One;
    const Framebuffer portrait_frame = render_model_preview(asset, portrait);
    check(differs_from_clear(portrait_frame, portrait), "portrait preview remains visibly framed");

    OfflineRenderSettings landscape = portrait;
    landscape.width = 96U;
    landscape.height = 32U;
    const Framebuffer landscape_frame = render_model_preview(asset, landscape);
    check(differs_from_clear(landscape_frame, landscape), "landscape preview remains visibly framed");
}

void test_invalid_requests_fail_closed() {
    const ModelAsset asset = triangle_asset();

    OfflineRenderSettings zero_width{};
    zero_width.width = 0U;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, zero_width); },
        "zero-width preview is rejected");

    OfflineRenderSettings oversized{};
    oversized.width = 4U * 1024U * 1024U + 1U;
    oversized.height = 1U;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, oversized); },
        "preview pixel budget is bounded");

    OfflineRenderSettings invalid_samples{};
    invalid_samples.sample_count = static_cast<SampleCount>(2U);
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, invalid_samples); },
        "unsupported preview sample count is rejected");

    OfflineRenderSettings invalid_clear{};
    invalid_clear.clear_color.x = std::numeric_limits<float>::infinity();
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, invalid_clear); },
        "non-finite preview clear color is rejected");

    OfflineRenderSettings invalid_fov{};
    invalid_fov.vertical_fov_radians = 0.0F;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, invalid_fov); },
        "invalid preview field of view is rejected");

    OfflineRenderSettings invalid_margin{};
    invalid_margin.framing_margin = 0.5F;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, invalid_margin); },
        "invalid preview framing margin is rejected");

    ModelAsset empty;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(empty); },
        "empty model preview is rejected");

    ModelAsset zero_extent = triangle_asset();
    for (Vertex& vertex : zero_extent.mesh.vertices) {
        vertex.position = {1.0F, 1.0F, 1.0F};
    }
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(zero_extent); },
        "zero-extent model preview is rejected");
}

void test_equirectangular_mapping_contract() {
    const Vec2 forward = equirectangular_uv({0.0F, 0.0F, -1.0F});
    check(nearly_equal(forward.x, 0.5F) && nearly_equal(forward.y, 0.5F), "-Z maps to equirectangular center");

    const Vec2 right = equirectangular_uv({1.0F, 0.0F, 0.0F});
    check(nearly_equal(right.x, 0.75F) && nearly_equal(right.y, 0.5F), "+X advances equirectangular longitude");

    const Vec2 left = equirectangular_uv({-1.0F, 0.0F, 0.0F});
    check(nearly_equal(left.x, 0.25F) && nearly_equal(left.y, 0.5F), "-X retreats equirectangular longitude");

    const Vec2 seam = equirectangular_uv({0.0F, 0.0F, 1.0F});
    check(nearly_equal(seam.x, 0.0F) && nearly_equal(seam.y, 0.5F), "+Z seam canonicalizes to u=0");

    const Vec2 north = equirectangular_uv({0.0F, 1.0F, 0.0F});
    const Vec2 south = equirectangular_uv({0.0F, -1.0F, 0.0F});
    check(nearly_equal(north.x, 0.5F) && nearly_equal(north.y, 0.0F), "north pole uses deterministic longitude and v=0");
    check(nearly_equal(south.x, 0.5F) && nearly_equal(south.y, 1.0F), "south pole uses deterministic longitude and v=1");

    const Vec2 yawed = equirectangular_uv({0.0F, 0.0F, -1.0F}, kPi * 0.5F);
    check(nearly_equal(yawed.x, 0.75F) && nearly_equal(yawed.y, 0.5F), "positive yaw rotates longitude around +Y");

    check_throws<std::invalid_argument>(
        [] { (void)equirectangular_uv({0.0F, 0.0F, 0.0F}); },
        "zero environment direction is rejected");
    check_throws<std::invalid_argument>(
        [] { (void)equirectangular_uv({std::numeric_limits<float>::infinity(), 0.0F, 0.0F}); },
        "non-finite environment direction is rejected");
    check_throws<std::invalid_argument>(
        [] { (void)equirectangular_uv({0.0F, 0.0F, -1.0F}, kPi + 0.01F); },
        "out-of-range environment yaw is rejected");
}

void test_environment_per_sample_reconstruction_and_attachment_preservation() {
    const Texture2D texture = quadrant_environment();
    const EnvironmentBackgroundState environment = nearest_environment(texture);
    const PerspectiveCameraState camera = canonical_environment_camera();

    Framebuffer single(1U, 1U, SampleCount::One);
    single.clear({0.1F, 0.1F, 0.1F}, 0.37F, 9U);
    draw_environment_background(single, camera, environment);
    check_vec3_near(single.sample_color_at(0U, 0U, 0U), {0.0F, 0.0F, 7.0F}, "1x center ray samples -Z environment texel");
    check(nearly_equal(single.sample_depth_at(0U, 0U, 0U), 0.37F), "1x background preserves depth");
    check(single.sample_stencil_at(0U, 0U, 0U) == 9U, "1x background preserves stencil");

    Framebuffer multisample(1U, 1U, SampleCount::Four);
    multisample.clear({0.1F, 0.1F, 0.1F}, 0.37F, 9U);
    draw_environment_background(multisample, camera, environment);
    const std::array<Vec3, 4> expected{
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{0.0F, 3.0F, 0.0F},
        Vec3{0.0F, 6.0F, 0.0F},
        Vec3{0.0F, 0.0F, 7.0F},
    };
    for (std::size_t sample = 0U; sample < expected.size(); ++sample) {
        check_vec3_near(
            multisample.sample_color_at(0U, 0U, sample),
            expected[sample],
            "4x background reconstructs the documented quarter-offset sample ray");
        check(nearly_equal(multisample.sample_depth_at(0U, 0U, sample), 0.37F), "4x background preserves per-sample depth");
        check(multisample.sample_stencil_at(0U, 0U, sample) == 9U, "4x background preserves per-sample stencil");
    }
    check_vec3_near(multisample.color_at(0U, 0U), {0.5F, 2.25F, 1.75F}, "4x background resolves existing sample colors deterministically");

    EnvironmentBackgroundState amplified = environment;
    amplified.intensity = 2.0F;
    amplified.yaw_radians = kPi * 0.5F;
    Framebuffer yawed(1U, 1U, SampleCount::One);
    draw_environment_background(yawed, camera, amplified);
    check_vec3_near(yawed.color_at(0U, 0U), {16.0F, 16.0F, 0.0F}, "yaw and HDR intensity are applied before framebuffer ownership");
}

void test_pfm_and_programmatic_environment_equivalence() {
    const Vec3 hdr{4.0F, 2.0F, 1.0F};
    const Texture2D imported = load_constant_hdr_pfm(hdr);
    const Texture2D programmatic(2U, 1U, {hdr, hdr});
    EnvironmentBackgroundState imported_state = nearest_environment(imported);
    EnvironmentBackgroundState programmatic_state = nearest_environment(programmatic);

    PerspectiveCameraState camera = canonical_environment_camera();
    camera.aspect = 5.0F / 3.0F;
    Framebuffer imported_frame(5U, 3U, SampleCount::Four);
    Framebuffer programmatic_frame(5U, 3U, SampleCount::Four);
    draw_environment_background(imported_frame, camera, imported_state);
    draw_environment_background(programmatic_frame, camera, programmatic_state);

    for (std::size_t y = 0U; y < imported_frame.height(); ++y) {
        for (std::size_t x = 0U; x < imported_frame.width(); ++x) {
            for (std::size_t sample = 0U; sample < imported_frame.samples_per_pixel(); ++sample) {
                check_vec3_near(
                    imported_frame.sample_color_at(x, y, sample),
                    programmatic_frame.sample_color_at(x, y, sample),
                    "PFM-imported and programmatic HDR environments are sample-equivalent");
            }
        }
    }
}

void test_environment_geometry_and_display_mapping_integration() {
    const Texture2D imported = load_constant_hdr_pfm({4.0F, 2.0F, 1.0F});
    const EnvironmentBackgroundState environment = nearest_environment(imported);
    PerspectiveCameraState camera = canonical_environment_camera();

    Framebuffer framebuffer(33U, 33U, SampleCount::Four);
    framebuffer.clear({0.0F, 0.0F, 0.0F}, std::numeric_limits<float>::infinity(), 17U);
    draw_environment_background(framebuffer, camera, environment);
    check_vec3_near(framebuffer.sample_color_at(0U, 0U, 0U), {4.0F, 2.0F, 1.0F}, "HDR environment reaches framebuffer before geometry");
    check(std::isinf(framebuffer.sample_depth_at(0U, 0U, 0U)), "background leaves clear depth untouched before geometry");
    check(framebuffer.sample_stencil_at(0U, 0U, 0U) == 17U, "background leaves stencil untouched before geometry");

    const ModelAsset asset = triangle_asset();
    draw_model_asset(framebuffer, asset, Mat4::identity());
    check_vec3_near(framebuffer.sample_color_at(16U, 16U, 0U), {0.8F, 0.2F, 0.1F}, "normal geometry depth path occludes environment at covered samples");
    check(nearly_equal(framebuffer.sample_depth_at(16U, 16U, 0U), 0.5F), "geometry, not environment, owns covered depth");
    check(framebuffer.sample_stencil_at(16U, 16U, 0U) == 17U, "environment plus default geometry preserves stencil");
    check_vec3_near(framebuffer.sample_color_at(0U, 0U, 0U), {4.0F, 2.0F, 1.0F}, "uncovered samples retain HDR environment radiance");

    const Vec3 archival_before = framebuffer.sample_color_at(0U, 0U, 0U);
    const std::vector<std::uint8_t> display = framebuffer.rgb8(
        DisplayMappingState{},
        OutputTransferFunction::Srgb);
    check(!display.empty(), "environment-plus-geometry result reaches M52 display-mapped sRGB export boundary");
    check(
        display != framebuffer.rgb8(),
        "HDR display mapping and sRGB output differ from legacy linear-clamped RGB8 export");
    check_vec3_near(
        framebuffer.sample_color_at(0U, 0U, 0U),
        archival_before,
        "display mapping does not mutate linear HDR framebuffer data used by PFM archival export");
}

void test_invalid_environment_requests_fail_before_mutation() {
    const Texture2D linear(1U, 1U, {{1.0F, 1.0F, 1.0F}});
    const Texture2D srgb(
        1U,
        1U,
        {{0.5F, 0.5F, 0.5F}},
        TextureTransferFunction::Srgb);
    const PerspectiveCameraState camera = canonical_environment_camera();

    auto make_target = [] {
        Framebuffer framebuffer(2U, 2U, SampleCount::Four);
        framebuffer.clear({0.15F, 0.25F, 0.35F}, 0.42F, 23U);
        return framebuffer;
    };

    {
        Framebuffer framebuffer = make_target();
        const FrameSnapshot before = snapshot_samples(framebuffer);
        EnvironmentBackgroundState environment;
        check_throws<std::invalid_argument>(
            [&] { draw_environment_background(framebuffer, camera, environment); },
            "null environment texture is rejected");
        check_snapshot_unchanged(framebuffer, before, "null environment texture rejects before framebuffer mutation");
    }
    {
        Framebuffer framebuffer = make_target();
        const FrameSnapshot before = snapshot_samples(framebuffer);
        EnvironmentBackgroundState environment = nearest_environment(srgb);
        check_throws<std::invalid_argument>(
            [&] { draw_environment_background(framebuffer, camera, environment); },
            "sRGB source texture is rejected for linear HDR environment use");
        check_snapshot_unchanged(framebuffer, before, "sRGB environment rejection is fail-closed");
    }
    {
        Framebuffer framebuffer = make_target();
        const FrameSnapshot before = snapshot_samples(framebuffer);
        EnvironmentBackgroundState environment = nearest_environment(linear);
        environment.sampler.mip_filter = MipFilterMode::Nearest;
        check_throws<std::invalid_argument>(
            [&] { draw_environment_background(framebuffer, camera, environment); },
            "environment mip filtering is rejected until ray differentials exist");
        check_snapshot_unchanged(framebuffer, before, "unsupported environment mip filtering rejects before mutation");
    }
    {
        Framebuffer framebuffer = make_target();
        const FrameSnapshot before = snapshot_samples(framebuffer);
        EnvironmentBackgroundState environment = nearest_environment(linear);
        environment.intensity = std::numeric_limits<float>::quiet_NaN();
        check_throws<std::invalid_argument>(
            [&] { draw_environment_background(framebuffer, camera, environment); },
            "non-finite environment intensity is rejected");
        check_snapshot_unchanged(framebuffer, before, "invalid environment intensity rejects before mutation");
    }
    {
        Framebuffer framebuffer = make_target();
        const FrameSnapshot before = snapshot_samples(framebuffer);
        EnvironmentBackgroundState environment = nearest_environment(linear);
        PerspectiveCameraState invalid_camera = camera;
        invalid_camera.target = invalid_camera.eye;
        check_throws<std::invalid_argument>(
            [&] { draw_environment_background(framebuffer, invalid_camera, environment); },
            "degenerate environment camera is rejected");
        check_snapshot_unchanged(framebuffer, before, "invalid environment camera rejects before mutation");
    }
    {
        const Texture2D huge(
            1U,
            1U,
            {{std::numeric_limits<float>::max(), 1.0F, 1.0F}});
        Framebuffer framebuffer = make_target();
        const FrameSnapshot before = snapshot_samples(framebuffer);
        EnvironmentBackgroundState environment = nearest_environment(huge);
        environment.intensity = 2.0F;
        check_throws<std::overflow_error>(
            [&] { draw_environment_background(framebuffer, camera, environment); },
            "HDR environment intensity overflow is rejected");
        check_snapshot_unchanged(framebuffer, before, "radiance overflow rejects before any sample is committed");
    }
}

}  // namespace

int main() {
    test_deterministic_rendering();
    test_auto_fit_is_translation_and_uniform_scale_invariant();
    test_limiting_axis_framing();
    test_invalid_requests_fail_closed();
    test_equirectangular_mapping_contract();
    test_environment_per_sample_reconstruction_and_attachment_preservation();
    test_pfm_and_programmatic_environment_equivalence();
    test_environment_geometry_and_display_mapping_integration();
    test_invalid_environment_requests_fail_before_mutation();

    if (failures != 0) {
        std::cerr << failures << " offline render/environment test(s) failed\n";
        return 1;
    }
    std::cout << "offline render/environment tests passed\n";
    return 0;
}
