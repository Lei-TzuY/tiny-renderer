#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/image_loader.hpp"
#include "tiny_renderer/pfm_loader.hpp"

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
    check(static_cast<bool>(input), "test output file opens for reading");
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

void append_u32_be(std::vector<std::uint8_t>& bytes, std::uint32_t bits) {
    bytes.push_back(static_cast<std::uint8_t>((bits >> 24U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
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

std::string byte_string(const std::vector<std::uint8_t>& bytes) {
    std::string result;
    result.reserve(bytes.size());
    for (const std::uint8_t byte : bytes) {
        result.push_back(static_cast<char>(byte));
    }
    return result;
}

bool exact_vec3(const Vec3& actual, const Vec3& expected) {
    return actual.x == expected.x && actual.y == expected.y && actual.z == expected.z;
}

void test_piecewise_srgb_encoding_and_clamp() {
    Framebuffer framebuffer(4U, 1U);
    framebuffer.test_and_write(0U, 0U, 0.0F, {-0.5F, 0.0F, 0.001F});
    framebuffer.test_and_write(1U, 0U, 0.0F, {0.0031308F, 0.18F, 0.25F});
    framebuffer.test_and_write(2U, 0U, 0.0F, {0.5F, 0.75F, 1.0F});
    framebuffer.test_and_write(3U, 0U, 0.0F, {2.0F, 0.5F, 0.0F});

    const std::vector<std::uint8_t> expected{
        0U, 0U, 3U,
        10U, 118U, 137U,
        188U, 225U, 255U,
        255U, 188U, 0U,
    };
    check(
        framebuffer.rgb8(OutputTransferFunction::Srgb) == expected,
        "sRGB export locks linear segment, nonlinear segment, rounding, and [0,1] clamp");

    const Vec3& stored = framebuffer.color_at(3U, 0U);
    check(
        stored.x == 2.0F && stored.y == 0.5F && stored.z == 0.0F,
        "output transfer does not mutate resolved linear framebuffer storage");
}

void test_linear_default_compatibility_and_hashes() {
    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({0.0F, 0.5F, 1.0F});

    const std::vector<std::uint8_t> expected_linear{0U, 128U, 255U};
    const std::vector<std::uint8_t> expected_srgb{0U, 188U, 255U};
    check(framebuffer.rgb8() == expected_linear, "historical no-argument RGB8 remains linear quantization");
    check(
        framebuffer.rgb8(OutputTransferFunction::Linear) == framebuffer.rgb8(),
        "explicit Linear RGB8 is byte-identical to historical export");
    check(
        framebuffer.rgb8(OutputTransferFunction::Srgb) == expected_srgb,
        "opt-in sRGB RGB8 differs only at export");

    constexpr std::uint64_t expected_linear_hash = 0xd79a37186a9dda16ULL;
    constexpr std::uint64_t expected_srgb_hash = 0xd7a837186aaa1772ULL;
    check(framebuffer.fnv1a64() == expected_linear_hash, "historical linear hash remains deterministic");
    check(
        framebuffer.fnv1a64(OutputTransferFunction::Linear) == expected_linear_hash,
        "explicit Linear hash matches historical hash");
    check(
        framebuffer.fnv1a64(OutputTransferFunction::Srgb) == expected_srgb_hash,
        "sRGB hash is computed from encoded export bytes");
}

void test_multisample_resolve_happens_before_encoding() {
    Framebuffer multisample(1U, 1U, SampleCount::Four);
    multisample.clear({0.0F, 0.0F, 0.0F});
    check(
        multisample.test_and_write_sample(0U, 0U, 2U, 0.5F, {1.0F, 1.0F, 1.0F}),
        "sample two write passes");
    check(
        multisample.test_and_write_sample(0U, 0U, 3U, 0.5F, {1.0F, 1.0F, 1.0F}),
        "sample three write passes");

    const Vec3& resolved = multisample.color_at(0U, 0U);
    check(
        resolved.x == 0.5F && resolved.y == 0.5F && resolved.z == 0.5F,
        "4x storage resolves to linear 0.5 before export");
    check(
        multisample.rgb8(OutputTransferFunction::Srgb)
            == std::vector<std::uint8_t>{188U, 188U, 188U},
        "sRGB encoding is applied after linear multisample resolve");

    Framebuffer single_sample(1U, 1U);
    single_sample.clear({0.5F, 0.5F, 0.5F});
    check(
        single_sample.rgb8(OutputTransferFunction::Srgb)
            == multisample.rgb8(OutputTransferFunction::Srgb),
        "1x and 4x targets with equal resolved linear RGB export identically");
}

void test_ppm_overloads_and_fail_closed_validation() {
    const std::filesystem::path legacy_path = "output_transfer_legacy.ppm";
    const std::filesystem::path linear_path = "output_transfer_linear.ppm";
    const std::filesystem::path srgb_path = "output_transfer_srgb.ppm";
    const std::filesystem::path invalid_path = "output_transfer_invalid.ppm";
    const std::filesystem::path nonfinite_path = "output_transfer_nonfinite.ppm";
    std::filesystem::remove(legacy_path);
    std::filesystem::remove(linear_path);
    std::filesystem::remove(srgb_path);
    std::filesystem::remove(invalid_path);
    std::filesystem::remove(nonfinite_path);

    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({0.0F, 0.5F, 1.0F});
    framebuffer.write_ppm(legacy_path.string());
    framebuffer.write_ppm(linear_path.string(), OutputTransferFunction::Linear);
    framebuffer.write_ppm(srgb_path.string(), OutputTransferFunction::Srgb);

    check(
        read_binary_file(legacy_path) == read_binary_file(linear_path),
        "explicit Linear PPM remains byte-identical to historical PPM");

    const std::vector<std::uint8_t> srgb_file = read_binary_file(srgb_path);
    const std::string header = "P6\n1 1\n255\n";
    std::vector<std::uint8_t> expected(header.begin(), header.end());
    expected.insert(expected.end(), {0U, 188U, 255U});
    check(srgb_file == expected, "sRGB PPM contains the standard header and encoded RGB bytes");

    const auto invalid_transfer = static_cast<OutputTransferFunction>(255);
    check_throws<std::invalid_argument>(
        [&] { (void)framebuffer.rgb8(invalid_transfer); },
        "unknown output transfer is rejected by RGB8 export");
    check_throws<std::invalid_argument>(
        [&] { (void)framebuffer.fnv1a64(invalid_transfer); },
        "unknown output transfer is rejected by hash export");
    check_throws<std::invalid_argument>(
        [&] { framebuffer.write_ppm(invalid_path.string(), invalid_transfer); },
        "unknown output transfer is rejected by PPM export");
    check(
        !std::filesystem::exists(invalid_path),
        "unknown transfer rejection occurs before creating the PPM file");

    Framebuffer nonfinite(1U, 1U);
    nonfinite.clear({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    check_throws<std::invalid_argument>(
        [&] { (void)nonfinite.rgb8(OutputTransferFunction::Srgb); },
        "sRGB RGB8 rejects non-finite resolved color");
    check_throws<std::invalid_argument>(
        [&] { (void)nonfinite.fnv1a64(OutputTransferFunction::Srgb); },
        "sRGB hash rejects non-finite resolved color");
    check_throws<std::invalid_argument>(
        [&] { nonfinite.write_ppm(nonfinite_path.string(), OutputTransferFunction::Srgb); },
        "sRGB PPM rejects non-finite resolved color");
    check(
        !std::filesystem::exists(nonfinite_path),
        "non-finite sRGB rejection occurs before creating the PPM file");

    std::filesystem::remove(legacy_path);
    std::filesystem::remove(linear_path);
    std::filesystem::remove(srgb_path);
    std::filesystem::remove(invalid_path);
    std::filesystem::remove(nonfinite_path);
}

void test_linear_pfm_exact_bytes_and_round_trip() {
    const std::filesystem::path path = "output_transfer_linear_hdr.pfm";
    std::filesystem::remove(path);

    Framebuffer framebuffer(2U, 2U);
    check(framebuffer.test_and_write(0U, 0U, 0.0F, {-1.0F, 0.0F, 0.5F}), "top-left HDR fixture write passes");
    check(framebuffer.test_and_write(1U, 0U, 0.0F, {1.0F, 1.5F, 2.0F}), "top-right HDR fixture write passes");
    check(framebuffer.test_and_write(0U, 1U, 0.0F, {0.25F, -0.25F, 4.0F}), "bottom-left HDR fixture write passes");
    check(framebuffer.test_and_write(1U, 1U, 0.0F, {-2.0F, 3.5F, 0.125F}), "bottom-right HDR fixture write passes");

    framebuffer.write_pfm(path.string());
    const std::vector<std::uint8_t> file = read_binary_file(path);

    const std::string header = "PF\n2 2\n-1.0\n";
    std::vector<std::uint8_t> expected(header.begin(), header.end());
    append_u32_le(expected, 0x3E800000U);
    append_u32_le(expected, 0xBE800000U);
    append_u32_le(expected, 0x40800000U);
    append_u32_le(expected, 0xC0000000U);
    append_u32_le(expected, 0x40600000U);
    append_u32_le(expected, 0x3E000000U);
    append_u32_le(expected, 0xBF800000U);
    append_u32_le(expected, 0x00000000U);
    append_u32_le(expected, 0x3F000000U);
    append_u32_le(expected, 0x3F800000U);
    append_u32_le(expected, 0x3FC00000U);
    append_u32_le(expected, 0x40000000U);
    check(file == expected, "PFM export locks header, little-endian marker, row order, and exact float bits");

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
    check(decoded == expected_decoded, "small PFM reader round-trips signed and >1 linear values exactly");

    const Texture2D loaded = load_pfm_file(path);
    check(loaded.width() == 2U && loaded.height() == 2U, "PFM loader preserves round-trip dimensions");
    check(
        loaded.source_transfer_function() == TextureTransferFunction::Linear,
        "PFM loader explicitly enters the linear texture domain");
    check(!loaded.texels_within_unit_range(), "HDR PFM round trip retains out-of-unit-range texels");
    check(exact_vec3(loaded.texel(0U, 0U), {-1.0F, 0.0F, 0.5F}), "PFM round trip restores top-left row orientation");
    check(exact_vec3(loaded.texel(1U, 0U), {1.0F, 1.5F, 2.0F}), "PFM round trip restores top-right texel");
    check(exact_vec3(loaded.texel(0U, 1U), {0.25F, -0.25F, 4.0F}), "PFM round trip restores bottom-left texel");
    check(exact_vec3(loaded.texel(1U, 1U), {-2.0F, 3.5F, 0.125F}), "PFM round trip restores bottom-right texel");
    check(
        exact_vec3(loaded.mip_texel(1U, 0U, 0U), {-0.4375F, 1.1875F, 1.65625F}),
        "PFM HDR texels feed the existing linear mip arithmetic unchanged");

    std::filesystem::remove(path);
}

void test_linear_pfm_uses_resolved_multisample_color() {
    const std::filesystem::path single_path = "output_transfer_hdr_single.pfm";
    const std::filesystem::path multi_path = "output_transfer_hdr_multi.pfm";
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
        "4x HDR fixture resolves in linear float before export");

    single.write_pfm(single_path.string());
    multi.write_pfm(multi_path.string());
    check(
        read_binary_file(single_path) == read_binary_file(multi_path),
        "1x and 4x targets with equal resolved linear RGB have identical PFM bytes");

    std::filesystem::remove(single_path);
    std::filesystem::remove(multi_path);
}

void check_nonfinite_pfm_preserves_existing_file(
    const std::filesystem::path& path,
    float nonfinite_value,
    const std::string& label) {
    const std::string sentinel = "preserve-me";
    {
        std::ofstream output(path, std::ios::binary);
        output << sentinel;
    }

    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({nonfinite_value, 0.0F, 0.0F});
    check_throws<std::invalid_argument>(
        [&] { framebuffer.write_pfm(path.string()); },
        label + " is rejected by PFM export");

    const std::vector<std::uint8_t> expected(sentinel.begin(), sentinel.end());
    check(
        read_binary_file(path) == expected,
        label + " rejection occurs before PFM file truncation");
    std::filesystem::remove(path);
}

void test_linear_pfm_fail_closed_and_8bit_independence() {
    const std::filesystem::path nan_path = "output_transfer_hdr_nan.pfm";
    const std::filesystem::path inf_path = "output_transfer_hdr_inf.pfm";
    const std::filesystem::path valid_path = "output_transfer_hdr_independence.pfm";
    std::filesystem::remove(nan_path);
    std::filesystem::remove(inf_path);
    std::filesystem::remove(valid_path);

    check_nonfinite_pfm_preserves_existing_file(
        nan_path,
        std::numeric_limits<float>::quiet_NaN(),
        "NaN resolved color");
    check_nonfinite_pfm_preserves_existing_file(
        inf_path,
        std::numeric_limits<float>::infinity(),
        "infinite resolved color");

    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({-1.0F, 0.5F, 2.0F});
    const std::vector<std::uint8_t> linear_before = framebuffer.rgb8();
    const std::vector<std::uint8_t> srgb_before = framebuffer.rgb8(OutputTransferFunction::Srgb);
    const std::uint64_t linear_hash_before = framebuffer.fnv1a64();
    const std::uint64_t srgb_hash_before = framebuffer.fnv1a64(OutputTransferFunction::Srgb);

    framebuffer.write_pfm(valid_path.string());

    check(framebuffer.rgb8() == linear_before, "PFM export leaves legacy linear RGB8 unchanged");
    check(
        framebuffer.rgb8(OutputTransferFunction::Srgb) == srgb_before,
        "PFM export leaves explicit sRGB RGB8 unchanged");
    check(framebuffer.fnv1a64() == linear_hash_before, "PFM export leaves legacy hash unchanged");
    check(
        framebuffer.fnv1a64(OutputTransferFunction::Srgb) == srgb_hash_before,
        "PFM export leaves explicit sRGB hash unchanged");

    std::filesystem::remove(valid_path);
}

void test_big_endian_pfm_import() {
    const std::string header = "PF\n1 2\n1.0\n";
    std::vector<std::uint8_t> bytes(header.begin(), header.end());
    // File row 0 is the image bottom row.
    append_u32_be(bytes, 0xC0000000U);  // -2.0
    append_u32_be(bytes, 0x40600000U);  // 3.5
    append_u32_be(bytes, 0x3E000000U);  // 0.125
    append_u32_be(bytes, 0x3F800000U);  // 1.0
    append_u32_be(bytes, 0x3FC00000U);  // 1.5
    append_u32_be(bytes, 0x40000000U);  // 2.0

    std::istringstream input(byte_string(bytes), std::ios::in | std::ios::binary);
    const Texture2D texture = load_pfm(input);
    check(texture.width() == 1U && texture.height() == 2U, "big-endian PFM dimensions decode");
    check(exact_vec3(texture.texel(0U, 0U), {1.0F, 1.5F, 2.0F}), "big-endian PFM restores top row");
    check(exact_vec3(texture.texel(0U, 1U), {-2.0F, 3.5F, 0.125F}), "big-endian PFM restores bottom row");
    check(
        exact_vec3(texture.mip_texel(1U, 0U, 0U), {-0.5F, 2.5F, 1.0625F}),
        "big-endian HDR PFM uses existing linear mip generation");
}

void check_pfm_parse_failure(const std::vector<std::uint8_t>& bytes, const std::string& message) {
    std::istringstream input(byte_string(bytes), std::ios::in | std::ios::binary);
    check_throws<PfmParseError>([&] { (void)load_pfm(input); }, message);
}

void test_pfm_import_validation() {
    const std::string valid_header = "PF\n1 1\n-1.0\n";
    std::vector<std::uint8_t> valid(valid_header.begin(), valid_header.end());
    append_u32_le(valid, 0x3F800000U);
    append_u32_le(valid, 0x3F000000U);
    append_u32_le(valid, 0x00000000U);

    {
        std::vector<std::uint8_t> malformed{'P', 'f', '\n', '1', ' ', '1', '\n', '-', '1', '.', '0', '\n'};
        malformed.insert(malformed.end(), valid.end() - 12, valid.end());
        check_pfm_parse_failure(malformed, "grayscale/lowercase Pf magic is rejected");
    }
    {
        const std::string header = "PF\n0 1\n-1.0\n";
        check_pfm_parse_failure(
            std::vector<std::uint8_t>(header.begin(), header.end()),
            "zero PFM dimension is rejected");
    }
    {
        const std::string header = "PF\n1 1\n-2.0\n";
        check_pfm_parse_failure(
            std::vector<std::uint8_t>(header.begin(), header.end()),
            "non-unit PFM scale magnitude is rejected");
    }
    {
        std::vector<std::uint8_t> truncated = valid;
        truncated.pop_back();
        check_pfm_parse_failure(truncated, "truncated PFM raster is rejected");
    }
    {
        std::vector<std::uint8_t> trailing = valid;
        trailing.push_back(0U);
        check_pfm_parse_failure(trailing, "trailing PFM raster bytes are rejected");
    }
    {
        const std::string header = "PF\n1 1\n-1.0\n";
        std::vector<std::uint8_t> nonfinite(header.begin(), header.end());
        append_u32_le(nonfinite, 0x7FC00000U);
        append_u32_le(nonfinite, 0x00000000U);
        append_u32_le(nonfinite, 0x00000000U);
        check_pfm_parse_failure(nonfinite, "non-finite PFM texel is rejected");
    }
    {
        const std::string header = "PF\n100000 100000\n-1.0\n";
        check_pfm_parse_failure(
            std::vector<std::uint8_t>(header.begin(), header.end()),
            "oversized PFM raster is rejected before payload allocation/read");
    }
}

void test_pfm_shared_dispatch_is_linear_only() {
    const std::filesystem::path path = "output_transfer_dispatch.PFM";
    std::filesystem::remove(path);

    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({-0.25F, 1.25F, 2.5F});
    framebuffer.write_pfm(path.string());

    const Texture2D texture = load_texture_image_file(path, TextureTransferFunction::Linear);
    check(
        exact_vec3(texture.texel(0U, 0U), {-0.25F, 1.25F, 2.5F}),
        "case-insensitive shared image dispatch preserves linear HDR PFM texel");
    check(
        texture.source_transfer_function() == TextureTransferFunction::Linear,
        "shared PFM dispatch records linear source interpretation");
    check_throws<std::invalid_argument>(
        [&] { (void)load_texture_image_file(path, TextureTransferFunction::Srgb); },
        "shared PFM dispatch rejects incompatible sRGB interpretation");

    std::filesystem::remove(path);
}

}  // namespace

int main() {
    test_piecewise_srgb_encoding_and_clamp();
    test_linear_default_compatibility_and_hashes();
    test_multisample_resolve_happens_before_encoding();
    test_ppm_overloads_and_fail_closed_validation();
    test_linear_pfm_exact_bytes_and_round_trip();
    test_linear_pfm_uses_resolved_multisample_color();
    test_linear_pfm_fail_closed_and_8bit_independence();
    test_big_endian_pfm_import();
    test_pfm_import_validation();
    test_pfm_shared_dispatch_is_linear_only();

    if (failures != 0) {
        std::cerr << failures << " output-transfer test(s) failed\n";
        return 1;
    }
    std::cout << "output-transfer tests passed\n";
    return 0;
}
