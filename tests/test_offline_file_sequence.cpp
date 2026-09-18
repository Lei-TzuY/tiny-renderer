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

bool same_matrix(const Mat4& a, const Mat4& b) {
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            if (a(row, column) != b(row, column)) {
                return false;
            }
        }
    }
    return true;
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

int run_frame_sequence_cli(
    const std::filesystem::path& cli,
    const std::filesystem::path& scene,
    const std::filesystem::path& output,
    const std::filesystem::path& frames) {
    const std::string command =
        quote_path(cli)
        + " " + quote_path(scene)
        + " " + quote_path(output)
        + " 64 48 4 --frame-sequence " + quote_path(frames);
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

void test_strict_bounded_frame_sequence_loader() {
    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const std::vector<OfflineSceneFrameState> frames =
        load_offline_frame_sequence_file(
            fixtures / "frame_sequence_aba.trframes",
            3U);
    check(frames.size() == 3U,
          "A-B-A affine sidecar loads exactly three frame records");
    if (frames.size() == 3U) {
        check(same_camera(frames[0].camera, frames[1].camera)
                  && same_camera(frames[0].camera, frames[2].camera),
              "A-B-A affine fixture keeps camera fixed so transform changes are isolated");
        check(frames[0].model_transforms.size() == 3U
                  && frames[1].model_transforms.size() == 3U
                  && frames[2].model_transforms.size() == 3U,
              "every parsed frame owns one exact transform per prepared scene entry");
        if (frames[0].model_transforms.size() == 3U
            && frames[1].model_transforms.size() == 3U
            && frames[2].model_transforms.size() == 3U) {
            check(
                same_matrix(
                    frames[0].model_transforms[0],
                    frames[2].model_transforms[0])
                    && same_matrix(
                        frames[0].model_transforms[1],
                        frames[2].model_transforms[1])
                    && same_matrix(
                        frames[0].model_transforms[2],
                        frames[2].model_transforms[2]),
                "repeated A frame preserves exact row-major affine matrices");
            check(
                !same_matrix(
                    frames[0].model_transforms[0],
                    frames[1].model_transforms[0]),
                "middle B frame carries an observably distinct model transform");
            check(
                frames[0].model_transforms[0](0U, 3U) == 0.55F
                    && frames[0].model_transforms[0](2U, 3U) == 0.25F,
                "row-major frame matrix parsing preserves translation components");
        }
    }

    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_frame_sequence_file(
                fixtures / "frame_sequence_invalid_later.trframes",
                3U);
        },
        "later finite projective transform rejects the complete affine sidecar");

    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_frame_sequence_file(
                fixtures / "frame_sequence_aba.trframes",
                detail::kMaxOfflineSceneEntries + 1U);
        },
        "frame sidecar rejects an expected model count above the bounded scene entry limit before allocation");

    const std::filesystem::path root =
        std::filesystem::current_path() / "tiny_renderer_frame_sequence_parser_fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    {
        std::ofstream missing_header(root / "missing_header.trframes");
        missing_header
            << "frame 0 0 3 0 0 0 0 1 0 0.8726646 0.1 100\n"
            << "end\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_frame_sequence_file(
                root / "missing_header.trframes",
                0U);
        },
        "frame sidecar requires the exact version header");

    {
        std::ofstream short_frame(root / "short_frame.trframes");
        short_frame
            << "tiny-renderer-frame-sequence-v1\n"
            << "frame 0 0 3 0 0 0 0 1 0 0.8726646 0.1 100\n"
            << "model 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n"
            << "end\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_frame_sequence_file(
                root / "short_frame.trframes",
                2U);
        },
        "frame sidecar rejects fewer transforms than the prepared scene entry count");

    {
        std::ofstream extra_model(root / "extra_model.trframes");
        extra_model
            << "tiny-renderer-frame-sequence-v1\n"
            << "frame 0 0 3 0 0 0 0 1 0 0.8726646 0.1 100\n"
            << "model 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n"
            << "model 1 0 0 1 0 1 0 0 0 0 1 0 0 0 0 1\n"
            << "end\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_frame_sequence_file(
                root / "extra_model.trframes",
                1U);
        },
        "frame sidecar rejects extra transforms beyond the prepared scene entry count");

    {
        std::ofstream unterminated(root / "unterminated.trframes");
        unterminated
            << "tiny-renderer-frame-sequence-v1\n"
            << "frame 0 0 3 0 0 0 0 1 0 0.8726646 0.1 100\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_frame_sequence_file(
                root / "unterminated.trframes",
                0U);
        },
        "frame sidecar rejects an unterminated frame transaction");

    {
        std::ofstream oversized(root / "oversized.trframes");
        oversized << "tiny-renderer-frame-sequence-v1\n";
        for (std::size_t i = 0U;
             i < detail::kMaxOfflineSequenceCameras + 1U;
             ++i) {
            oversized
                << "frame 0 0 3 0 0 0 0 1 0 0.8726646 0.1 100\n"
                << "end\n";
        }
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_frame_sequence_file(
                root / "oversized.trframes",
                0U);
        },
        "frame sidecar rejects more than the bounded frame count");

    std::filesystem::remove_all(root, ignored);
}

