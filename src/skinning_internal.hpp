#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "tiny_renderer/rasterizer.hpp"
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

inline void validate_skinning_normal_binding(
    const NormalBinding& binding,
    const Mesh& mesh) {
    for (const Vertex& vertex : mesh.vertices) {
        const VaryingPack& pack = vertex.varyings;
        if (binding.x >= pack.count
            || binding.y >= pack.count
            || binding.z >= pack.count) {
            throw std::out_of_range(
                "skinning normal binding references unavailable varying channel");
        }
        const Interpolation mode = pack.interpolation[binding.x];
        if (pack.interpolation[binding.y] != mode
            || pack.interpolation[binding.z] != mode) {
            throw std::invalid_argument(
                "skinning normal binding channels must use one interpolation qualifier");
        }
    }
}

[[nodiscard]] inline Mesh apply_linear_blend_skinning(
    const SkinningState& skinning,
    const Mesh& mesh,
    const NormalBinding* normal_binding = nullptr) {
    validate_skinning_mesh_ownership(skinning, mesh);

    Mesh result = mesh;
    const std::span<const VertexSkinBinding> bindings =
        skinning.vertex_bindings();
    const std::span<const Mat4> matrices = skinning.skin_matrices();

    std::vector<Mat3> normal_matrices;
    if (normal_binding != nullptr) {
        validate_skinning_normal_binding(*normal_binding, mesh);
        normal_matrices.reserve(matrices.size());
        for (const Mat4& matrix : matrices) {
            normal_matrices.push_back(normal_matrix(matrix));
        }
    }

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

        if (normal_binding != nullptr) {
            const VaryingPack& source_pack =
                mesh.vertices[vertex_index].varyings;
            const Vec3 source_normal{
                source_pack.values[normal_binding->x],
                source_pack.values[normal_binding->y],
                source_pack.values[normal_binding->z],
            };
            if (!std::isfinite(source_normal.x)
                || !std::isfinite(source_normal.y)
                || !std::isfinite(source_normal.z)
                || length(source_normal) <= kEpsilon) {
                throw std::invalid_argument(
                    "skinning source normal must be finite and non-zero");
            }

            double accumulated_normal_x = 0.0;
            double accumulated_normal_y = 0.0;
            double accumulated_normal_z = 0.0;
            for (const SkinInfluence influence :
                 bindings[vertex_index].influences()) {
                const Vec3 transformed_normal =
                    normal_matrices[influence.joint] * source_normal;
                if (!std::isfinite(transformed_normal.x)
                    || !std::isfinite(transformed_normal.y)
                    || !std::isfinite(transformed_normal.z)) {
                    throw std::invalid_argument(
                        "skinning normal transform produced a non-finite direction");
                }
                const double weight =
                    static_cast<double>(influence.weight);
                accumulated_normal_x +=
                    weight * static_cast<double>(transformed_normal.x);
                accumulated_normal_y +=
                    weight * static_cast<double>(transformed_normal.y);
                accumulated_normal_z +=
                    weight * static_cast<double>(transformed_normal.z);
            }

            const double normal_length_squared =
                accumulated_normal_x * accumulated_normal_x
                + accumulated_normal_y * accumulated_normal_y
                + accumulated_normal_z * accumulated_normal_z;
            if (!std::isfinite(normal_length_squared)
                || normal_length_squared
                    <= static_cast<double>(kEpsilon)
                        * static_cast<double>(kEpsilon)) {
                throw std::invalid_argument(
                    "skinning weighted normal is numerically unstable");
            }
            const double inverse_normal_length =
                1.0 / std::sqrt(normal_length_squared);
            const Vec3 normalized{
                static_cast<float>(
                    accumulated_normal_x * inverse_normal_length),
                static_cast<float>(
                    accumulated_normal_y * inverse_normal_length),
                static_cast<float>(
                    accumulated_normal_z * inverse_normal_length),
            };
            if (!std::isfinite(normalized.x)
                || !std::isfinite(normalized.y)
                || !std::isfinite(normalized.z)) {
                throw std::invalid_argument(
                    "skinning normalized normal must remain finite");
            }

            VaryingPack& output_pack =
                result.vertices[vertex_index].varyings;
            output_pack.values[normal_binding->x] = normalized.x;
            output_pack.values[normal_binding->y] = normalized.y;
            output_pack.values[normal_binding->z] = normalized.z;
        }
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
    const SkeletalPoseStatePtr& skeletal_pose,
    const VertexProgramPtr& vertex_program,
    const Mesh& mesh,
    const NormalBinding* skinning_normal_binding = nullptr) {
    validate_vertex_program_static(
        vertex_program,
        vertex_program_varying_count(mesh));
    if (skinning && skeletal_pose) {
        throw std::invalid_argument(
            "object-space preparation cannot bind direct skinning and a skeletal pose simultaneously");
    }

    const SkinningStatePtr resolved_skinning =
        skeletal_pose ? skeletal_pose->resolve() : skinning;
    if (!resolved_skinning && !vertex_program) {
        return PreparedObjectSpaceMesh{&mesh, std::nullopt};
    }

    Mesh transformed = resolved_skinning
        ? apply_linear_blend_skinning(
            *resolved_skinning,
            mesh,
            skinning_normal_binding)
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
