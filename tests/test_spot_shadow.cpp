#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/rasterizer.hpp"
#include "tiny_renderer/spot_shadow.hpp"
#include "tiny_renderer/spot_shadow_renderer.hpp"
#include "tiny_renderer/texture.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 8.0e-3F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

Vertex normal_vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.0F, 0.0F, 1.0F});
}

Vertex uv_vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.5F, 0.5F});
}

Triangle triangle_at(float z, float half_extent = 0.65F, float x_offset = 0.0F) {
    return Triangle{{
        normal_vertex({x_offset - half_extent, -half_extent, z}),
        normal_vertex({x_offset + half_extent, -half_extent, z}),
        normal_vertex({x_offset, half_extent, z}),
    }};
}

Triangle uv_triangle_at(float z, float half_extent = 0.65F) {
    return Triangle{{
        uv_vertex({-half_extent, -half_extent, z}),
        uv_vertex({half_extent, -half_extent, z}),
        uv_vertex({0.0F, half_extent, z}),
    }};
}

MaterialState diffuse_material() {
    MaterialState material;
    material.albedo = {1.0F, 1.0F, 1.0F};
    material.specular = {0.0F, 0.0F, 0.0F};
    return material;
}

ModelAsset model_from_triangle(const Triangle& triangle) {
    ModelAsset asset;
    asset.mesh.vertices = {triangle[0], triangle[1], triangle[2]};
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "spot-shadow";
    draw.material = diffuse_material();
    asset.draws.push_back(draw);
    return asset;
}

ModelAsset opacity_model(
    const Triangle& triangle,
    std::shared_ptr<const Texture2D> opacity_texture) {
    ModelAsset asset;
    asset.mesh.vertices = {triangle[0], triangle[1], triangle[2]};
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "cutout";
    draw.material = diffuse_material();
    draw.opacity_texture = std::move(opacity_texture);
    asset.draws.push_back(std::move(draw));
    return asset;
}

SpotLight spot_light(float ambient = 0.1F, float diffuse = 0.6F) {
    SpotLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = {0.0F, 0.0F, 2.0F};
    light.direction = {0.0F, 0.0F, -1.0F};
    light.ambient = ambient;
    light.diffuse = diffuse;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    light.inner_cone_cos = 0.95F;
    light.outer_cone_cos = 0.8F;
    return light;
}

PointLight point_light() {
    PointLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = {0.0F, 0.0F, 2.0F};
    light.diffuse = 1.0F;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    return light;
}

FixedLight fixed_spot(const SpotLight& light) {
    FixedLight result;
    result.type = FixedLightType::Spot;
    result.spot = light;
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

std::shared_ptr<const SpotShadowMap> captured_occluder_map(const SpotLight& light) {
    const PreparedModelSubmission prepared = prepare_model_asset(
        model_from_triangle(triangle_at(1.0F)));
    const PreparedModelListEntry entries[] = {{&prepared, Mat4::identity()}};
    return render_spot_shadow_map(
        entries,
        light,
        SpotShadowMapOptions{65U, 0.1F, 10.0F});
}

std::shared_ptr<const SpotShadowMap> empty_map(const SpotLight& light) {
    return render_spot_shadow_map(
        {},
        light,
        SpotShadowMapOptions{65U, 0.1F, 10.0F});
}

ModelRenderOptions shadowed_options(
    const SpotLight& light,
    const std::shared_ptr<const SpotShadowMap>& map) {
    ModelRenderOptions options;
    options.fixed_lights = collection({fixed_spot(light)});
    options.fixed_lights.shadowed_spot_index = 0U;
    options.fixed_lights.spot_shadow_state = {true, map, 0.0F};
    return options;
}

Framebuffer render_receiver(const ModelRenderOptions& options) {
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        model_from_triangle(triangle_at(0.0F)),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);
    return framebuffer;
}

