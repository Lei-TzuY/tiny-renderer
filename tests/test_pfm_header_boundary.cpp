#include <bit>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "tiny_renderer/pfm_loader.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void append_u32_le(std::string& bytes, std::uint32_t bits) {
    bytes.push_back(static_cast<char>(bits & 0xFFU));
    bytes.push_back(static_cast<char>((bits >> 8U) & 0xFFU));
    bytes.push_back(static_cast<char>((bits >> 16U) & 0xFFU));
    bytes.push_back(static_cast<char>((bits >> 24U) & 0xFFU));
}

Texture2D load_bytes(const std::string& bytes) {
    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    return load_pfm(input);
}

void test_crlf_header_boundary() {
    std::string bytes = "PF\r\n1 1\r\n-1.0\r\n";
    append_u32_le(bytes, 0x3F800000U);  // 1.0
    append_u32_le(bytes, 0xBF000000U);  // -0.5
    append_u32_le(bytes, 0x40000000U);  // 2.0

    const Texture2D texture = load_bytes(bytes);
    const Vec3 texel = texture.texel(0U, 0U);
    check(texture.width() == 1U && texture.height() == 1U, "CRLF PFM dimensions decode");
    check(texel.x == 1.0F && texel.y == -0.5F && texel.z == 2.0F,
        "CRLF scale line ends before binary raster payload");
}

void test_payload_whitespace_byte_is_not_consumed() {
    std::string bytes = "PF\n1 1\n-1.0\n";
    constexpr std::uint32_t red_bits = 0x3F80000AU;
    constexpr std::uint32_t green_bits = 0x3F00000DU;
    constexpr std::uint32_t blue_bits = 0x40000020U;
    append_u32_le(bytes, red_bits);
    append_u32_le(bytes, green_bits);
    append_u32_le(bytes, blue_bits);

    const Texture2D texture = load_bytes(bytes);
    const Vec3 texel = texture.texel(0U, 0U);
    check(std::bit_cast<std::uint32_t>(texel.x) == red_bits,
        "LF-valued first payload byte remains part of red float");
    check(std::bit_cast<std::uint32_t>(texel.y) == green_bits,
        "CR-valued first payload byte remains part of green float");
    check(std::bit_cast<std::uint32_t>(texel.z) == blue_bits,
        "space-valued first payload byte remains part of blue float");
}

}  // namespace

int main() {
    test_crlf_header_boundary();
    test_payload_whitespace_byte_is_not_consumed();

    if (failures != 0) {
        std::cerr << failures << " PFM header-boundary test(s) failed\n";
        return 1;
    }
    std::cout << "PFM header-boundary tests passed\n";
    return 0;
}
