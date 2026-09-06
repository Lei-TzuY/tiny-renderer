#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/point_shadow.hpp"
#include "tiny_renderer/point_shadow_renderer.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 5.0e-3F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

Vertex normal_vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.0F, 0.0F, 1.0F});
}

Triangle camera_triangle(float z = 0.0F) {
    return Triangle{{
        normal_vertex({-0.7F, -0.7F, z}),
        normal_vertex({0.7F, -0.7F, z}),
        normal_vertex({0.0F, 0.7F, z}),
    }};
}

Triangle negative_z_occluder() {
    return Triangle{{
        normal_vertex({-0.7F, -0.7F, -2.0F}),
        normal_vertex({0.7F, -0.7F, -2.0F}),
        normal_vertex({0.0F, 0.7F, -2.0F}),
    }};
}

ModelAsset model_from_triangle(const Triangle& triangle) {
    ModelAsset asset;
    asset.mesh.vertices = {triangle[0], triangle[1], triangle[2]};
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "point-shadow";
    draw.material.albedo = {1.0F, 1.0F, 1.0F};
    draw.material.specular = {0.0F, 0.0F, 0.0F};
    asset.draws.push_back(draw);
    return asset;
}

PointLight point_light(float ambient, float diffuse, float z = 2.0F) {
    PointLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.position = {0.0F, 0.0F, z};
    light.ambient = ambient;
    light.diffuse = diffuse;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    return light;
}

DirectionalLight directional_light() {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.direction_to_light = {0.0F, 0.0F, 1.0F};
    light.diffuse = 1.0F;
    light.viewer_position = {0.0F, 0.0F, 4.0F};
    return light;
}

FixedLight fixed_point(const PointLight& light) {
    FixedLight result;
    result.type = FixedLightType::Point;
    result.point = light;
    return result;
}