void test_capture_depth_and_identity() {
    const SpotLight light = spot_light();
    const auto map = captured_occluder_map(light);

    check(map->depth_texture().width() == 65U && map->depth_texture().height() == 65U,
          "spot shadow capture owns the requested square depth resource");
    check(map->depth_texture().depth_at(32U, 32U) < 1.0F,
          "spot shadow capture records center occluder depth through the existing raster path");
    check(map->light_position().x == light.position.x
              && map->light_position().y == light.position.y
              && map->light_position().z == light.position.z,
          "spot shadow resource retains the exact capture position");
    check_near(map->light_direction().x, 0.0F,
               "spot shadow resource stores normalized capture direction x", 1.0e-6F);
    check_near(map->light_direction().y, 0.0F,
               "spot shadow resource stores normalized capture direction y", 1.0e-6F);
    check_near(map->light_direction().z, -1.0F,
               "spot shadow resource stores normalized capture direction z", 1.0e-6F);
    check(map->outer_cone_cos() == light.outer_cone_cos,
          "spot shadow resource retains the exact capture outer cone cosine");

    const auto clear = empty_map(light);
    check(clear->depth_texture().depth_at(32U, 32U) == 1.0F,
          "empty spotlight capture preserves deterministic clear depth");
}

void test_occluded_and_unoccluded_camera_shading() {
    const SpotLight light = spot_light();
    const Framebuffer unoccluded = render_receiver(shadowed_options(light, empty_map(light)));
    const Framebuffer occluded = render_receiver(shadowed_options(light, captured_occluder_map(light)));

    check_near(
        unoccluded.color_at(32U, 32U).x,
        0.7F,
        "unoccluded spotlight shadow map preserves ambient plus direct lighting");
    check_near(
        occluded.color_at(32U, 32U).x,
        0.1F,
        "occluded spotlight suppresses only its direct response while preserving ambient");
}

void test_selective_mixed_spot_shadowing() {
    SpotLight first = spot_light(0.1F, 0.3F);
    SpotLight second = spot_light(0.1F, 0.3F);
    const auto map = captured_occluder_map(first);

    ModelRenderOptions options;
    options.fixed_lights = collection({fixed_spot(first), fixed_spot(second)});
    options.fixed_lights.shadowed_spot_index = 0U;
    options.fixed_lights.spot_shadow_state = {true, map, 0.0F};

    const Framebuffer framebuffer = render_receiver(options);
    check_near(
        framebuffer.color_at(32U, 32U).x,
        0.5F,
        "spot shadow suppresses only the associated spotlight direct term and leaves other lights independent");
}

void test_prepared_ownership_and_list_equivalence() {
    const SpotLight light = spot_light();
    std::shared_ptr<const SpotShadowMap> map = captured_occluder_map(light);
    std::weak_ptr<const SpotShadowMap> weak = map;
    ModelRenderOptions options = shadowed_options(light, map);
    const ModelAsset asset = model_from_triangle(triangle_at(0.0F));
    const PreparedModelSubmission prepared = prepare_model_asset(asset, options);

    options.fixed_lights.spot_shadow_state.map.reset();
    map.reset();
    check(!weak.expired(), "prepared model retains spotlight shadow resource lifetime");

    Framebuffer direct(65U, 65U);
    draw_prepared_model(
        direct,
        prepared,
        Mat4::identity(), Mat4::identity(), Mat4::identity());

    Framebuffer listed(65U, 65U);
    const PreparedModelListEntry entries[] = {{&prepared, Mat4::identity()}};
    draw_prepared_model_list(listed, entries, Mat4::identity(), Mat4::identity());
    check(listed.rgb8() == direct.rgb8(),
          "prepared-list spotlight shadow execution is byte-equivalent to prepared single submission");
    check(listed.fnv1a64() == direct.fnv1a64(),
          "prepared-list spotlight shadow execution is hash-equivalent to prepared single submission");
}

