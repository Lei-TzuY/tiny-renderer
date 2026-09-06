#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 7.0e-3F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

Vertex normal_vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.0F, 0.0F, 1.0F});
}

Triangle canonical_triangle(float z = 0.0F) {
    return Triangle{{
        normal_vertex({-0.7F, -0.7F, z}),
        normal_vertex({0.7F, -0.7F, z}),
        normal_vertex({0.0F, 0.7F, z}),
    }};
}

MaterialState diffuse_material() {
    MaterialState material;
    material.albedo = {1.0F, 1.0F, 1.0F};
    material.specular = {0.0F, 0.0F, 0.0F};
    return material;
}

MaterialState specular_material() {
    MaterialState material;
    material.albedo = {1.0F, 1.0F, 1.0F};
    material.specular = {0.2F, 0.2F, 0.2F};
    material.shininess = 16.0F;
    return material;
}

ModelAsset model_from_triangle(const MaterialState& material) {
    const Triangle triangle = canonical_triangle();
    ModelAsset asset;
    asset.mesh.vertices = {triangle[0], triangle[1], triangle[2]};
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "spot";
    draw.material = material;
    asset.draws.push_back(draw);
    return asset;
}

SpotLight spot(float diffuse = 0.6F) {
    SpotLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = {0.0F, 0.0F, 2.0F};
    light.direction = {0.0F, 0.0F, -1.0F};
    light.ambient = 0.1F;
    light.diffuse = diffuse;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    light.inner_cone_cos = 0.9F;
    light.outer_cone_cos = 0.8F;
    return light;
}

DirectionalLight directional(float diffuse) {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.direction_to_light = {0.0F, 0.0F, 1.0F};
    light.diffuse = diffuse;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    return light;
}

PointLight point(float diffuse) {
    PointLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = {0.0F, 0.0F, 2.0F};
    light.diffuse = diffuse;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    return light;
}

FixedLight fixed_spot(const SpotLight& light) {
    FixedLight result;
    result.type = FixedLightType::Spot;
    result.spot = light;
    return result;
}

FixedLight fixed_directional(const DirectionalLight& light) {
    FixedLight result;
    result.type = FixedLightType::Directional;
    result.directional = light;
    return result;
}

FixedLight fixed_point(const PointLight& light) {
    FixedLight result;
    result.type = FixedLightType::Point;
    result.point = light;
    return result;
}

FixedLightCollection collection(std::initializer_list<FixedLight> lights) {
    FixedLightCollection result;
    result.count = lights.size();
    std::size_t index = 0U;
    for (const FixedLight& light : lights) {
        result.lights[index++] = light;
    }
    return result;
}

ModelRenderOptions options_for(const SpotLight& light) {
    ModelRenderOptions options;
    options.fixed_lights = collection({fixed_spot(light)});
    return options;
}

