#include "rasterizer_validation.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace tiny_renderer::detail {
namespace {

constexpr std::int64_t kSubpixelScale = 256;
constexpr std::int64_t kSubpixelHalf = kSubpixelScale / 2;
constexpr std::int64_t kMaxFixedCoordinate = 2'000'000'000LL;
constexpr std::size_t kMaxRasterCoordinate =
    static_cast<std::size_t>((kMaxFixedCoordinate - kSubpixelHalf) / kSubpixelScale);
constexpr float kMaxTextureCoordinateMagnitude = 1.0e20F;

bool finite_vec3(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite_mat4(const Mat4& matrix) {
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            if (!std::isfinite(matrix(row, column))) {
                return false;
            }
        }
    }
    return true;
}

void validate_draw_range(const Mesh& mesh, DrawRange range) {
    if (range.first_triangle > mesh.triangles.size()
        || range.triangle_count > mesh.triangles.size() - range.first_triangle) {
        throw std::out_of_range("mesh draw range exceeds triangle list");
    }
}

void validate_raster_target(const Framebuffer& framebuffer) {
    if (framebuffer.width() - 1U > kMaxRasterCoordinate || framebuffer.height() - 1U > kMaxRasterCoordinate) {
        throw std::overflow_error("framebuffer dimensions exceed fixed-point rasterizer safety bound");
    }
}

void validate_rect_addition(const RasterRect& rect, const char* label) {
    if (rect.width > std::numeric_limits<std::size_t>::max() - rect.x
        || rect.height > std::numeric_limits<std::size_t>::max() - rect.y) {
        throw std::overflow_error(std::string(label) + " bounds overflow size_t");
    }
}

void validate_rect_fits_target(
    const RasterRect& rect,
    std::size_t target_width,
    std::size_t target_height,
    const char* label) {
    if (rect.x > target_width || rect.y > target_height
        || rect.width > target_width - rect.x
        || rect.height > target_height - rect.y) {
        throw std::out_of_range(std::string(label) + " exceeds framebuffer bounds");
    }
}

BaseColorSource prepare_base_color_source(BaseColorSource source, const TextureBinding& texture_binding) {
    switch (source) {
        case BaseColorSource::Auto:
            return texture_binding.texture != nullptr
                ? BaseColorSource::Texture
                : BaseColorSource::VaryingColor;
        case BaseColorSource::VaryingColor:
            if (texture_binding.texture != nullptr) {
                throw std::invalid_argument("varying-color base source conflicts with a bound texture");
            }
            return source;
        case BaseColorSource::Texture:
            if (texture_binding.texture == nullptr) {
                throw std::invalid_argument("texture base-color source requires a bound texture");
            }
            return source;
        case BaseColorSource::ConstantWhite:
            if (texture_binding.texture != nullptr) {
                throw std::invalid_argument("constant-white base-color source conflicts with a bound texture");
            }
            return source;
    }
    throw std::invalid_argument("unknown base-color source");
}

void validate_material_state(const MaterialState& material) {
    if (!finite_vec3(material.albedo)
        || material.albedo.x < 0.0F || material.albedo.x > 1.0F
        || material.albedo.y < 0.0F || material.albedo.y > 1.0F
        || material.albedo.z < 0.0F || material.albedo.z > 1.0F) {
        throw std::invalid_argument("material albedo components must be finite and within [0, 1]");
    }
    if (!std::isfinite(material.opacity) || material.opacity < 0.0F || material.opacity > 1.0F) {
        throw std::invalid_argument("material opacity must be finite and within [0, 1]");
    }
}

DirectionalLight prepare_directional_light(const DirectionalLight& light) {
    if (!light.enabled) {
        return light;
    }
    if (!finite_vec3(light.direction_to_light) || length(light.direction_to_light) <= kEpsilon) {
        throw std::invalid_argument("directional light direction must be finite and non-zero");
    }
    if (!std::isfinite(light.ambient) || !std::isfinite(light.diffuse)
        || light.ambient < 0.0F || light.diffuse < 0.0F
        || light.ambient > 1.0F || light.diffuse > 1.0F
        || light.ambient + light.diffuse > 1.0F + kEpsilon) {
        throw std::invalid_argument("directional light coefficients must be finite, non-negative, and sum to at most one");
    }
    DirectionalLight prepared = light;
    prepared.direction_to_light = normalize(light.direction_to_light);
    return prepared;
}

void validate_pack(const VaryingPack& pack) {
    if (pack.count > kMaxVaryings) {
        throw std::invalid_argument("varying pack count exceeds fixed capacity");
    }
}

