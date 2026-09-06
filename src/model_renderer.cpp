#include "tiny_renderer/model_renderer.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "rasterizer_validation.hpp"
#include "vertex_program_internal.hpp"

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
    if (!std::isfinite(material.opacity) || material.opacity < 0.0F || material.opacity > 1.0F) {
        throw std::invalid_argument("model material opacity must be finite and within [0, 1]");
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

void validate_fragment_program_for_mesh(
    const FragmentProgramPtr& fragment_program,
    const Mesh& mesh) {
    if (!fragment_program) {
        return;
    }
    const std::size_t varying_count = mesh.vertices.empty()
        ? 0U
        : mesh.vertices.front().varyings.count;
    fragment_program->validate(varying_count);
}

void validate_static_model_state(const ModelAsset& asset, const ModelRenderOptions& options) {
    validate_model_structure(asset);
    detail::validate_face_culling(options.cull_mode, options.front_face);
    validate_depth_state(options.depth_state);
    validate_stencil_state(options.stencil_state);
    validate_blend_state(options.blend_state);
    detail::validate_viewport_state_definition(options.viewport_state);
    detail::validate_shadow_state_definition(
        options.shadow_state,
        options.directional_light.enabled);
    detail::validate_alpha_test_state(options.alpha_test_state);
    validate_fragment_program_for_mesh(options.fragment_program, asset.mesh);
    detail::validate_vertex_program_static(
        options.vertex_program,
        detail::vertex_program_varying_count(asset.mesh));
    bool sampler_needed = false;
    for (const MaterialDraw& draw : asset.draws) {
        validate_material(draw.material);
        sampler_needed = sampler_needed
            || static_cast<bool>(draw.diffuse_texture)
            || static_cast<bool>(draw.opacity_texture);
    }
    if (sampler_needed) {
        validate_sampler(options.sampler);
    }
}

TextureBinding texture_binding_for(const MaterialDraw& draw, const ModelRenderOptions& options) {
    TextureBinding binding{
        draw.diffuse_texture.get(),
        options.u_channel,
        options.v_channel,
        options.sampler,
    };
    binding.opacity_texture = draw.opacity_texture.get();
    return binding;
}

BaseColorSource base_color_source_for(const MaterialDraw& draw) {
    return draw.diffuse_texture ? BaseColorSource::Texture : BaseColorSource::ConstantWhite;
}

void preflight_prepared_model_transform(
    const Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    const Mesh& mesh,
    const Mat4& model) {
    const ModelAsset& asset = prepared.asset();
    const ModelRenderOptions& options = prepared.options();
    detail::validate_alpha_test_state(options.alpha_test_state);
    for (const MaterialDraw& draw : asset.draws) {
        detail::preflight_mesh_range_submission(
            framebuffer,
            mesh,
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
            options.alpha_to_coverage_state,
            options.shadow_state,
            &model,
            false);
    }
}

void preflight_prepared_model_mvp(
    const Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    const Mesh& mesh) {
    const ModelAsset& asset = prepared.asset();
    const ModelRenderOptions& options = prepared.options();
    detail::validate_alpha_test_state(options.alpha_test_state);
    for (const MaterialDraw& draw : asset.draws) {
        detail::preflight_mesh_range_submission(
            framebuffer,
            mesh,
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
            options.alpha_to_coverage_state,
            options.shadow_state,
            nullptr,
            true);
    }
}

Rasterizer model_rasterizer(
    Framebuffer& framebuffer,
    const MaterialDraw& draw,
    const ModelRenderOptions& options) {
    return Rasterizer(
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
        options.blend_state,
        options.alpha_to_coverage_state,
        options.shadow_state,
        options.alpha_test_state,
        options.fragment_program,
        {});
}

void execute_prepared_model_transform(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    const Mesh& mesh,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection) {
    const ModelAsset& asset = prepared.asset();
    const ModelRenderOptions& options = prepared.options();
    for (const MaterialDraw& draw : asset.draws) {
        Rasterizer rasterizer = model_rasterizer(framebuffer, draw, options);
        rasterizer.draw_mesh_range(mesh, draw.range, model, view, projection);
    }
}

void execute_prepared_model_mvp(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    const Mesh& mesh,
    const Mat4& mvp) {
    const ModelAsset& asset = prepared.asset();
    const ModelRenderOptions& options = prepared.options();
    for (const MaterialDraw& draw : asset.draws) {
        Rasterizer rasterizer = model_rasterizer(framebuffer, draw, options);
        rasterizer.draw_mesh_range(mesh, draw.range, mvp);
    }
}

template <typename PreflightDraw, typename SubmitRange>
void draw_validated_model_impl(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const Mesh& mesh,
    const ModelRenderOptions& options,
    PreflightDraw&& preflight_draw,
    SubmitRange&& submit_range) {
    if (asset.draws.empty()) {
        return;
    }

    detail::validate_alpha_test_state(options.alpha_test_state);
    for (const MaterialDraw& draw : asset.draws) {
        preflight_draw(draw);
    }

    for (const MaterialDraw& draw : asset.draws) {
        Rasterizer rasterizer = model_rasterizer(framebuffer, draw, options);
        submit_range(rasterizer, mesh, draw.range);
    }
}

}  // namespace

