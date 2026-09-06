#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/rasterizer.hpp"
#include "tiny_renderer/shadow.hpp"
#include "tiny_renderer/vertex_program.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check_near(float actual, float expected, const std::string& message, float epsilon = 5.0e-3F) {
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

ModelAsset model_from_triangle(const MaterialState& material) {
    const Triangle triangle = canonical_triangle();
    ModelAsset asset;
    asset.mesh.vertices = {triangle[0], triangle[1], triangle[2]};
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "multi";
    draw.material = material;
    asset.draws.push_back(draw);
    return asset;
}

DirectionalLight directional(float diffuse = 1.0F) {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.direction_to_light = {0.0F, 0.0F, 1.0F};
    light.ambient = 0.0F;
    light.diffuse = diffuse;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    return light;
}

PointLight point(float diffuse = 1.0F) {
    PointLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = {0.0F, 0.0F, 2.0F};
    light.ambient = 0.0F;
    light.diffuse = diffuse;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    return light;
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

MaterialState diffuse_material() {
    MaterialState material;
    material.albedo = {1.0F, 1.0F, 1.0F};
    material.specular = {0.0F, 0.0F, 0.0F};
    return material;
}

MaterialState specular_material() {
    MaterialState material;
    material.albedo = {0.2F, 0.3F, 0.4F};
    material.specular = {0.35F, 0.2F, 0.1F};
    material.shininess = 16.0F;
    return material;
}

Framebuffer render_model(const ModelRenderOptions& options, const MaterialState& material) {
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        model_from_triangle(material),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return framebuffer;
}

void check_single_light_collection_matches_legacy(
    const MaterialState& material,
    const std::string& material_label) {
    ModelRenderOptions legacy_directional;
    legacy_directional.directional_light = directional(0.75F);
    ModelRenderOptions collection_directional;
    collection_directional.fixed_lights = collection({fixed_directional(directional(0.75F))});

    const Framebuffer legacy_d = render_model(legacy_directional, material);
    const Framebuffer collection_d = render_model(collection_directional, material);
    check(collection_d.rgb8() == legacy_d.rgb8(),
          "one directional collection light is byte-equivalent to the legacy directional path for "
              + material_label);
    check(collection_d.fnv1a64() == legacy_d.fnv1a64(),
          "one directional collection light is hash-equivalent to the legacy directional path for "
              + material_label);

    ModelRenderOptions legacy_point;
    legacy_point.point_light = point(0.75F);
    ModelRenderOptions collection_point;
    collection_point.fixed_lights = collection({fixed_point(point(0.75F))});

    const Framebuffer legacy_p = render_model(legacy_point, material);
    const Framebuffer collection_p = render_model(collection_point, material);
    check(collection_p.rgb8() == legacy_p.rgb8(),
          "one point collection light is byte-equivalent to the legacy point path for "
              + material_label);
    check(collection_p.fnv1a64() == legacy_p.fnv1a64(),
          "one point collection light is hash-equivalent to the legacy point path for "
              + material_label);
}

void test_single_light_collection_matches_legacy() {
    check_single_light_collection_matches_legacy(diffuse_material(), "diffuse-only material");
    check_single_light_collection_matches_legacy(specular_material(), "Blinn-Phong material");
}

void test_two_point_lights_accumulate_analytically() {
    ModelRenderOptions options;
    options.fixed_lights = collection({fixed_point(point(0.25F)), fixed_point(point(0.25F))});
    const Framebuffer framebuffer = render_model(options, diffuse_material());
    check_near(
        framebuffer.color_at(32U, 32U).x,
        0.5F,
        "two caller-ordered point lights accumulate their Lambert contributions");
}

void test_directional_shadow_only_modulates_associated_light() {
    ModelRenderOptions options;
    options.fixed_lights = collection({
        fixed_directional(directional(0.5F)),
        fixed_point(point(0.25F)),
    });
    options.fixed_lights.shadowed_directional_index = 0U;
    options.shadow_state.enabled = true;
    options.shadow_state.map = std::make_shared<const DepthTexture2D>(
        1U, 1U, std::vector<float>{0.0F});
    options.shadow_state.light_view_projection = Mat4::identity();

    const Framebuffer framebuffer = render_model(options, diffuse_material());
    check_near(
        framebuffer.color_at(32U, 32U).x,
        0.25F,
        "a shadowed directional contribution is suppressed while the point contribution remains");
}

class SquareAccumulatedRgb final : public FragmentProgram {
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

void test_fragment_program_runs_after_accumulation() {
    ModelRenderOptions options;
    options.fixed_lights = collection({fixed_point(point(0.25F)), fixed_point(point(0.25F))});
    options.fragment_program = std::make_shared<const SquareAccumulatedRgb>();
    const Framebuffer framebuffer = render_model(options, diffuse_material());
    check_near(
        framebuffer.color_at(32U, 32U).x,
        0.25F,
        "fragment program receives the completed 0.5 multi-light result exactly once per sample");
}

class LiftVertices final : public VertexProgram {
public:
    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        VertexProgramOutput output{input.position, input.varyings};
        output.position.z += 1.0F;
        return output;
    }
};

void test_vertex_program_precedes_multi_light_world_position() {
    PointLight light = point(1.0F);
    light.quadratic_attenuation = 0.25F;
    ModelRenderOptions options;
    options.fixed_lights = collection({fixed_point(light)});
    options.vertex_program = std::make_shared<const LiftVertices>();

    const Framebuffer framebuffer = render_model(options, diffuse_material());
    check_near(
        framebuffer.color_at(32U, 32U).x,
        0.8F,
        "vertex-program deformation feeds the world position used by point-light attenuation");
}

void test_collection_validation_fails_closed() {
    const ModelAsset asset = model_from_triangle(diffuse_material());

    {
        ModelRenderOptions options;
        options.fixed_lights.count = kMaxFixedLights + 1U;
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "fixed-light collections larger than the documented capacity are rejected");
    }

    {
        ModelRenderOptions options;
        options.directional_light = directional();
        options.fixed_lights = collection({fixed_point(point())});
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "legacy and collection fixed-light state cannot be active simultaneously");
    }

    {
        DirectionalLight a = directional();
        PointLight b = point();
        b.normal = {0U, 2U, 1U};
        ModelRenderOptions options;
        options.fixed_lights = collection({fixed_directional(a), fixed_point(b)});
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "collection lights with different normal bindings are rejected before execution");
    }

    {
        ModelRenderOptions options;
        options.fixed_lights = collection({fixed_point(point())});
        options.fixed_lights.shadowed_directional_index = 0U;
        options.shadow_state.enabled = true;
        options.shadow_state.map = std::make_shared<const DepthTexture2D>(
            1U, 1U, std::vector<float>{1.0F});
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "shadow association cannot target a point light");
    }
}

