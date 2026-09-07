#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

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

}  // namespace

int main() {
    test_deterministic_rendering();
    test_auto_fit_is_translation_and_uniform_scale_invariant();
    test_limiting_axis_framing();
    test_invalid_requests_fail_closed();

    if (failures != 0) {
        std::cerr << failures << " offline render test(s) failed\n";
        return 1;
    }
    std::cout << "offline render tests passed\n";
    return 0;
}