Framebuffer render_model(const ModelRenderOptions& options, const MaterialState& material) {
    Framebuffer framebuffer(64U, 64U);
    draw_model_asset(
        framebuffer,
        model_from_triangle(material),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return framebuffer;
}

void test_inside_transition_and_outside_cone() {
    {
        const Framebuffer framebuffer = render_model(options_for(spot()), diffuse_material());
        check_near(
            framebuffer.color_at(31U, 31U).x,
            0.7F,
            "inside-cone spotlight applies ambient plus full diffuse contribution");
    }

    {
        SpotLight light = spot();
        constexpr float kTransitionCos = 0.85F;
        const float x = std::sqrt(1.0F - kTransitionCos * kTransitionCos);
        light.direction = {x, 0.0F, -kTransitionCos};
        const Framebuffer framebuffer = render_model(options_for(light), diffuse_material());
        check_near(
            framebuffer.color_at(31U, 31U).x,
            0.4F,
            "spotlight transition cone linearly interpolates in cosine space");
    }

    {
        SpotLight light = spot();
        light.direction = {1.0F, 0.0F, 0.0F};
        const Framebuffer framebuffer = render_model(options_for(light), diffuse_material());
        check_near(
            framebuffer.color_at(31U, 31U).x,
            0.1F,
            "outside-cone spotlight preserves ambient while suppressing direct lighting");
    }
}

void test_distance_attenuation_composes_with_cone() {
    SpotLight light = spot();
    light.linear_attenuation = 0.5F;
    const Framebuffer framebuffer = render_model(options_for(light), diffuse_material());
    check_near(
        framebuffer.color_at(31U, 31U).x,
        0.4F,
        "spotlight distance attenuation multiplies direct cone lighting but not ambient");
}

void test_blinn_phong_specular_uses_spot_direct_factor() {
    SpotLight light = spot(0.5F);
    light.ambient = 0.0F;
    const Framebuffer framebuffer = render_model(options_for(light), specular_material());
    check_near(
        framebuffer.color_at(31U, 31U).x,
        0.6F,
        "spotlight Blinn-Phong specular shares the cone and attenuation direct factor");
}

void test_mixed_fixed_light_collection_accumulates() {
    SpotLight s = spot(0.2F);
    s.ambient = 0.0F;
    ModelRenderOptions options;
    options.fixed_lights = collection({
        fixed_directional(directional(0.2F)),
        fixed_point(point(0.2F)),
        fixed_spot(s),
    });
    const Framebuffer framebuffer = render_model(options, diffuse_material());
    check_near(
        framebuffer.color_at(31U, 31U).x,
        0.6F,
        "directional, point, and spot records accumulate in caller collection order");
}

class SquareFixedRgb final : public FragmentProgram {
public:
    FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
        return {
            {
                input.fixed_rgb.x * input.fixed_rgb.x,
                input.fixed_rgb.y * input.fixed_rgb.y,
                input.fixed_rgb.z * input.fixed_rgb.z,
            },
            input.fixed_opacity,
            false,
        };
    }
};

void test_fragment_program_runs_after_spotlight_shading() {
    SpotLight light = spot(0.5F);
    light.ambient = 0.0F;
    ModelRenderOptions options = options_for(light);
    options.fragment_program = std::make_shared<const SquareFixedRgb>();
    const Framebuffer framebuffer = render_model(options, diffuse_material());
    check_near(
        framebuffer.color_at(31U, 31U).x,
        0.25F,
        "fragment program observes the completed spotlight fixed-light result");
}

void test_direct_range_matches_model_submission() {
    SpotLight light = spot(0.5F);
    light.ambient = 0.0F;
    const MaterialState material = diffuse_material();
    const ModelAsset asset = model_from_triangle(material);
    const FixedLightCollection lights = collection({fixed_spot(light)});

    Framebuffer range_framebuffer(64U, 64U);
    Rasterizer rasterizer(
        range_framebuffer,
        {},
        {},
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
        {},
        {},
        {},
        {},
        {},
        lights,
        {});
    rasterizer.draw_mesh_range(
        asset.mesh,
        {0U, 1U},
        Mat4::identity(), Mat4::identity(), Mat4::identity());

    ModelRenderOptions options;
    options.fixed_lights = lights;
    const Framebuffer model_framebuffer = render_model(options, material);
    check(
        range_framebuffer.rgb8() == model_framebuffer.rgb8(),
        "direct draw range and ModelAsset submission share the spotlight raster path");
    check(
        range_framebuffer.fnv1a64() == model_framebuffer.fnv1a64(),
        "direct draw range and ModelAsset spotlight hashes match");
}