void test_invalid_associations_fail_closed() {
    const SpotLight light = spot_light();
    const auto map = captured_occluder_map(light);
    const ModelAsset asset = model_from_triangle(triangle_at(0.0F));

    {
        ModelRenderOptions options;
        options.fixed_lights = collection({fixed_point(point_light())});
        options.fixed_lights.shadowed_spot_index = 0U;
        options.fixed_lights.spot_shadow_state = {true, map, 0.0F};
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "spot shadow association cannot target a point-light record");
    }

    {
        ModelRenderOptions options;
        options.fixed_lights = collection({fixed_spot(light)});
        options.fixed_lights.shadowed_spot_index = 1U;
        options.fixed_lights.spot_shadow_state = {true, map, 0.0F};
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        check(threw, "spot shadow association outside the collection is rejected");
    }

    {
        ModelRenderOptions options;
        options.fixed_lights = collection({fixed_spot(light)});
        options.fixed_lights.shadowed_spot_index = 0U;
        options.fixed_lights.spot_shadow_state.enabled = true;
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "enabled spot shadow state requires an owned depth resource");
    }

    {
        ModelRenderOptions options = shadowed_options(light, map);
        options.fixed_lights.spot_shadow_state.bias = -0.01F;
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "negative spotlight shadow bias is rejected");
    }

    {
        SpotLight mismatch = light;
        mismatch.position.z = 3.0F;
        ModelRenderOptions options = shadowed_options(mismatch, map);
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "spot shadow capture position must match the associated spotlight");
    }

    {
        SpotLight mismatch = light;
        mismatch.direction = {0.1F, 0.0F, -1.0F};
        ModelRenderOptions options = shadowed_options(mismatch, map);
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "spot shadow capture direction must match the associated spotlight");
    }

    {
        SpotLight mismatch = light;
        mismatch.outer_cone_cos = 0.7F;
        ModelRenderOptions options = shadowed_options(mismatch, map);
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "spot shadow capture cone must match the associated spotlight");
    }
}

void test_capture_option_and_resource_validation() {
    const SpotLight light = spot_light();

    bool size_threw = false;
    try {
        (void)render_spot_shadow_map({}, light, SpotShadowMapOptions{0U, 0.1F, 10.0F});
    } catch (const std::invalid_argument&) {
        size_threw = true;
    }
    check(size_threw, "spot shadow capture rejects zero map size");

    bool near_far_threw = false;
    try {
        (void)render_spot_shadow_map({}, light, SpotShadowMapOptions{8U, 1.0F, 1.0F});
    } catch (const std::invalid_argument&) {
        near_far_threw = true;
    }
    check(near_far_threw, "spot shadow capture rejects non-increasing near/far planes");

    SpotLight wide = light;
    wide.outer_cone_cos = -0.1F;
    bool cone_threw = false;
    try {
        (void)render_spot_shadow_map({}, wide, SpotShadowMapOptions{8U, 0.1F, 10.0F});
    } catch (const std::invalid_argument&) {
        cone_threw = true;
    }
    check(cone_threw, "spot shadow capture rejects outer cones that cannot form the bounded perspective slice");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Mat4 bad_transform({
        nan, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    });
    bool transform_threw = false;
    try {
        (void)SpotShadowMap(
            light.position,
            light.direction,
            light.outer_cone_cos,
            bad_transform,
            DepthTexture2D(1U, 1U, std::vector<float>{1.0F}));
    } catch (const std::invalid_argument&) {
        transform_threw = true;
    }
    check(transform_threw, "spot shadow resource rejects a non-finite projection transform");
}

void test_alpha_tested_cutout_capture() {
    const SpotLight light = spot_light();
    const auto transparent = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{0.0F, 0.0F, 0.0F}});
    const auto opaque = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{1.0F, 1.0F, 1.0F}});

    ModelRenderOptions alpha_options;
    alpha_options.alpha_test_state = {true, 0.5F};
    const PreparedModelSubmission transparent_prepared = prepare_model_asset(
        opacity_model(uv_triangle_at(1.0F), transparent), alpha_options);
    const PreparedModelSubmission opaque_prepared = prepare_model_asset(
        opacity_model(uv_triangle_at(1.0F), opaque), alpha_options);

    const PreparedModelListEntry transparent_entries[] = {
        {&transparent_prepared, Mat4::identity()},
    };
    const PreparedModelListEntry opaque_entries[] = {
        {&opaque_prepared, Mat4::identity()},
    };
    const auto transparent_map = render_spot_shadow_map(
        transparent_entries, light, SpotShadowMapOptions{65U, 0.1F, 10.0F});
    const auto opaque_map = render_spot_shadow_map(
        opaque_entries, light, SpotShadowMapOptions{65U, 0.1F, 10.0F});

    check(transparent_map->depth_texture().depth_at(32U, 32U) == 1.0F,
          "alpha-tested transparent caster is discarded before spot-shadow depth ownership");
    check(opaque_map->depth_texture().depth_at(32U, 32U) < 1.0F,
          "alpha-tested opaque caster writes spotlight shadow depth through the shared path");
}

