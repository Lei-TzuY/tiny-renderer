#include <cstddef>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "tiny_renderer/model_fingerprint.hpp"
#include "tiny_renderer/model_inspection.hpp"
#include "tiny_renderer/obj_loader.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::filesystem::path fixture_path(const char* name) {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for asset inspection tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR) / "tests" / "fixtures" / name;
}

std::string fingerprint_line(const ModelAsset& asset) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << model_asset_fnv1a64(asset);
    return "fingerprint_fnv1a64=" + output.str() + "\n";
}

void test_file_driven_summary_is_deterministic_and_ordered() {
    const std::filesystem::path fixture = fixture_path("material_texture_sequence.obj");
    const ModelAsset first = load_obj_model_asset_file(fixture);
    const ModelAsset second = load_obj_model_asset_file(fixture);

    const std::string first_summary = inspect_model_asset(first);
    const std::string second_summary = inspect_model_asset(second);
    check(first_summary == second_summary,
          "independent canonical file loads produce byte-identical inspection output");

    check(first_summary.starts_with(
              "format=tiny-renderer-model-asset-inspect-v1\n" + fingerprint_line(first)),
          "inspection output begins with versioned format and canonical fingerprint");
    check(first_summary.find("vertices=4\n") != std::string::npos,
          "inspection exposes four canonical unified vertices from the fixture");
    check(first_summary.find("triangles=3\n") != std::string::npos,
          "inspection exposes canonical triangle count");
    check(first_summary.find("draws=3\n") != std::string::npos,
          "inspection exposes ordered material draw count");

    const std::size_t warm0 = first_summary.find("draw[0].material=warm\n");
    const std::size_t cool1 = first_summary.find("draw[1].material=cool\n");
    const std::size_t warm2 = first_summary.find("draw[2].material=warm\n");
    check(warm0 != std::string::npos && cool1 != std::string::npos && warm2 != std::string::npos,
          "inspection exposes every canonical material name");
    check(warm0 < cool1 && cool1 < warm2,
          "inspection preserves canonical A-B-A material draw order");

    check(first_summary.find("draw[0].first_triangle=0\n") != std::string::npos
              && first_summary.find("draw[1].first_triangle=1\n") != std::string::npos
              && first_summary.find("draw[2].first_triangle=2\n") != std::string::npos,
          "inspection exposes contiguous canonical draw ranges");
    check(first_summary.find("draw[1].diffuse_texture=none\n") != std::string::npos,
          "inspection distinguishes Kd-only draws from mapped draws");
}

void test_structural_mutation_changes_summary() {
    ModelAsset asset = load_obj_model_asset_file(fixture_path("material_texture_sequence.obj"));
    const std::string before = inspect_model_asset(asset);
    check(!asset.draws.empty(), "inspection mutation fixture contains a material draw");
    if (!asset.draws.empty()) {
        asset.draws.front().material_name = "changed";
        const std::string after = inspect_model_asset(asset);
        check(before != after,
              "logical ModelAsset mutation changes the deterministic inspection output");
        check(after.find("draw[0].material=changed\n") != std::string::npos,
              "inspection reflects the mutated canonical material identity");
    }
}

}  // namespace

int main() {
    try {
        test_file_driven_summary_is_deterministic_and_ordered();
        test_structural_mutation_changes_summary();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " asset inspection test(s) failed\n";
        return 1;
    }
    std::cout << "all asset inspection tests passed\n";
    return 0;
}
