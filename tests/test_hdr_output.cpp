#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Exception, typename Function>
void check_throws(Function&& function, const std::string& message) {
    bool threw_expected = false;
    try {
        function();
    } catch (const Exception&) {
        threw_expected = true;
    } catch (...) {
    }
    check(threw_expected, message);
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "HDR test output file opens for reading");
    const std::vector<char> raw{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    std::vector<std::uint8_t> bytes;
    bytes.reserve(raw.size());
    for (const char value : raw) {
        bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
    }
    return bytes;
}

void append_u32_le(std::vector<std::uint8_t>& bytes, std::uint32_t bits) {
    bytes.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 24U) & 0xFFU));
}

std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes.at(offset))
        | (static_cast<std::uint32_t>(bytes.at(offset + 1U)) << 8U)
        | (static_cast<std::uint32_t>(bytes.at(offset + 2U)) << 16U)
        | (static_cast<std::uint32_t>(bytes.at(offset + 3U)) << 24U);
}

float read_float_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return std::bit_cast<float>(read_u32_le(bytes, offset));
}

void write_pixel(Framebuffer& framebuffer, std::size_t x, std::size_t y, const Vec3& color) {
    check(
        framebuffer.test_and_write(x, y, 0.0F, color),
        "single-sample HDR fixture pixel write passes");
}

void test_exact_pfm_header_row_order_and_float_bits() {
    const std::filesystem::path path = "hdr_output_exact.pfm";
    std::filesystem::remove(path);

    Framebuffer framebuffer(2U, 2U);
    write_pixel(framebuffer, 0U, 0U, {-1.0F, 0.0F, 0.5F});
    write_pixel(framebuffer, 1U, 0U, {1.0F, 1.5F, 2.0F});
    write_pixel(framebuffer, 0U, 1U, {0.25F, -0.25F, 4.0F});
    write_pixel(framebuffer, 1U, 1U, {-2.0F, 3.5F, 0.125F});

    framebuffer.write_pfm(path.string());
    const std::vector<std::uint8_t> file = read_binary_file(path);

    const std::string header = "PF\n2 2\n-1.0\n";
    std::vector<std::uint8_t> expected(header.begin(), header.end());

    // PFM payload is bottom-to-top, left-to-right, RGB, little-endian IEEE-754.
    append_u32_le(expected, 0x3E800000U);  // 0.25
    append_u32_le(expected, 0xBE800000U);  // -0.25
    append_u32_le(expected, 0x40800000U);  // 4.0
    append_u32_le(expected, 0xC0000000U);  // -2.0
    append_u32_le(expected, 0x40600000U);  // 3.5
    append_u32_le(expected, 0x3E000000U);  // 0.125
    append_u32_le(expected, 0xBF800000U);  // -1.0
    append_u32_le(expected, 0x00000000U);  // 0.0
    append_u32_le(expected, 0x3F000000U);  // 0.5
    append_u32_le(expected, 0x3F800000U);  // 1.0
    append_u32_le(expected, 0x3FC00000U);  // 1.5
    append_u32_le(expected, 0x40000000U);  // 2.0

    check(file == expected, "PFM export locks header, endian marker, row order, and exact float bits");

    const std::size_t payload = header.size();
    const std::vector<float> decoded{
        read_float_le(file, payload + 0U),
        read_float_le(file, payload + 4U),
        read_float_le(file, payload + 8U),
        read_float_le(file, payload + 12U),
        read_float_le(file, payload + 16U),
        read_float_le(file, payload + 20U),
        read_float_le(file, payload + 24U),
        read_float_le(file, payload + 28U),
        read_float_le(file, payload + 32U),
        read_float_le(file, payload + 36U),
        read_float_le(file, payload + 40U),
        read_float_le(file, payload + 44U),
    };
    const std::vector<float> expected_decoded{
        0.25F, -0.25F, 4.0F,
        -2.0F, 3.5F, 0.125F,
        -1.0F, 0.0F, 0.5F,
        1.0F, 1.5F, 2.0F,
    };
    check(decoded == expected_decoded, "small PFM test reader round-trips the deterministic linear payload");

    std::filesystem::remove(path);
}

