#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/obj_loader.hpp"
#include "tiny_renderer/ppm_loader.hpp"
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

void check_near(float actual, float expected, const std::string& message, float epsilon = 1.0e-6F) {
    check(std::fabs(actual - expected) <= epsilon,
          message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

Texture2D parse_bytes(const std::string& bytes) {
    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    return load_ppm(input);
}

std::string make_p6(std::size_t width, std::size_t height, const std::string& payload) {
    return "P6\n# deterministic memory fixture\n" + std::to_string(width) + " "
        + std::to_string(height) + "\n255\n" + payload;
}

void expect_parse_error(const std::string& bytes, const std::string& message) {
    bool threw = false;
    try {
        (void)parse_bytes(bytes);
    } catch (const PpmParseError&) {
        threw = true;
    }
    check(threw, message);
}

Vertex uv_vertex(const Vec3& position, float u, float v) {
    return Vertex::with_varyings(position, VaryingPack{u, v});
}

Mesh manual_fixture_mesh() {
    Mesh mesh;
    mesh.vertices = {
        uv_vertex({-0.8F, -0.8F, 0.0F}, 0.0F, 0.0F),
        uv_vertex({0.8F, -0.8F, 0.0F}, 1.0F, 0.0F),
        uv_vertex({0.8F, 0.8F, 0.0F}, 1.0F, 1.0F),
        uv_vertex({0.8F, 0.8F, 0.0F}, 0.25F, 0.75F),
        uv_vertex({-0.8F, 0.8F, 0.0F}, 0.0F, 1.0F),
    };
    mesh.triangles = {
        TriangleIndices{0U, 1U, 2U},
        TriangleIndices{0U, 3U, 4U},
    };
    return mesh;
}

std::size_t count_non_black(const Framebuffer& framebuffer) {
    std::size_t count = 0U;
    for (std::size_t y = 0; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0; x < framebuffer.width(); ++x) {
            const Vec3& color = framebuffer.color_at(x, y);
            if (color.x != 0.0F || color.y != 0.0F || color.z != 0.0F) {
                ++count;
            }
        }
    }
    return count;
}

void test_binary_payload_decodes_exact_rgb_bytes() {
    std::string payload;
    payload.push_back(static_cast<char>(0x00));
    payload.push_back(static_cast<char>(0x7f));
    payload.push_back(static_cast<char>(0xff));
    payload.push_back(static_cast<char>(0x20));
    payload.push_back(static_cast<char>(0x40));
    payload.push_back(static_cast<char>(0x80));

    const Texture2D texture = parse_bytes(make_p6(2U, 1U, payload));
    check(texture.width() == 2U && texture.height() == 1U, "P6 dimensions become Texture2D dimensions");
    check_near(texture.texel(0U, 0U).x, 0.0F, "first texel red decodes byte 0");
    check_near(texture.texel(0U, 0U).y, 127.0F / 255.0F, "first texel green decodes byte 127");
    check_near(texture.texel(0U, 0U).z, 1.0F, "first texel blue decodes byte 255");
    check_near(texture.texel(1U, 0U).x, 32.0F / 255.0F, "second texel red decodes byte 32");
    check_near(texture.texel(1U, 0U).y, 64.0F / 255.0F, "second texel green decodes byte 64");
    check_near(texture.texel(1U, 0U).z, 128.0F / 255.0F, "second texel blue decodes byte 128");
}

void test_header_comments_and_token_whitespace() {
    std::string ppm = "P6\n# comment before width\n2\t# between dimensions\n1\n# before maxval\n255\n";
    ppm += "ABCDEF";
    const Texture2D texture = parse_bytes(ppm);
    check(texture.width() == 2U && texture.height() == 1U, "header comments and ASCII token whitespace are accepted");
    check_near(texture.texel(0U, 0U).x, 65.0F / 255.0F, "raster begins immediately after maxval separator");
    check_near(texture.texel(1U, 0U).z, 70.0F / 255.0F, "header parser does not consume raster bytes");
}

void test_invalid_headers_and_payload_lengths_fail_closed() {
    expect_parse_error("P3\n1 1\n255\nABC", "ASCII P3 magic is rejected by the P6-only decoder");
    expect_parse_error("P6\n0 1\n255\n", "zero width is rejected");
    expect_parse_error("P6\n1 0\n255\n", "zero height is rejected");
    expect_parse_error("P6\n1 1\n254\nABC", "maxval other than 255 is rejected");
    expect_parse_error("P6\nnope 1\n255\nABC", "malformed width is rejected");
    expect_parse_error("P6\n18446744073709551615 18446744073709551615\n255\n", "dimension multiplication overflow is rejected");
    expect_parse_error(make_p6(2U, 1U, "ABCDE"), "truncated raster payload is rejected");
    expect_parse_error(make_p6(1U, 1U, "ABCD"), "trailing bytes after the exact raster payload are rejected");
    expect_parse_error("P6\n1 1\n255", "missing raster separator/payload is rejected");
}

void test_file_driven_obj_and_ppm_render_matches_programmatic_assets() {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for PPM fixture tests
#endif
    const std::filesystem::path root = std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures";
    const Mesh imported_mesh = load_obj_file(root / "textured_quad.obj");
    const Texture2D imported_texture = load_ppm_file(root / "checker.ppm");

    const float lo = 32.0F / 255.0F;
    const float hi = 120.0F / 255.0F;
    const Texture2D manual_texture(2U, 2U, {
        {hi, lo, lo},
        {lo, hi, lo},
        {lo, lo, hi},
        {hi, hi, hi},
    });
    const Mesh manual_mesh = manual_fixture_mesh();

    const SamplerState sampler{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear};
    const TextureBinding imported_binding{&imported_texture, 0U, 1U, sampler};
    const TextureBinding manual_binding{&manual_texture, 0U, 1U, sampler};

    Framebuffer imported_fb(65U, 65U);
    Rasterizer imported_rasterizer(imported_fb, {}, imported_binding);
    imported_rasterizer.draw_mesh(imported_mesh, Mat4::identity());

    Framebuffer manual_fb(65U, 65U);
    Rasterizer manual_rasterizer(manual_fb, {}, manual_binding);
    manual_rasterizer.draw_mesh(manual_mesh, Mat4::identity());

    check(count_non_black(imported_fb) > 0U, "file-driven OBJ + PPM path produces visible fragments");
    check(imported_fb.rgb8() == manual_fb.rgb8(),
          "file-driven OBJ + PPM render is byte-identical to programmatic mesh + texels");
    check(imported_fb.fnv1a64() == manual_fb.fnv1a64(),
          "file-driven and programmatic asset paths retain the same framebuffer hash");
}

}  // namespace

int main() {
    try {
        test_binary_payload_decodes_exact_rgb_bytes();
        test_header_comments_and_token_whitespace();
        test_invalid_headers_and_payload_lengths_fail_closed();
        test_file_driven_obj_and_ppm_render_matches_programmatic_assets();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " PPM loader test(s) failed\n";
        return 1;
    }
    std::cout << "all PPM loader tests passed\n";
    return 0;
}