void expect_prepare_rejects(
    const SpotLight& light,
    const std::string& message) {
    ModelRenderOptions options = options_for(light);
    bool threw = false;
    try {
        (void)prepare_model_asset(model_from_triangle(diffuse_material()), options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, message);
}

void test_invalid_spotlight_state_fails_closed() {
    {
        SpotLight light = spot();
        light.direction = {0.0F, 0.0F, 0.0F};
        expect_prepare_rejects(light, "zero spotlight direction is rejected during static preparation");
    }
    {
        SpotLight light = spot();
        light.inner_cone_cos = light.outer_cone_cos;
        expect_prepare_rejects(light, "degenerate spotlight cone is rejected during static preparation");
    }
    {
        SpotLight light = spot();
        light.outer_cone_cos = -1.1F;
        expect_prepare_rejects(light, "spotlight cone cosine outside [-1,1] is rejected");
    }
    {
        SpotLight light = spot();
        light.linear_attenuation = -0.1F;
        expect_prepare_rejects(light, "negative spotlight attenuation is rejected");
    }
    {
        SpotLight light = spot();
        light.position.x = std::numeric_limits<float>::infinity();
        expect_prepare_rejects(light, "non-finite spotlight position is rejected");
    }

    {
        ModelRenderOptions options = options_for(spot());
        options.fixed_lights.lights[0].point = point(0.1F);
        bool threw = false;
        try {
            (void)prepare_model_asset(model_from_triangle(diffuse_material()), options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "spot collection record rejects an enabled point payload");
    }

    {
        ModelRenderOptions options = options_for(spot());
        options.fixed_lights.shadowed_point_index = 0U;
        options.point_shadow_state.enabled = true;
        bool threw = false;
        try {
            (void)prepare_model_asset(model_from_triangle(diffuse_material()), options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "point-shadow association cannot target a spotlight record");
    }
}

void test_prepared_list_equivalence_and_late_fail_closed() {
    ModelRenderOptions options = options_for(spot(0.5F));
    options.fixed_lights.lights[0].spot.ambient = 0.0F;
    const ModelAsset asset = model_from_triangle(diffuse_material());
    const PreparedModelSubmission prepared = prepare_model_asset(asset, options);

    Framebuffer direct(64U, 64U);
    draw_model_asset(
        direct,
        asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);

    Framebuffer listed(64U, 64U);
    const PreparedModelListEntry one[] = {{&prepared, Mat4::identity()}};
    draw_prepared_model_list(listed, one, Mat4::identity(), Mat4::identity());
    check(
        listed.rgb8() == direct.rgb8(),
        "prepared-list spotlight execution is byte-equivalent to direct model submission");

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
    Framebuffer fail_closed(64U, 64U);
    fail_closed.clear({0.2F, 0.3F, 0.4F}, 0.9F, 13U);
    const auto before = fail_closed.rgb8();
    const float before_depth = fail_closed.depth_at(31U, 31U);
    const std::uint8_t before_stencil = fail_closed.stencil_at(31U, 31U);

    bool threw = false;
    try {
        draw_prepared_model_list(
            fail_closed,
            entries,
            Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a malformed later spotlight world transform rejects the complete prepared list");
    check(fail_closed.rgb8() == before,
          "later spotlight preflight failure leaves earlier list color untouched");
    check(fail_closed.depth_at(31U, 31U) == before_depth,
          "later spotlight preflight failure leaves earlier list depth untouched");
    check(fail_closed.stencil_at(31U, 31U) == before_stencil,
          "later spotlight preflight failure leaves earlier list stencil untouched");
}

}  // namespace

int main() {
    try {
        test_inside_transition_and_outside_cone();
        test_distance_attenuation_composes_with_cone();
        test_blinn_phong_specular_uses_spot_direct_factor();
        test_mixed_fixed_light_collection_accumulates();
        test_fragment_program_runs_after_spotlight_shading();
        test_direct_range_matches_model_submission();
        test_invalid_spotlight_state_fails_closed();
        test_prepared_list_equivalence_and_late_fail_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " spotlight test(s) failed\n";
        return 1;
    }
    std::cout << "all spotlight tests passed\n";
    return 0;
}
