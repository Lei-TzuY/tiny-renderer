#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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

void check_unchanged(
    const Framebuffer& framebuffer,
    const std::vector<std::uint8_t>& before,
    const std::string& context) {
    check(framebuffer.rgb8() == before, context + " preserves framebuffer color");
    check(std::isinf(framebuffer.depth_at(32U, 32U)), context + " preserves framebuffer depth");
}

Vertex uv_vertex(const Vec3& position, float u, float v) {
    return Vertex::with_varyings(position, VaryingPack{u, v});
}

Mesh make_two_triangle_uv_mesh() {
    Mesh mesh;
    mesh.vertices = {
        uv_vertex({-0.9F, -0.7F, 0.0F}, 0.0F, 0.0F),
        uv_vertex({-0.1F, -0.7F, 0.0F}, 1.0F, 0.0F),
        uv_vertex({-0.5F, 0.7F, 0.0F}, 0.5F, 1.0F),
        uv_vertex({0.1F, -0.7F, 0.0F}, 0.0F, 0.0F),
        uv_vertex({0.9F, -0.7F, 0.0F}, 1.0F, 0.0F),
        uv_vertex({0.5F, 0.7F, 0.0F}, 0.5F, 1.0F),
    };
    mesh.triangles = {{0U, 1U, 2U}, {3U, 4U, 5U}};
    return mesh;
}

void test_later_model_uv_binding_fails_before_earlier_draw_write() {
    ModelAsset asset;
    asset.mesh = make_two_triangle_uv_mesh();

    MaterialDraw first;
    first.range = {0U, 1U};
    first.material.albedo = {0.8F, 0.2F, 0.2F};

    MaterialDraw second;
    second.range = {1U, 1U};
    second.material.albedo = {1.0F, 1.0F, 1.0F};
    second.diffuse_texture = std::make_shared<const Texture2D>(
        1U,
        1U,
        std::vector<Vec3>{{0.2F, 0.8F, 0.2F}});

    asset.draws = {first, second};

    ModelRenderOptions options;
    options.u_channel = 2U;
    options.v_channel = 1U;

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.25F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    bool threw = false;
    try {
        draw_model_asset(framebuffer, asset, Mat4::identity(), options);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(threw, "later mapped model draw with invalid UV binding is rejected");
    check_unchanged(framebuffer, before, "later mapped model UV-binding rejection");
}

void test_direct_range_uses_same_uv_binding_preflight() {
    const Mesh mesh = make_two_triangle_uv_mesh();
    const Texture2D texture(1U, 1U, {{0.2F, 0.8F, 0.2F}});
    const TextureBinding binding{&texture, 2U, 1U, {}};

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.25F, 0.125F, 0.375F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();

    Rasterizer rasterizer(
        framebuffer,
        {},
        binding,
        {},
        {},
        BaseColorSource::Texture);
    bool threw = false;
    try {
        rasterizer.draw_mesh_range(mesh, DrawRange{0U, 1U}, Mat4::identity());
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(threw, "direct range rejects the same invalid UV binding as model preflight");
    check_unchanged(framebuffer, before, "direct range UV-binding rejection");
}

void test_range_preserves_all_vertex_uv_validation_contract() {
    Mesh mesh = make_two_triangle_uv_mesh();
    mesh.vertices[5].varyings.values[0] = std::numeric_limits<float>::quiet_NaN();

    const Texture2D texture(1U, 1U, {{1.0F, 1.0F, 1.0F}});
    Rasterizer rasterizer(
        *new Framebuffer(1U, 1U));
    (void)rasterizer;

    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.125F, 0.375F, 0.25F});
    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    Rasterizer ranged(
        framebuffer,
        {},
        TextureBinding{&texture, 0U, 1U, {}},
        {},
        {},
        BaseColorSource::Texture);

    bool threw = false;
    try {
        ranged.draw_mesh_range(mesh, DrawRange{0U, 1U}, Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw,
          "selected range still rejects invalid UV state on vertices outside the selected triangle set");
    check_unchanged(framebuffer, before, "all-vertex UV validation contract");
}

}  // namespace

int main() {
    try {
        test_later_model_uv_binding_fails_before_earlier_draw_write();
        test_direct_range_uses_same_uv_binding_preflight();
        test_range_preserves_all_vertex_uv_validation_contract();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " shared preflight test(s) failed\n";
        return 1;
    }
    std::cout << "all shared preflight tests passed\n";
    return 0;
}
