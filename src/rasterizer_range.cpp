#include "tiny_renderer/rasterizer.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace tiny_renderer {
namespace {

void validate_draw_range(const Mesh& mesh, DrawRange range) {
    if (range.first_triangle > mesh.triangles.size()
        || range.triangle_count > mesh.triangles.size() - range.first_triangle) {
        throw std::out_of_range("mesh draw range exceeds triangle list");
    }
}

Mesh materialize_draw_range(const Mesh& mesh, DrawRange range) {
    validate_draw_range(mesh, range);

    Mesh selected;
    selected.vertices = mesh.vertices;
    using Difference = std::vector<TriangleIndices>::difference_type;
    const auto first = mesh.triangles.begin() + static_cast<Difference>(range.first_triangle);
    const auto last = first + static_cast<Difference>(range.triangle_count);
    selected.triangles.assign(first, last);
    return selected;
}

}  // namespace

void Rasterizer::draw_mesh_range(
    const Mesh& mesh,
    DrawRange range,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection) {
    draw_mesh(materialize_draw_range(mesh, range), model, view, projection);
}

void Rasterizer::draw_mesh_range(const Mesh& mesh, DrawRange range, const Mat4& mvp) {
    draw_mesh(materialize_draw_range(mesh, range), mvp);
}

}  // namespace tiny_renderer
