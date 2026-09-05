#include <cstddef>
#include <iostream>
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

void test_shared_edge_has_single_owner() {
    const Triangle lower_right{{
        {{-0.5F, -0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        {{0.5F, -0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        {{0.5F, 0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}},
    }};
    const Triangle upper_left{{
        {{-0.5F, -0.5F, 0.0F}, {0.0F, 1.0F, 0.0F}},
        {{0.5F, 0.5F, 0.0F}, {0.0F, 1.0F, 0.0F}},
        {{-0.5F, 0.5F, 0.0F}, {0.0F, 1.0F, 0.0F}},
    }};

    Framebuffer forward(33, 33);
    Rasterizer forward_rasterizer(forward);
    forward_rasterizer.draw_triangle(lower_right, Mat4::identity());
    forward_rasterizer.draw_triangle(upper_left, Mat4::identity());

    Framebuffer reverse(33, 33);
    Rasterizer reverse_rasterizer(reverse);
    reverse_rasterizer.draw_triangle(upper_left, Mat4::identity());
    reverse_rasterizer.draw_triangle(lower_right, Mat4::identity());

    check(forward.rgb8() == reverse.rgb8(), "shared-edge ownership is independent of draw order");
    check(count_non_black(forward) == 256U, "two colored triangles cover the 16x16 quad exactly once");
}

void test_subpixel_quantization_stability() {
    constexpr float edge_y = 0.484375F;
    constexpr float perturbation = 0.00002F;
    const Vec3 white{1.0F, 1.0F, 1.0F};

    const Triangle exact{{
        {{-0.5F, edge_y, 0.0F}, white},
        {{0.5F, edge_y, 0.0F}, white},
        {{0.0F, -0.5F, 0.0F}, white},
    }};
    const Triangle same_fixed_coordinates{{
        {{-0.5F, edge_y - perturbation, 0.0F}, white},
        {{0.5F, edge_y - perturbation, 0.0F}, white},
        {{0.0F, -0.5F, 0.0F}, white},
    }};

    Framebuffer exact_fb(65, 65);
    Rasterizer exact_rasterizer(exact_fb);
    exact_rasterizer.draw_triangle(exact, Mat4::identity());

    Framebuffer perturbed_fb(65, 65);
    Rasterizer perturbed_rasterizer(perturbed_fb);
    perturbed_rasterizer.draw_triangle(same_fixed_coordinates, Mat4::identity());

    check(exact_fb.rgb8() == perturbed_fb.rgb8(),
          "geometry that quantizes to the same 1/256-pixel coordinates has identical coverage");
    check(count_non_black(exact_fb) > 0U, "subpixel stability fixture actually rasterizes fragments");
}

void test_subpixel_collapsed_triangle_is_rejected() {
    const Vec3 white{1.0F, 1.0F, 1.0F};
    const Triangle collapsed{{
        {{-0.375F, 0.375F, 0.0F}, white},
        {{-0.37499F, 0.375F, 0.0F}, white},
        {{-0.375F, 0.37499F, 0.0F}, white},
    }};

    Framebuffer fb(65, 65);
    Rasterizer rasterizer(fb);
    rasterizer.draw_triangle(collapsed, Mat4::identity());
    check(count_non_black(fb) == 0U, "triangle collapsed by fixed-point setup produces no fragments");
}

}  // namespace

int main() {
    test_shared_edge_has_single_owner();
    test_subpixel_quantization_stability();
    test_subpixel_collapsed_triangle_is_rejected();

    if (failures != 0) {
        std::cerr << failures << " fixed-point test(s) failed\n";
        return 1;
    }
    std::cout << "all fixed-point coverage tests passed\n";
    return 0;
}
