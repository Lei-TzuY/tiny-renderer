#include "rasterizer_validation.hpp"

#include <algorithm>
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

bool same_normal_binding(const NormalBinding& a, const NormalBinding& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
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

void validate_directional_light(const DirectionalLight& light) {
    if (!finite_vec3(light.direction_to_light)
        || length(light.direction_to_light) <= kEpsilon) {
        throw std::invalid_argument("directional light direction must be finite and non-zero");
    }
    if (!finite_vec3(light.viewer_position)) {
        throw std::invalid_argument("directional light viewer position must be finite");
    }
    validate_light_coefficients(light.ambient, light.diffuse, "directional light");
}

void validate_point_light(const PointLight& light) {
    if (!finite_vec3(light.position)) {
        throw std::invalid_argument("point light position must be finite");
    }
    if (!finite_vec3(light.viewer_position)) {
        throw std::invalid_argument("point light viewer position must be finite");
    }
    validate_light_coefficients(light.ambient, light.diffuse, "point light");
    if (!std::isfinite(light.linear_attenuation)
        || !std::isfinite(light.quadratic_attenuation)
        || light.linear_attenuation < 0.0F
        || light.quadratic_attenuation < 0.0F) {
        throw std::invalid_argument(
            "point light attenuation coefficients must be finite and non-negative");
    }
}

void validate_spot_light(const SpotLight& light) {
    if (!finite_vec3(light.position)) {
        throw std::invalid_argument("spot light position must be finite");
    }
    if (!finite_vec3(light.direction) || length(light.direction) <= kEpsilon) {
        throw std::invalid_argument("spot light direction must be finite and non-zero");
    }
    if (!finite_vec3(light.viewer_position)) {
        throw std::invalid_argument("spot light viewer position must be finite");
    }
    validate_light_coefficients(light.ambient, light.diffuse, "spot light");
    if (!std::isfinite(light.linear_attenuation)
        || !std::isfinite(light.quadratic_attenuation)
        || light.linear_attenuation < 0.0F
        || light.quadratic_attenuation < 0.0F) {
        throw std::invalid_argument(
            "spot light attenuation coefficients must be finite and non-negative");
    }
    if (!std::isfinite(light.inner_cone_cos)
        || !std::isfinite(light.outer_cone_cos)
        || light.inner_cone_cos < -1.0F || light.inner_cone_cos > 1.0F
        || light.outer_cone_cos < -1.0F || light.outer_cone_cos > 1.0F
        || light.inner_cone_cos <= light.outer_cone_cos) {
        throw std::invalid_argument(
            "spot light cone cosines must be finite within [-1, 1] with inner greater than outer");
    }
}

const NormalBinding& selected_normal_binding(const FixedLight& light) {
    switch (light.type) {
        case FixedLightType::Directional:
            return light.directional.normal;
        case FixedLightType::Point:
            return light.point.normal;
        case FixedLightType::Spot:
            return light.spot.normal;
    }
    throw std::logic_error("validated fixed-light collection contains an unknown light type");
}

bool collection_has_directional(const FixedLightCollection& fixed_lights) {
    const std::size_t count = std::min(fixed_lights.count, kMaxFixedLights);
    for (std::size_t i = 0U; i < count; ++i) {
        if (fixed_lights.lights[i].type == FixedLightType::Directional) {
            return true;
        }
    }
    return false;
}

bool collection_has_point(const FixedLightCollection& fixed_lights) {
    const std::size_t count = std::min(fixed_lights.count, kMaxFixedLights);
    for (std::size_t i = 0U; i < count; ++i) {
        if (fixed_lights.lights[i].type == FixedLightType::Point) {
            return true;
        }
    }
    return false;
}

bool collection_has_spot(const FixedLightCollection& fixed_lights) {
    const std::size_t count = std::min(fixed_lights.count, kMaxFixedLights);
    for (std::size_t i = 0U; i < count; ++i) {
        if (fixed_lights.lights[i].type == FixedLightType::Spot) {
            return true;
        }
    }
    return false;
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
    const FixedLightCollection& fixed_lights,
    const MaterialState& material,
    const Mat4& model) {
    if (!fixed_lighting_world_position_required(
            directional_light, point_light, fixed_lights, material)) {
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

bool fixed_lighting_enabled(
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const FixedLightCollection& fixed_lights) {
    return directional_light.enabled || point_light.enabled || fixed_lights.count != 0U;
}

const NormalBinding* active_normal_binding(
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const FixedLightCollection& fixed_lights) {
    if (fixed_lights.count != 0U) {
        if (fixed_lights.count > kMaxFixedLights) {
            return nullptr;
        }
        return &selected_normal_binding(fixed_lights.lights[0]);
    }
    if (directional_light.enabled) {
        return &directional_light.normal;
    }
    if (point_light.enabled) {
        return &point_light.normal;
    }
    return nullptr;
}

bool fixed_lighting_world_position_required(
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const FixedLightCollection& fixed_lights,
    const MaterialState& material) {
    if (fixed_lights.count != 0U) {
        return collection_has_point(fixed_lights)
            || collection_has_spot(fixed_lights)
            || (material_has_specular(material) && collection_has_directional(fixed_lights));
    }
    return point_light.enabled
        || (directional_light.enabled && material_has_specular(material));
}

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

void validate_fixed_lighting_definition(
    const DirectionalLight& directional_light,
    const PointLight& point_light,
    const FixedLightCollection& fixed_lights) {
    if (fixed_lights.count > kMaxFixedLights) {
        throw std::invalid_argument("fixed-light collection exceeds its bounded capacity");
    }
    if (fixed_lights.count == 0U) {
        if (directional_light.enabled && point_light.enabled) {
            throw std::invalid_argument("legacy directional and point lights are mutually exclusive");
        }
        if (directional_light.enabled) {
            validate_directional_light(directional_light);
        }
        if (point_light.enabled) {
            validate_point_light(point_light);
        }
        return;
    }

    if (directional_light.enabled || point_light.enabled) {
        throw std::invalid_argument(
            "fixed-light collection is mutually exclusive with legacy single-light state");
    }

    const NormalBinding* shared_normal = nullptr;
    for (std::size_t i = 0U; i < fixed_lights.count; ++i) {
        const FixedLight& light = fixed_lights.lights[i];
        const NormalBinding* normal = nullptr;
        switch (light.type) {
            case FixedLightType::Directional:
                if (!light.directional.enabled || light.point.enabled || light.spot.enabled) {
                    throw std::invalid_argument(
                        "directional collection record requires only its directional payload enabled");
                }
                validate_directional_light(light.directional);
                normal = &light.directional.normal;
                break;
            case FixedLightType::Point:
                if (!light.point.enabled || light.directional.enabled || light.spot.enabled) {
                    throw std::invalid_argument(
                        "point collection record requires only its point payload enabled");
                }
                validate_point_light(light.point);
                normal = &light.point.normal;
                break;
            case FixedLightType::Spot:
                if (!light.spot.enabled || light.directional.enabled || light.point.enabled) {
                    throw std::invalid_argument(
                        "spot collection record requires only its spot payload enabled");
                }
                validate_spot_light(light.spot);
                normal = &light.spot.normal;
                break;
            default:
                throw std::invalid_argument("fixed-light collection contains an unknown light type");
        }
        if (shared_normal == nullptr) {
            shared_normal = normal;
        } else if (!same_normal_binding(*shared_normal, *normal)) {
            throw std::invalid_argument("fixed-light collection must share one normal binding");
        }
    }
}

void validate_shadow_state_definition(
    const ShadowState& state,
    const DirectionalLight& directional_light,
    const FixedLightCollection& fixed_lights) {
    if (fixed_lights.count == 0U) {
        if (fixed_lights.shadowed_directional_index) {
            throw std::invalid_argument("legacy fixed lighting cannot name a collection shadow index");
        }
        if (!state.enabled) {
            return;
        }
        if (!directional_light.enabled) {
            throw std::invalid_argument("directional shadow mapping requires an enabled directional light");
        }
    } else {
        if (!state.enabled) {
            if (fixed_lights.shadowed_directional_index) {
                throw std::invalid_argument("shadow association requires enabled shadow state");
            }
            return;
        }
        if (!fixed_lights.shadowed_directional_index) {
            throw std::invalid_argument("multi-light shadow mapping requires a directional light association");
        }
        const std::size_t index = *fixed_lights.shadowed_directional_index;
        if (index >= fixed_lights.count) {
            throw std::out_of_range("multi-light shadow association exceeds the fixed-light collection");
        }
        if (fixed_lights.lights[index].type != FixedLightType::Directional) {
            throw std::invalid_argument("multi-light shadow association must target a directional light");
        }
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

void validate_point_shadow_state_definition(
    const PointShadowState& state,
    const FixedLightCollection& fixed_lights) {
    if (fixed_lights.count == 0U) {
        if (fixed_lights.shadowed_point_index) {
            throw std::invalid_argument("legacy fixed lighting cannot name a point-shadow collection index");
        }
        if (state.enabled) {
            throw std::invalid_argument("point shadow mapping requires a fixed-light collection association");
        }
        return;
    }

    if (!state.enabled) {
        if (fixed_lights.shadowed_point_index) {
            throw std::invalid_argument("point-shadow association requires enabled point shadow state");
        }
        return;
    }
    if (!fixed_lights.shadowed_point_index) {
        throw std::invalid_argument("point shadow mapping requires a point-light association");
    }
    const std::size_t index = *fixed_lights.shadowed_point_index;
    if (index >= fixed_lights.count) {
        throw std::out_of_range("point-shadow association exceeds the fixed-light collection");
    }
    if (fixed_lights.lights[index].type != FixedLightType::Point) {
        throw std::invalid_argument("point-shadow association must target a point light");
    }
    if (!state.map) {
        throw std::invalid_argument("point shadow mapping requires a depth cubemap");
    }
    const Vec3& capture_position = state.map->light_position();
    const Vec3& light_position = fixed_lights.lights[index].point.position;
    if (capture_position.x != light_position.x
        || capture_position.y != light_position.y
        || capture_position.z != light_position.z) {
        throw std::invalid_argument(
            "point shadow cubemap capture position must match the associated point light");
    }
    if (!std::isfinite(state.bias) || state.bias < 0.0F) {
        throw std::invalid_argument("point shadow bias must be finite and non-negative");
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
    const FixedLightCollection& fixed_lights,
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
    const PointShadowState& point_shadow_state,
    const Mat4* model,
    bool mvp_only) {
    validate_draw_range(mesh, range);
    validate_fixed_lighting_definition(directional_light, point_light, fixed_lights);
    const bool lighting_enabled = fixed_lighting_enabled(
        directional_light, point_light, fixed_lights);
    if (mvp_only && (lighting_enabled || shadow_state.enabled || point_shadow_state.enabled)) {
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
    validate_shadow_state_definition(shadow_state, directional_light, fixed_lights);
    validate_point_shadow_state_definition(point_shadow_state, fixed_lights);
    (void)resolve_viewport_state(framebuffer, viewport_state);
    const BaseColorSource source = prepare_base_color_source(base_color_source, texture_binding);
    validate_material_state(material_state);
    const NormalBinding* normal_binding = active_normal_binding(
        directional_light, point_light, fixed_lights);
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
            fixed_lights,
            material_state,
            *model);
    }
    if (texture_binding.normal_texture != nullptr && model != nullptr) {
        preflight_tangent_frames(mesh, range, texture_binding, *model);
    }
}

}  // namespace tiny_renderer::detail