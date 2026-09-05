#include "tiny_renderer/model_renderer.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include "rasterizer_validation.hpp"

namespace tiny_renderer {
namespace {

void validate_sampler(const SamplerState& sampler) {
    const auto validate_address = [](AddressMode mode) {
        switch (mode) {
            case AddressMode::Clamp:
            case AddressMode::Repeat:
                return;
        }
        throw std::invalid_argument("model texture sampler uses an unknown address mode");
    };
    validate_address(sampler.address_u);
    validate_address(sampler.address_v);

    switch (sampler.filter) {
        case FilterMode::Nearest:
        case FilterMode::Bilinear:
            return;
    }
    throw std::invalid_argument("model texture sampler uses an unknown filter mode");
}

void validate_material(const MaterialState& material) {
    const Vec3& albedo = material.albedo;
    if (!std::isfinite(albedo.x) || !std::isfinite(albedo.y) || !std::isfinite(albedo.z)
        || albedo.x < 0.0F || albedo.x > 1.0F
        || albedo.y < 0.0F || albedo.y > 1.0F
        || albedo.z < 0.0F || albedo.z > 1.0F) {
        throw std::invalid_argument("model material albedo components must be finite and within [0, 1]");
    }
}

void validate_model_structure(const ModelAsset& asset) {
    const std::size_t triangle_count = asset.mesh.triangles.size();
    if (triangle_count == 0U) {
        if (!asset.draws.empty()) {
            throw std::invalid_argument("empty model mesh must not contain material draws");
        }
        return;
    }
    if (asset.draws.empty()) {
        throw std::invalid_argument("non-empty model mesh requires material draws");
    }

    std::size_t cursor = 0U;
    for (const MaterialDraw& draw : asset.draws) {
        if (draw.range.first_triangle > triangle_count
            || draw.range.triangle_count > triangle_count - draw.range.first_triangle) {
            throw std::out_of_range("model material draw range exceeds triangle list");
        }
        if (draw.range.triangle_count == 0U) {
            throw std::invalid_argument("model material draw ranges must be non-empty");
        }
        if (draw.range.first_triangle != cursor) {
            throw std::invalid_argument("model material draw ranges must be contiguous and ordered");
        }
        cursor = draw.range.first_triangle + draw.range.triangle_count;
    }
    if (cursor != triangle_count) {
        throw std::invalid_argument("model material draw ranges must cover the canonical triangle list exactly once");
    }

    for (const TriangleIndices& triangle : asset.mesh.triangles) {
        for (const std::uint32_t index : triangle) {
            if (static_cast<std::size_t>(index) >= asset.mesh.vertices.size()) {
                throw std::out_of_range("model mesh triangle index out of range");
            }
        }
    }
}

void validate_static_model_state(const ModelAsset& asset, const ModelRenderOptions& options) {
    validate_model_structure(asset);
    detail::validate_face_culling(options.cull_mode, options.front_face);
    validate_depth_state(options.depth_state);
    validate_stencil_state(options.stencil_state);
    validate_blend_state(options.blend_state);
    detail::validate_viewport_state_definition(options.viewport_state);
    bool sampler_needed = false;
    for (const MaterialDraw& draw : asset.draws) {
        validate_material(draw.material);
        sampler_needed = sampler_needed || static_cast<bool>(draw.diffuse_texture);
    }
    if (sampler_needed) {
        validate_sampler(options.sampler);
    }
}

TextureBinding texture_binding_for(const MaterialDraw& draw, const ModelRenderOptions& options) {
    if (!draw.diffuse_texture) {
        return {};
    }
    return TextureBinding{
        draw.diffuse_texture.get(),
        options.u_channel,
        options.v_channel,
        options.sampler,
    };
}

BaseColorSource base_color_source_for(const MaterialDraw& draw) {
    return draw.diffuse_texture ? BaseColorSource::Texture : BaseColorSource::ConstantWhite;
}

void preflight_prepared_model_transform(
    const Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    const Mat4& model) {
    const ModelAsset& asset = prepared.asset();
    const ModelRenderOptions& options = prepared.options();
    for (const MaterialDraw& draw : asset.draws) {
        detail::preflight_mesh_range_submission(
            framebuffer,
            asset.mesh,
            DrawRange{0U, 0U},
            {},
            texture_binding_for(draw, options),
            options.directional_light,
            draw.material,
            base_color_source_for(draw),
            options.cull_mode,
            options.front_face,
            options.depth_state,
            options.viewport_state,
            options.stencil_state,
            options.blend_state,
            &model,
            false);
    }
}

void execute_prepared_model_transform(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection) {
    const ModelAsset& asset = prepared.asset();
    const ModelRenderOptions& options = prepared.options();
    for (const MaterialDraw& draw : asset.draws) {
        Rasterizer rasterizer(
            framebuffer,
            {},
            texture_binding_for(draw, options),
            options.directional_light,
            draw.material,
            base_color_source_for(draw),
            options.cull_mode,
            options.front_face,
            options.depth_state,
            options.viewport_state,
            options.stencil_state,
            options.blend_state);
        rasterizer.draw_mesh_range(asset.mesh, draw.range, model, view, projection);
    }
}

template <typename PreflightDraw, typename SubmitRange>
void draw_validated_model_impl(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const ModelRenderOptions& options,
    PreflightDraw&& preflight_draw,
    SubmitRange&& submit_range) {
    if (asset.draws.empty()) {
        return;
    }

    for (const MaterialDraw& draw : asset.draws) {
        preflight_draw(draw);
    }

    for (const MaterialDraw& draw : asset.draws) {
        Rasterizer rasterizer(
            framebuffer,
            {},
            texture_binding_for(draw, options),
            options.directional_light,
            draw.material,
            base_color_source_for(draw),
            options.cull_mode,
            options.front_face,
            options.depth_state,
            options.viewport_state,
            options.stencil_state,
            options.blend_state);
        submit_range(rasterizer, draw.range);
    }
}

}  // namespace

PreparedModelSubmission::PreparedModelSubmission(ModelAsset asset, ModelRenderOptions options)
    : asset_(std::move(asset)), options_(options) {}

PreparedModelSubmission prepare_model_asset(ModelAsset asset, ModelRenderOptions options) {
    validate_static_model_state(asset, options);
    return PreparedModelSubmission{std::move(asset), options};
}

void draw_prepared_model_instances(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    std::span<const Mat4> models,
    const Mat4& view,
    const Mat4& projection) {
    if (models.empty() || prepared.asset().draws.empty()) {
        return;
    }

    for (const Mat4& model : models) {
        preflight_prepared_model_transform(framebuffer, prepared, model);
    }

    for (const Mat4& model : models) {
        execute_prepared_model_transform(framebuffer, prepared, model, view, projection);
    }
}

void draw_prepared_model_instances(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    std::span<const Mat4> mvps) {
    const ModelAsset& asset = prepared.asset();
    const ModelRenderOptions& options = prepared.options();
    if (mvps.empty() || asset.draws.empty()) {
        return;
    }

    for (const Mat4& mvp : mvps) {
        (void)mvp;
        for (const MaterialDraw& draw : asset.draws) {
            detail::preflight_mesh_range_submission(
                framebuffer,
                asset.mesh,
                DrawRange{0U, 0U},
                {},
                texture_binding_for(draw, options),
                options.directional_light,
                draw.material,
                base_color_source_for(draw),
                options.cull_mode,
                options.front_face,
                options.depth_state,
                options.viewport_state,
                options.stencil_state,
                options.blend_state,
                nullptr,
                true);
        }
    }

    for (const Mat4& mvp : mvps) {
        for (const MaterialDraw& draw : asset.draws) {
            Rasterizer rasterizer(
                framebuffer,
                {},
                texture_binding_for(draw, options),
                options.directional_light,
                draw.material,
                base_color_source_for(draw),
                options.cull_mode,
                options.front_face,
                options.depth_state,
                options.viewport_state,
                options.stencil_state,
                options.blend_state);
            rasterizer.draw_mesh_range(asset.mesh, draw.range, mvp);
        }
    }
}

void draw_prepared_model_list(
    Framebuffer& framebuffer,
    std::span<const PreparedModelListEntry> entries,
    const Mat4& view,
    const Mat4& projection) {
    if (entries.empty()) {
        return;
    }

    for (const PreparedModelListEntry& entry : entries) {
        if (entry.prepared == nullptr) {
            throw std::invalid_argument("prepared model list entry requires a prepared plan");
        }
    }

    for (const PreparedModelListEntry& entry : entries) {
        preflight_prepared_model_transform(framebuffer, *entry.prepared, entry.model);
    }

    for (const PreparedModelListEntry& entry : entries) {
        execute_prepared_model_transform(
            framebuffer,
            *entry.prepared,
            entry.model,
            view,
            projection);
    }
}

void draw_prepared_model(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection) {
    draw_prepared_model_instances(
        framebuffer,
        prepared,
        std::span<const Mat4>{&model, 1U},
        view,
        projection);
}

void draw_prepared_model(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    const Mat4& mvp) {
    draw_prepared_model_instances(
        framebuffer,
        prepared,
        std::span<const Mat4>{&mvp, 1U});
}

void draw_model_asset(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection,
    ModelRenderOptions options) {
    validate_static_model_state(asset, options);
    draw_validated_model_impl(
        framebuffer,
        asset,
        options,
        [&](const MaterialDraw& draw) {
            detail::preflight_mesh_range_submission(
                framebuffer,
                asset.mesh,
                DrawRange{0U, 0U},
                {},
                texture_binding_for(draw, options),
                options.directional_light,
                draw.material,
                base_color_source_for(draw),
                options.cull_mode,
                options.front_face,
                options.depth_state,
                options.viewport_state,
                options.stencil_state,
                options.blend_state,
                &model,
                false);
        },
        [&](Rasterizer& rasterizer, DrawRange range) {
            rasterizer.draw_mesh_range(asset.mesh, range, model, view, projection);
        });
}

void draw_model_asset(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const Mat4& mvp,
    ModelRenderOptions options) {
    validate_static_model_state(asset, options);
    draw_validated_model_impl(
        framebuffer,
        asset,
        options,
        [&](const MaterialDraw& draw) {
            detail::preflight_mesh_range_submission(
                framebuffer,
                asset.mesh,
                DrawRange{0U, 0U},
                {},
                texture_binding_for(draw, options),
                options.directional_light,
                draw.material,
                base_color_source_for(draw),
                options.cull_mode,
                options.front_face,
                options.depth_state,
                options.viewport_state,
                options.stencil_state,
                options.blend_state,
                nullptr,
                true);
        },
        [&](Rasterizer& rasterizer, DrawRange range) {
            rasterizer.draw_mesh_range(asset.mesh, range, mvp);
        });
}

}  // namespace tiny_renderer