void test_prepared_and_list_equivalence_and_late_fail_closed() {
    ModelRenderOptions options;
    options.fixed_lights = collection({fixed_directional(directional(0.25F)), fixed_point(point(0.25F))});
    const ModelAsset asset = model_from_triangle(diffuse_material());
    const PreparedModelSubmission prepared = prepare_model_asset(asset, options);

    Framebuffer direct(65U, 65U);
    draw_model_asset(
        direct, asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(), options);

    Framebuffer listed(65U, 65U);
    const PreparedModelListEntry one[] = {{&prepared, Mat4::identity()}};
    draw_prepared_model_list(listed, one, Mat4::identity(), Mat4::identity());
    check(listed.rgb8() == direct.rgb8(),
          "prepared-list multi-light execution is byte-equivalent to direct model submission");

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
    Framebuffer fail_closed(65U, 65U);
    fail_closed.clear({0.2F, 0.3F, 0.4F}, 0.9F, 13U);
    const auto before = fail_closed.rgb8();
    const float before_depth = fail_closed.depth_at(32U, 32U);
    const std::uint8_t before_stencil = fail_closed.stencil_at(32U, 32U);

    bool threw = false;
    try {
        draw_prepared_model_list(
            fail_closed,
            entries,
            Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a malformed later multi-light world transform rejects the complete prepared list");
    check(fail_closed.rgb8() == before,
          "later multi-light preflight failure leaves earlier list color untouched");
    check(fail_closed.depth_at(32U, 32U) == before_depth,
          "later multi-light preflight failure leaves earlier list depth untouched");
    check(fail_closed.stencil_at(32U, 32U) == before_stencil,
          "later multi-light preflight failure leaves earlier list stencil untouched");
}

}  // namespace

int main() {
    try {
        test_single_light_collection_matches_legacy();
        test_two_point_lights_accumulate_analytically();
        test_directional_shadow_only_modulates_associated_light();
        test_fragment_program_runs_after_accumulation();
        test_vertex_program_precedes_multi_light_world_position();
        test_collection_validation_fails_closed();
        test_prepared_and_list_equivalence_and_late_fail_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " multi-light test(s) failed\n";
        return 1;
    }
    std::cout << "all multi-light tests passed\n";
    return 0;
}
