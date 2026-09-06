#include "tiny_renderer/shadow_renderer.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "tiny_renderer/point_shadow_renderer.hpp"
#include "rasterizer_validation.hpp"
#include "vertex_program_internal.hpp"

namespace tiny_renderer {
namespace {

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

bool finite_vec3(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void validate_culling(CullMode cull_mode, FrontFace front_face) {
    switch (cull_mode) {
        case CullMode::None:
        case CullMode::Back:
        case CullMode::Front:
            break;
        default:
            throw std::invalid_argument("shadow pass uses an unknown cull mode");
    }
    switch (front_face) {
        case FrontFace::CounterClockwise:
        case FrontFace::Clockwise:
            return;
        default:
            throw std::invalid_argument("shadow pass uses an unknown front-face winding");
    }
}

TextureBinding shadow_texture_binding(
    const MaterialDraw& draw,
    const ModelRenderOptions& options) {
    TextureBinding binding{};
    binding.u_channel = options.u_channel;
    binding.v_channel = options.v_channel;
    binding.sampler = options.sampler;
    binding.opacity_texture = draw.opacity_texture.get();
    return binding;
}

BlendState depth_only_blend_state() {
    BlendState state{};
    state.write_mask = {false, false, false};
    return state;
}

void validate_shadow_entries(
    std::span<const PreparedModelListEntry> entries) {
    for (const PreparedModelListEntry& entry : entries) {
        if (entry.prepared == nullptr) {
            throw std::invalid_argument("shadow pass entry requires a prepared model");
        }
    }
}

std::vector<detail::PreparedVertexMesh> prepare_shadow_meshes(
    std::span<const PreparedModelListEntry> entries) {
    std::vector<detail::PreparedVertexMesh> meshes;
    meshes.reserve(entries.size());
    for (const PreparedModelListEntry& entry : entries) {
        meshes.push_back(detail::prepare_vertex_program_mesh(
            entry.prepared->options().vertex_program,
            entry.prepared->asset().mesh));
    }
    return meshes;
}

void preflight_shadow_entries(
    const Framebuffer& framebuffer,
    std::span<const PreparedModelListEntry> entries,
    const std::vector<detail::PreparedVertexMesh>& meshes,
    CullMode cull_mode,
    FrontFace front_face) {
    const BlendState depth_only_blend = depth_only_blend_state();
    for (std::size_t entry_index = 0U; entry_index < entries.size(); ++entry_index) {
        const PreparedModelListEntry& entry = entries[entry_index];
        const ModelAsset& asset = entry.prepared->asset();
        const ModelRenderOptions& options = entry.prepared->options();
        const Mesh& mesh = meshes[entry_index].get();
        detail::validate_alpha_test_state(options.alpha_test_state);
        for (const MaterialDraw& draw : asset.draws) {
            detail::preflight_mesh_range_submission(
                framebuffer,
                mesh,
                draw.range,
                {},
                shadow_texture_binding(draw, options),
                {},
                {},
                {},
                draw.material,
                BaseColorSource::ConstantWhite,
                cull_mode,
                front_face,
                DepthState{DepthCompare::Less, true},
                {},
                {},
                depth_only_blend,
                {},
                {},
                {},
                nullptr,
                true);
        }
    }
}

void draw_shadow_entries(
    Framebuffer& framebuffer,
    std::span<const PreparedModelListEntry> entries,
    const std::vector<detail::PreparedVertexMesh>& meshes,
    const Mat4& light_view_projection,
    CullMode cull_mode,
    FrontFace front_face) {
    const BlendState depth_only_blend = depth_only_blend_state();
    for (std::size_t entry_index = 0U; entry_index < entries.size(); ++entry_index) {
        const PreparedModelListEntry& entry = entries[entry_index];
        const ModelAsset& asset = entry.prepared->asset();
        const ModelRenderOptions& options = entry.prepared->options();
        const Mesh& mesh = meshes[entry_index].get();
        const Mat4 light_mvp = light_view_projection * entry.model;
        for (const MaterialDraw& draw : asset.draws) {
            Rasterizer rasterizer(
                framebuffer,
                {},
                shadow_texture_binding(draw, options),
                {},
                draw.material,
                BaseColorSource::ConstantWhite,
                cull_mode,
                front_face,
                DepthState{DepthCompare::Less, true},
                {},
                {},
                depth_only_blend,
                {},
                {},
                options.alpha_test_state);
            rasterizer.draw_mesh_range(mesh, draw.range, light_mvp);
        }
    }
}

std::size_t checked_cubemap_storage_size(std::size_t size) {
    if (size == 0U) {
        throw std::invalid_argument("point shadow cubemap face size must be non-zero");
    }
    if (size > std::numeric_limits<std::size_t>::max() / size) {
        throw std::overflow_error("point shadow cubemap face storage size overflows size_t");
    }
    const std::size_t face_pixels = size * size;
    if (face_pixels > std::numeric_limits<std::size_t>::max() / kCubemapFaceCount) {
        throw std::overflow_error("point shadow cubemap storage size overflows size_t");
    }
    return face_pixels * kCubemapFaceCount;
}

std::array<Mat4, kCubemapFaceCount> point_shadow_face_view_projections(
    const Vec3& light_position,
    float near_plane,
    float far_plane) {
    if (!finite_vec3(light_position)) {
        throw std::invalid_argument("point shadow light position must be finite");
    }
    if (!std::isfinite(near_plane) || !std::isfinite(far_plane)
        || near_plane <= 0.0F || far_plane <= near_plane) {
        throw std::invalid_argument(
            "point shadow near/far planes must be finite with 0 < near < far");
    }

    const Mat4 projection = Mat4::perspective(
        radians(90.0F),
        1.0F,
        near_plane,
        far_plane);
    const std::array<Vec3, kCubemapFaceCount> directions{{
        {1.0F, 0.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, -1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, -1.0F},
    }};
    const std::array<Vec3, kCubemapFaceCount> ups{{
        {0.0F, -1.0F, 0.0F},
        {0.0F, -1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, -1.0F},
        {0.0F, -1.0F, 0.0F},
        {0.0F, -1.0F, 0.0F},
    }};

    std::array<Mat4, kCubemapFaceCount> result{};
    for (std::size_t face = 0U; face < kCubemapFaceCount; ++face) {
        result[face] = projection * Mat4::look_at(
            light_position,
            light_position + directions[face],
            ups[face]);
        if (!finite_mat4(result[face])) {
            throw std::invalid_argument("point shadow face view-projection transform must be finite");
        }
    }
    return result;
}

void capture_depth_face(
    const Framebuffer& framebuffer,
    std::size_t face_index,
    std::vector<float>& depths) {
    const std::size_t size = framebuffer.width();
    const std::size_t face_pixels = size * size;
    const std::size_t offset = face_index * face_pixels;
    for (std::size_t y = 0U; y < size; ++y) {
        for (std::size_t x = 0U; x < size; ++x) {
            depths[offset + y * size + x] = framebuffer.depth_at(x, y);
        }
    }
}

}  // namespace

std::shared_ptr<const DepthTexture2D> render_directional_shadow_map(
    std::span<const PreparedModelListEntry> entries,
    const Mat4& light_view_projection,
    DirectionalShadowMapOptions options) {
    if (options.width == 0U || options.height == 0U) {
        throw std::invalid_argument("shadow map dimensions must be non-zero");
    }
    if (!finite_mat4(light_view_projection)) {
        throw std::invalid_argument("shadow light view-projection transform must be finite");
    }
    validate_culling(options.cull_mode, options.front_face);
    validate_shadow_entries(entries);
    const std::vector<detail::PreparedVertexMesh> meshes =
        prepare_shadow_meshes(entries);

    Framebuffer framebuffer{options.width, options.height, SampleCount::One};
    preflight_shadow_entries(
        framebuffer,
        entries,
        meshes,
        options.cull_mode,
        options.front_face);
    framebuffer.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
    draw_shadow_entries(
        framebuffer,
        entries,
        meshes,
        light_view_projection,
        options.cull_mode,
        options.front_face);

    return std::make_shared<const DepthTexture2D>(capture_depth_texture(framebuffer));
}

std::shared_ptr<const DepthCubemap> render_point_shadow_cubemap(
    std::span<const PreparedModelListEntry> entries,
    const Vec3& light_position,
    PointShadowCubemapOptions options) {
    const std::size_t storage_size = checked_cubemap_storage_size(options.size);
    const std::array<Mat4, kCubemapFaceCount> face_view_projections =
        point_shadow_face_view_projections(
            light_position,
            options.near_plane,
            options.far_plane);
    validate_culling(options.cull_mode, options.front_face);
    validate_shadow_entries(entries);
    const std::vector<detail::PreparedVertexMesh> meshes =
        prepare_shadow_meshes(entries);

    Framebuffer framebuffer{options.size, options.size, SampleCount::One};
    preflight_shadow_entries(
        framebuffer,
        entries,
        meshes,
        options.cull_mode,
        options.front_face);

    std::vector<float> depths(storage_size, 1.0F);
    for (std::size_t face = 0U; face < kCubemapFaceCount; ++face) {
        framebuffer.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
        draw_shadow_entries(
            framebuffer,
            entries,
            meshes,
            face_view_projections[face],
            options.cull_mode,
            options.front_face);
        capture_depth_face(framebuffer, face, depths);
    }

    return std::make_shared<const DepthCubemap>(
        options.size,
        light_position,
        face_view_projections,
        std::move(depths));
}

}  // namespace tiny_renderer
