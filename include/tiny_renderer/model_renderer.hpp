#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

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
    FragmentProgramPtr fragment_program{};
    VertexProgramPtr vertex_program{};
    PointLight point_light{};
    FixedLightCollection fixed_lights{};
    PointShadowState point_shadow_state{};
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

// Stable painter-order submission for a caller-selected transparent prepared
// list. Each non-empty entry is sorted by the mean view-space Z of its
// canonical mesh vertices after the entry model transform; more-negative Z is
// submitted first, matching the right-handed camera convention used by
// Mat4::look_at/perspective. Equal-depth entries preserve caller order.
//
// This bounded entry-level pass deliberately does not classify opaque vs
// transparent materials, split mixed-material models, sort individual
// triangles, or claim order-independent transparency. The caller retains
// ownership of blend/depth state. Vertex programs are rejected because they
// may move geometry after the canonical sort key has been computed.
inline void draw_prepared_model_list_back_to_front(
    Framebuffer& framebuffer,
    std::span<const PreparedModelListEntry> entries,
    const Mat4& view,
    const Mat4& projection) {
    struct SortRecord {
        PreparedModelListEntry entry;
        double view_depth{};
    };

    if (entries.empty()) {
        return;
    }

    std::vector<SortRecord> records;
    records.reserve(entries.size());
    for (const PreparedModelListEntry& entry : entries) {
        if (entry.prepared == nullptr) {
            throw std::invalid_argument("back-to-front prepared list entry requires a prepared plan");
        }

        const ModelAsset& asset = entry.prepared->asset();
        const Mesh& mesh = asset.mesh;
        if (asset.draws.empty()) {
            continue;
        }
        if (entry.prepared->options().vertex_program) {
            throw std::invalid_argument(
                "back-to-front prepared list does not support position-changing vertex programs");
        }
        if (mesh.vertices.empty()) {
            throw std::logic_error("non-empty prepared draw list requires canonical mesh vertices");
        }

        const Mat4 view_model = view * entry.model;
        double depth_sum = 0.0;
        for (const Vertex& vertex : mesh.vertices) {
            const Vec4 position = view_model * Vec4{
                vertex.position.x,
                vertex.position.y,
                vertex.position.z,
                1.0F,
            };
            if (!std::isfinite(position.x)
                || !std::isfinite(position.y)
                || !std::isfinite(position.z)
                || !std::isfinite(position.w)
                || std::fabs(position.w) <= kEpsilon) {
                throw std::invalid_argument(
                    "back-to-front prepared list produced a non-finite view-space sort position");
            }
            const double depth = static_cast<double>(position.z)
                / static_cast<double>(position.w);
            if (!std::isfinite(depth)) {
                throw std::invalid_argument(
                    "back-to-front prepared list produced a non-finite view-space sort depth");
            }
            depth_sum += depth;
            if (!std::isfinite(depth_sum)) {
                throw std::overflow_error(
                    "back-to-front prepared list sort-depth accumulation overflowed");
            }
        }

        const double mean_depth = depth_sum / static_cast<double>(mesh.vertices.size());
        if (!std::isfinite(mean_depth)) {
            throw std::invalid_argument(
                "back-to-front prepared list produced a non-finite mean sort depth");
        }
        records.push_back({entry, mean_depth});
    }

    std::stable_sort(
        records.begin(),
        records.end(),
        [](const SortRecord& left, const SortRecord& right) {
            return left.view_depth < right.view_depth;
        });

    std::vector<PreparedModelListEntry> ordered;
    ordered.reserve(records.size());
    for (const SortRecord& record : records) {
        ordered.push_back(record.entry);
    }

    draw_prepared_model_list(
        framebuffer,
        std::span<const PreparedModelListEntry>{ordered.data(), ordered.size()},
        view,
        projection);
}

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
