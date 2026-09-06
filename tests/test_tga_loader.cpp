#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/image_loader.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/obj_loader.hpp"
#include "tiny_renderer/tga_loader.hpp"

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

void put_u16(std::string& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<char>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<char>((value >> 8U) & 0xFFU);
}

std::string make_tga(
    std::uint16_t width,
    std::uint16_t height,
    std::uint8_t descriptor,
    const std::string& payload,
    const std::string& image_id = {}) {
    if (image_id.size() > 255U) {
        throw std::logic_error("test image ID exceeds TGA byte field");
    }
    std::string bytes(18U, '\0');
    bytes[0] = static_cast<char>(image_id.size());
    bytes[2] = static_cast<char>(2U);
    put_u16(bytes, 12U, width);
    put_u16(bytes, 14U, height);
    bytes[16] = static_cast<char>(24U);
    bytes[17] = static_cast<char>(descriptor);
    bytes += image_id;
    bytes += payload;
    return bytes;
}

Texture2D parse_tga(const std::string& bytes) {
    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    return load_tga(input);
}

void expect_tga_error(const std::string& bytes, const std::string& message) {
    bool threw = false;
    try {
        (void)parse_tga(bytes);
    } catch (const TgaParseError&) {
        threw = true;
    }
    check(threw, message);
}

std::string top_left_payload() {
    std::string payload;
    const auto append_bgr = [&](unsigned char red, unsigned char green, unsigned char blue) {
        payload.push_back(static_cast<char>(blue));
        payload.push_back(static_cast<char>(green));
        payload.push_back(static_cast<char>(red));
    };
    append_bgr(255U, 0U, 0U);
    append_bgr(0U, 255U, 0U);
    append_bgr(0U, 0U, 255U);
    append_bgr(255U, 255U, 255U);
    return payload;
}

std::string bottom_left_payload() {
    const std::string top = top_left_payload();
    return top.substr(6U, 6U) + top.substr(0U, 6U);
}

void check_reference_texels(const Texture2D& texture, const std::string& label) {
    check(texture.width() == 2U && texture.height() == 2U, label + " preserves dimensions");
    check_near(texture.texel(0U, 0U).x, 1.0F, label + " top-left red");
    check_near(texture.texel(0U, 0U).y, 0.0F, label + " top-left green component");
    check_near(texture.texel(1U, 0U).y, 1.0F, label + " top-right green");
    check_near(texture.texel(0U, 1U).z, 1.0F, label + " bottom-left blue");
    check_near(texture.texel(1U, 1U).x, 1.0F, label + " bottom-right white red");
    check_near(texture.texel(1U, 1U).y, 1.0F, label + " bottom-right white green");
    check_near(texture.texel(1U, 1U).z, 1.0F, label + " bottom-right white blue");
}

void test_bgr_decode_and_vertical_origin_normalization() {
    check_reference_texels(parse_tga(make_tga(2U, 2U, 0x20U, top_left_payload())), "top-origin TGA");
    check_reference_texels(parse_tga(make_tga(2U, 2U, 0x00U, bottom_left_payload())), "bottom-origin TGA");

    std::string one_pixel;
    one_pixel.push_back(static_cast<char>(30U));
    one_pixel.push_back(static_cast<char>(20U));
    one_pixel.push_back(static_cast<char>(10U));
    const Texture2D with_id = parse_tga(make_tga(1U, 1U, 0x20U, one_pixel, "bounded-id"));
    check_near(with_id.texel(0U, 0U).x, 10.0F / 255.0F, "image ID is skipped before BGR raster");
    check_near(with_id.texel(0U, 0U).y, 20.0F / 255.0F, "BGR green decodes after image ID");
    check_near(with_id.texel(0U, 0U).z, 30.0F / 255.0F, "BGR blue decodes after image ID");
}

void test_unsupported_headers_and_payloads_fail_closed() {
    std::string header_only = make_tga(1U, 1U, 0x20U, "");
    expect_tga_error(header_only, "truncated raster payload is rejected");

    std::string color_mapped = make_tga(1U, 1U, 0x20U, "ABC");
    color_mapped[1] = static_cast<char>(1U);
    expect_tga_error(color_mapped, "color-mapped TGA is rejected");

    std::string wrong_type = make_tga(1U, 1U, 0x20U, "ABC");
    wrong_type[2] = static_cast<char>(10U);
    expect_tga_error(wrong_type, "RLE TGA image type is rejected");

    std::string zero_width = make_tga(0U, 1U, 0x20U, "");
    expect_tga_error(zero_width, "zero width is rejected");

    std::string thirty_two_bit = make_tga(1U, 1U, 0x20U, "ABCD");
    thirty_two_bit[16] = static_cast<char>(32U);
    expect_tga_error(thirty_two_bit, "32-bit TGA is rejected rather than silently discarding alpha");

    expect_tga_error(make_tga(1U, 1U, 0x30U, "ABC"), "right-to-left pixel order is rejected");
    expect_tga_error(make_tga(1U, 1U, 0x60U, "ABC"), "interleaved pixel order is rejected");
    expect_tga_error(make_tga(1U, 1U, 0x21U, "ABC"), "attribute bits are rejected for 24-bit input");

    std::string nonzero_origin = make_tga(1U, 1U, 0x20U, "ABC");
    nonzero_origin[8] = static_cast<char>(1U);
    expect_tga_error(nonzero_origin, "non-zero image origin coordinate is rejected");

    expect_tga_error(make_tga(1U, 1U, 0x20U, "ABCD"), "trailing bytes after exact raster are rejected");
    expect_tga_error(make_tga(65535U, 65535U, 0x20U, ""), "decoder safety bound rejects oversized raster before allocation");

    std::string truncated_id = make_tga(1U, 1U, 0x20U, "ABC", "id");
    truncated_id.resize(19U);
    expect_tga_error(truncated_id, "truncated image ID field fails closed");
}