void validate_layout_match(const VaryingPack& reference, const VaryingPack& candidate) {
    validate_pack(candidate);
    if (candidate.count != reference.count) {
        throw std::invalid_argument("varying packs must use the same channel count");
    }
    for (std::size_t channel = 0U; channel < reference.count; ++channel) {
        if (candidate.interpolation[channel] != reference.interpolation[channel]) {
            throw std::invalid_argument("varying packs must use the same interpolation qualifiers");
        }
    }
}

bool texture_coordinates_required(
    const TextureBinding& texture_binding,
    BaseColorSource source) {
    return source == BaseColorSource::Texture || texture_binding.opacity_texture != nullptr;
}

void validate_output_binding(
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    BaseColorSource source,
    std::size_t varying_count) {
    switch (source) {
        case BaseColorSource::VaryingColor:
            if (color_binding.red >= varying_count
                || color_binding.green >= varying_count
                || color_binding.blue >= varying_count) {
                throw std::out_of_range("color binding references unavailable varying channel");
            }
            break;
        case BaseColorSource::Texture:
            break;
        case BaseColorSource::ConstantWhite:
            break;
        case BaseColorSource::Auto:
            throw std::logic_error("automatic base-color source must be resolved before validation");
    }

    if (texture_coordinates_required(texture_binding, source)
        && (texture_binding.u_channel >= varying_count || texture_binding.v_channel >= varying_count)) {
        throw std::out_of_range("texture binding references unavailable varying channel");
    }
}

void validate_normal_binding(const NormalBinding& binding, const VaryingPack& pack) {
    if (binding.x >= pack.count || binding.y >= pack.count || binding.z >= pack.count) {
        throw std::out_of_range("normal binding references unavailable varying channel");
    }
    const Interpolation mode = pack.interpolation[binding.x];
    if (pack.interpolation[binding.y] != mode || pack.interpolation[binding.z] != mode) {
        throw std::invalid_argument("normal binding channels must use one interpolation qualifier");
    }
}

void validate_vertex_state(
    const Vertex& vertex,
    const VaryingPack& reference,
    const TextureBinding& texture_binding,
    BaseColorSource source,
    const DirectionalLight& light) {
    validate_layout_match(reference, vertex.varyings);
    if (texture_coordinates_required(texture_binding, source)) {
        const float u = vertex.varyings.values[texture_binding.u_channel];
        const float v = vertex.varyings.values[texture_binding.v_channel];
        if (!std::isfinite(u) || !std::isfinite(v)
            || std::fabs(u) > kMaxTextureCoordinateMagnitude
            || std::fabs(v) > kMaxTextureCoordinateMagnitude) {
            throw std::invalid_argument("texture coordinates exceed safe finite range");
        }
    }
    if (light.enabled) {
        const Vec3 normal{
            vertex.varyings.values[light.normal.x],
            vertex.varyings.values[light.normal.y],
            vertex.varyings.values[light.normal.z],
        };
        if (!finite_vec3(normal) || length(normal) <= kEpsilon) {
            throw std::invalid_argument("vertex normal must be finite and non-zero");
        }
    }
}

void validate_mesh_range(
    const Mesh& mesh,
    DrawRange range,
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    BaseColorSource source,
    const DirectionalLight& light) {
    const std::size_t end = range.first_triangle + range.triangle_count;
    for (std::size_t triangle_index = range.first_triangle; triangle_index < end; ++triangle_index) {
        for (const std::uint32_t index : mesh.triangles[triangle_index]) {
            if (static_cast<std::size_t>(index) >= mesh.vertices.size()) {
                throw std::out_of_range("mesh triangle index out of range");
            }
        }
    }

    if (mesh.vertices.empty()) {
        return;
    }

    validate_pack(mesh.vertices.front().varyings);
    validate_output_binding(color_binding, texture_binding, source, mesh.vertices.front().varyings.count);
    if (light.enabled) {
        validate_normal_binding(light.normal, mesh.vertices.front().varyings);
    }
    for (const Vertex& vertex : mesh.vertices) {
        validate_vertex_state(vertex, mesh.vertices.front().varyings, texture_binding, source, light);
    }
}

void preflight_transformed_normals(const Mesh& mesh, const DirectionalLight& light, const Mat3& matrix) {
    if (!light.enabled) {
        return;
    }
    for (const Vertex& vertex : mesh.vertices) {
        const Vec3 transformed = matrix * Vec3{
            vertex.varyings.values[light.normal.x],
            vertex.varyings.values[light.normal.y],
            vertex.varyings.values[light.normal.z],
        };
        if (!finite_vec3(transformed) || length(transformed) <= kEpsilon) {
            throw std::invalid_argument("transformed vertex normal is numerically unstable");
        }
    }
}

}  // namespace

