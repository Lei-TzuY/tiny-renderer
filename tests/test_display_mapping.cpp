#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/obj_loader.hpp"
#include "tiny_renderer/offline_render.hpp"

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
    check(static_cast<bool>(input), "test output opens for reading");
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

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    for (const std::uint8_t byte : bytes) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= prime;
    }
    return hash;
}

void test_component_reinhard_and_negative_rule() {
    Framebuffer framebuffer(2U, 1U);
    check(
        framebuffer.test_and_write(0U, 0U, 0.0F, {-2.0F, 0.0F, 1.0F}),
        "first HDR display fixture write passes");
    check(
        framebuffer.test_and_write(1U, 0U, 0.0F, {3.0F, 0.5F, 0.25F}),
        "second HDR display fixture write passes");

    const DisplayMappingState mapping{2.0F, ToneMapOperator::Reinhard};
    const std::vector<std::uint8_t> expected{
        0U, 0U, 170U,
        219U, 128U, 85U,
    };
    check(
        framebuffer.rgb8(mapping, OutputTransferFunction::Linear) == expected,
        "display mapping applies negative-to-zero, exposure, and component Reinhard before linear quantization");
}

void test_transfer_runs_after_tone_mapping_and_hashes_mapped_bytes() {
    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({1.0F, 1.0F, 1.0F});
    const DisplayMappingState mapping{1.0F, ToneMapOperator::Reinhard};

    const std::vector<std::uint8_t> linear{128U, 128U, 128U};
    const std::vector<std::uint8_t> srgb{188U, 188U, 188U};
    check(
        framebuffer.rgb8(mapping, OutputTransferFunction::Linear) == linear,
        "Reinhard maps linear one to one-half before linear 8-bit quantization");
    check(
        framebuffer.rgb8(mapping, OutputTransferFunction::Srgb) == srgb,
        "sRGB encoding occurs after Reinhard mapping of resolved linear color");
    check(
        framebuffer.fnv1a64(mapping, OutputTransferFunction::Linear) == fnv1a64(linear),
        "display-mapped linear hash is computed from mapped output bytes");
    check(
        framebuffer.fnv1a64(mapping, OutputTransferFunction::Srgb) == fnv1a64(srgb),
        "display-mapped sRGB hash is computed from mapped encoded bytes");
}

void test_multisample_resolve_precedes_display_mapping() {
    Framebuffer single(1U, 1U);
    single.clear({2.0F, 2.0F, 2.0F});

    Framebuffer multi(1U, 1U, SampleCount::Four);
    multi.clear({0.0F, 0.0F, 0.0F});
    const std::vector<float> sample_values{0.0F, 1.0F, 2.0F, 5.0F};
    for (std::size_t sample = 0U; sample < sample_values.size(); ++sample) {
        const float value = sample_values[sample];
        check(
            multi.test_and_write_sample(0U, 0U, sample, 0.0F, {value, value, value}),
            "multisample HDR display fixture write passes");
    }
    check(
        multi.color_at(0U, 0U).x == 2.0F,
        "multisample HDR fixture resolves to linear two before output mapping");

    const DisplayMappingState mapping{1.0F, ToneMapOperator::Reinhard};
    check(
        multi.rgb8(mapping, OutputTransferFunction::Linear)
            == single.rgb8(mapping, OutputTransferFunction::Linear),
        "equal resolved 1x and 4x linear colors have identical mapped linear output");
    check(
        multi.rgb8(mapping, OutputTransferFunction::Srgb)
            == single.rgb8(mapping, OutputTransferFunction::Srgb),
        "equal resolved 1x and 4x linear colors have identical mapped sRGB output");
}

