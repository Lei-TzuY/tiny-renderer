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
#include "tiny_renderer/image_loader.hpp"
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

Texture2D parse_bytes(
    const std::string& bytes,
    TextureTransferFunction transfer_function = TextureTransferFunction::Linear) {
    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    return load_ppm(input, transfer_function);
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
    check(texture.source_transfer_function() == TextureTransferFunction::Linear,
          "legacy PPM decode defaults to explicit linear source interpretation");
    check_near(texture.texel(0U, 0U).x, 0.0F, "first texel red decodes byte 0");
    check_near(texture.texel(0U, 0U).y, 127.0F / 255.0F, "first texel green decodes byte 127");
    check_near(texture.texel(0U, 0U).z, 1.0F, "first texel blue decodes byte 255");
    check_near(texture.texel(1U, 0U).x, 32.0F / 255.0F, "second texel red decodes byte 32");
    check_near(texture.texel(1U, 0U).y, 64.0F / 255.0F, "second texel green decodes byte 64");
    check_near(texture.texel(1U, 0U).z, 128.0F / 255.0F, "second texel blue decodes byte 128");
}

void test_srgb_decode_precedes_mip_generation() {
    const Vec3 encoded_half{0.5F, 0.5F, 0.5F};
    const Vec3 encoded_one{1.0F, 1.0F, 1.0F};
    const Texture2D linear(2U, 2U, {encoded_half, encoded_half, encoded_one, encoded_one});
    const Texture2D srgb(
        2U,
        2U,
        {encoded_half, encoded_half, encoded_one, encoded_one},
        TextureTransferFunction::Srgb);

    check(linear.source_transfer_function() == TextureTransferFunction::Linear,
          "programmatic texture default remains linear");
    check(srgb.source_transfer_function() == TextureTransferFunction::Srgb,
          "sRGB source interpretation remains queryable after decode");
    check_near(linear.texel(0U, 0U).x, 0.5F,
               "linear source keeps encoded half value unchanged");
    check_near(srgb.texel(0U, 0U).x, 0.21404114F,
               "sRGB half value decodes to linear before sampling", 2.0e-6F);
    check_near(srgb.mip_texel(1U, 0U, 0U).x, 0.60702056F,
               "mip level averages decoded linear texels rather than encoded values", 2.0e-6F);
    check(std::fabs(srgb.mip_texel(1U, 0U, 0U).x - 0.52252156F) > 0.05F,
          "decode-before-filter result is observably distinct from decoding an encoded-space average");

    bool invalid_transfer_threw = false;
    try {
        (void)Texture2D(
            1U,
            1U,
            {{0.5F, 0.5F, 0.5F}},
            static_cast<TextureTransferFunction>(99));
    } catch (const std::invalid_argument&) {
        invalid_transfer_threw = true;
    }
    check(invalid_transfer_threw, "unknown texture transfer interpretation rejects deterministically");

    bool out_of_range_srgb_threw = false;
    try {
        (void)Texture2D(
            1U,
            1U,
            {{1.01F, 0.5F, 0.5F}},
            TextureTransferFunction::Srgb);
    } catch (const std::invalid_argument&) {
        out_of_range_srgb_threw = true;
    }
    check(out_of_range_srgb_threw, "sRGB decode rejects source values outside normalized range");
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

void test_image_dispatch_and_material_role_transfer_identity() {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for PPM fixture tests
#endif
    const std::filesystem::path root = std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures";
    const Texture2D linear_dispatch = load_texture_image_file(root / "checker.ppm");
    const Texture2D srgb_dispatch = load_texture_image_file(
        root / "checker.ppm",
        TextureTransferFunction::Srgb);
    check(linear_dispatch.source_transfer_function() == TextureTransferFunction::Linear,
          "shared image dispatch preserves legacy linear default");
    check(srgb_dispatch.source_transfer_function() == TextureTransferFunction::Srgb,
          "shared image dispatch propagates explicit sRGB interpretation");

    const ModelAsset legacy = load_obj_model_asset_file(root / "transfer_roles.obj");
    check(legacy.draws.size() == 1U, "transfer-role fixture produces one canonical draw");
    if (legacy.draws.size() == 1U) {
        const MaterialDraw& draw = legacy.draws.front();
        check(draw.diffuse_texture && draw.opacity_texture && draw.normal_texture,
              "transfer-role fixture loads all three texture roles");
        check(draw.diffuse_texture == draw.opacity_texture && draw.opacity_texture == draw.normal_texture,
              "legacy linear import deduplicates one normalized path across all linear roles");
        if (draw.diffuse_texture) {
            check(draw.diffuse_texture->source_transfer_function() == TextureTransferFunction::Linear,
                  "legacy material diffuse texture remains linear");
        }
    }

    ModelAssetLoadOptions options;
    options.diffuse_transfer = TextureTransferFunction::Srgb;
    const ModelAsset interpreted = load_obj_model_asset_file(root / "transfer_roles.obj", options);
    check(interpreted.draws.size() == 1U, "sRGB-option fixture produces one canonical draw");
    if (interpreted.draws.size() == 1U) {
        const MaterialDraw& draw = interpreted.draws.front();
        check(draw.diffuse_texture && draw.opacity_texture && draw.normal_texture,
              "sRGB-option import retains all texture resources");
        check(draw.diffuse_texture != draw.opacity_texture,
              "cache identity separates sRGB diffuse from linear data texture using the same file");
        check(draw.opacity_texture == draw.normal_texture,
              "opacity and normal roles still deduplicate the same linear data texture");
        if (draw.diffuse_texture) {
            check(draw.diffuse_texture->source_transfer_function() == TextureTransferFunction::Srgb,
                  "diffuse role records sRGB source interpretation");
        }
        if (draw.opacity_texture) {
            check(draw.opacity_texture->source_transfer_function() == TextureTransferFunction::Linear,
                  "opacity role remains explicit linear data under sRGB diffuse import");
        }
        if (draw.normal_texture) {
            check(draw.normal_texture->source_transfer_function() == TextureTransferFunction::Linear,
                  "normal role remains explicit linear data under sRGB diffuse import");
        }
    }

    ModelAssetLoadOptions invalid;
    invalid.diffuse_transfer = static_cast<TextureTransferFunction>(99);
    bool invalid_option_threw = false;
    try {
        (void)load_obj_model_asset_file(root / "transfer_roles.obj", invalid);
    } catch (const std::invalid_argument&) {
        invalid_option_threw = true;
    }
    check(invalid_option_threw, "model asset import rejects unknown diffuse transfer interpretation");
}

}  // namespace

int main() {
    try {
        test_binary_payload_decodes_exact_rgb_bytes();
        test_srgb_decode_precedes_mip_generation();
        test_header_comments_and_token_whitespace();
        test_invalid_headers_and_payload_lengths_fail_closed();
        test_file_driven_obj_and_ppm_render_matches_programmatic_assets();
        test_image_dispatch_and_material_role_transfer_identity();
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
