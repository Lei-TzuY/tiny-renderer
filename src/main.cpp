#include <array>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/offline_benchmark.hpp"
#include "tiny_renderer/rasterizer.hpp"

using namespace tiny_renderer;

namespace {

std::size_t parse_benchmark_count(std::string_view text, const char* label) {
    std::size_t consumed = 0U;
    unsigned long long value = 0ULL;
    try {
        value = std::stoull(std::string(text), &consumed, 10);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " must be a non-negative integer");
    }
    if (consumed != text.size()
        || value > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(std::string(label) + " must be a non-negative integer");
    }
    return static_cast<std::size_t>(value);
}

VaryingPack benchmark_normal_varyings() {
    VaryingPack varyings;
    varyings.count = 3U;
    varyings.values[0] = 0.0F;
    varyings.values[1] = 0.0F;
    varyings.values[2] = 1.0F;
    return varyings;
}

ModelAsset benchmark_triangle_asset(
    const Vec3& albedo,
    const Vec3& specular,
    float opacity) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.9F, -0.9F, 0.0F}, benchmark_normal_varyings()),
        Vertex::with_varyings({0.9F, -0.9F, 0.0F}, benchmark_normal_varyings()),
        Vertex::with_varyings({0.0F, 0.9F, 0.0F}, benchmark_normal_varyings()),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};

    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "benchmark";
    draw.material.albedo = albedo;
    draw.material.specular = specular;
    draw.material.shininess = 32.0F;
    draw.material.opacity = opacity;
    asset.draws.push_back(draw);
    return asset;
}