void check_write_rejects_without_mutation(
    const DisplayMappingState& mapping,
    OutputTransferFunction transfer_function,
    const std::string& label) {
    const std::filesystem::path path = "display_mapping_invalid.ppm";
    const std::string sentinel = "preserve-me";
    {
        std::ofstream output(path, std::ios::binary);
        output << sentinel;
    }

    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({1.0F, 1.0F, 1.0F});
    check_throws<std::invalid_argument>(
        [&] { framebuffer.write_ppm(path.string(), mapping, transfer_function); },
        label + " is rejected by mapped PPM output");
    const std::vector<std::uint8_t> expected(sentinel.begin(), sentinel.end());
    check(
        read_binary_file(path) == expected,
        label + " rejection occurs before destination truncation");
    std::filesystem::remove(path);
}

void test_invalid_mapping_is_fail_closed() {
    check_write_rejects_without_mutation(
        DisplayMappingState{-1.0F, ToneMapOperator::Reinhard},
        OutputTransferFunction::Linear,
        "negative exposure");
    check_write_rejects_without_mutation(
        DisplayMappingState{std::numeric_limits<float>::quiet_NaN(), ToneMapOperator::Reinhard},
        OutputTransferFunction::Linear,
        "NaN exposure");
    check_write_rejects_without_mutation(
        DisplayMappingState{std::numeric_limits<float>::infinity(), ToneMapOperator::Reinhard},
        OutputTransferFunction::Linear,
        "infinite exposure");
    check_write_rejects_without_mutation(
        DisplayMappingState{1.0F, static_cast<ToneMapOperator>(255)},
        OutputTransferFunction::Linear,
        "unknown tone-map operator");
    check_write_rejects_without_mutation(
        DisplayMappingState{},
        static_cast<OutputTransferFunction>(255),
        "unknown transfer after display mapping");

    const std::filesystem::path path = "display_mapping_nonfinite.ppm";
    std::filesystem::remove(path);
    Framebuffer nonfinite(1U, 1U);
    nonfinite.clear({std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    check_throws<std::invalid_argument>(
        [&] { nonfinite.write_ppm(path.string(), DisplayMappingState{}); },
        "non-finite resolved color is rejected by display mapping");
    check(!std::filesystem::exists(path), "non-finite mapped output rejects before file creation");
}

void test_mapping_does_not_mutate_legacy_or_pfm_output() {
    const std::filesystem::path before_path = "display_mapping_before.pfm";
    const std::filesystem::path after_path = "display_mapping_after.pfm";
    const std::filesystem::path ppm_path = "display_mapping.ppm";
    std::filesystem::remove(before_path);
    std::filesystem::remove(after_path);
    std::filesystem::remove(ppm_path);

    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({-1.0F, 0.5F, 2.0F});
    const Vec3 stored_before = framebuffer.color_at(0U, 0U);
    const std::vector<std::uint8_t> legacy_before = framebuffer.rgb8();
    const std::vector<std::uint8_t> srgb_before = framebuffer.rgb8(OutputTransferFunction::Srgb);
    const std::uint64_t legacy_hash_before = framebuffer.fnv1a64();
    const std::uint64_t srgb_hash_before = framebuffer.fnv1a64(OutputTransferFunction::Srgb);
    framebuffer.write_pfm(before_path.string());

    const DisplayMappingState mapping{2.0F, ToneMapOperator::Reinhard};
    const std::vector<std::uint8_t> mapped = framebuffer.rgb8(mapping, OutputTransferFunction::Srgb);
    framebuffer.write_ppm(ppm_path.string(), mapping, OutputTransferFunction::Srgb);
    framebuffer.write_pfm(after_path.string());

    const Vec3 stored_after = framebuffer.color_at(0U, 0U);
    check(
        stored_after.x == stored_before.x
            && stored_after.y == stored_before.y
            && stored_after.z == stored_before.z,
        "display mapping never mutates resolved linear framebuffer storage");
    check(framebuffer.rgb8() == legacy_before, "legacy linear RGB8 remains unchanged after mapped export");
    check(
        framebuffer.rgb8(OutputTransferFunction::Srgb) == srgb_before,
        "transfer-only sRGB RGB8 remains unchanged after mapped export");
    check(framebuffer.fnv1a64() == legacy_hash_before, "legacy hash remains unchanged after mapped export");
    check(
        framebuffer.fnv1a64(OutputTransferFunction::Srgb) == srgb_hash_before,
        "transfer-only sRGB hash remains unchanged after mapped export");
    check(
        read_binary_file(before_path) == read_binary_file(after_path),
        "PFM archival output is byte-identical before and after display mapping");

    const std::vector<std::uint8_t> ppm = read_binary_file(ppm_path);
    const std::string header = "P6\n1 1\n255\n";
    std::vector<std::uint8_t> expected(header.begin(), header.end());
    expected.insert(expected.end(), mapped.begin(), mapped.end());
    check(ppm == expected, "display-mapped PPM stores the same bytes returned by mapped RGB8");

    std::filesystem::remove(before_path);
    std::filesystem::remove(after_path);
    std::filesystem::remove(ppm_path);
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to create headless display fixture");
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("failed to write headless display fixture");
    }
}

std::string quote(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

int run_renderer(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments) {
    std::string command = quote(executable);
    for (const std::string& argument : arguments) {
        command += " \"" + argument + "\"";
    }
    return std::system(command.c_str());
}

std::filesystem::path renderer_executable(const std::filesystem::path& argv0) {
    std::filesystem::path executable = std::filesystem::absolute(argv0).parent_path()
        / "tiny_renderer_render";
    if (!std::filesystem::exists(executable)) {
        const std::filesystem::path exe_candidate = executable.string() + ".exe";
        if (std::filesystem::exists(exe_candidate)) {
            executable = exe_candidate;
        }
    }
    return executable;
}

void test_headless_display_output_controls(const std::filesystem::path& argv0) {
    const std::filesystem::path executable = renderer_executable(argv0);
    check(std::filesystem::exists(executable), "headless renderer is available to display-output integration test");
    if (!std::filesystem::exists(executable)) {
        return;
    }

    const std::size_t path_hash = std::hash<std::string>{}(
        std::filesystem::absolute(argv0).string());
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("tiny_renderer_display_cli_" + std::to_string(path_hash));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::filesystem::path obj = root / "display.obj";
    const std::filesystem::path mtl = root / "display.mtl";
    write_text_file(
        obj,
        "mtllib display.mtl\n"
        "v -0.8 -0.8 0\n"
        "v 0.8 -0.8 0\n"
        "v 0 0.8 0\n"
        "usemtl matte\n"
        "f 1 2 3\n");
    write_text_file(
        mtl,
        "newmtl matte\n"
        "Kd 0.8 0.35 0.1\n");

    const std::filesystem::path legacy_ppm = root / "legacy.ppm";
    const std::filesystem::path explicit_default_ppm = root / "explicit_default.ppm";
    const std::filesystem::path custom_ppm = root / "custom.ppm";
    const std::filesystem::path library_ppm = root / "library.ppm";
    const std::filesystem::path cli_pfm = root / "archive.pfm";
    const std::filesystem::path library_pfm = root / "library.pfm";

    check(
        run_renderer(executable, {obj.string(), legacy_ppm.string(), "32", "24", "4"}) == 0,
        "legacy headless PPM render succeeds");
    check(
        run_renderer(
            executable,
            {obj.string(), explicit_default_ppm.string(), "32", "24", "4",
             "--display-exposure", "1", "--output-transfer", "srgb"}) == 0,
        "explicit default headless display controls succeed");
    if (std::filesystem::exists(legacy_ppm) && std::filesystem::exists(explicit_default_ppm)) {
        check(
            read_binary_file(legacy_ppm) == read_binary_file(explicit_default_ppm),
            "implicit and explicit default display controls are byte-identical");
    }

    check(
        run_renderer(
            executable,
            {obj.string(), custom_ppm.string(), "32", "24", "4",
             "--display-exposure", "2", "--output-transfer", "linear"}) == 0,
        "custom headless display controls succeed");

    const ModelAsset asset = load_obj_model_asset_file(obj);
    OfflineRenderSettings settings{};
    settings.width = 32U;
    settings.height = 24U;
    settings.sample_count = SampleCount::Four;
    const Framebuffer framebuffer = render_model_preview(asset, settings);
    const DisplayMappingState mapping{2.0F, ToneMapOperator::Reinhard};
    framebuffer.write_ppm(library_ppm.string(), mapping, OutputTransferFunction::Linear);
    if (std::filesystem::exists(custom_ppm) && std::filesystem::exists(library_ppm)) {
        check(
            read_binary_file(custom_ppm) == read_binary_file(library_ppm),
            "headless exposure/transfer output is byte-identical to the library display boundary");
        check(
            read_binary_file(custom_ppm) != read_binary_file(legacy_ppm),
            "custom display controls observably change PPM output");
    }

    check(
        run_renderer(executable, {obj.string(), cli_pfm.string(), "32", "24", "4"}) == 0,
        "legacy PFM archival render succeeds");
    framebuffer.write_pfm(library_pfm.string());
    if (std::filesystem::exists(cli_pfm) && std::filesystem::exists(library_pfm)) {
        check(
            read_binary_file(cli_pfm) == read_binary_file(library_pfm),
            "headless PFM remains byte-identical to the data-preserving library path");
    }

    const std::filesystem::path invalid_pfm = root / "invalid_display_control.pfm";
    check(
        run_renderer(
            executable,
            {obj.string(), invalid_pfm.string(), "32", "24", "4",
             "--display-exposure", "2"}) != 0,
        "PFM rejects display-only exposure control");
    check(!std::filesystem::exists(invalid_pfm), "PFM display-control rejection occurs before output creation");

    const std::filesystem::path invalid_transfer_pfm = root / "invalid_transfer_control.pfm";
    check(
        run_renderer(
            executable,
            {obj.string(), invalid_transfer_pfm.string(), "32", "24", "4",
             "--output-transfer", "linear"}) != 0,
        "PFM rejects display-only transfer control");
    check(
        !std::filesystem::exists(invalid_transfer_pfm),
        "PFM transfer-control rejection occurs before output creation");

    const std::filesystem::path duplicate_ppm = root / "duplicate.ppm";
    check(
        run_renderer(
            executable,
            {obj.string(), duplicate_ppm.string(), "32", "24", "4",
             "--display-exposure", "1", "--display-exposure", "2"}) != 0,
        "duplicate display exposure is rejected");
    check(!std::filesystem::exists(duplicate_ppm), "duplicate display exposure rejects before output creation");

    const std::filesystem::path negative_ppm = root / "negative.ppm";
    check(
        run_renderer(
            executable,
            {obj.string(), negative_ppm.string(), "32", "24", "4",
             "--display-exposure", "-1"}) != 0,
        "negative display exposure is rejected");
    check(!std::filesystem::exists(negative_ppm), "negative exposure rejects before output creation");

    const std::filesystem::path unknown_transfer_ppm = root / "unknown_transfer.ppm";
    check(
        run_renderer(
            executable,
            {obj.string(), unknown_transfer_ppm.string(), "32", "24", "4",
             "--output-transfer", "gamma22"}) != 0,
        "unknown output transfer is rejected");
    check(
        !std::filesystem::exists(unknown_transfer_ppm),
        "unknown output transfer rejects before output creation");

    std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main(int argc, char** argv) {
    test_component_reinhard_and_negative_rule();
    test_transfer_runs_after_tone_mapping_and_hashes_mapped_bytes();
    test_multisample_resolve_precedes_display_mapping();
    test_invalid_mapping_is_fail_closed();
    test_mapping_does_not_mutate_legacy_or_pfm_output();
    if (argc > 0) {
        test_headless_display_output_controls(argv[0]);
    }

    if (failures != 0) {
        std::cerr << failures << " display-mapping test(s) failed\n";
        return 1;
    }
    std::cout << "display-mapping tests passed\n";
    return 0;
}
