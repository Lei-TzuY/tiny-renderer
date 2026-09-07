#include <cmath>
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
    check(static_cast<bool>(input), "test output file opens for reading");
    const std::vector<char> raw(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    std::vector<std::uint8_t> bytes;
    bytes.reserve(raw.size());
    for (const char value : raw) {
        bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
    }
    return bytes;
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

}  // namespace

int main() {
    test_piecewise_srgb_encoding_and_clamp();
    test_linear_default_compatibility_and_hashes();
    test_multisample_resolve_happens_before_encoding();
    test_ppm_overloads_and_fail_closed_validation();

    if (failures != 0) {
        std::cerr << failures << " output-transfer test(s) failed\n";
        return 1;
    }
    std::cout << "output-transfer tests passed\n";
    return 0;
}