void test_multisample_export_uses_resolved_linear_color() {
    const std::filesystem::path single_path = "hdr_output_single.pfm";
    const std::filesystem::path multi_path = "hdr_output_multi.pfm";
    std::filesystem::remove(single_path);
    std::filesystem::remove(multi_path);

    Framebuffer single(1U, 1U);
    single.clear({-0.5F, 1.5F, 0.625F});

    Framebuffer multi(1U, 1U, SampleCount::Four);
    multi.clear({0.0F, 0.0F, 0.0F});
    const std::vector<Vec3> samples{
        {-2.0F, 0.0F, 0.25F},
        {-1.0F, 1.0F, 0.5F},
        {0.0F, 2.0F, 0.75F},
        {1.0F, 3.0F, 1.0F},
    };
    for (std::size_t sample = 0U; sample < samples.size(); ++sample) {
        check(
            multi.test_and_write_sample(0U, 0U, sample, 0.0F, samples[sample]),
            "4x HDR fixture sample write passes");
    }

    const Vec3& resolved = multi.color_at(0U, 0U);
    check(
        resolved.x == -0.5F && resolved.y == 1.5F && resolved.z == 0.625F,
        "4x HDR fixture resolves to the intended signed/high-range linear color");

    single.write_pfm(single_path.string());
    multi.write_pfm(multi_path.string());
    check(
        read_binary_file(single_path) == read_binary_file(multi_path),
        "1x and 4x targets with equal resolved linear RGB have identical PFM bytes");

    std::filesystem::remove(single_path);
    std::filesystem::remove(multi_path);
}

void test_nonfinite_export_is_fail_closed_before_truncation() {
    const std::filesystem::path nan_path = "hdr_output_nan.pfm";
    const std::filesystem::path inf_path = "hdr_output_inf.pfm";
    const std::string sentinel = "preserve-me";

    for (const auto& entry : std::vector<std::pair<std::filesystem::path, float>>{
             {nan_path, std::numeric_limits<float>::quiet_NaN()},
             {inf_path, std::numeric_limits<float>::infinity()},
         }) {
        {
            std::ofstream output(entry.first, std::ios::binary);
            output << sentinel;
        }

        Framebuffer framebuffer(1U, 1U);
        framebuffer.clear({entry.second, 0.0F, 0.0F});
        check_throws<std::invalid_argument>(
            [&] { framebuffer.write_pfm(entry.first.string()); },
            "PFM export rejects non-finite resolved RGB");

        const std::vector<std::uint8_t> bytes = read_binary_file(entry.first);
        check(
            std::string(bytes.begin(), bytes.end()) == sentinel,
            "PFM non-finite rejection happens before file truncation");
        std::filesystem::remove(entry.first);
    }
}

void test_pfm_export_does_not_mutate_8bit_output_state() {
    const std::filesystem::path path = "hdr_output_independence.pfm";
    std::filesystem::remove(path);

    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({-1.0F, 0.5F, 2.0F});
    const std::vector<std::uint8_t> linear_before = framebuffer.rgb8();
    const std::vector<std::uint8_t> srgb_before = framebuffer.rgb8(OutputTransferFunction::Srgb);
    const std::uint64_t linear_hash_before = framebuffer.fnv1a64();
    const std::uint64_t srgb_hash_before = framebuffer.fnv1a64(OutputTransferFunction::Srgb);

    framebuffer.write_pfm(path.string());

    check(framebuffer.rgb8() == linear_before, "PFM export leaves legacy linear RGB8 unchanged");
    check(
        framebuffer.rgb8(OutputTransferFunction::Srgb) == srgb_before,
        "PFM export leaves explicit sRGB bytes unchanged");
    check(framebuffer.fnv1a64() == linear_hash_before, "PFM export leaves legacy hash unchanged");
    check(
        framebuffer.fnv1a64(OutputTransferFunction::Srgb) == srgb_hash_before,
        "PFM export leaves sRGB hash unchanged");

    std::filesystem::remove(path);
}

}  // namespace

int main() {
    test_exact_pfm_header_row_order_and_float_bits();
    test_multisample_export_uses_resolved_linear_color();
    test_nonfinite_export_is_fail_closed_before_truncation();
    test_pfm_export_does_not_mutate_8bit_output_state();

    if (failures != 0) {
        std::cerr << failures << " HDR-output test(s) failed\n";
        return 1;
    }
    std::cout << "HDR-output tests passed\n";
    return 0;
}
