#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/rasterizer.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

Vertex lit_vertex(const Vec3& position, const Vec3& normal) {
    return Vertex::with_varyings(position, VaryingPack{
        1.0F, 1.0F, 1.0F,
        normal.x, normal.y, normal.z,
    });
}

Mesh make_lit_mesh() {
    const Vec3 left_normal = normalize(Vec3{1.0F, 0.0F, 1.0F});
    const Vec3 right_normal = normalize(Vec3{0.0F, 1.0F, 1.0F});
    Mesh mesh;
    mesh.vertices = {
        lit_vertex({-0.8F, -0.6F, 0.0F}, left_normal),
        lit_vertex({-0.1F, -0.6F, 0.0F}, left_normal),
        lit_vertex({-0.45F, 0.6F, 0.0F}, left_normal),
        lit_vertex({0.1F, -0.6F, 0.0F}, right_normal),
        lit_vertex({0.8F, -0.6F, 0.0F}, right_normal),
        lit_vertex({0.45F, 0.6F, 0.0F}, right_normal),
    };
    mesh.triangles = {{0U, 1U, 2U}, {3U, 4U, 5U}};
    return mesh;
}

DirectionalLight make_light() {
    return DirectionalLight{
        true,
        NormalBinding{3U, 4U, 5U},
        normalize(Vec3{0.2F, 0.5F, 1.0F}),
        0.1F,
        0.9F,
    };
}

void test_direct_range_matches_explicit_lit_submesh() {
    const Mesh mesh = make_lit_mesh();
    const Mat4 model = Mat4::scale({1.7F, 0.8F, 0.5F});
    const DirectionalLight light = make_light();

    Framebuffer ranged_fb(65U, 65U);
    Rasterizer ranged(ranged_fb, {}, {}, light);
    ranged.draw_mesh_range(
        mesh,
        DrawRange{1U, 1U},
        model,
        Mat4::identity(),
        Mat4::identity());

    Mesh explicit_mesh;
    explicit_mesh.vertices = mesh.vertices;
    explicit_mesh.triangles.push_back(mesh.triangles[1]);
    Framebuffer explicit_fb(65U, 65U);
    Rasterizer explicit_rasterizer(explicit_fb, {}, {}, light);
    explicit_rasterizer.draw_mesh(
        explicit_mesh,
        model,
        Mat4::identity(),
        Mat4::identity());

    check(ranged_fb.rgb8() == explicit_fb.rgb8(),
          "direct lit draw range is byte-identical to the equivalent explicit submesh under non-uniform scaling");
    check(ranged_fb.fnv1a64() == explicit_fb.fnv1a64(),
          "direct lit draw range preserves deterministic framebuffer hashing");
}

void test_direct_range_singular_normal_transform_fails_closed() {
    const Mesh mesh = make_lit_mesh();
    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::uint64_t before = framebuffer.fnv1a64();

    Rasterizer rasterizer(framebuffer, {}, {}, make_light());
    bool threw = false;
    try {
        rasterizer.draw_mesh_range(
            mesh,
            DrawRange{1U, 1U},
            Mat4::scale({0.0F, 1.0F, 1.0F}),
            Mat4::identity(),
            Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw, "direct range rejects a singular model transform before drawing when lighting needs a normal matrix");
    check(framebuffer.fnv1a64() == before,
          "direct range singular normal transform rejection occurs before framebuffer mutation");
}

void test_empty_direct_range_still_validates_global_state() {
    const Mesh mesh = make_lit_mesh();
    Framebuffer framebuffer(33U, 33U);
    const std::uint64_t before = framebuffer.fnv1a64();
    DirectionalLight invalid = make_light();
    invalid.diffuse = 1.1F;
    Rasterizer rasterizer(framebuffer, {}, {}, invalid);

    bool threw = false;
    try {
        rasterizer.draw_mesh_range(
            mesh,
            DrawRange{0U, 0U},
            Mat4::identity(),
            Mat4::identity(),
            Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw, "empty direct range still validates renderer state like the copy-backed range adapter");
    check(framebuffer.fnv1a64() == before,
          "empty direct range state validation is fail-closed");
}

}  // namespace

int main() {
    try {
        test_direct_range_matches_explicit_lit_submesh();
        test_direct_range_singular_normal_transform_fails_closed();
        test_empty_direct_range_still_validates_global_state();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " direct range test(s) failed\n";
        return 1;
    }
    std::cout << "all direct range tests passed\n";
    return 0;
}