PreparedModelSubmission::PreparedModelSubmission(ModelAsset asset, ModelRenderOptions options)
    : asset_(std::move(asset)), options_(std::move(options)) {}

PreparedModelSubmission prepare_model_asset(ModelAsset asset, ModelRenderOptions options) {
    validate_static_model_state(asset, options);
    return PreparedModelSubmission{std::move(asset), std::move(options)};
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

    std::vector<detail::PreparedVertexMesh> meshes;
    meshes.reserve(models.size());
    for (std::size_t i = 0U; i < models.size(); ++i) {
        meshes.push_back(detail::prepare_vertex_program_mesh(
            prepared.options().vertex_program,
            prepared.asset().mesh));
    }

    for (std::size_t i = 0U; i < models.size(); ++i) {
        preflight_prepared_model_transform(
            framebuffer,
            prepared,
            meshes[i].get(),
            models[i]);
    }

    for (std::size_t i = 0U; i < models.size(); ++i) {
        execute_prepared_model_transform(
            framebuffer,
            prepared,
            meshes[i].get(),
            models[i],
            view,
            projection);
    }
}

void draw_prepared_model_instances(
    Framebuffer& framebuffer,
    const PreparedModelSubmission& prepared,
    std::span<const Mat4> mvps) {
    if (mvps.empty() || prepared.asset().draws.empty()) {
        return;
    }

    std::vector<detail::PreparedVertexMesh> meshes;
    meshes.reserve(mvps.size());
    for (std::size_t i = 0U; i < mvps.size(); ++i) {
        meshes.push_back(detail::prepare_vertex_program_mesh(
            prepared.options().vertex_program,
            prepared.asset().mesh));
    }

    for (std::size_t i = 0U; i < mvps.size(); ++i) {
        preflight_prepared_model_mvp(framebuffer, prepared, meshes[i].get());
    }

    for (std::size_t i = 0U; i < mvps.size(); ++i) {
        execute_prepared_model_mvp(framebuffer, prepared, meshes[i].get(), mvps[i]);
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

    std::vector<detail::PreparedVertexMesh> meshes;
    meshes.reserve(entries.size());
    for (const PreparedModelListEntry& entry : entries) {
        meshes.push_back(detail::prepare_vertex_program_mesh(
            entry.prepared->options().vertex_program,
            entry.prepared->asset().mesh));
    }

    for (std::size_t i = 0U; i < entries.size(); ++i) {
        preflight_prepared_model_transform(
            framebuffer,
            *entries[i].prepared,
            meshes[i].get(),
            entries[i].model);
    }

    for (std::size_t i = 0U; i < entries.size(); ++i) {
        execute_prepared_model_transform(
            framebuffer,
            *entries[i].prepared,
            meshes[i].get(),
            entries[i].model,
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
    const detail::PreparedVertexMesh programmed =
        detail::prepare_vertex_program_mesh(options.vertex_program, asset.mesh);
    const Mesh& mesh = programmed.get();

    draw_validated_model_impl(
        framebuffer,
        asset,
        mesh,
        options,
        [&](const MaterialDraw& draw) {
            detail::preflight_mesh_range_submission(
                framebuffer,
                mesh,
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
                options.alpha_to_coverage_state,
                options.shadow_state,
                &model,
                false);
        },
        [&](Rasterizer& rasterizer, const Mesh& render_mesh, DrawRange range) {
            rasterizer.draw_mesh_range(render_mesh, range, model, view, projection);
        });
}

void draw_model_asset(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const Mat4& mvp,
    ModelRenderOptions options) {
    validate_static_model_state(asset, options);
    const detail::PreparedVertexMesh programmed =
        detail::prepare_vertex_program_mesh(options.vertex_program, asset.mesh);
    const Mesh& mesh = programmed.get();

    draw_validated_model_impl(
        framebuffer,
        asset,
        mesh,
        options,
        [&](const MaterialDraw& draw) {
            detail::preflight_mesh_range_submission(
                framebuffer,
                mesh,
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
                options.alpha_to_coverage_state,
                options.shadow_state,
                nullptr,
                true);
        },
        [&](Rasterizer& rasterizer, const Mesh& render_mesh, DrawRange range) {
            rasterizer.draw_mesh_range(render_mesh, range, mvp);
        });
}

}  // namespace tiny_renderer
