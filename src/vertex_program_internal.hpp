#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>

#include "tiny_renderer/vertex_program.hpp"

namespace tiny_renderer::detail {

constexpr float kMaxProgramVertexMagnitude = 1.0e20F;

inline bool finite_program_scalar(float value) {
    return std::isfinite(value) && std::fabs(value) <= kMaxProgramVertexMagnitude;
}

inline std::size_t vertex_program_varying_count(const Mesh& mesh) {
    return mesh.vertices.empty() ? 0U : mesh.vertices.front().varyings.count;
}

inline void validate_vertex_program_static(
    const VertexProgramPtr& program,
    std::size_t varying_count) {
    if (program) {
        program->validate(varying_count);
    }
}

inline void validate_vertex_program_output(
    const Vertex& source,
    const VertexProgramOutput& output) {
    if (!finite_program_scalar(output.position.x)
        || !finite_program_scalar(output.position.y)
        || !finite_program_scalar(output.position.z)) {
        throw std::invalid_argument(
            "vertex program position output must be finite and within the safe magnitude bound");
    }
    if (output.varyings.count != source.varyings.count) {
        throw std::invalid_argument("vertex program must preserve varying channel count");
    }
    for (std::size_t channel = 0U; channel < source.varyings.count; ++channel) {
        if (output.varyings.interpolation[channel]
                != source.varyings.interpolation[channel]) {
            throw std::invalid_argument(
                "vertex program must preserve varying interpolation qualifiers");
        }
        if (!finite_program_scalar(output.varyings.values[channel])) {
            throw std::invalid_argument(
                "vertex program varying output must be finite and within the safe magnitude bound");
        }
    }
}

inline Vertex apply_vertex_program(
    const VertexProgramPtr& program,
    const Vertex& source) {
    if (!program) {
        return source;
    }
    const VertexProgramOutput output = program->process(
        VertexProgramInput{source.position, source.varyings});
    validate_vertex_program_output(source, output);
    return Vertex{output.position, output.varyings};
}

inline Triangle apply_vertex_program(
    const VertexProgramPtr& program,
    const Triangle& triangle) {
    if (!program) {
        return triangle;
    }
    validate_vertex_program_static(program, triangle.front().varyings.count);
    Triangle result{};
    for (std::size_t i = 0U; i < triangle.size(); ++i) {
        result[i] = apply_vertex_program(program, triangle[i]);
    }
    return result;
}

inline Mesh apply_vertex_program(
    const VertexProgramPtr& program,
    const Mesh& mesh) {
    if (!program) {
        return mesh;
    }
    validate_vertex_program_static(program, vertex_program_varying_count(mesh));
    Mesh result = mesh;
    for (std::size_t i = 0U; i < mesh.vertices.size(); ++i) {
        result.vertices[i] = apply_vertex_program(program, mesh.vertices[i]);
    }
    return result;
}

struct PreparedVertexMesh {
    const Mesh* source{nullptr};
    std::optional<Mesh> transformed{};

    [[nodiscard]] const Mesh& get() const {
        if (transformed) {
            return *transformed;
        }
        if (source == nullptr) {
            throw std::logic_error("prepared vertex mesh has no source");
        }
        return *source;
    }
};

inline PreparedVertexMesh prepare_vertex_program_mesh(
    const VertexProgramPtr& program,
    const Mesh& mesh) {
    validate_vertex_program_static(program, vertex_program_varying_count(mesh));
    if (!program) {
        return PreparedVertexMesh{&mesh, std::nullopt};
    }
    return PreparedVertexMesh{&mesh, apply_vertex_program(program, mesh)};
}

}  // namespace tiny_renderer::detail
