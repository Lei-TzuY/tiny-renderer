#include "tiny_renderer/rasterizer.hpp"

#include <cstddef>

#include "rasterizer_validation.hpp"
#include "vertex_program_internal.hpp"

namespace tiny_renderer {
namespace {

Triangle assemble_triangle(const Mesh& mesh, const TriangleIndices& indices) {
    return Triangle{mesh.vertices[indices[0]], mesh.vertices[indices[1]], mesh.vertices[indices[2]]};
}

void validate_fragment_program_for_mesh(
    const FragmentProgramPtr& fragment_program,
    const Mesh& mesh) {
    if (!fragment_program) {
        return;
    }
    const std::size_t varying_count = mesh.vertices.empty()
        ? 0U
        : mesh.vertices.front().varyings.count;
    fragment_program->validate(varying_count);
}

}  // namespace

void Rasterizer::draw_mesh_range(
    const Mesh& mesh,
    DrawRange range,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection) {
    const detail::PreparedVertexMesh programmed =
        detail::prepare_vertex_program_mesh(vertex_program_, mesh);
    const Mesh& prepared_mesh = programmed.get();

    detail::validate_alpha_test_state(alpha_test_state_);
    detail::preflight_mesh_range_submission(
        framebuffer_,
        prepared_mesh,
        range,
        color_binding_,
        texture_binding_,
        directional_light_,
        point_light_,
        fixed_lights_,
        material_state_,
        base_color_source_,
        cull_mode_,
        front_face_,
        depth_state_,
        viewport_state_,
        stencil_state_,
        blend_state_,
        alpha_to_coverage_state_,
        shadow_state_,
        point_shadow_state_,
        &model,
        false);
    validate_fragment_program_for_mesh(fragment_program_, prepared_mesh);

    Rasterizer execution(
        framebuffer_,
        color_binding_,
        texture_binding_,
        directional_light_,
        material_state_,
        base_color_source_,
        cull_mode_,
        front_face_,
        depth_state_,
        viewport_state_,
        stencil_state_,
        blend_state_,
        alpha_to_coverage_state_,
        shadow_state_,
        alpha_test_state_,
        fragment_program_,
        {},
        point_light_,
        fixed_lights_,
        point_shadow_state_);

    const std::size_t end = range.first_triangle + range.triangle_count;
    for (std::size_t triangle_index = range.first_triangle; triangle_index < end; ++triangle_index) {
        execution.draw_triangle(
            assemble_triangle(prepared_mesh, prepared_mesh.triangles[triangle_index]),
            model,
            view,
            projection);
    }
}

void Rasterizer::draw_mesh_range(const Mesh& mesh, DrawRange range, const Mat4& mvp) {
    const detail::PreparedVertexMesh programmed =
        detail::prepare_vertex_program_mesh(vertex_program_, mesh);
    const Mesh& prepared_mesh = programmed.get();

    detail::validate_alpha_test_state(alpha_test_state_);
    detail::preflight_mesh_range_submission(
        framebuffer_,
        prepared_mesh,
        range,
        color_binding_,
        texture_binding_,
        directional_light_,
        point_light_,
        fixed_lights_,
        material_state_,
        base_color_source_,
        cull_mode_,
        front_face_,
        depth_state_,
        viewport_state_,
        stencil_state_,
        blend_state_,
        alpha_to_coverage_state_,
        shadow_state_,
        point_shadow_state_,
        nullptr,
        true);
    validate_fragment_program_for_mesh(fragment_program_, prepared_mesh);

    Rasterizer execution(
        framebuffer_,
        color_binding_,
        texture_binding_,
        directional_light_,
        material_state_,
        base_color_source_,
        cull_mode_,
        front_face_,
        depth_state_,
        viewport_state_,
        stencil_state_,
        blend_state_,
        alpha_to_coverage_state_,
        shadow_state_,
        alpha_test_state_,
        fragment_program_,
        {},
        point_light_,
        fixed_lights_,
        point_shadow_state_);

    const std::size_t end = range.first_triangle + range.triangle_count;
    for (std::size_t triangle_index = range.first_triangle; triangle_index < end; ++triangle_index) {
        execution.draw_triangle(
            assemble_triangle(prepared_mesh, prepared_mesh.triangles[triangle_index]),
            mvp);
    }
}

}  // namespace tiny_renderer
