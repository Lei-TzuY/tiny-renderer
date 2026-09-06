#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/model_renderer.hpp"
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

void check_near(
    float actual,
    float expected,
    const std::string& message,
    float epsilon = 4.0e-3F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

Vertex normal_vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.0F, 0.0F, 1.0F});
}

Triangle canonical_triangle() {
    return Triangle{{
        normal_vertex({-0.7F, -0.7F, 0.0F}),
        normal_vertex({0.7F, -0.7F, 0.0F}),
        normal_vertex({0.0F, 0.7F, 0.0F}),
    }};
}

Vertex uv_normal_vertex(
    const Vec3& position,
    float u,
    float v,
    const Vec3& normal) {
    return Vertex::with_varyings(position, VaryingPack{
        u, v, normal.x, normal.y, normal.z,
    });
}

Triangle uv_triangle() {
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    return Triangle{{
        uv_normal_vertex({-0.7F, -0.7F, 0.0F}, 0.0F, 0.0F, normal),
        uv_normal_vertex({0.7F, -0.7F, 0.0F}, 1.0F, 0.0F, normal),
        uv_normal_vertex({0.0F, 0.7F, 0.0F}, 0.5F, 1.0F, normal),
    }};
}

PointLight point_light(
    const Vec3& position,
    const Vec3& viewer_position = {0.0F, 0.0F, 4.0F}) {
    PointLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = position;
    light.ambient = 0.0F;
    light.diffuse = 1.0F;
    light.viewer_position = viewer_position;
    return light;
}

Rasterizer point_rasterizer(
    Framebuffer& framebuffer,
    const MaterialState& material,
    const PointLight& light,
    TextureBinding texture_binding = {},
    ShadowState shadow_state = {}) {
    return Rasterizer(
        framebuffer,
        {},
        texture_binding,
        {},
        material,
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        {},
        {},
        {},
        {},
        {},
        shadow_state,
        {},
        {},
        {},
        light);
}

