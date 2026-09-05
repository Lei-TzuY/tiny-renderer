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
#include "tiny_renderer/texture.hpp"

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

void check_color(const Vec3& actual, const Vec3& expected, const std::string& message, float epsilon = 2.0e-4F) {
    check_near(actual.x, expected.x, message + " red", epsilon);
    check_near(actual.y, expected.y, message + " green", epsilon);
    check_near(actual.z, expected.z, message + " blue", epsilon);
}

Vec3 xyz(const Vec4& value) {
    return {value.x, value.y, value.z};
}

Vertex colored_normal_vertex(const Vec3& position, const Vec3& color, const Vec3& normal) {
    return Vertex::with_varyings(position, VaryingPack{
        color.x, color.y, color.z,
        normal.x, normal.y, normal.z,
    });
}

Vertex uv_normal_vertex(const Vec3& position, float u, float v, const Vec3& normal) {
    return Vertex::with_varyings(position, VaryingPack{
        u, v, normal.x, normal.y, normal.z,
    });
}

DirectionalLight light_for(
    NormalBinding normal_binding,
    const Vec3& direction_to_light,
    float ambient = 0.0F,
    float diffuse = 1.0F) {
    return DirectionalLight{true, normal_binding, direction_to_light, ambient, diffuse};
}

void test_inverse_transpose_normal_matrix() {
    const Mat4 model = Mat4::scale({2.0F, 1.0F, 0.5F});
    const Vec3 object_normal = normalize(Vec3{1.0F, 0.0F, 1.0F});
    const Vec3 transformed = normalize(normal_matrix(model) * object_normal);
    const Vec3 expected = normalize(Vec3{0.5F, 0.0F, 2.0F});
    check_color(transformed, expected, "normal matrix uses inverse-transpose under non-uniform scale");

    const Vec3 object_tangent = normalize(Vec3{1.0F, 0.0F, -1.0F});
    const Vec3 world_tangent = xyz(model * Vec4{object_tangent.x, object_tangent.y, object_tangent.z, 0.0F});
    check_near(dot(transformed, world_tangent), 0.0F,
               "inverse-transpose normal remains perpendicular to transformed tangent");
}

void test_non_uniform_model_transform_lights_with_world_normal() {
    const Vec3 object_normal = normalize(Vec3{1.0F, 0.0F, 1.0F});
    const Mat4 model = Mat4::scale({2.0F, 1.0F, 0.5F});
    const Vec3 world_normal = normalize(normal_matrix(model) * object_normal);
    const Triangle triangle{{
        colored_normal_vertex({-0.25F, -0.25F, 0.0F}, {1.0F, 1.0F, 1.0F}, object_normal),
        colored_normal_vertex({0.25F, -0.25F, 0.0F}, {1.0F, 1.0F, 1.0F}, object_normal),
        colored_normal_vertex({0.0F, 0.35F, 0.0F}, {1.0F, 1.0F, 1.0F}, object_normal),
    }};

    Framebuffer framebuffer(33U, 33U);
    Rasterizer rasterizer(
        framebuffer,
        {},
        {},
        light_for({3U, 4U, 5U}, world_normal));
    rasterizer.draw_triangle(triangle, model, Mat4::identity(), Mat4::identity());

    check_color(framebuffer.color_at(16U, 17U), {1.0F, 1.0F, 1.0F},
                "Lambert lighting uses inverse-transpose transformed normal rather than model times normal");
}

void test_texture_color_is_modulated_by_lambert_intensity() {
    const Texture2D texture(1U, 1U, {{0.8F, 0.4F, 0.2F}});
    const TextureBinding texture_binding{&texture, 0U, 1U, {}};
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    const Triangle triangle{{
        uv_normal_vertex({-0.6F, -0.6F, 0.0F}, 0.0F, 0.0F, normal),
        uv_normal_vertex({0.6F, -0.6F, 0.0F}, 1.0F, 0.0F, normal),
        uv_normal_vertex({0.0F, 0.6F, 0.0F}, 0.5F, 1.0F, normal),
    }};

    Framebuffer framebuffer(33U, 33U);
    Rasterizer rasterizer(
        framebuffer,
        {},
        texture_binding,
        light_for({2U, 3U, 4U}, {0.0F, 0.0F, 1.0F}, 0.25F, 0.5F));
    rasterizer.draw_triangle(triangle, Mat4::identity(), Mat4::identity(), Mat4::identity());

    check_color(framebuffer.color_at(16U, 16U), {0.6F, 0.3F, 0.15F},
                "directional lighting modulates the existing textured fragment color");
}

Vertex lerp_vertex(const Vertex& a, const Vertex& b, float t) {
    VaryingPack varyings;
    varyings.count = a.varyings.count;
    varyings.interpolation = a.varyings.interpolation;
    for (std::size_t channel = 0U; channel < varyings.count; ++channel) {
        varyings.values[channel] = a.varyings.values[channel]
            + (b.varyings.values[channel] - a.varyings.values[channel]) * t;
    }
    return Vertex::with_varyings(
        a.position + (b.position - a.position) * t,
        varyings);
}

