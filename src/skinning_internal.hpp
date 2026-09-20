#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>

#include "tiny_renderer/skinning.hpp"
#include "vertex_program_internal.hpp"

namespace tiny_renderer::detail {

constexpr double kMaxSkinnedVertexMagnitude = 1.0e20;

inline void validate_skinning_mesh_ownership(
    const SkinningState& skinning,
    const Mesh& mesh) {
    if (skinning.vertex_bindings().size() != mesh.vertices.size()) {
        throw std::invalid_argument(
            "skinning vertex binding count must match canonical mesh vertex count");
    }
}

[[nodiscard]] inline Mesh apply_linear_blend_skinning(
    const SkinningState& skinning,
    const Mesh& mesh) {
    validate_skinning_mesh_ownership(skinning, mesh);

    Mesh result = mesh;
    const std::span<const VertexSkinBinding> bindings =
        skinning.vertex_bindings();
    const std::span<const Mat4> matrices = skinning.skin_matrices();

    for (std::size_t vertex_index = 0U;
         vertex_index < mesh.vertices.size();
         ++vertex_index) {
        const Vec3 source = mesh.vertices[vertex_index].position;
        if (!std::isfinite(source.x)
            || !std::isfinite(source.y)
            || !std::isfinite(source.z)) {
            throw std::invalid_argument(
                "skinning source position must be finite");
        }

        double accumulated_x = 0.0;
        double accumulated_y = 0.0;
        double accumulated_z = 0.0;
        double total_weight = 0.0;
        for (const SkinInfluence influence : bindings[vertex_index].influences()) {
            const Mat4& matrix = matrices[influence.joint];
            const Vec4 transformed = matrix * Vec4{
                source.x,
                source.y,
                source.z,
                1.0F,
            };
            if (!std::isfinite(transformed.x)
                || !std::isfinite(transformed.y)
                || !std::isfinite(transformed.z)
                || !std::isfinite(transformed.w)
                || std::fabs(transformed.w - 1.0F) > kEpsilon) {
                throw std::invalid_argument(
                    "skinning matrix application produced an invalid affine position");
            }

            const double weight = static_cast<double>(influence.weight);
            accumulated_x += weight * static_cast<double>(transformed.x);
            accumulated_y += weight * static_cast<double>(transformed.y);
            accumulated_z += weight * static_cast<double>(transformed.z);
            total_weight += weight;
        }

        if (!std::isfinite(accumulated_x)
            || !std::isfinite(accumulated_y)
            || !std::isfinite(accumulated_z)
            || !std::isfinite(total_weight)
            || total_weight <= 0.0) {
            throw std::invalid_argument(
                "skinning weighted position accumulation is not finite");
        }

        const double x = accumulated_x / total_weight;
        const double y = accumulated_y / total_weight;
        const double z = accumulated_z / total_weight;
        if (!std::isfinite(x)
            || !std::isfinite(y)
            || !std::isfinite(z)
            || std::fabs(x) > kMaxSkinnedVertexMagnitude
            || std::fabs(y) > kMaxSkinnedVertexMagnitude
            || std::fabs(z) > kMaxSkinnedVertexMagnitude) {
            throw std::invalid_argument(
                "skinning position output must be finite and within the safe magnitude bound");
        }

        result.vertices[vertex_index].position = {
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(z),
        };
    }
    return result;
}

// Owns the complete object-space deformation result when skinning and/or the
// M35 vertex program is active; otherwise it aliases the canonical mesh.
struct PreparedObjectSpaceMesh {
    const Mesh* source{nullptr};
    std::optional<Mesh> transformed{};

    [[nodiscard]] const Mesh& get() const {
        if (transformed) {
            return *transformed;
        }
        if (source == nullptr) {
            throw std::logic_error(
                "prepared object-space mesh has no source");
        }
        return *source;
    }
};

[[nodiscard]] inline PreparedObjectSpaceMesh prepare_object_space_mesh(
    const SkinningStatePtr& skinning,
    const VertexProgramPtr& vertex_program,
    const Mesh& mesh) {
    validate_vertex_program_static(
        vertex_program,
        vertex_program_varying_count(mesh));

    if (!skinning && !vertex_program) {
        return PreparedObjectSpaceMesh{&mesh, std::nullopt};
    }

    Mesh transformed = skinning
        ? apply_linear_blend_skinning(*skinning, mesh)
        : mesh;
    if (vertex_program) {
        transformed = apply_vertex_program(vertex_program, transformed);
    }
    return PreparedObjectSpaceMesh{
        &mesh,
        std::move(transformed),
    };
}

}  // namespace tiny_renderer::detail
