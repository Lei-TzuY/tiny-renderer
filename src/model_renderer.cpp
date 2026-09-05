#include "tiny_renderer/model_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

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

TextureBinding texture_binding_for(const MaterialDraw& draw, const ModelRenderOptions& options) {
    if (!draw.diffuse_texture) {
        return {};
    }
    validate_sampler(options.sampler);
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

template <typename SubmitRange>
void draw_model_asset_impl(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const ModelRenderOptions& options,
    SubmitRange&& submit_range) {
    validate_model_structure(asset);
    if (asset.draws.empty()) {
        return;
    }

    // First pass: reuse the range submission's empty-range contract to validate
    // every draw state without touching color or depth. This prevents a bad later
    // material/texture/light state from partially committing earlier draws.
    for (const MaterialDraw& draw : asset.draws) {
        Rasterizer rasterizer(
            framebuffer,
            {},
            texture_binding_for(draw, options),
            options.directional_light,
            draw.material,
            base_color_source_for(draw));
        submit_range(rasterizer, DrawRange{0U, 0U});
    }

    // Second pass: all model structure and per-draw renderer state is known valid.
    for (const MaterialDraw& draw : asset.draws) {
        Rasterizer rasterizer(
            framebuffer,
            {},
            texture_binding_for(draw, options),
            options.directional_light,
            draw.material,
            base_color_source_for(draw));
        submit_range(rasterizer, draw.range);
    }
}

}  // namespace

void draw_model_asset(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection,
    ModelRenderOptions options) {
    draw_model_asset_impl(
        framebuffer,
        asset,
        options,
        [&](Rasterizer& rasterizer, DrawRange range) {
            rasterizer.draw_mesh_range(asset.mesh, range, model, view, projection);
        });
}

void draw_model_asset(
    Framebuffer& framebuffer,
    const ModelAsset& asset,
    const Mat4& mvp,
    ModelRenderOptions options) {
    draw_model_asset_impl(
        framebuffer,
        asset,
        options,
        [&](Rasterizer& rasterizer, DrawRange range) {
            rasterizer.draw_mesh_range(asset.mesh, range, mvp);
        });
}

}  // namespace tiny_renderer