class ShiftXProgram final : public VertexProgram {
public:
    explicit ShiftXProgram(float delta) : delta_(delta) {}

    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        return {
            {input.position.x + delta_, input.position.y, input.position.z},
            input.varyings,
        };
    }

private:
    float delta_{};
};

void test_vertex_program_deformation_participates_in_capture() {
    const SpotLight light = spot_light();
    const ModelAsset source = model_from_triangle(triangle_at(1.0F, 0.65F, 3.0F));

    const PreparedModelSubmission baseline = prepare_model_asset(source);
    ModelRenderOptions moved_options;
    moved_options.vertex_program = std::make_shared<const ShiftXProgram>(-3.0F);
    const PreparedModelSubmission moved = prepare_model_asset(source, moved_options);

    const PreparedModelListEntry baseline_entries[] = {{&baseline, Mat4::identity()}};
    const PreparedModelListEntry moved_entries[] = {{&moved, Mat4::identity()}};
    const auto baseline_map = render_spot_shadow_map(
        baseline_entries, light, SpotShadowMapOptions{65U, 0.1F, 10.0F});
    const auto moved_map = render_spot_shadow_map(
        moved_entries, light, SpotShadowMapOptions{65U, 0.1F, 10.0F});

    check(baseline_map->depth_texture().depth_at(32U, 32U) == 1.0F,
          "off-cone undeformed geometry leaves the center spot-shadow depth clear");
    check(moved_map->depth_texture().depth_at(32U, 32U) < 1.0F,
          "vertex-program deformation is applied before spotlight shadow clipping and rasterization");
}

void test_whole_list_fail_closed_and_disabled_compatibility() {
    const SpotLight light = spot_light();
    const auto map = captured_occluder_map(light);
    ModelRenderOptions options = shadowed_options(light, map);
    const PreparedModelSubmission prepared = prepare_model_asset(
        model_from_triangle(triangle_at(0.0F)), options);

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
    fail_closed.clear({0.2F, 0.3F, 0.4F}, 0.9F, 17U);
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
    check(threw, "malformed later spotlight-shadow list entry rejects the complete list");
    check(fail_closed.rgb8() == before,
          "later spotlight-shadow preflight failure leaves earlier list color untouched");
    check(fail_closed.depth_at(32U, 32U) == before_depth,
          "later spotlight-shadow preflight failure leaves earlier list depth untouched");
    check(fail_closed.stencil_at(32U, 32U) == before_stencil,
          "later spotlight-shadow preflight failure leaves earlier list stencil untouched");

    ModelRenderOptions baseline_options;
    baseline_options.fixed_lights = collection({fixed_spot(light)});
    ModelRenderOptions disabled_options = baseline_options;
    disabled_options.fixed_lights.spot_shadow_state.map = map;
    const Framebuffer baseline = render_receiver(baseline_options);
    const Framebuffer disabled = render_receiver(disabled_options);
    check(disabled.rgb8() == baseline.rgb8(),
          "disabled spotlight shadow state preserves pre-M42 byte output even with an unused retained map");
}

}  // namespace

int main() {
    try {
        test_capture_depth_and_identity();
        test_occluded_and_unoccluded_camera_shading();
        test_selective_mixed_spot_shadowing();
        test_prepared_ownership_and_list_equivalence();
        test_invalid_associations_fail_closed();
        test_capture_option_and_resource_validation();
        test_alpha_tested_cutout_capture();
        test_vertex_program_deformation_participates_in_capture();
        test_whole_list_fail_closed_and_disabled_compatibility();
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED EXCEPTION: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " spotlight-shadow regression(s) failed\n";
        return 1;
    }
    std::cout << "spotlight-shadow tests passed\n";
    return 0;
}
