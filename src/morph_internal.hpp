#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>

#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/morph.hpp"
#include "tiny_renderer/rasterizer.hpp"

namespace tiny_renderer::detail {

inline constexpr double kMaxMorphedVertexMagnitude = 1.0e20;

inline void validate_morph_mesh_ownership(
    const MorphState& morph,
    const Mesh& mesh) {
    if (morph.target_set().vertex_count() != mesh.vertices.size()) {
        throw std::invalid_argument(
            "morph target vertex count must match canonical mesh vertex count");
    }
}

inline void validate_morph_normal_binding(
    const NormalBinding& binding,
    const Mesh& mesh) {
    for (const Vertex& vertex : mesh.vertices) {
        const VaryingPack& pack = vertex.varyings;
        if (binding.x >= pack.count
            || binding.y >= pack.count
            || binding.z >= pack.count) {
            throw std::out_of_range(
                "morph normal binding references unavailable varying channel");
        }
        const Interpolation interpolation =
            pack.interpolation[binding.x];
        if (pack.interpolation[binding.y] != interpolation
            || pack.interpolation[binding.z] != interpolation) {
            throw std::invalid_argument(
                "morph normal binding channels must share one interpolation qualifier");
        }
    }
}

[[nodiscard]] inline Mesh apply_morph_targets(
    const MorphState& morph,
    const Mesh& mesh,
    const NormalBinding* normal_binding = nullptr) {
    validate_morph_mesh_ownership(morph, mesh);
    if (!morph.has_active_weights()) {
        return mesh;
    }

    const auto targets = morph.target_set().targets();
    const auto weights = morph.weights();
    if (normal_binding != nullptr) {
        validate_morph_normal_binding(*normal_binding, mesh);
        for (std::size_t target = 0U;
             target < targets.size();
             ++target) {
            if (weights[target] != 0.0F
                && !targets[target].normal_deltas) {
                throw std::invalid_argument(
                    "active lit morph target requires complete normal deltas");
            }
        }
    }

    Mesh result = mesh;
    for (std::size_t vertex_index = 0U;
         vertex_index < mesh.vertices.size();
         ++vertex_index) {
        const Vec3 source = mesh.vertices[vertex_index].position;
        if (!std::isfinite(source.x)
            || !std::isfinite(source.y)
            || !std::isfinite(source.z)) {
            throw std::invalid_argument(
                "morph source position must be finite");
        }

        double x = static_cast<double>(source.x);
        double y = static_cast<double>(source.y);
        double z = static_cast<double>(source.z);
        for (std::size_t target = 0U;
             target < targets.size();
             ++target) {
            const double weight =
                static_cast<double>(weights[target]);
            if (weight == 0.0) {
                continue;
            }
            const Vec3 delta =
                targets[target].position_deltas[vertex_index];
            x += weight * static_cast<double>(delta.x);
            y += weight * static_cast<double>(delta.y);
            z += weight * static_cast<double>(delta.z);
        }
        if (!std::isfinite(x)
            || !std::isfinite(y)
            || !std::isfinite(z)
            || std::fabs(x) > kMaxMorphedVertexMagnitude
            || std::fabs(y) > kMaxMorphedVertexMagnitude
            || std::fabs(z) > kMaxMorphedVertexMagnitude) {
            throw std::invalid_argument(
                "morph position output must be finite and within the safe magnitude bound");
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
                    "morph source normal must be finite and non-zero");
            }

            double nx = static_cast<double>(source_normal.x);
            double ny = static_cast<double>(source_normal.y);
            double nz = static_cast<double>(source_normal.z);
            for (std::size_t target = 0U;
                 target < targets.size();
                 ++target) {
                const double weight =
                    static_cast<double>(weights[target]);
                if (weight == 0.0) {
                    continue;
                }
                const Vec3 delta =
                    (*targets[target].normal_deltas)[vertex_index];
                nx += weight * static_cast<double>(delta.x);
                ny += weight * static_cast<double>(delta.y);
                nz += weight * static_cast<double>(delta.z);
            }
            const double length_squared =
                nx * nx + ny * ny + nz * nz;
            if (!std::isfinite(length_squared)
                || length_squared
                    <= static_cast<double>(kEpsilon)
                        * static_cast<double>(kEpsilon)) {
                throw std::invalid_argument(
                    "morph weighted normal is finite but non-normalizable");
            }
            const double inverse_length =
                1.0 / std::sqrt(length_squared);
            const Vec3 normal{
                static_cast<float>(nx * inverse_length),
                static_cast<float>(ny * inverse_length),
                static_cast<float>(nz * inverse_length),
            };
            if (!std::isfinite(normal.x)
                || !std::isfinite(normal.y)
                || !std::isfinite(normal.z)) {
                throw std::invalid_argument(
                    "morph normalized normal must remain finite");
            }
            VaryingPack& output =
                result.vertices[vertex_index].varyings;
            output.values[normal_binding->x] = normal.x;
            output.values[normal_binding->y] = normal.y;
            output.values[normal_binding->z] = normal.z;
        }
    }
    return result;
}

}  // namespace tiny_renderer::detail
