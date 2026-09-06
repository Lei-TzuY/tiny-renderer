#include "rasterizer_validation.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "normal_mapping_internal.hpp"

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

bool material_has_specular(const MaterialState& material) {
    return material.specular.x > 0.0F
        || material.specular.y > 0.0F
        || material.specular.z > 0.0F;
}

bool fixed_lighting_enabled(
    const DirectionalLight& directional_light,
    const PointLight& point_light) {
    return directional_light.enabled || point_light.enabled;
}

const NormalBinding* active_normal_binding(
    const DirectionalLight& directional_light,
    const PointLight& point_light) {
    if (directional_light.enabled) {
        return &directional_light.normal;
    }
    if (point_light.enabled) {
        return &point_light.normal;
    }
    return nullptr;
}

bool world_position_required(
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const MaterialState& material) {
    return point_light.enabled
        || (directional_light.enabled && material_has_specular(material));
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
    if (!finite_vec3(material.specular)
        || material.specular.x < 0.0F || material.specular.x > 1.0F
        || material.specular.y < 0.0F || material.specular.y > 1.0F
        || material.specular.z < 0.0F || material.specular.z > 1.0F) {
        throw std::invalid_argument("material specular components must be finite and within [0, 1]");
    }
    if (!std::isfinite(material.shininess)
        || material.shininess < 1.0F || material.shininess > 1000.0F) {
        throw std::invalid_argument("material shininess must be finite and within [1, 1000]");
    }
}