void test_lit_clipping_matches_manual_geometry() {
    const Vec3 white{1.0F, 1.0F, 1.0F};
    const Triangle crossing{{
        colored_normal_vertex({-1.5F, 0.0F, 0.0F}, white, normalize(Vec3{1.0F, 0.0F, 1.0F})),
        colored_normal_vertex({0.0F, -0.7F, 0.0F}, white, normalize(Vec3{0.0F, 0.0F, 1.0F})),
        colored_normal_vertex({0.0F, 0.7F, 0.0F}, white, normalize(Vec3{0.0F, 1.0F, 1.0F})),
    }};

    const float d2 = 1.0F + crossing[2].position.x;
    const float d0 = 1.0F + crossing[0].position.x;
    const float d1 = 1.0F + crossing[1].position.x;
    const float t20 = d2 / (d2 - d0);
    const float t01 = d0 / (d0 - d1);
    const Vertex intersection_20 = lerp_vertex(crossing[2], crossing[0], t20);
    const Vertex intersection_01 = lerp_vertex(crossing[0], crossing[1], t01);
    const Triangle manual_a{{intersection_20, intersection_01, crossing[1]}};
    const Triangle manual_b{{intersection_20, crossing[1], crossing[2]}};

    const DirectionalLight light = light_for({3U, 4U, 5U}, normalize(Vec3{0.2F, 0.3F, 1.0F}), 0.1F, 0.9F);

    Framebuffer automatic(65U, 65U);
    Rasterizer automatic_rasterizer(automatic, {}, {}, light);
    automatic_rasterizer.draw_triangle(crossing, Mat4::identity(), Mat4::identity(), Mat4::identity());

    Framebuffer manual(65U, 65U);
    Rasterizer manual_rasterizer(manual, {}, {}, light);
    manual_rasterizer.draw_triangle(manual_a, Mat4::identity(), Mat4::identity(), Mat4::identity());
    manual_rasterizer.draw_triangle(manual_b, Mat4::identity(), Mat4::identity(), Mat4::identity());

    check(automatic.rgb8() == manual.rgb8(),
          "lit clipping is byte-identical to equivalent explicitly clipped geometry");
}

void test_invalid_lighting_contracts_fail_before_framebuffer_mutation() {
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    const Triangle triangle{{
        colored_normal_vertex({-0.5F, -0.5F, 0.0F}, {1.0F, 1.0F, 1.0F}, normal),
        colored_normal_vertex({0.5F, -0.5F, 0.0F}, {1.0F, 1.0F, 1.0F}, normal),
        colored_normal_vertex({0.0F, 0.5F, 0.0F}, {1.0F, 1.0F, 1.0F}, normal),
    }};

    {
        Framebuffer framebuffer(33U, 33U);
        const std::uint64_t before = framebuffer.fnv1a64();
        Rasterizer rasterizer(framebuffer, {}, {}, light_for({3U, 4U, 6U}, {0.0F, 0.0F, 1.0F}));
        bool threw = false;
        try {
            rasterizer.draw_triangle(triangle, Mat4::identity(), Mat4::identity(), Mat4::identity());
        } catch (const std::out_of_range&) {
            threw = true;
        }
        check(threw, "out-of-range normal binding is rejected");
        check(framebuffer.fnv1a64() == before, "invalid normal binding is fail-closed");
    }

    {
        Framebuffer framebuffer(33U, 33U);
        const std::uint64_t before = framebuffer.fnv1a64();
        Rasterizer rasterizer(framebuffer, {}, {}, light_for({3U, 4U, 5U}, {0.0F, 0.0F, 1.0F}));
        bool threw = false;
        try {
            rasterizer.draw_triangle(triangle, Mat4::identity());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "lighting with precomposed MVP is rejected because model normal transform is unavailable");
        check(framebuffer.fnv1a64() == before, "lighting MVP-only overload is fail-closed");
    }

    {
        Framebuffer framebuffer(33U, 33U);
        const std::uint64_t before = framebuffer.fnv1a64();
        Rasterizer rasterizer(framebuffer, {}, {}, light_for({3U, 4U, 5U}, {0.0F, 0.0F, 1.0F}));
        bool threw = false;
        try {
            rasterizer.draw_triangle(
                triangle,
                Mat4::scale({0.0F, 1.0F, 1.0F}),
                Mat4::identity(),
                Mat4::identity());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "singular model transform is rejected when lighting needs a normal matrix");
        check(framebuffer.fnv1a64() == before, "singular normal transform is fail-closed");
    }
}

}  // namespace

int main() {
    try {
        test_inverse_transpose_normal_matrix();
        test_non_uniform_model_transform_lights_with_world_normal();
        test_texture_color_is_modulated_by_lambert_intensity();
        test_lit_clipping_matches_manual_geometry();
        test_invalid_lighting_contracts_fail_before_framebuffer_mutation();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " lighting test(s) failed\n";
        return 1;
    }
    std::cout << "all lighting tests passed\n";
    return 0;
}