void test_file_driven_affine_frame_sequence_transaction(const char* argv0) {
    const std::filesystem::path cli = render_cli_path(argv0);
    check(std::filesystem::exists(cli),
          "affine frame sequence integration locates tiny_renderer_render sibling executable");
    if (!std::filesystem::exists(cli)) {
        return;
    }

    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const std::filesystem::path scene = fixtures / "flat_scene_sequence_mixed.trscene";
    const std::filesystem::path frames = fixtures / "frame_sequence_aba.trframes";
    const std::filesystem::path invalid =
        fixtures / "frame_sequence_invalid_later.trframes";
    const std::filesystem::path cameras =
        fixtures / "camera_sequence_aba.trcameras";

    const std::filesystem::path root =
        std::filesystem::current_path() / "tiny_renderer_frame_sequence_cli_fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::filesystem::path first_base = root / "frames.ppm";
    check(run_frame_sequence_cli(cli, scene, first_base, frames) == 0,
          "file-driven A-B-A affine frame sequence renders successfully");
    const std::array<std::filesystem::path, 3> outputs{{
        indexed_output(first_base, 0U),
        indexed_output(first_base, 1U),
        indexed_output(first_base, 2U),
    }};
    for (const auto& output : outputs) {
        check(std::filesystem::exists(output),
              "affine frame sequence creates every indexed output");
    }
    check(!std::filesystem::exists(first_base),
          "affine frame sequence never writes an ambiguous unsuffixed output");
    if (std::filesystem::exists(outputs[0])
        && std::filesystem::exists(outputs[1])
        && std::filesystem::exists(outputs[2])) {
        const std::vector<char> a0 = read_binary_file(outputs[0]);
        const std::vector<char> b = read_binary_file(outputs[1]);
        const std::vector<char> a1 = read_binary_file(outputs[2]);
        check(a0 == a1,
              "repeated A affine frames are byte-identical");
        check(a0 != b,
              "B differs with a fixed camera, proving file-driven model transforms execute");
    }

    // Frame A encodes the manifest's original transforms exactly and uses the
    // first compatibility camera, so both input paths must produce the same
    // canonical prepared-scene output.
    const std::filesystem::path camera_reference = root / "camera_reference.ppm";
    check(run_sequence_cli(cli, scene, camera_reference, cameras) == 0,
          "camera-sequence compatibility reference renders successfully");
    const std::filesystem::path camera_a = indexed_output(camera_reference, 0U);
    if (std::filesystem::exists(outputs[0])
        && std::filesystem::exists(camera_a)) {
        check(read_binary_file(outputs[0]) == read_binary_file(camera_a),
              "explicit frame-A matrices are byte-equivalent to the same static manifest transforms");
    }

    const std::filesystem::path repeat_base = root / "repeat.ppm";
    check(run_frame_sequence_cli(cli, scene, repeat_base, frames) == 0,
          "repeated affine frame-sequence invocation succeeds");
    for (std::size_t i = 0U; i < outputs.size(); ++i) {
        const std::filesystem::path repeat = indexed_output(repeat_base, i);
        check(std::filesystem::exists(repeat),
              "repeated affine sequence creates every indexed frame");
        if (std::filesystem::exists(outputs[i])
            && std::filesystem::exists(repeat)) {
            check(read_binary_file(outputs[i]) == read_binary_file(repeat),
                  "file-driven affine frame output is deterministic across runs");
        }
    }

    const std::filesystem::path invalid_base = root / "invalid.ppm";
    check(run_frame_sequence_cli(cli, scene, invalid_base, invalid) != 0,
          "later invalid projective transform rejects the CLI frame transaction");
    check(!std::filesystem::exists(invalid_base)
              && !std::filesystem::exists(indexed_output(invalid_base, 0U))
              && !std::filesystem::exists(indexed_output(invalid_base, 1U)),
          "later invalid affine frame rejects before any indexed output is written");

    const std::filesystem::path conflict_base = root / "conflict.ppm";
    const std::string conflict_command =
        quote_path(cli)
        + " " + quote_path(scene)
        + " " + quote_path(conflict_base)
        + " 64 48 4 --camera-sequence " + quote_path(cameras)
        + " --frame-sequence " + quote_path(frames);
    check(std::system(conflict_command.c_str()) != 0,
          "camera and affine frame sidecars are rejected as ambiguous together");
    check(!std::filesystem::exists(conflict_base)
              && !std::filesystem::exists(indexed_output(conflict_base, 0U)),
          "sidecar-option conflict rejects before output");

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
    test_strict_bounded_frame_sequence_loader();
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        test_file_driven_cli_sequence_transaction(argv[0]);
        test_file_driven_affine_frame_sequence_transaction(argv[0]);
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