void validate_light_coefficients(float ambient, float diffuse, const char* label) {
    if (!std::isfinite(ambient) || !std::isfinite(diffuse)
        || ambient < 0.0F || diffuse < 0.0F
        || ambient > 1.0F || diffuse > 1.0F
        || ambient + diffuse > 1.0F + kEpsilon) {
        throw std::invalid_argument(
            std::string(label)
            + " coefficients must be finite, non-negative, and sum to at most one");
    }
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
    return source == BaseColorSource::Texture
        || texture_binding.opacity_texture != nullptr
        || texture_binding.normal_texture != nullptr;
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
    const NormalBinding* normal_binding) {
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
    if (normal_binding != nullptr) {
        const Vec3 normal{
            vertex.varyings.values[normal_binding->x],
            vertex.varyings.values[normal_binding->y],
            vertex.varyings.values[normal_binding->z],
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
    const NormalBinding* normal_binding) {
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
    if (normal_binding != nullptr) {
        validate_normal_binding(*normal_binding, mesh.vertices.front().varyings);
    }
    for (const Vertex& vertex : mesh.vertices) {
        validate_vertex_state(
            vertex,
            mesh.vertices.front().varyings,
            texture_binding,
            source,
            normal_binding);
    }
}

void preflight_transformed_normals(
    const Mesh& mesh,
    const NormalBinding* normal_binding,
    const Mat3& matrix) {
    if (normal_binding == nullptr) {
        return;
    }
    for (const Vertex& vertex : mesh.vertices) {
        const Vec3 transformed = matrix * Vec3{
            vertex.varyings.values[normal_binding->x],
            vertex.varyings.values[normal_binding->y],
            vertex.varyings.values[normal_binding->z],
        };
        if (!finite_vec3(transformed) || length(transformed) <= kEpsilon) {
            throw std::invalid_argument("transformed vertex normal is numerically unstable");
        }
    }
}

void preflight_lighting_world_positions(
    const Mesh& mesh,
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const MaterialState& material,
    const Mat4& model) {
    if (!world_position_required(directional_light, point_light, material)) {
        return;
    }
    for (const Vertex& vertex : mesh.vertices) {
        const Vec4 world = model * Vec4{
            vertex.position.x, vertex.position.y, vertex.position.z, 1.0F};
        if (!std::isfinite(world.x) || !std::isfinite(world.y)
            || !std::isfinite(world.z) || !std::isfinite(world.w)
            || std::fabs(world.w) <= kEpsilon) {
            throw std::invalid_argument(
                "lighting world-space position transform is numerically unstable");
        }
        const float inv_w = 1.0F / world.w;
        const Vec3 position{world.x * inv_w, world.y * inv_w, world.z * inv_w};
        if (!finite_vec3(position)) {
            throw std::invalid_argument("lighting world-space position must remain finite");
        }
    }
}

Triangle assemble_triangle(const Mesh& mesh, const TriangleIndices& indices) {
    return Triangle{mesh.vertices[indices[0]], mesh.vertices[indices[1]], mesh.vertices[indices[2]]};
}

void preflight_tangent_frames(
    const Mesh& mesh,
    DrawRange range,
    const TextureBinding& texture_binding,
    const Mat4& model) {
    if (texture_binding.normal_texture == nullptr) {
        return;
    }
    validate_normal_texture(*texture_binding.normal_texture);
    const std::size_t end = range.first_triangle + range.triangle_count;
    for (std::size_t triangle_index = range.first_triangle; triangle_index < end; ++triangle_index) {
        (void)prepare_tangent_frame(
            assemble_triangle(mesh, mesh.triangles[triangle_index]),
            texture_binding,
            model);
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

void validate_fixed_lighting_definition(
    const DirectionalLight& directional_light,
    const PointLight& point_light) {
    if (directional_light.enabled && point_light.enabled) {
        throw std::invalid_argument("directional and point lights are mutually exclusive");
    }

    if (directional_light.enabled) {
        if (!finite_vec3(directional_light.direction_to_light)
            || length(directional_light.direction_to_light) <= kEpsilon) {
            throw std::invalid_argument("directional light direction must be finite and non-zero");
        }
        if (!finite_vec3(directional_light.viewer_position)) {
            throw std::invalid_argument("directional light viewer position must be finite");
        }
        validate_light_coefficients(
            directional_light.ambient,
            directional_light.diffuse,
            "directional light");
    }

    if (point_light.enabled) {
        if (!finite_vec3(point_light.position)) {
            throw std::invalid_argument("point light position must be finite");
        }
        if (!finite_vec3(point_light.viewer_position)) {
            throw std::invalid_argument("point light viewer position must be finite");
        }
        validate_light_coefficients(point_light.ambient, point_light.diffuse, "point light");
        if (!std::isfinite(point_light.linear_attenuation)
            || !std::isfinite(point_light.quadratic_attenuation)
            || point_light.linear_attenuation < 0.0F
            || point_light.quadratic_attenuation < 0.0F) {
            throw std::invalid_argument(
                "point light attenuation coefficients must be finite and non-negative");
        }
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
    const PointLight& point_light,
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
    validate_fixed_lighting_definition(directional_light, point_light);
    const bool lighting_enabled = fixed_lighting_enabled(directional_light, point_light);
    if (mvp_only && (lighting_enabled || shadow_state.enabled)) {
        throw std::invalid_argument("fixed lighting and shadows require separate model/view/projection transforms");
    }
    if (texture_binding.normal_texture != nullptr && !lighting_enabled) {
        throw std::invalid_argument("normal mapping requires an enabled fixed light");
    }
    if (texture_binding.normal_texture != nullptr && (mvp_only || model == nullptr)) {
        throw std::invalid_argument("normal mapping requires separate model/view/projection transforms");
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
    const NormalBinding* normal_binding = active_normal_binding(directional_light, point_light);
    validate_mesh_range(mesh, range, color_binding, texture_binding, source, normal_binding);

    if (normal_binding != nullptr && !mesh.vertices.empty()) {
        if (model == nullptr) {
            throw std::logic_error("model transform required for lit range preflight");
        }
        preflight_transformed_normals(mesh, normal_binding, normal_matrix(*model));
    }
    if (model != nullptr) {
        preflight_lighting_world_positions(
            mesh,
            directional_light,
            point_light,
            material_state,
            *model);
    }
    if (texture_binding.normal_texture != nullptr && model != nullptr) {
        preflight_tangent_frames(mesh, range, texture_binding, *model);
    }
}

}  // namespace tiny_renderer::detail