struct TempDirectory {
    std::filesystem::path path;

    TempDirectory()
        : path(std::filesystem::temp_directory_path() / "tiny_renderer_bounded_tga_texture_test") {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::filesystem::create_directories(path);
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write_binary(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to create test file: " + path.string());
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("failed to write test file: " + path.string());
    }
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    write_binary(path, text);
}

std::string make_reference_ppm() {
    std::string payload;
    const auto append_rgb = [&](unsigned char red, unsigned char green, unsigned char blue) {
        payload.push_back(static_cast<char>(red));
        payload.push_back(static_cast<char>(green));
        payload.push_back(static_cast<char>(blue));
    };
    append_rgb(255U, 0U, 0U);
    append_rgb(0U, 255U, 0U);
    append_rgb(0U, 0U, 255U);
    append_rgb(255U, 255U, 255U);
    return "P6\n2 2\n255\n" + payload;
}

std::string triangle_obj(const std::string& material_library) {
    return
        "mtllib " + material_library + "\n"
        "v -0.8 -0.8 0\n"
        "v 0.8 -0.8 0\n"
        "v 0 0.8 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0.5 1\n"
        "vn 0 0 1\n"
        "usemtl mapped\n"
        "f 1/1/1 2/2/1 3/3/1\n";
}

void test_extension_dispatch_and_material_asset_integration() {
    TempDirectory temp;
    write_binary(temp.path / "shared.tga", make_tga(2U, 2U, 0x20U, top_left_payload()));
    write_binary(temp.path / "shared.ppm", make_reference_ppm());

    const Texture2D tga = load_texture_image_file(temp.path / "shared.tga");
    const Texture2D ppm = load_texture_image_file(temp.path / "shared.ppm");
    for (std::size_t y = 0U; y < 2U; ++y) {
        for (std::size_t x = 0U; x < 2U; ++x) {
            const Vec3& a = tga.texel(x, y);
            const Vec3& b = ppm.texel(x, y);
            check_near(a.x, b.x, "TGA/PPM dispatch red equivalence");
            check_near(a.y, b.y, "TGA/PPM dispatch green equivalence");
            check_near(a.z, b.z, "TGA/PPM dispatch blue equivalence");
        }
    }

    bool unsupported_threw = false;
    try {
        (void)load_texture_image_file(temp.path / "unsupported.png");
    } catch (const std::invalid_argument&) {
        unsupported_threw = true;
    }
    check(unsupported_threw, "unsupported image extension fails explicitly");

    write_text(
        temp.path / "roles.mtl",
        "newmtl mapped\n"
        "Kd 1 1 1\n"
        "map_Kd shared.tga\n"
        "map_d shared.tga\n"
        "map_Bump shared.tga\n");
    write_text(temp.path / "roles.obj", triangle_obj("roles.mtl"));
    const ModelAsset roles = load_obj_model_asset_file(temp.path / "roles.obj");
    check(roles.draws.size() == 1U, "TGA-backed material produces one canonical draw");
    if (roles.draws.size() == 1U) {
        const MaterialDraw& draw = roles.draws.front();
        check(static_cast<bool>(draw.diffuse_texture), "map_Kd loads TGA through shared image dispatch");
        check(static_cast<bool>(draw.opacity_texture), "map_d loads TGA through shared image dispatch");
        check(static_cast<bool>(draw.normal_texture), "map_Bump loads TGA through shared image dispatch");
        check(draw.diffuse_texture == draw.opacity_texture && draw.diffuse_texture == draw.normal_texture,
              "all material roles referencing one normalized TGA path share one owned texture resource");
    }

    write_text(
        temp.path / "tga_render.mtl",
        "newmtl mapped\n"
        "Kd 1 1 1\n"
        "map_Kd shared.tga\n");
    write_text(
        temp.path / "ppm_render.mtl",
        "newmtl mapped\n"
        "Kd 1 1 1\n"
        "map_Kd shared.ppm\n");
    write_text(temp.path / "tga_render.obj", triangle_obj("tga_render.mtl"));
    write_text(temp.path / "ppm_render.obj", triangle_obj("ppm_render.mtl"));

    const ModelAsset tga_asset = load_obj_model_asset_file(temp.path / "tga_render.obj");
    const ModelAsset ppm_asset = load_obj_model_asset_file(temp.path / "ppm_render.obj");
    Framebuffer tga_fb(41U, 41U);
    Framebuffer ppm_fb(41U, 41U);
    draw_model_asset(tga_fb, tga_asset, Mat4::identity());
    draw_model_asset(ppm_fb, ppm_asset, Mat4::identity());
    check(tga_fb.rgb8() == ppm_fb.rgb8(), "file-driven TGA material render matches equivalent PPM bytes");
    check(tga_fb.fnv1a64() == ppm_fb.fnv1a64(), "file-driven TGA/PPM render hashes match deterministically");
}

}  // namespace

int main() {
    try {
        test_bgr_decode_and_vertical_origin_normalization();
        test_unsupported_headers_and_payloads_fail_closed();
        test_extension_dispatch_and_material_asset_integration();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " TGA/image loader test(s) failed\n";
        return 1;
    }
    std::cout << "all TGA/image loader tests passed\n";
    return 0;
}