void validate_face_culling(CullMode cull_mode, FrontFace front_face) {
    switch (cull_mode) {
        case CullMode::None:
        case CullMode::Back:
        case CullMode::Front:
            break;
        default:
            throw std::invalid_argument("unknown face culling mode");
    }

    switch (front_face) {
        case FrontFace::CounterClockwise:
        case FrontFace::Clockwise:
            return;
        default:
            throw std::invalid_argument("unknown front-face winding");
    }
}

void validate_viewport_state_definition(const ViewportState& state) {
    if (state.viewport) {
        if (state.viewport->width == 0U || state.viewport->height == 0U) {
            throw std::invalid_argument("viewport extent must be non-zero");
        }
        validate_rect_addition(*state.viewport, "viewport");
    }
    if (state.scissor) {
        validate_rect_addition(*state.scissor, "scissor");
    }
}

void validate_alpha_to_coverage_target(
    const Framebuffer& framebuffer,
    const AlphaToCoverageState& state) {
    if (state.enabled && framebuffer.sample_count() != SampleCount::Four) {
        throw std::invalid_argument("alpha-to-coverage requires a 4x multisample framebuffer");
    }
}

void validate_shadow_state_definition(
    const ShadowState& state,
    bool directional_light_enabled) {
    if (!state.enabled) {
        return;
    }
    if (!directional_light_enabled) {
        throw std::invalid_argument("directional shadow mapping requires an enabled directional light");
    }
    if (!state.map) {
        throw std::invalid_argument("directional shadow mapping requires a depth texture");
    }
    if (!std::isfinite(state.bias) || state.bias < 0.0F) {
        throw std::invalid_argument("directional shadow bias must be finite and non-negative");
    }
    if (!finite_mat4(state.light_view_projection)) {
        throw std::invalid_argument("directional shadow light view-projection transform must be finite");
    }
}

ResolvedViewportState resolve_viewport_state(
    const Framebuffer& framebuffer,
    const ViewportState& state) {
    validate_viewport_state_definition(state);

    ResolvedViewportState resolved{
        state.viewport.value_or(RasterRect{0U, 0U, framebuffer.width(), framebuffer.height()}),
        state.scissor,
    };
    validate_rect_fits_target(
        resolved.viewport,
        framebuffer.width(),
        framebuffer.height(),
        "viewport");
    if (resolved.scissor) {
        validate_rect_fits_target(
            *resolved.scissor,
            framebuffer.width(),
            framebuffer.height(),
            "scissor");
    }
    return resolved;
}

void preflight_mesh_range_submission(
    const Framebuffer& framebuffer,
    const Mesh& mesh,
    DrawRange range,
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    const DirectionalLight& directional_light,
    const MaterialState& material_state,
    BaseColorSource base_color_source,
    CullMode cull_mode,
    FrontFace front_face,
    const DepthState& depth_state,
    const ViewportState& viewport_state,
    const StencilState& stencil_state,
    const BlendState& blend_state,
    const AlphaToCoverageState& alpha_to_coverage_state,
    const ShadowState& shadow_state,
    const Mat4* model,
    bool mvp_only) {
    validate_draw_range(mesh, range);
    if (mvp_only && (directional_light.enabled || shadow_state.enabled)) {
        throw std::invalid_argument("directional lighting and shadows require separate model/view/projection transforms");
    }

    validate_face_culling(cull_mode, front_face);
    validate_depth_state(depth_state);
    validate_stencil_state(stencil_state);
    validate_blend_state(blend_state);
    validate_raster_target(framebuffer);
    validate_alpha_to_coverage_target(framebuffer, alpha_to_coverage_state);
    validate_shadow_state_definition(shadow_state, directional_light.enabled);
    (void)resolve_viewport_state(framebuffer, viewport_state);
    const BaseColorSource source = prepare_base_color_source(base_color_source, texture_binding);
    validate_material_state(material_state);
    const DirectionalLight light = prepare_directional_light(directional_light);
    validate_mesh_range(mesh, range, color_binding, texture_binding, source, light);

    if (light.enabled && !mesh.vertices.empty()) {
        if (model == nullptr) {
            throw std::logic_error("model transform required for lit range preflight");
        }
        preflight_transformed_normals(mesh, light, normal_matrix(*model));
    }
}

}  // namespace tiny_renderer::detail