Texture2D benchmark_environment_texture() {
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

OfflineEnvironmentReflectionState benchmark_reflection(const Texture2D& texture) {
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

OfflineSceneCamera benchmark_camera(const Vec3& eye) {
    OfflineSceneCamera camera;
    camera.eye = eye;
    camera.target = {0.0F, 0.0F, 0.0F};
    camera.up = {0.0F, 1.0F, 0.0F};
    camera.vertical_fov_radians = radians(55.0F);
    camera.near_plane = 0.1F;
    camera.far_plane = 20.0F;
    return camera;
}

int run_controlled_benchmark(int argc, char** argv) {
    if (argc > 4) {
        throw std::invalid_argument(
            "benchmark usage: tiny_renderer_sample --benchmark [MEASURED_ITERATIONS] [WARMUP_ITERATIONS]");
    }

    OfflineBenchmarkConfig config;
    if (argc >= 3) {
        config.measured_iterations = parse_benchmark_count(argv[2], "measured iteration count");
    }
    if (argc >= 4) {
        config.warmup_iterations = parse_benchmark_count(argv[3], "warmup iteration count");
    }

    const Texture2D environment = benchmark_environment_texture();
    const ModelAsset reflective = benchmark_triangle_asset(
        {0.0F, 0.0F, 0.0F},
        {0.9F, 0.65F, 0.35F},
        1.0F);
    const ModelAsset transparent = benchmark_triangle_asset(
        {0.1F, 0.7F, 0.25F},
        {0.0F, 0.0F, 0.0F},
        0.45F);
    const ModelAsset coverage = benchmark_triangle_asset(
        {0.65F, 0.18F, 0.85F},
        {0.0F, 0.0F, 0.0F},
        0.62F);

    const std::array<OfflineSceneEntry, 3> entries{{
        OfflineSceneEntry{
            &reflective,
            Mat4::translation({-0.28F, 0.0F, 0.0F}),
            {},
            OfflineSceneTransparencyMode::Opaque},
        OfflineSceneEntry{
            &transparent,
            Mat4::translation({0.22F, 0.03F, -0.22F}),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
        OfflineSceneEntry{
            &coverage,
            Mat4::translation({0.48F, -0.18F, 0.18F})
                * Mat4::scale({0.55F, 0.55F, 0.55F}),
            {},
            OfflineSceneTransparencyMode::AlphaToCoverage},
    }};

    OfflineRenderSettings settings;
    settings.width = 160U;
    settings.height = 120U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.01F, 0.015F, 0.02F};
    settings.environment_reflection = benchmark_reflection(environment);

    const OfflineSceneCamera camera_a = benchmark_camera({0.0F, 0.0F, 3.0F});
    const OfflineSceneCamera camera_b = benchmark_camera({1.35F, 0.15F, 3.0F});
    const std::array<OfflineSceneCamera, 3> cameras{{camera_a, camera_b, camera_a}};

    const OfflineBenchmarkReport report = benchmark_offline_mixed_scene(
        entries,
        settings,
        cameras,
        config);

    std::cout
        << "benchmark=tiny-renderer-offline-v1"
        << " width=" << settings.width
        << " height=" << settings.height
        << " samples=" << static_cast<unsigned>(settings.sample_count)
        << " cameras=" << report.camera_count
        << " warmup=" << report.warmup_iterations
        << " measured=" << report.samples.size()
        << '\n';
    std::cout << std::fixed << std::setprecision(3);
    for (std::size_t index = 0U; index < report.samples.size(); ++index) {
        const OfflineBenchmarkSample& sample = report.samples[index];
        std::cout
            << "sample=" << index
            << " prepare_us=" << sample.preparation_microseconds
            << " eval_preflight_us=" << sample.evaluation_preflight_microseconds
            << " submission_raster_us=" << sample.submission_raster_microseconds
            << " hash=0x" << std::hex << sample.sequence_hash << std::dec
            << '\n';
    }
    std::cout
        << "note=raw-timings-no-performance-claim; use fixed hardware and Release builds for comparisons\n";
    return 0;
}

int run_sample(int argc, char** argv) {
    const std::string output = argc > 1 ? argv[1] : "milestone1.ppm";
    Framebuffer framebuffer(320, 240);
    framebuffer.clear({0.035F, 0.045F, 0.07F});
    Rasterizer rasterizer(framebuffer);

    const Mat4 view = Mat4::look_at({0.0F, 0.0F, 0.5F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F});
    const Mat4 projection = Mat4::perspective(radians(60.0F), 320.0F / 240.0F, 0.1F, 10.0F);

    const Triangle far_triangle{{
        {{-1.15F, -0.75F, -3.1F}, {0.08F, 0.25F, 0.95F}},
        {{1.15F, -0.75F, -3.1F}, {0.25F, 0.85F, 1.00F}},
        {{0.0F, 1.05F, -3.1F}, {0.20F, 0.45F, 0.95F}},
    }};

    const Triangle near_triangle{{
        {{-0.8F, -0.45F, -2.1F}, {1.00F, 0.12F, 0.10F}},
        {{0.95F, -0.25F, -2.1F}, {1.00F, 0.75F, 0.05F}},
        {{0.05F, 0.95F, -2.1F}, {0.95F, 0.20F, 0.55F}},
    }};

    const Triangle clipped_triangle{{
        {{-2.6F, -0.1F, -2.7F}, {0.10F, 0.95F, 0.30F}},
        {{-0.65F, -0.95F, -2.7F}, {0.15F, 0.65F, 0.20F}},
        {{-0.55F, 0.35F, -2.7F}, {0.65F, 1.00F, 0.25F}},
    }};

    rasterizer.draw_triangle(far_triangle, Mat4::identity(), view, projection);
    rasterizer.draw_triangle(clipped_triangle, Mat4::rotation_y(radians(-7.0F)), view, projection);
    rasterizer.draw_triangle(near_triangle, Mat4::translation({0.12F, -0.02F, 0.0F}), view, projection);

    framebuffer.write_ppm(output);
    std::cout << "wrote " << output << " (320x240), framebuffer FNV-1a64=0x" << std::hex << framebuffer.fnv1a64() << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--benchmark") {
            return run_controlled_benchmark(argc, argv);
        }
        return run_sample(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "tiny_renderer_sample: " << error.what() << '\n';
        return 1;
    }
}
