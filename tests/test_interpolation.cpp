#include <cmath>
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 2.0e-4F) {
    check(std::fabs(actual - expected) <= epsilon,
          message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

std::size_t count_non_black(const Framebuffer& fb) {
    std::size_t count = 0;
    for (std::size_t y = 0; y < fb.height(); ++y) {
        for (std::size_t x = 0; x < fb.width(); ++x) {
            const Vec3& color = fb.color_at(x, y);
            if (color.x != 0.0F || color.y != 0.0F || color.z != 0.0F) {
                ++count;
            }
        }
    }
    return count;
}

Mat4 projective_w_from_z() {
    Mat4 projective{};
    projective(0, 0) = 1.0F;
    projective(1, 1) = 1.0F;
    projective(3, 2) = 1.0F;
    return projective;
}

VaryingPack qualified_pack(float smooth, float noperspective, float flat) {
    VaryingPack pack{smooth, noperspective, flat};
    pack.set_interpolation(0U, Interpolation::Smooth);
    pack.set_interpolation(1U, Interpolation::NoPerspective);
    pack.set_interpolation(2U, Interpolation::Flat);
    return pack;
}

VaryingPack noperspective_red(float red) {
    VaryingPack pack{red, 0.0F, 0.0F};
    pack.set_interpolation(0U, Interpolation::NoPerspective);
    return pack;
}

VaryingPack flat_rgb(const Vec3& color) {
    VaryingPack pack{color.x, color.y, color.z};
    pack.set_interpolation(0U, Interpolation::Flat);
    pack.set_interpolation(1U, Interpolation::Flat);
    pack.set_interpolation(2U, Interpolation::Flat);
    return pack;
}

void test_mixed_qualifiers_are_analytic() {
    const Triangle triangle{{
        Vertex::with_varyings({-0.5F, -0.5F, 1.0F}, qualified_pack(1.0F, 0.0F, 0.25F)),
        Vertex::with_varyings({1.0F, -1.0F, 2.0F}, qualified_pack(0.0F, 1.0F, 0.75F)),
        Vertex::with_varyings({-2.0F, 2.0F, 4.0F}, qualified_pack(0.0F, 0.0F, 1.0F)),
    }};

    Framebuffer fb(33, 33);
    Rasterizer rasterizer(fb);
    rasterizer.draw_triangle(triangle, projective_w_from_z());

    const auto bary = barycentric_coordinates({8.0F, 24.0F}, {24.0F, 24.0F}, {8.0F, 8.0F}, {13.5F, 18.5F});
    check(bary.has_value(), "mixed-qualifier probe has barycentric coordinates");
    if (!bary) {
        return;
    }

    const float reciprocal_w = bary->x / 1.0F + bary->y / 2.0F + bary->z / 4.0F;
    const float expected_smooth = (bary->x / 1.0F) / reciprocal_w;
    const float expected_noperspective = bary->y;
    constexpr float expected_flat = 0.25F;

    const Vec3& actual = fb.color_at(13U, 18U);
    check_near(actual.x, expected_smooth, "smooth channel uses perspective-correct interpolation");
    check_near(actual.y, expected_noperspective, "noperspective channel uses screen-linear barycentrics");
    check_near(actual.z, expected_flat, "flat channel uses first submitted provoking vertex");
    check(std::fabs(actual.x - bary->x) > 0.1F, "smooth result differs from affine interpolation");
}

void test_noperspective_survives_homogeneous_clipping() {
    const Triangle crossing{{
        Vertex::with_varyings({-2.0F, 0.0F, 1.0F}, noperspective_red(0.0F)),
        Vertex::with_varyings({0.0F, -1.0F, 2.0F}, noperspective_red(1.0F)),
        Vertex::with_varyings({0.0F, 2.0F, 4.0F}, noperspective_red(0.0F)),
    }};

    const Vertex intersection_ca = Vertex::with_varyings(
        {-1.6F, 0.4F, 1.6F}, noperspective_red(0.0F));
    const Vertex intersection_ab = Vertex::with_varyings(
        {-4.0F / 3.0F, -1.0F / 3.0F, 4.0F / 3.0F}, noperspective_red(0.5F));
    const Triangle manual_a{{intersection_ca, intersection_ab, crossing[1]}};
    const Triangle manual_b{{intersection_ca, crossing[1], crossing[2]}};

    const Mat4 projective = projective_w_from_z();
    Framebuffer clipped(65, 65);
    Rasterizer clipped_rasterizer(clipped);
    clipped_rasterizer.draw_triangle(crossing, projective);

    Framebuffer manual(65, 65);
    Rasterizer manual_rasterizer(manual);
    manual_rasterizer.draw_triangle(manual_a, projective);
    manual_rasterizer.draw_triangle(manual_b, projective);

    check(count_non_black(clipped) > 0U, "noperspective clipping fixture produces fragments");
    check(clipped.rgb8() == manual.rgb8(),
          "noperspective values at generated clip vertices match projected screen-edge interpolation");
}

void test_flat_provoking_value_survives_clipping() {
    const Vec3 provoking{0.2F, 0.4F, 0.6F};
    const Triangle crossing{{
        Vertex::with_varyings({-2.0F, 0.0F, 1.0F}, flat_rgb(provoking)),
        Vertex::with_varyings({0.0F, -1.0F, 2.0F}, flat_rgb({0.9F, 0.1F, 0.2F})),
        Vertex::with_varyings({0.0F, 2.0F, 4.0F}, flat_rgb({0.1F, 0.9F, 0.3F})),
    }};

    Framebuffer fb(65, 65);
    Rasterizer rasterizer(fb);
    rasterizer.draw_triangle(crossing, projective_w_from_z());

    std::size_t fragments = 0U;
    for (std::size_t y = 0; y < fb.height(); ++y) {
        for (std::size_t x = 0; x < fb.width(); ++x) {
            const Vec3& color = fb.color_at(x, y);
            if (color.x == 0.0F && color.y == 0.0F && color.z == 0.0F) {
                continue;
            }
            ++fragments;
            check_near(color.x, provoking.x, "flat red survives clipping");
            check_near(color.y, provoking.y, "flat green survives clipping");
            check_near(color.z, provoking.z, "flat blue survives clipping");
        }
    }
    check(fragments > 0U, "flat clipping fixture produces fragments");
}

void test_qualifier_layout_mismatch_is_fail_closed() {
    VaryingPack smooth{1.0F, 0.0F, 0.0F};
    VaryingPack mismatched{0.0F, 1.0F, 0.0F};
    mismatched.set_interpolation(0U, Interpolation::Flat);

    const Triangle triangle{{
        Vertex::with_varyings({-0.5F, -0.5F, 0.0F}, smooth),
        Vertex::with_varyings({0.5F, -0.5F, 0.0F}, mismatched),
        Vertex::with_varyings({0.0F, 0.5F, 0.0F}, smooth),
    }};

    Framebuffer fb(33, 33);
    Rasterizer rasterizer(fb);
    const std::uint64_t before = fb.fnv1a64();
    bool threw = false;
    try {
        rasterizer.draw_triangle(triangle, Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw, "triangle with mismatched interpolation qualifiers is rejected");
    check(fb.fnv1a64() == before, "qualifier validation happens before framebuffer mutation");
}

}  // namespace

int main() {
    try {
        test_mixed_qualifiers_are_analytic();
        test_noperspective_survives_homogeneous_clipping();
        test_flat_provoking_value_survives_clipping();
        test_qualifier_layout_mismatch_is_fail_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " interpolation qualifier test(s) failed\n";
        return 1;
    }
    std::cout << "all interpolation qualifier tests passed\n";
    return 0;
}
