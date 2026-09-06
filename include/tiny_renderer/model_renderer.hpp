#pragma once

#include <cstddef>
#include <span>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model.hpp"
#include "tiny_renderer/rasterizer.hpp"

namespace tiny_renderer {

struct ModelRenderOptions {
    std::size_t u_channel{0U};
    std::size_t v_channel{1U};
    SamplerState sampler{};
    DirectionalLight directional_light{};
    CullMode cull_mode{CullMode::None};
    FrontFace front_face{FrontFace::CounterClockwise};
    DepthState depth_state{};
    ViewportState viewport_state{};
    StencilState stencil_state{};
    BlendState blend_state{};
    AlphaToCoverageState alpha_to_coverage_state{};
    ShadowState shadow_state{};
    AlphaTestState alpha_test_state{};
};

class PreparedModelSubmission {
public:
    PreparedModelSubmission(const PreparedModelSubmission&) = default;
    PreparedModelSubmission(PreparedModelSubmission&&) noexcept = default;
    PreparedModelSubmission& operator=(const PreparedModelSubmission&) = default;
    PreparedModelSubmission& operator=(PreparedModelSubmission&&) noexcept = default;

    [[nodiscard]] const ModelAsset& asset() const noexcept { return asset_; }
    [[nodiscard]] const ModelRenderOptions& options() const noexcept { return options_; }

private:
    friend PreparedModelSubmission prepare_model_asset(ModelAsset asset, ModelRenderOptions options);

    PreparedModelSubmission(ModelAsset asset, ModelRenderOptions options);

    ModelAsset asset_;
    ModelRenderOptions options_;
};

struct PreparedModelListEntry {
    const PreparedModelSubmission* prepared{nullptr};
    Mat4 model{Mat4::identity()};
};

[[nodiscard]] PreparedModelSubmission prepare_model_asset(
    ModelAsset asset,
    ModelRenderOptions options = {});

void draw_prepared_model(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection);

void draw_prepared_model(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    const Mat4& mvp);

void draw_prepared_model_instances(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    std::span<const Mat4> models,
    const Mat4& view,
    const Mat4& projection);

void draw_prepared_model_instances(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    std::span<const Mat4> mvps);

void draw_prepared_model_list(
    Framebuffer& framebuffer,
    std::span<const PreparedModelListEntry> entries,
    const Mat4& view,
    const Mat4& projection);

void draw_model_asset(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection,
    ModelRenderOptions options = {});

void draw_model_asset(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const Mat4& mvp,
    ModelRenderOptions options = {});

}  // namespace tiny_renderer
