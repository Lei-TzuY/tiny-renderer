#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "tiny_renderer/detail/rasterizer_validation.hpp"
#include "tiny_renderer/detail/vertex_program_execution.hpp"
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

// Stable painter-order submission for caller-selected transparent prepared
// work. Every material draw receives a post-vertex-program view-space centroid
// sort key; more-negative Z is submitted first and equal-depth draws preserve
// canonical caller-entry/material-draw order. The function does not classify
// opaque/translucent work, split triangles, or claim order-independent
// transparency. Caller-provided blend/depth state remains authoritative.
inline void draw_prepared_model_list_back_to_front(
    Framebuffer& framebuffer,
    std::span<const PreparedModelListEntry> entries,
    const Mat4& view,
    const Mat4& projection) {
    struct PreparedEntry {
        PreparedModelListEntry entry;
        detail::PreparedVertexMesh mesh;
    };
    struct DrawRecord {
        std::size_t entry_index{};
        std::size_t draw_index{};
        double view_depth{};
    };

    if (entries.empty()) {
        return;
    }

    const auto texture_binding_for = [](
        const MaterialDraw& draw,
        const ModelRenderOptions& options) {
        TextureBinding binding{
            draw.diffuse_texture.get(),
            options.u_channel,
            options.v_channel,
            options.sampler,
        };
        binding.opacity_texture = draw.opacity_texture.get();
        binding.normal_texture = draw.normal_texture.get();
        binding.specular_texture = draw.specular_texture.get();
        binding.emissive_texture = draw.emissive_texture.get();
        binding.shininess_texture = draw.shininess_texture.get();
        return binding;
    };
    const auto color_binding_for = [](const ModelAsset& asset) {
        if (!asset.vertex_color_channels) {
            return ColorBinding{};
        }
        const VertexColorChannels channels = *asset.vertex_color_channels;
        return ColorBinding{channels.red, channels.green, channels.blue};
    };
    const auto base_color_source_for = [](const ModelAsset& asset, const MaterialDraw& draw) {
        if (draw.diffuse_texture) {
            return BaseColorSource::Texture;
        }
        return asset.vertex_color_channels
            ? BaseColorSource::VaryingColor
            : BaseColorSource::ConstantWhite;
    };

    for (const PreparedModelListEntry& entry : entries) {
        if (entry.prepared == nullptr) {
            throw std::invalid_argument(
                "back-to-front prepared list entry requires a prepared plan");
        }
    }

    std::vector<PreparedEntry> prepared_entries;
    prepared_entries.reserve(entries.size());
    for (const PreparedModelListEntry& entry : entries) {
        const ModelAsset& asset = entry.prepared->asset();
        const ModelRenderOptions& options = entry.prepared->options();
        prepared_entries.push_back({
            entry,
            detail::prepare_vertex_program_mesh(options.vertex_program, asset.mesh),
        });
    }

    // Whole-list dynamic preflight remains a strict barrier before sort-key
    // generation or raster writes. PreparedModelSubmission already guarantees
    // immutable static topology/material state; this pass validates every
    // target-dependent draw against the post-program mesh.
    for (const PreparedEntry& prepared_entry : prepared_entries) {
        const PreparedModelSubmission& prepared = *prepared_entry.entry.prepared;
        const ModelAsset& asset = prepared.asset();
        const ModelRenderOptions& options = prepared.options();
        const Mesh& mesh = prepared_entry.mesh.get();
        detail::validate_alpha_test_state(options.alpha_test_state);
        for (const MaterialDraw& draw : asset.draws) {
            detail::preflight_mesh_range_submission(
                framebuffer,
                mesh,
                draw.range,
                color_binding_for(asset),
                texture_binding_for(draw, options),
                options.directional_light,
                options.point_light,
                options.fixed_lights,
                draw.material,
                base_color_source_for(asset, draw),
                options.cull_mode,
                options.front_face,
                options.depth_state,
                options.viewport_state,
                options.stencil_state,
                options.blend_state,
                options.alpha_to_coverage_state,
                options.shadow_state,
                options.point_shadow_state,
                &prepared_entry.entry.model,
                false);
        }
    }

    std::vector<DrawRecord> draws;
    for (std::size_t entry_index = 0U;
         entry_index < prepared_entries.size();
         ++entry_index) {
        const PreparedEntry& prepared_entry = prepared_entries[entry_index];
        const ModelAsset& asset = prepared_entry.entry.prepared->asset();
        const Mesh& mesh = prepared_entry.mesh.get();
        const Mat4 view_model = view * prepared_entry.entry.model;
        for (std::size_t draw_index = 0U;
             draw_index < asset.draws.size();
             ++draw_index) {
            const MaterialDraw& draw = asset.draws[draw_index];
            double depth_sum = 0.0;
            std::size_t corner_count = 0U;
            const std::size_t end = draw.range.first_triangle + draw.range.triangle_count;
            for (std::size_t triangle_index = draw.range.first_triangle;
                 triangle_index < end;
                 ++triangle_index) {
                for (const std::uint32_t vertex_index : mesh.triangles[triangle_index]) {
                    const Vertex& vertex = mesh.vertices[static_cast<std::size_t>(vertex_index)];
                    const Vec4 view_position = view_model * Vec4{
                        vertex.position.x,
                        vertex.position.y,
                        vertex.position.z,
                        1.0F,
                    };
                    if (!std::isfinite(view_position.x)
                        || !std::isfinite(view_position.y)
                        || !std::isfinite(view_position.z)
                        || !std::isfinite(view_position.w)
                        || std::fabs(view_position.w) <= kEpsilon) {
                        throw std::invalid_argument(
                            "back-to-front prepared draw produced an invalid view-space sort position");
                    }
                    const double depth = static_cast<double>(view_position.z)
                        / static_cast<double>(view_position.w);
                    if (!std::isfinite(depth)) {
                        throw std::invalid_argument(
                            "back-to-front prepared draw produced a non-finite view-space sort depth");
                    }
                    depth_sum += depth;
                    if (!std::isfinite(depth_sum)) {
                        throw std::overflow_error(
                            "back-to-front prepared draw sort-depth accumulation overflowed");
                    }
                    ++corner_count;
                }
            }
            if (corner_count == 0U) {
                throw std::logic_error(
                    "prepared material draw must reference at least one triangle corner");
            }
            const double mean_depth = depth_sum / static_cast<double>(corner_count);
            if (!std::isfinite(mean_depth)) {
                throw std::invalid_argument(
                    "back-to-front prepared draw produced a non-finite mean sort depth");
            }
            draws.push_back({entry_index, draw_index, mean_depth});
        }
    }

    std::stable_sort(
        draws.begin(),
        draws.end(),
        [](const DrawRecord& left, const DrawRecord& right) {
            return left.view_depth < right.view_depth;
        });

    for (const DrawRecord& record : draws) {
        const PreparedEntry& prepared_entry = prepared_entries[record.entry_index];
        const PreparedModelSubmission& prepared = *prepared_entry.entry.prepared;
        const ModelAsset& asset = prepared.asset();
        const ModelRenderOptions& options = prepared.options();
        const MaterialDraw& draw = asset.draws[record.draw_index];
        const Mesh& mesh = prepared_entry.mesh.get();
        Rasterizer rasterizer(
            framebuffer,
            color_binding_for(asset),
            texture_binding_for(draw, options),
            options.directional_light,
            draw.material,
            base_color_source_for(asset, draw),
            options.cull_mode,
            options.front_face,
            options.depth_state,
            options.viewport_state,
            options.stencil_state,
            options.blend_state,
            options.alpha_to_coverage_state,
            options.shadow_state,
            options.alpha_test_state,
            options.fragment_program,
            {},
            options.point_light,
            options.fixed_lights,
            options.point_shadow_state);
        rasterizer.draw_mesh_range(
            mesh,
            draw.range,
            prepared_entry.entry.model,
            view,
            projection);
    }
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