float render_center_red(
    const MaterialState& material,
    const PointLight& light,
    TextureBinding texture_binding = {}) {
    Framebuffer framebuffer(65U, 65U);
    Rasterizer rasterizer = point_rasterizer(
        framebuffer, material, light, texture_binding);
    const Triangle triangle = texture_binding.normal_texture != nullptr
        ? uv_triangle()
        : canonical_triangle();
    rasterizer.draw_triangle(
        triangle,
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    return framebuffer.color_at(32U, 32U).x;
}

ModelAsset model_from_triangle(const MaterialState& material) {
    const Triangle triangle = canonical_triangle();
    ModelAsset asset;
    asset.mesh.vertices = {triangle[0], triangle[1], triangle[2]};
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "point";
    draw.material = material;
    asset.draws.push_back(std::move(draw));
    return asset;
}

ModelRenderOptions point_options(const PointLight& light) {
    ModelRenderOptions options;
    options.point_light = light;
    return options;
}

void test_inverse_distance_polynomial_attenuation() {
    MaterialState material;
    material.albedo = {1.0F, 1.0F, 1.0F};
    material.specular = {0.0F, 0.0F, 0.0F};

    PointLight unattenuated = point_light({0.0F, 0.0F, 2.0F});
    PointLight attenuated = unattenuated;
    attenuated.quadratic_attenuation = 0.25F;

    const float full = render_center_red(material, unattenuated);
    const float half = render_center_red(material, attenuated);
    check_near(full, 1.0F,
               "zero point attenuation preserves the full center Lambert response");
    check_near(half, 0.5F,
               "quadratic point attenuation follows 1/(1 + q*d^2) at the center sample");
}

void test_point_position_changes_lambert_response() {
    MaterialState material;
    material.albedo = {1.0F, 1.0F, 1.0F};

    const float in_front = render_center_red(
        material,
        point_light({0.0F, 0.0F, 2.0F}));
    const float behind = render_center_red(
        material,
        point_light({0.0F, 0.0F, -2.0F}));

    check(in_front > 0.98F,
          "point light in front of the +Z normal produces a bright Lambert response");
    check(behind < 0.01F,
          "point light behind the +Z normal produces no diffuse response");
}

void test_point_specular_remains_view_dependent() {
    MaterialState material;
    material.albedo = {0.0F, 0.0F, 0.0F};
    material.specular = {1.0F, 1.0F, 1.0F};
    material.shininess = 32.0F;

    const float aligned = render_center_red(
        material,
        point_light({0.0F, 0.0F, 2.0F}, {0.0F, 0.0F, 4.0F}));
    const float off_axis = render_center_red(
        material,
        point_light({0.0F, 0.0F, 2.0F}, {4.0F, 0.0F, 4.0F}));

    check(aligned > 0.9F,
          "point-light Blinn-Phong specular is strong for an aligned viewer");
    check(off_axis < aligned * 0.5F,
          "point-light Blinn-Phong specular falls when the viewer moves off axis");
}

void test_point_light_reuses_tangent_space_normal_mapping() {
    const Texture2D normal_map(1U, 1U, {{1.0F, 0.5F, 0.5F}});
    TextureBinding binding{};
    binding.u_channel = 0U;
    binding.v_channel = 1U;
    binding.normal_texture = &normal_map;

    MaterialState material;
    material.albedo = {1.0F, 1.0F, 1.0F};

    PointLight mapped_light = point_light({2.0F, 0.0F, 0.0F});
    mapped_light.normal = {2U, 3U, 4U};
    const PointLight geometric_light = point_light({2.0F, 0.0F, 0.0F});
    const float mapped = render_center_red(material, mapped_light, binding);
    const float geometric = render_center_red(material, geometric_light);

    check(mapped > 0.98F,
          "tangent +X normal map aligns the center sample with a +X point light");
    check(geometric < 0.02F,
          "geometric +Z normal remains dark for an in-plane +X point light");
}

void test_invalid_fixed_light_combinations_fail_closed() {
    MaterialState material;
    material.albedo = {1.0F, 1.0F, 1.0F};

    DirectionalLight directional;
    directional.enabled = true;
    directional.normal = {0U, 1U, 2U};
    directional.direction_to_light = {0.0F, 0.0F, 1.0F};
    directional.diffuse = 1.0F;

    {
        ModelRenderOptions options = point_options(point_light({0.0F, 0.0F, 2.0F}));
        options.directional_light = directional;
        bool threw = false;
        try {
            (void)prepare_model_asset(model_from_triangle(material), options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "prepared model construction rejects simultaneous directional and point lights");
    }

    {
        ModelRenderOptions options = point_options(point_light({0.0F, 0.0F, 2.0F}));
        options.shadow_state.enabled = true;
        bool threw = false;
        try {
            (void)prepare_model_asset(model_from_triangle(material), options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "directional shadow state is rejected for a point-only light");
    }

    {
        PointLight invalid = point_light({0.0F, 0.0F, 2.0F});
        invalid.linear_attenuation = -0.1F;
        bool threw = false;
        try {
            (void)prepare_model_asset(
                model_from_triangle(material), point_options(invalid));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "negative point-light attenuation is rejected during static preparation");
    }
}

void test_point_light_mvp_only_submission_fails_before_writes() {
    MaterialState material;
    material.albedo = {1.0F, 1.0F, 1.0F};
    ModelRenderOptions options = point_options(point_light({0.0F, 0.0F, 2.0F}));

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 7U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);

    bool threw = false;
    try {
        draw_model_asset(
            framebuffer,
            model_from_triangle(material),
            Mat4::identity(),
            options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw,
          "point-light model submission rejects an MVP-only transform");
    check(framebuffer.rgb8() == before,
          "MVP-only point-light rejection happens before color mutation");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "MVP-only point-light rejection happens before depth mutation");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "MVP-only point-light rejection happens before stencil mutation");
}

void test_prepared_list_preflights_later_bad_world_transform_before_writes() {
    MaterialState material;
    material.albedo = {1.0F, 1.0F, 1.0F};
    const PreparedModelSubmission prepared = prepare_model_asset(
        model_from_triangle(material),
        point_options(point_light({0.0F, 0.0F, 2.0F})));

    const Mat4 invalid_projective({
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 0.0F,
    });
    const PreparedModelListEntry entries[] = {
        {&prepared, Mat4::identity()},
        {&prepared, invalid_projective},
    };

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 11U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);

    bool threw = false;
    try {
        draw_prepared_model_list(
            framebuffer,
            entries,
            Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    check(threw,
          "later prepared-list point-light world transform is rejected");
    check(framebuffer.rgb8() == before,
          "later point-light transform rejection happens before earlier color writes");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "later point-light transform rejection happens before earlier depth writes");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "later point-light transform rejection happens before earlier stencil writes");
}

}  // namespace

int main() {
    try {
        test_inverse_distance_polynomial_attenuation();
        test_point_position_changes_lambert_response();
        test_point_specular_remains_view_dependent();
        test_point_light_reuses_tangent_space_normal_mapping();
        test_invalid_fixed_light_combinations_fail_closed();
        test_point_light_mvp_only_submission_fails_before_writes();
        test_prepared_list_preflights_later_bad_world_transform_before_writes();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " point-light test(s) failed\n";
        return 1;
    }
    std::cout << "all point-light tests passed\n";
    return 0;
}