FixedLight fixed_directional(const DirectionalLight& light) {
    FixedLight result;
    result.type = FixedLightType::Directional;
    result.directional = light;
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

std::array<Mat4, kCubemapFaceCount> copy_face_transforms(const DepthCubemap& source) {
    return {
        source.face_view_projection(CubemapFace::PositiveX),
        source.face_view_projection(CubemapFace::NegativeX),
        source.face_view_projection(CubemapFace::PositiveY),
        source.face_view_projection(CubemapFace::NegativeY),
        source.face_view_projection(CubemapFace::PositiveZ),
        source.face_view_projection(CubemapFace::NegativeZ),
    };
}

std::shared_ptr<const DepthCubemap> fully_occluding_cubemap(const Vec3& light_position) {
    const auto reference = render_point_shadow_cubemap(
        {},
        light_position,
        PointShadowCubemapOptions{1U, 0.1F, 10.0F});
    return std::make_shared<const DepthCubemap>(
        1U,
        reference->light_position(),
        copy_face_transforms(*reference),
        std::vector<float>(kCubemapFaceCount, 0.0F));
}

void test_resource_and_face_addressing() {
    check(cubemap_face_for_direction({1.0F, 0.0F, 0.0F}) == CubemapFace::PositiveX,
          "+X direction selects the positive-X face");
    check(cubemap_face_for_direction({-1.0F, 0.0F, 0.0F}) == CubemapFace::NegativeX,
          "-X direction selects the negative-X face");
    check(cubemap_face_for_direction({0.0F, 1.0F, 0.0F}) == CubemapFace::PositiveY,
          "+Y direction selects the positive-Y face");
    check(cubemap_face_for_direction({0.0F, -1.0F, 0.0F}) == CubemapFace::NegativeY,
          "-Y direction selects the negative-Y face");
    check(cubemap_face_for_direction({0.0F, 0.0F, 1.0F}) == CubemapFace::PositiveZ,
          "+Z direction selects the positive-Z face");
    check(cubemap_face_for_direction({0.0F, 0.0F, -1.0F}) == CubemapFace::NegativeZ,
          "-Z direction selects the negative-Z face");
    check(cubemap_face_for_direction({1.0F, 1.0F, 1.0F}) == CubemapFace::PositiveX,
          "dominant-axis ties deterministically prefer X before Y before Z");
    check(cubemap_face_for_direction({0.0F, 1.0F, 1.0F}) == CubemapFace::PositiveY,
          "Y/Z dominant-axis ties deterministically prefer Y");

    bool zero_threw = false;
    try {
        (void)cubemap_face_for_direction({0.0F, 0.0F, 0.0F});
    } catch (const std::invalid_argument&) {
        zero_threw = true;
    }
    check(zero_threw, "zero cubemap directions are rejected");

    std::array<Mat4, kCubemapFaceCount> transforms{};
    transforms.fill(Mat4::identity());
    bool depth_threw = false;
    try {
        (void)DepthCubemap(
            1U,
            {0.0F, 0.0F, 0.0F},
            transforms,
            std::vector<float>(kCubemapFaceCount, -0.1F));
    } catch (const std::invalid_argument&) {
        depth_threw = true;
    }
    check(depth_threw, "depth cubemap construction rejects depths outside [0,1]");

    bool position_threw = false;
    try {
        (void)DepthCubemap(
            1U,
            {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
            transforms,
            std::vector<float>(kCubemapFaceCount, 1.0F));
    } catch (const std::invalid_argument&) {
        position_threw = true;
    }
    check(position_threw, "depth cubemap construction rejects a non-finite capture origin");
}

void test_six_face_capture_uses_existing_depth_path() {
    const PreparedModelSubmission prepared = prepare_model_asset(
        model_from_triangle(negative_z_occluder()));
    const PreparedModelListEntry entries[] = {{&prepared, Mat4::identity()}};
    const auto cubemap = render_point_shadow_cubemap(
        entries,
        {0.0F, 0.0F, 0.0F},
        PointShadowCubemapOptions{65U, 0.1F, 10.0F});

    check(cubemap->light_position().x == 0.0F
              && cubemap->light_position().y == 0.0F
              && cubemap->light_position().z == 0.0F,
          "point shadow cubemap retains the exact capture light position");
    check(cubemap->depth_at(CubemapFace::NegativeZ, 32U, 32U) < 1.0F,
          "negative-Z face captures the visible occluder through the existing depth raster path");
    check(cubemap->depth_at(CubemapFace::PositiveZ, 32U, 32U) == 1.0F,
          "opposite positive-Z face remains at the clear depth");

    bool near_far_threw = false;
    try {
        (void)render_point_shadow_cubemap(
            entries,
            {0.0F, 0.0F, 0.0F},
            PointShadowCubemapOptions{8U, 1.0F, 1.0F});
    } catch (const std::invalid_argument&) {
        near_far_threw = true;
    }
    check(near_far_threw, "point shadow capture rejects non-increasing near/far planes");
}

ModelRenderOptions shadowed_two_point_options(
    const std::shared_ptr<const DepthCubemap>& map) {
    ModelRenderOptions options;
    options.fixed_lights = collection({
        fixed_point(point_light(0.1F, 0.4F)),
        fixed_point(point_light(0.1F, 0.4F)),
    });
    options.fixed_lights.shadowed_point_index = 0U;
    options.point_shadow_state.enabled = true;
    options.point_shadow_state.map = map;
    options.point_shadow_state.bias = 0.0F;
    return options;
}

void test_only_associated_point_light_is_shadowed() {
    const auto map = fully_occluding_cubemap({0.0F, 0.0F, 2.0F});
    const ModelRenderOptions options = shadowed_two_point_options(map);
    Framebuffer framebuffer(65U, 65U);
    draw_model_asset(
        framebuffer,
        model_from_triangle(camera_triangle()),
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        options);

    check_near(
        framebuffer.color_at(32U, 32U).x,
        0.6F,
        "point shadow suppresses only the associated light diffuse term while preserving both ambient terms and the other point light");
}

void test_prepared_ownership_and_list_equivalence() {
    std::shared_ptr<const DepthCubemap> map = fully_occluding_cubemap({0.0F, 0.0F, 2.0F});
    std::weak_ptr<const DepthCubemap> weak = map;
    ModelRenderOptions options = shadowed_two_point_options(map);
    const ModelAsset asset = model_from_triangle(camera_triangle());
    const PreparedModelSubmission prepared = prepare_model_asset(asset, options);
    options.point_shadow_state.map.reset();
    map.reset();
    check(!weak.expired(), "prepared model retains owned point-shadow cubemap lifetime");

    Framebuffer direct(65U, 65U);
    draw_prepared_model(
        direct,
        prepared,
        Mat4::identity(), Mat4::identity(), Mat4::identity());

    Framebuffer listed(65U, 65U);
    const PreparedModelListEntry one[] = {{&prepared, Mat4::identity()}};
    draw_prepared_model_list(listed, one, Mat4::identity(), Mat4::identity());
    check(listed.rgb8() == direct.rgb8(),
          "prepared-list point shadow execution is byte-equivalent to prepared single submission");
    check(listed.fnv1a64() == direct.fnv1a64(),
          "prepared-list point shadow execution is hash-equivalent to prepared single submission");
}

void test_invalid_associations_reject_before_rendering() {
    const ModelAsset asset = model_from_triangle(camera_triangle());
    const auto map = fully_occluding_cubemap({0.0F, 0.0F, 2.0F});

    {
        ModelRenderOptions options;
        options.fixed_lights = collection({fixed_directional(directional_light())});
        options.fixed_lights.shadowed_point_index = 0U;
        options.point_shadow_state = {true, map, 0.0F};
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "point shadow association cannot target a directional record");
    }

    {
        ModelRenderOptions options;
        options.fixed_lights = collection({fixed_point(point_light(0.0F, 1.0F))});
        options.fixed_lights.shadowed_point_index = 1U;
        options.point_shadow_state = {true, map, 0.0F};
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        check(threw, "point shadow association indices outside the collection are rejected");
    }

    {
        ModelRenderOptions options;
        options.fixed_lights = collection({fixed_point(point_light(0.0F, 1.0F))});
        options.fixed_lights.shadowed_point_index = 0U;
        options.point_shadow_state.enabled = true;
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "enabled point shadow state requires a cubemap resource");
    }

    {
        ModelRenderOptions options;
        options.fixed_lights = collection({fixed_point(point_light(0.0F, 1.0F))});
        options.fixed_lights.shadowed_point_index = 0U;
        options.point_shadow_state = {true, map, -0.01F};
        bool threw = false;
        try {
            (void)prepare_model_asset(asset, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "point shadow bias must be finite and non-negative");
    }

    {
        ModelRenderOptions options;
        options.fixed_lights = collection({fixed_point(point_light(0.0F, 1.0F, 3.0F))});
        options.fixed_lights.shadowed_point_index = 0U;
        options.point_shadow_state = {true, map, 0.0F};

        Framebuffer framebuffer(65U, 65U);
        framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 13U);
        const auto before = framebuffer.rgb8();
        const float before_depth = framebuffer.depth_at(32U, 32U);
        const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);
        bool threw = false;
        try {
            draw_model_asset(
                framebuffer,
                asset,
                Mat4::identity(), Mat4::identity(), Mat4::identity(),
                options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "point shadow cubemap capture position must match the associated point light");
        check(framebuffer.rgb8() == before,
              "mismatched point-shadow capture origin rejects before color mutation");
        check(framebuffer.depth_at(32U, 32U) == before_depth,
              "mismatched point-shadow capture origin rejects before depth mutation");
        check(framebuffer.stencil_at(32U, 32U) == before_stencil,
              "mismatched point-shadow capture origin rejects before stencil mutation");
    }
}

void test_later_invalid_list_transform_is_fail_closed() {
    const auto map = fully_occluding_cubemap({0.0F, 0.0F, 2.0F});
    const PreparedModelSubmission prepared = prepare_model_asset(
        model_from_triangle(camera_triangle()),
        shadowed_two_point_options(map));
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
    framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 13U);
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
    check(threw, "a malformed later point-shadow list transform rejects the whole list");
    check(framebuffer.rgb8() == before,
          "later point-shadow list preflight failure leaves earlier color untouched");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "later point-shadow list preflight failure leaves earlier depth untouched");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "later point-shadow list preflight failure leaves earlier stencil untouched");
}

}  // namespace

int main() {
    try {
        test_resource_and_face_addressing();
        test_six_face_capture_uses_existing_depth_path();
        test_only_associated_point_light_is_shadowed();
        test_prepared_ownership_and_list_equivalence();
        test_invalid_associations_reject_before_rendering();
        test_later_invalid_list_transform_is_fail_closed();
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "UNEXPECTED EXCEPTION: " << error.what() << '\n';
    }

    if (failures != 0) {
        std::cerr << failures << " point-shadow test(s) failed\n";
        return 1;
    }
    std::cout << "point shadow tests passed\n";
    return 0;
}
