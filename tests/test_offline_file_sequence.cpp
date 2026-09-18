#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/offline_sequence.hpp"

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

bool same_camera(const OfflineSceneCamera& a, const OfflineSceneCamera& b) {
    return a.eye.x == b.eye.x && a.eye.y == b.eye.y && a.eye.z == b.eye.z
        && a.target.x == b.target.x && a.target.y == b.target.y && a.target.z == b.target.z
        && a.up.x == b.up.x && a.up.y == b.up.y && a.up.z == b.up.z
        && a.vertical_fov_radians == b.vertical_fov_radians
        && a.near_plane == b.near_plane
        && a.far_plane == b.far_plane;
}

std::filesystem::path source_dir() {
#ifdef TINY_RENDERER_SOURCE_DIR
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path render_cli_path(const char* argv0) {
    std::filesystem::path cli =
        std::filesystem::absolute(argv0).parent_path() / "tiny_renderer_render";
#ifdef _WIN32
    cli += ".exe";
#endif
    return cli;
}

std::string quote_path(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

std::vector<char> read_binary_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<char>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::filesystem::path indexed_output(
    const std::filesystem::path& base,
    std::size_t index) {
    std::string suffix = "_";
    const std::string digits = std::to_string(index);
    suffix.append(4U - digits.size(), '0');
    suffix += digits;
    return base.parent_path()
        / (base.stem().string() + suffix + base.extension().string());
}

int run_sequence_cli(
    const std::filesystem::path& cli,
    const std::filesystem::path& scene,
    const std::filesystem::path& output,
    const std::filesystem::path& cameras) {
    const std::string command =
        quote_path(cli)
        + " " + quote_path(scene)
        + " " + quote_path(output)
        + " 64 48 4 --camera-sequence " + quote_path(cameras);
    return std::system(command.c_str());
}

void test_strict_bounded_camera_sequence_loader() {
    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const std::vector<OfflineSceneCamera> cameras =
        load_offline_camera_sequence_file(fixtures / "camera_sequence_aba.trcameras");
    check(cameras.size() == 3U, "A-B-A sidecar loads exactly three cameras");
    if (cameras.size() == 3U) {
        check(same_camera(cameras[0], cameras[2]),
              "A-B-A sidecar preserves exact repeated camera records");
        check(!same_camera(cameras[0], cameras[1]),
              "A-B-A sidecar preserves a distinct middle camera");
    }

    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_camera_sequence_file(
                fixtures / "camera_sequence_invalid_later.trcameras");
        },
        "later invalid camera rejects the complete sidecar");

    const std::filesystem::path root =
        std::filesystem::current_path() / "tiny_renderer_camera_sequence_parser_fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    {
        std::ofstream missing_header(root / "missing_header.trcameras");
        missing_header
            << "camera 0 0 3 0 0 0 0 1 0 0.8726646 0.1 100\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_camera_sequence_file(
                root / "missing_header.trcameras");
        },
        "camera sidecar requires the exact version header");

    {
        std::ofstream trailing(root / "trailing.trcameras");
        trailing
            << "tiny-renderer-camera-sequence-v1\n"
            << "camera 0 0 3 0 0 0 0 1 0 0.8726646 0.1 100 extra\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_camera_sequence_file(root / "trailing.trcameras");
        },
        "camera sidecar rejects trailing camera tokens");

    {
        std::ofstream oversized(root / "oversized.trcameras");
        oversized << "tiny-renderer-camera-sequence-v1\n";
        for (std::size_t i = 0U; i < detail::kMaxOfflineSequenceCameras + 1U; ++i) {
            oversized
                << "camera 0 0 3 0 0 0 0 1 0 0.8726646 0.1 100\n";
        }
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_camera_sequence_file(root / "oversized.trcameras");
        },
        "camera sidecar rejects more than the bounded camera count");

    std::filesystem::remove_all(root, ignored);
}

void test_file_driven_cli_sequence_transaction(const char* argv0) {
    const std::filesystem::path cli = render_cli_path(argv0);
    check(std::filesystem::exists(cli),
          "camera sequence integration locates tiny_renderer_render sibling executable");
    if (!std::filesystem::exists(cli)) {
        return;
    }

    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const std::filesystem::path scene = fixtures / "flat_scene_sequence_mixed.trscene";
    const std::filesystem::path cameras = fixtures / "camera_sequence_aba.trcameras";
    const std::filesystem::path invalid =
        fixtures / "camera_sequence_invalid_later.trcameras";

    const std::filesystem::path root =
        std::filesystem::current_path() / "tiny_renderer_camera_sequence_cli_fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::filesystem::path first_base = root / "sequence.ppm";
    check(run_sequence_cli(cli, scene, first_base, cameras) == 0,
          "file-driven A-B-A camera sequence renders successfully");

    const std::array<std::filesystem::path, 3> first_frames{{
        indexed_output(first_base, 0U),
        indexed_output(first_base, 1U),
        indexed_output(first_base, 2U),
    }};
    for (const auto& frame : first_frames) {
        check(std::filesystem::exists(frame),
              "camera sequence creates every indexed frame output");
    }
    check(!std::filesystem::exists(first_base),
          "camera sequence does not ambiguously write the unsuffixed output path");

    if (std::filesystem::exists(first_frames[0])
        && std::filesystem::exists(first_frames[1])
        && std::filesystem::exists(first_frames[2])) {
        const std::vector<char> a0 = read_binary_file(first_frames[0]);
        const std::vector<char> b = read_binary_file(first_frames[1]);
        const std::vector<char> a1 = read_binary_file(first_frames[2]);
        check(a0 == a1,
              "repeated A camera frames are byte-identical in file-driven output");
        check(a0 != b,
              "distinct B camera produces observably distinct file-driven output");
    }

    const std::filesystem::path repeat_base = root / "repeat.ppm";
    check(run_sequence_cli(cli, scene, repeat_base, cameras) == 0,
          "repeated file-driven camera sequence invocation succeeds");
    for (std::size_t i = 0U; i < first_frames.size(); ++i) {
        const std::filesystem::path repeat = indexed_output(repeat_base, i);
        check(std::filesystem::exists(repeat),
              "repeated camera sequence creates every indexed frame");
        if (std::filesystem::exists(first_frames[i])
            && std::filesystem::exists(repeat)) {
            check(read_binary_file(first_frames[i]) == read_binary_file(repeat),
                  "file-driven sequence output is byte-deterministic across runs");
        }
    }

    const std::filesystem::path invalid_base = root / "invalid.ppm";
    check(run_sequence_cli(cli, scene, invalid_base, invalid) != 0,
          "later invalid camera rejects the CLI sequence");
    check(!std::filesystem::exists(invalid_base),
          "invalid camera sequence never writes an unsuffixed output");
    check(!std::filesystem::exists(indexed_output(invalid_base, 0U))
              && !std::filesystem::exists(indexed_output(invalid_base, 1U)),
          "later invalid camera rejects before any earlier frame file is written");

    std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main(int argc, char** argv) {
    test_strict_bounded_camera_sequence_loader();
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        test_file_driven_cli_sequence_transaction(argv[0]);
    } else {
        check(false, "camera sequence integration test executable path is available");
    }

    if (failures != 0) {
        std::cerr << failures << " offline file-sequence test(s) failed\n";
        return 1;
    }
    std::cout << "offline file-sequence tests passed\n";
    return 0;
}
