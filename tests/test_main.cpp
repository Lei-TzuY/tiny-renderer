#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 1.0e-4F) {
    check(std::fabs(actual - expected) <= epsilon, message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

std::size_t count_non_black(const Framebuffer& fb) {
    std::size_t count = 0;
    for (std::size_t y = 0; y < fb.height(); ++y) {
        for (std::size_t x = 0; x < fb.width(); ++x) {
            const Vec3& c = fb.color_at(x, y);
            if (c.x != 0.0F || c.y != 0.0F || c.z != 0.0F) {
                ++count;
            }
        }
    }
    return count;
}

Triangle ndc_triangle(float z, const Vec3& color) {
    return Triangle{{
        {{-0.75F, -0.75F, z}, color},
        {{0.75F, -0.75F, z}, color},
        {{0.0F, 0.75F, z}, color},
    }};
}

Mesh centered_quad(const Vec3& color) {
    Mesh mesh;
    mesh.vertices = {
        {{-0.5F, -0.5F, 0.0F}, color},
        {{0.5F, -0.5F, 0.0F}, color},
        {{0.5F, 0.5F, 0.0F}, color},
        {{-0.5F, 0.5F, 0.0F}, color},
    };
    mesh.triangles = {
        TriangleIndices{0U, 1U, 2U},
        TriangleIndices{0U, 2U, 3U},
    };
    return mesh;
}

void test_math() {
    const Vec3 a{1.0F, 2.0F, 3.0F};
    const Vec3 b{-2.0F, 0.5F, 4.0F};
    check_near(dot(a, b), 11.0F, "dot product");
    const Vec3 c = cross({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    check_near(c.x, 0.0F, "cross x");
    check_near(c.y, 0.0F, "cross y");
    check_near(c.z, 1.0F, "cross z");

    const Mat4 transform = Mat4::translation({2.0F, -1.0F, 3.0F}) * Mat4::scale({2.0F, 3.0F, 4.0F});
    const Vec4 transformed = transform * Vec4{1.0F, 1.0F, 1.0F, 1.0F};
    check_near(transformed.x, 4.0F, "matrix transform x");
    check_near(transformed.y, 2.0F, "matrix transform y");
    check_near(transformed.z, 7.0F, "matrix transform z");
    check_near(transformed.w, 1.0F, "matrix transform w");
}

void test_transforms() {
    const Mat4 view = Mat4::look_at({0.0F, 0.0F, 2.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    const Vec4 origin_in_view = view * Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    check_near(origin_in_view.z, -2.0F, "look_at places target in front of camera");

    const Mat4 projection = Mat4::perspective(radians(90.0F), 1.0F, 1.0F, 10.0F);
    const Vec4 clip = projection * Vec4{1.0F, 0.0F, -2.0F, 1.0F};
    check_near(clip.x / clip.w, 0.5F, "perspective divide x");
    check(clip.z / clip.w >= -1.0F && clip.z / clip.w <= 1.0F, "perspective maps visible z into clip range");
}

void test_barycentric() {
    const Vec2 a{0.0F, 0.0F};
    const Vec2 b{2.0F, 0.0F};
    const Vec2 c{0.0F, 2.0F};
    const auto center = barycentric_coordinates(a, b, c, {2.0F / 3.0F, 2.0F / 3.0F});
    check(center.has_value(), "centroid has barycentric coordinates");
    check_near(center->x, 1.0F / 3.0F, "centroid barycentric a");
    check_near(center->y, 1.0F / 3.0F, "centroid barycentric b");
    check_near(center->z, 1.0F / 3.0F, "centroid barycentric c");
    check(barycentric_inside(*center), "centroid is inside");

    const auto edge_point = barycentric_coordinates(a, b, c, {1.0F, 0.0F});
    check(edge_point.has_value() && barycentric_inside(*edge_point), "point on edge is inside geometrically");
    const auto outside = barycentric_coordinates(a, b, c, {2.0F, 2.0F});
    check(outside.has_value() && !barycentric_inside(*outside), "outside point is rejected");
    check(!barycentric_coordinates({0.0F, 0.0F}, {1.0F, 1.0F}, {2.0F, 2.0F}, {0.0F, 0.0F}).has_value(), "degenerate barycentric triangle rejected");
}

void test_degenerate_triangle() {
    Framebuffer fb(32, 32);
    Rasterizer rasterizer(fb);
    const Triangle degenerate{{
        {{-0.5F, -0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        {{0.5F, 0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}},
    }};
    rasterizer.draw_triangle(degenerate, Mat4::identity());
    check(count_non_black(fb) == 0U, "degenerate triangle produces no fragments");
}

void test_orientation_invariance() {
    Framebuffer ccw(32, 32);
    Rasterizer r_ccw(ccw);
    Triangle triangle = ndc_triangle(0.0F, {0.8F, 0.2F, 0.4F});
    r_ccw.draw_triangle(triangle, Mat4::identity());

    Framebuffer cw(32, 32);
    Rasterizer r_cw(cw);
    std::swap(triangle[1], triangle[2]);
    r_cw.draw_triangle(triangle, Mat4::identity());

    check(ccw.rgb8() == cw.rgb8(), "opposite triangle winding rasterizes identically");
}

void test_depth_ordering() {
    Framebuffer fb_a(48, 48);
    Rasterizer ra(fb_a);
    ra.draw_triangle(ndc_triangle(0.5F, {1.0F, 0.0F, 0.0F}), Mat4::identity());
    ra.draw_triangle(ndc_triangle(-0.5F, {0.0F, 1.0F, 0.0F}), Mat4::identity());

    Framebuffer fb_b(48, 48);
    Rasterizer rb(fb_b);
    rb.draw_triangle(ndc_triangle(-0.5F, {0.0F, 1.0F, 0.0F}), Mat4::identity());
    rb.draw_triangle(ndc_triangle(0.5F, {1.0F, 0.0F, 0.0F}), Mat4::identity());

    const Vec3& center_a = fb_a.color_at(24, 24);
    const Vec3& center_b = fb_b.color_at(24, 24);
    check(center_a.y > 0.9F && center_a.x < 0.1F, "near triangle wins depth test");
    check(center_b.y > 0.9F && center_b.x < 0.1F, "depth result independent of draw order");
    check(fb_a.rgb8() == fb_b.rgb8(), "depth ordering produces deterministic framebuffer");
}

void test_clipping_and_bounds() {
    Framebuffer fb(40, 30);
    Rasterizer rasterizer(fb);
    const Triangle crossing{{
        {{-4.0F, -0.3F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        {{0.7F, -0.8F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        {{0.4F, 0.9F, 0.0F}, {1.0F, 1.0F, 1.0F}},
    }};
    rasterizer.draw_triangle(crossing, Mat4::identity());
    check(count_non_black(fb) > 0U, "partially clipped triangle still rasterizes");

    const std::uint64_t before = fb.fnv1a64();
    const Triangle outside{{
        {{2.0F, 2.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        {{3.0F, 2.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        {{2.0F, 3.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
    }};
    rasterizer.draw_triangle(outside, Mat4::identity());
    check(before == fb.fnv1a64(), "fully clipped triangle cannot corrupt framebuffer");
}

void test_deterministic_hash() {
    Framebuffer fb(16, 16);
    Rasterizer rasterizer(fb);
    rasterizer.draw_triangle(ndc_triangle(0.0F, {0.25F, 0.5F, 0.75F}), Mat4::identity());
    const std::uint64_t hash = fb.fnv1a64();
    constexpr std::uint64_t expected = 0x224b8e00590a737cULL;
    check(hash == expected, "deterministic framebuffer hash");
}

void test_indexed_mesh_matches_explicit_triangles() {
    const Mesh mesh = centered_quad({0.2F, 0.7F, 0.4F});

    Framebuffer indexed(33, 33);
    Rasterizer indexed_rasterizer(indexed);
    indexed_rasterizer.draw_mesh(mesh, Mat4::identity());

    Framebuffer explicit_triangles(33, 33);
    Rasterizer explicit_rasterizer(explicit_triangles);
    explicit_rasterizer.draw_triangle(Triangle{mesh.vertices[0], mesh.vertices[1], mesh.vertices[2]}, Mat4::identity());
    explicit_rasterizer.draw_triangle(Triangle{mesh.vertices[0], mesh.vertices[2], mesh.vertices[3]}, Mat4::identity());

    check(indexed.rgb8() == explicit_triangles.rgb8(), "indexed mesh matches equivalent explicit triangle submission");
}

void test_shared_edge_crack_free() {
    Framebuffer fb(33, 33);
    Rasterizer rasterizer(fb);
    rasterizer.draw_mesh(centered_quad({1.0F, 1.0F, 1.0F}), Mat4::identity());

    check(count_non_black(fb) == 256U, "two indexed triangles cover their shared-edge quad without cracks or double-coverage spill");
}

void test_invalid_mesh_is_fail_closed() {
    Mesh mesh = centered_quad({0.8F, 0.3F, 0.1F});
    mesh.triangles.push_back(TriangleIndices{0U, 2U, 99U});

    Framebuffer fb(33, 33);
    Rasterizer rasterizer(fb);
    const std::uint64_t before = fb.fnv1a64();
    bool threw = false;
    try {
        rasterizer.draw_mesh(mesh, Mat4::identity());
    } catch (const std::out_of_range&) {
        threw = true;
    }

    check(threw, "out-of-range mesh index is rejected");
    check(fb.fnv1a64() == before, "malformed indexed mesh is rejected before any framebuffer write");
}

void test_perspective_correct_color() {
    const Triangle triangle{{
        {{-0.5F, -0.5F, 1.0F}, {1.0F, 0.0F, 0.0F}},
        {{1.0F, -1.0F, 2.0F}, {0.0F, 1.0F, 0.0F}},
        {{-2.0F, 2.0F, 4.0F}, {0.0F, 0.0F, 1.0F}},
    }};

    Mat4 projective{};
    projective(0, 0) = 1.0F;
    projective(1, 1) = 1.0F;
    projective(3, 2) = 1.0F;

    Framebuffer fb(33, 33);
    Rasterizer rasterizer(fb);
    rasterizer.draw_triangle(triangle, projective);

    const Vec2 screen_a{8.0F, 24.0F};
    const Vec2 screen_b{24.0F, 24.0F};
    const Vec2 screen_c{8.0F, 8.0F};
    const Vec2 probe{13.5F, 18.5F};
    const auto bary = barycentric_coordinates(screen_a, screen_b, screen_c, probe);
    check(bary.has_value(), "perspective interpolation probe has barycentric coordinates");
    if (!bary) {
        return;
    }

    const float denominator = bary->x / 1.0F + bary->y / 2.0F + bary->z / 4.0F;
    const Vec3 expected{
        (bary->x / 1.0F) / denominator,
        (bary->y / 2.0F) / denominator,
        (bary->z / 4.0F) / denominator,
    };
    const Vec3& actual = fb.color_at(13U, 18U);
    check_near(actual.x, expected.x, "perspective-correct red", 2.0e-4F);
    check_near(actual.y, expected.y, "perspective-correct green", 2.0e-4F);
    check_near(actual.z, expected.z, "perspective-correct blue", 2.0e-4F);
    check(std::fabs(actual.x - bary->x) > 0.1F, "perspective-correct result is observably different from affine interpolation");
}

void test_general_varying_binding_perspective() {
    const Triangle triangle{{
        Vertex::with_varyings({-0.5F, -0.5F, 1.0F}, VaryingPack{7.0F, 8.0F, 1.0F, 0.0F, 0.0F}),
        Vertex::with_varyings({1.0F, -1.0F, 2.0F}, VaryingPack{7.0F, 8.0F, 0.0F, 1.0F, 0.0F}),
        Vertex::with_varyings({-2.0F, 2.0F, 4.0F}, VaryingPack{7.0F, 8.0F, 0.0F, 0.0F, 1.0F}),
    }};

    Mat4 projective{};
    projective(0, 0) = 1.0F;
    projective(1, 1) = 1.0F;
    projective(3, 2) = 1.0F;

    Framebuffer fb(33, 33);
    Rasterizer rasterizer(fb, ColorBinding{2U, 3U, 4U});
    rasterizer.draw_triangle(triangle, projective);

    const auto bary = barycentric_coordinates({8.0F, 24.0F}, {24.0F, 24.0F}, {8.0F, 8.0F}, {13.5F, 18.5F});
    check(bary.has_value(), "general varying probe has barycentric coordinates");
    if (!bary) {
        return;
    }

    const float denominator = bary->x / 1.0F + bary->y / 2.0F + bary->z / 4.0F;
    const Vec3 expected{
        (bary->x / 1.0F) / denominator,
        (bary->y / 2.0F) / denominator,
        (bary->z / 4.0F) / denominator,
    };
    const Vec3& actual = fb.color_at(13U, 18U);
    check_near(actual.x, expected.x, "bound varying red is perspective-correct", 2.0e-4F);
    check_near(actual.y, expected.y, "bound varying green is perspective-correct", 2.0e-4F);
    check_near(actual.z, expected.z, "bound varying blue is perspective-correct", 2.0e-4F);
}

void test_general_varyings_survive_clipping() {
    const ColorBinding binding{3U, 4U, 5U};
    const Triangle crossing{{
        Vertex::with_varyings({-2.0F, 0.0F, 0.0F}, VaryingPack{9.0F, 9.0F, 9.0F, 0.0F, 0.0F, 0.0F}),
        Vertex::with_varyings({0.0F, -0.8F, 0.0F}, VaryingPack{9.0F, 9.0F, 9.0F, 1.0F, 0.0F, 0.0F}),
        Vertex::with_varyings({0.0F, 0.8F, 0.0F}, VaryingPack{9.0F, 9.0F, 9.0F, 0.0F, 0.0F, 1.0F}),
    }};

    const Vertex lower = Vertex::with_varyings({-1.0F, -0.4F, 0.0F}, VaryingPack{9.0F, 9.0F, 9.0F, 0.5F, 0.0F, 0.0F});
    const Vertex upper = Vertex::with_varyings({-1.0F, 0.4F, 0.0F}, VaryingPack{9.0F, 9.0F, 9.0F, 0.0F, 0.0F, 0.5F});
    const Triangle manual_a{{lower, crossing[1], crossing[2]}};
    const Triangle manual_b{{lower, crossing[2], upper}};

    Framebuffer clipped(41, 41);
    Rasterizer clipped_rasterizer(clipped, binding);
    clipped_rasterizer.draw_triangle(crossing, Mat4::identity());

    Framebuffer manual(41, 41);
    Rasterizer manual_rasterizer(manual, binding);
    manual_rasterizer.draw_triangle(manual_a, Mat4::identity());
    manual_rasterizer.draw_triangle(manual_b, Mat4::identity());

    check(clipped.rgb8() == manual.rgb8(), "non-color varying channels are interpolated correctly through homogeneous clipping");
}

void test_varying_contract_is_fail_closed() {
    Triangle triangle{{
        Vertex::with_varyings({-0.5F, -0.5F, 0.0F}, VaryingPack{1.0F, 0.0F, 0.0F, 2.0F}),
        Vertex::with_varyings({0.5F, -0.5F, 0.0F}, VaryingPack{0.0F, 1.0F, 0.0F}),
        Vertex::with_varyings({0.0F, 0.5F, 0.0F}, VaryingPack{0.0F, 0.0F, 1.0F}),
    }};

    Framebuffer fb(33, 33);
    Rasterizer rasterizer(fb);
    const std::uint64_t before = fb.fnv1a64();
    bool mismatch_threw = false;
    try {
        rasterizer.draw_triangle(triangle, Mat4::identity());
    } catch (const std::invalid_argument&) {
        mismatch_threw = true;
    }
    check(mismatch_threw, "mismatched varying counts are rejected");
    check(fb.fnv1a64() == before, "varying-count validation happens before framebuffer mutation");

    Triangle valid{{
        Vertex::with_varyings({-0.5F, -0.5F, 0.0F}, VaryingPack{1.0F, 0.0F, 0.0F}),
        Vertex::with_varyings({0.5F, -0.5F, 0.0F}, VaryingPack{0.0F, 1.0F, 0.0F}),
        Vertex::with_varyings({0.0F, 0.5F, 0.0F}, VaryingPack{0.0F, 0.0F, 1.0F}),
    }};
    Rasterizer bad_binding(fb, ColorBinding{0U, 1U, 5U});
    bool binding_threw = false;
    try {
        bad_binding.draw_triangle(valid, Mat4::identity());
    } catch (const std::out_of_range&) {
        binding_threw = true;
    }
    check(binding_threw, "out-of-range framebuffer color binding is rejected");
    check(fb.fnv1a64() == before, "color-binding validation happens before framebuffer mutation");
}

}  // namespace

int main() {
    try {
        test_math();
        test_transforms();
        test_barycentric();
        test_degenerate_triangle();
        test_orientation_invariance();
        test_depth_ordering();
        test_clipping_and_bounds();
        test_deterministic_hash();
        test_indexed_mesh_matches_explicit_triangles();
        test_shared_edge_crack_free();
        test_invalid_mesh_is_fail_closed();
        test_perspective_correct_color();
        test_general_varying_binding_perspective();
        test_general_varyings_survive_clipping();
        test_varying_contract_is_fail_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
