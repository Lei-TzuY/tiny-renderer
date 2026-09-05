#include "tiny_renderer/rasterizer.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace tiny_renderer {
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

MaterialState prepare_material_state(const MaterialState& material) {
    if (!finite_vec3(material.albedo)
        || material.albedo.x < 0.0F || material.albedo.x > 1.0F
        || material.albedo.y < 0.0F || material.albedo.y > 1.0F
        || material.albedo.z < 0.0F || material.albedo.z > 1.0F) {
        throw std::invalid_argument("material albedo components must be finite and within [0, 1]");
    }
    return material;
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
            return;
        case BaseColorSource::Texture:
            if (texture_binding.u_channel >= varying_count || texture_binding.v_channel >= varying_count) {
                throw std::out_of_range("texture binding references unavailable varying channel");
            }
            return;
        case BaseColorSource::ConstantWhite:
            return;
        case BaseColorSource::Auto:
            throw std::logic_error("automatic base-color source must be resolved before validation");
    }
    throw std::logic_error("unreachable base-color source validation state");
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
    if (source == BaseColorSource::Texture) {
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

Triangle assemble_triangle(const Mesh& mesh, const TriangleIndices& indices) {
    return Triangle{mesh.vertices[indices[0]], mesh.vertices[indices[1]], mesh.vertices[indices[2]]};
}

}  // namespace

void Rasterizer::draw_mesh_range(
    const Mesh& mesh,
    DrawRange range,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection) {
    validate_draw_range(mesh, range);
    validate_raster_target(framebuffer_);
    const BaseColorSource source = prepare_base_color_source(base_color_source_, texture_binding_);
    (void)prepare_material_state(material_state_);
    const DirectionalLight light = prepare_directional_light(directional_light_);
    validate_mesh_range(mesh, range, color_binding_, texture_binding_, source, light);

    if (light.enabled && !mesh.vertices.empty()) {
        const Mat3 matrix = normal_matrix(model);
        preflight_transformed_normals(mesh, light, matrix);
    }

    const std::size_t end = range.first_triangle + range.triangle_count;
    for (std::size_t triangle_index = range.first_triangle; triangle_index < end; ++triangle_index) {
        draw_triangle(
            assemble_triangle(mesh, mesh.triangles[triangle_index]),
            model,
            view,
            projection);
    }
}

void Rasterizer::draw_mesh_range(const Mesh& mesh, DrawRange range, const Mat4& mvp) {
    validate_draw_range(mesh, range);
    if (directional_light_.enabled) {
        throw std::invalid_argument("directional lighting requires separate model/view/projection transforms");
    }
    validate_raster_target(framebuffer_);
    const BaseColorSource source = prepare_base_color_source(base_color_source_, texture_binding_);
    (void)prepare_material_state(material_state_);
    const DirectionalLight light = prepare_directional_light(directional_light_);
    validate_mesh_range(mesh, range, color_binding_, texture_binding_, source, light);

    const std::size_t end = range.first_triangle + range.triangle_count;
    for (std::size_t triangle_index = range.first_triangle; triangle_index < end; ++triangle_index) {
        draw_triangle(assemble_triangle(mesh, mesh.triangles[triangle_index]), mvp);
    }
}

}  // namespace tiny_renderer
