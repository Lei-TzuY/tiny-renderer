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

bool exact_frame_equal(const Framebuffer& left, const Framebuffer& right) {
    if (left.width() != right.width()
        || left.height() != right.height()
        || left.samples_per_pixel() != right.samples_per_pixel()
        || left.rgb8() != right.rgb8()
        || left.fnv1a64() != right.fnv1a64()) {
        return false;
    }
    for (std::size_t y = 0U; y < left.height(); ++y) {
        for (std::size_t x = 0U; x < left.width(); ++x) {
            for (std::size_t sample = 0U; sample < left.samples_per_pixel(); ++sample) {
                const Vec3 a = left.sample_color_at(x, y, sample);
                const Vec3 b = right.sample_color_at(x, y, sample);
                if (a.x != b.x || a.y != b.y || a.z != b.z
                    || left.sample_depth_at(x, y, sample) != right.sample_depth_at(x, y, sample)
                    || left.sample_stencil_at(x, y, sample) != right.sample_stencil_at(x, y, sample)) {
                    return false;
                }
            }
        }
    }
    return true;
}

VaryingPack normal_varyings() {
    VaryingPack varyings;
    varyings.count = 3U;
    varyings.values[0] = 0.0F;
    varyings.values[1] = 0.0F;
    varyings.values[2] = 1.0F;
    return varyings;
}

ModelAsset triangle_asset(Vec3 albedo, float opacity) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.9F, -0.9F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.9F, -0.9F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.0F, 0.9F, 0.0F}, normal_varyings()),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};

    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "frame-equivalence";
    draw.material.albedo = albedo;
    draw.material.opacity = opacity;
    asset.draws.push_back(draw);
    return asset;
}

Mat4 fixture_affine_transform(float x, float z) {
    Mat4 matrix = Mat4::identity();
    matrix(0U, 0U) = 0.8F;
    matrix(1U, 1U) = 0.8F;
    matrix(2U, 2U) = 0.8F;
    matrix(0U, 3U) = x;
    matrix(2U, 3U) = z;
    return matrix;
}

Mat4 fixture_hierarchy_local_transform(float scale_x, float x, float z) {
    Mat4 matrix = Mat4::identity();
    matrix(0U, 0U) = scale_x;
    matrix(0U, 3U) = x;
    matrix(2U, 3U) = z;
    return matrix;
}

OfflineSceneCamera fixture_frame_camera() {
    OfflineSceneCamera camera;
    camera.eye = {0.0F, 0.0F, 3.0F};
    camera.target = {0.0F, 0.0F, 0.0F};
    camera.up = {0.0F, 1.0F, 0.0F};
    camera.vertical_fov_radians = 0.8726646259971648F;
    camera.near_plane = 0.1F;
    camera.far_plane = 100.0F;
    return camera;
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

int run_timeline_sequence_cli(
    const std::filesystem::path& cli,
    const std::filesystem::path& scene,
    const std::filesystem::path& output,
    const std::filesystem::path& timeline) {
    const std::string command =
        quote_path(cli)
        + " " + quote_path(scene)
        + " " + quote_path(output)
        + " 64 48 4 --timeline-sequence " + quote_path(timeline);
    return std::system(command.c_str());
}

int run_hierarchical_timeline_sequence_cli(
    const std::filesystem::path& cli,
    const std::filesystem::path& scene,
    const std::filesystem::path& output,
    const std::filesystem::path& timeline) {
    const std::string command =
        quote_path(cli)
        + " " + quote_path(scene)
        + " " + quote_path(output)
        + " 64 48 4 --hierarchy-timeline-sequence " + quote_path(timeline);
    return std::system(command.c_str());
}

int run_transform_graph_timeline_sequence_cli(
    const std::filesystem::path& cli,
    const std::filesystem::path& scene,
    const std::filesystem::path& output,
    const std::filesystem::path& timeline) {
    const std::string command =
        quote_path(cli)
        + " " + quote_path(scene)
        + " " + quote_path(output)
        + " 64 48 4 --transform-graph-timeline-sequence "
        + quote_path(timeline);
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

void test_strict_bounded_timeline_sequence_loader() {
    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const OfflineSceneTimelineFile timeline =
        load_offline_timeline_sequence_file(
            fixtures / "timeline_sequence_ab.trtimeline",
            3U);

    check(timeline.keyframes.size() == 2U,
          "timeline sidecar loads exactly two bounded keyframes");
    check(timeline.sample_times.size() == 4U,
          "timeline sidecar preserves four explicit sample requests");
    if (timeline.keyframes.size() == 2U) {
        check(timeline.keyframes[0].time == 0.0F
                  && timeline.keyframes[1].time == 2.0F,
              "timeline keyframe times preserve exact finite file values");
        check(!same_camera(
                  timeline.keyframes[0].frame.camera,
                  timeline.keyframes[1].frame.camera),
              "timeline keyframes preserve independently specified camera state");
        check(timeline.keyframes[0].frame.model_transforms.size() == 3U
                  && timeline.keyframes[1].frame.model_transforms.size() == 3U,
              "every timeline keyframe owns exactly one transform per scene entry");
    }
    if (timeline.sample_times.size() == 4U) {
        check(timeline.sample_times[0] == 0.0F
                  && timeline.sample_times[1] == 1.0F
                  && timeline.sample_times[2] == 0.0F
                  && timeline.sample_times[3] == 2.0F,
              "timeline sample directives retain exact caller order and repetition");
    }

    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_timeline_sequence_file(
                fixtures / "timeline_sequence_invalid_later.trtimeline",
                3U);
        },
        "later out-of-domain sample rejects the complete timeline sidecar");

    const std::filesystem::path root =
        std::filesystem::current_path() / "tiny_renderer_timeline_parser_fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::string camera =
        "0 0 3 0 0 0 0 1 0 0.8726646 0.1 100";

    {
        std::ofstream missing(root / "missing_header.trtimeline");
        missing
            << "keyframe 0 " << camera << "\n"
            << "end\n"
            << "keyframe 2 " << camera << "\n"
            << "end\n"
            << "sample 1\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_timeline_sequence_file(
                root / "missing_header.trtimeline",
                0U);
        },
        "timeline sidecar requires the exact version header");

    {
        std::ofstream duplicate(root / "duplicate_time.trtimeline");
        duplicate
            << "tiny-renderer-timeline-v1\n"
            << "keyframe 0 " << camera << "\n"
            << "end\n"
            << "keyframe 0 " << camera << "\n"
            << "end\n"
            << "sample 0\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_timeline_sequence_file(
                root / "duplicate_time.trtimeline",
                0U);
        },
        "timeline sidecar rejects duplicate or non-increasing keyframe time");

    {
        std::ofstream nonfinite(root / "nonfinite_time.trtimeline");
        nonfinite
            << "tiny-renderer-timeline-v1\n"
            << "keyframe 0 " << camera << "\n"
            << "end\n"
            << "keyframe nan " << camera << "\n"
            << "end\n"
            << "sample 0\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_timeline_sequence_file(
                root / "nonfinite_time.trtimeline",
                0U);
        },
        "timeline sidecar rejects non-finite keyframe time");

    {
        std::ofstream phase(root / "keyframe_after_sample.trtimeline");
        phase
            << "tiny-renderer-timeline-v1\n"
            << "keyframe 0 " << camera << "\n"
            << "end\n"
            << "keyframe 2 " << camera << "\n"
            << "end\n"
            << "sample 1\n"
            << "keyframe 3 " << camera << "\n"
            << "end\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_timeline_sequence_file(
                root / "keyframe_after_sample.trtimeline",
                0U);
        },
        "timeline grammar rejects keyframes after sample directives begin");

    {
        std::ofstream projective(root / "projective.trtimeline");
        projective
            << "tiny-renderer-timeline-v1\n"
            << "keyframe 0 " << camera << "\n"
            << "model 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n"
            << "end\n"
            << "keyframe 2 " << camera << "\n"
            << "model 1 0 0 0 0 1 0 0 0 0 1 0 0 0 -1 0\n"
            << "end\n"
            << "sample 1\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_timeline_sequence_file(
                root / "projective.trtimeline",
                1U);
        },
        "timeline sidecar rejects finite projective model transforms");

    {
        std::ofstream short_models(root / "short_models.trtimeline");
        short_models
            << "tiny-renderer-timeline-v1\n"
            << "keyframe 0 " << camera << "\n"
            << "model 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n"
            << "end\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_timeline_sequence_file(
                root / "short_models.trtimeline",
                2U);
        },
        "timeline keyframe rejects fewer transforms than prepared scene ownership");

    {
        std::ofstream trailing(root / "trailing_sample.trtimeline");
        trailing
            << "tiny-renderer-timeline-v1\n"
            << "keyframe 0 " << camera << "\n"
            << "end\n"
            << "keyframe 2 " << camera << "\n"
            << "end\n"
            << "sample 1 extra\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_timeline_sequence_file(
                root / "trailing_sample.trtimeline",
                0U);
        },
        "timeline sample rejects unexpected trailing tokens");

    {
        std::ofstream oversized(root / "oversized_samples.trtimeline");
        oversized
            << "tiny-renderer-timeline-v1\n"
            << "keyframe 0 " << camera << "\n"
            << "end\n"
            << "keyframe 2 " << camera << "\n"
            << "end\n";
        for (std::size_t i = 0U;
             i < detail::kMaxOfflineTimelineSamples + 1U;
             ++i) {
            oversized << "sample 1\n";
        }
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_timeline_sequence_file(
                root / "oversized_samples.trtimeline",
                0U);
        },
        "timeline sidecar rejects more than the bounded sample count");

    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_timeline_sequence_file(
                fixtures / "timeline_sequence_ab.trtimeline",
                detail::kMaxOfflineSceneEntries + 1U);
        },
        "timeline sidecar rejects expected model ownership above the scene bound before allocation");

    std::filesystem::remove_all(root, ignored);
}


void test_strict_bounded_hierarchical_timeline_loader() {
    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const OfflineSceneHierarchicalTimelineFile timeline =
        load_offline_hierarchical_timeline_sequence_file(
            fixtures / "hierarchy_timeline_ab.trhtimeline",
            3U);

    const auto parents = timeline.hierarchy.parents();
    check(parents.size() == 3U,
          "hierarchical timeline owns exactly one topology record per prepared entry");
    if (parents.size() == 3U) {
        check(parents[0] && *parents[0] == 2U,
              "hierarchical timeline preserves a forward parent reference");
        check(parents[1] && *parents[1] == 0U,
              "hierarchical timeline preserves a child chain independent of record order");
        check(!parents[2],
              "hierarchical timeline preserves explicit root ownership");
    }
    check(timeline.keyframes.size() == 2U,
          "hierarchical timeline loads exactly two bounded keyframes");
    check(timeline.sample_times.size() == 4U,
          "hierarchical timeline preserves four caller-ordered samples");
    if (timeline.keyframes.size() == 2U) {
        check(timeline.keyframes[0].frame.local_transforms.size() == 3U
                  && timeline.keyframes[1].frame.local_transforms.size() == 3U,
              "each hierarchical keyframe owns one local transform per scene entry");
        check(timeline.keyframes[0].time == 0.0F
                  && timeline.keyframes[1].time == 2.0F,
              "hierarchical keyframe times preserve exact file values");
    }
    if (timeline.sample_times.size() == 4U) {
        check(timeline.sample_times[0] == 0.0F
                  && timeline.sample_times[1] == 1.0F
                  && timeline.sample_times[2] == 0.0F
                  && timeline.sample_times[3] == 2.0F,
              "hierarchical sample order and repetition are preserved exactly");
    }

    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_hierarchical_timeline_sequence_file(
                fixtures / "hierarchy_timeline_ab.trhtimeline",
                detail::kMaxOfflineSceneEntries + 1U);
        },
        "hierarchical sidecar rejects expected scene ownership above the bounded entry limit");

    const std::filesystem::path root =
        std::filesystem::current_path()
        / "tiny_renderer_hierarchical_timeline_parser_fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::string camera =
        "0 0 3 0 0 0 0 1 0 0.8726646 0.1 100";
    const std::string identity =
        "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";

    {
        std::ofstream duplicate(root / "duplicate_parent.trhtimeline");
        duplicate
            << "tiny-renderer-hierarchy-timeline-v1\n"
            << "parent 0 root\n"
            << "parent 0 root\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_hierarchical_timeline_sequence_file(
                root / "duplicate_parent.trhtimeline", 3U);
        },
        "hierarchical sidecar rejects duplicate topology ownership");

    {
        std::ofstream missing(root / "missing_parent.trhtimeline");
        missing
            << "tiny-renderer-hierarchy-timeline-v1\n"
            << "parent 0 root\n"
            << "parent 1 root\n"
            << "keyframe 0 " << camera << "\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_hierarchical_timeline_sequence_file(
                root / "missing_parent.trhtimeline", 3U);
        },
        "hierarchical sidecar rejects missing topology before dynamic state");

    {
        std::ofstream out_of_range(root / "out_of_range_parent.trhtimeline");
        out_of_range
            << "tiny-renderer-hierarchy-timeline-v1\n"
            << "parent 0 3\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_hierarchical_timeline_sequence_file(
                root / "out_of_range_parent.trhtimeline", 3U);
        },
        "hierarchical sidecar rejects out-of-range parent references");

    {
        std::ofstream signed_index(root / "signed_parent_index.trhtimeline");
        signed_index
            << "tiny-renderer-hierarchy-timeline-v1\n"
            << "parent -1 root\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_hierarchical_timeline_sequence_file(
                root / "signed_parent_index.trhtimeline", 3U);
        },
        "hierarchical sidecar index grammar rejects signed integer tokens");

    {
        std::ofstream self_parent(root / "self_parent.trhtimeline");
        self_parent
            << "tiny-renderer-hierarchy-timeline-v1\n"
            << "parent 0 0\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_hierarchical_timeline_sequence_file(
                root / "self_parent.trhtimeline", 3U);
        },
        "hierarchical sidecar rejects self-parenting");

    {
        std::ofstream cycle(root / "cycle.trhtimeline");
        cycle
            << "tiny-renderer-hierarchy-timeline-v1\n"
            << "parent 0 1\n"
            << "parent 1 0\n"
            << "parent 2 root\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_hierarchical_timeline_sequence_file(
                root / "cycle.trhtimeline", 3U);
        },
        "hierarchical sidecar rejects cycles when topology becomes complete");

    {
        std::ofstream short_local(root / "short_local.trhtimeline");
        short_local
            << "tiny-renderer-hierarchy-timeline-v1\n"
            << "parent 0 root\n"
            << "parent 1 root\n"
            << "parent 2 root\n"
            << "keyframe 0 " << camera << "\n"
            << "local " << identity << "\n"
            << "local " << identity << "\n"
            << "end\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_hierarchical_timeline_sequence_file(
                root / "short_local.trhtimeline", 3U);
        },
        "hierarchical keyframe rejects missing local transforms");

    {
        std::ofstream projective(root / "projective_local.trhtimeline");
        projective
            << "tiny-renderer-hierarchy-timeline-v1\n"
            << "parent 0 root\n"
            << "keyframe 0 " << camera << "\n"
            << "local 1 0 0 0 0 1 0 0 0 0 1 0 0.1 0 0 1\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_hierarchical_timeline_sequence_file(
                root / "projective_local.trhtimeline", 1U);
        },
        "hierarchical sidecar rejects finite projective local matrices");

    std::filesystem::remove_all(root, ignored);
}


void test_strict_bounded_transform_graph_timeline_loader() {
    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const OfflineSceneTransformGraphTimelineFile timeline =
        load_offline_transform_graph_timeline_sequence_file(
            fixtures / "transform_graph_timeline_ab.trgtimeline",
            3U);

    const auto parents = timeline.graph.parents();
    const auto bindings = timeline.graph.render_entry_nodes();
    check(parents.size() == 4U,
          "graph timeline owns four explicit graph nodes including one transform-only pivot");
    check(bindings.size() == 3U,
          "graph timeline owns exactly one render binding per prepared scene entry");
    if (parents.size() == 4U) {
        check(!parents[0]
                  && parents[1] && *parents[1] == 0U
                  && parents[2] && *parents[2] == 0U
                  && !parents[3],
              "graph timeline preserves arbitrary-order topology and transform-only parent ownership");
    }
    if (bindings.size() == 3U) {
        check(bindings[0] == 1U
                  && bindings[1] == 2U
                  && bindings[2] == 3U,
              "graph timeline reorders arbitrary bind records into prepared-entry ownership");
    }
    check(timeline.keyframes.size() == 2U,
          "graph timeline loads exactly two bounded keyframes");
    check(timeline.sample_times.size() == 4U,
          "graph timeline preserves four caller-ordered samples");
    if (timeline.keyframes.size() == 2U) {
        check(timeline.keyframes[0].frame.local_transforms.size() == 4U
                  && timeline.keyframes[1].frame.local_transforms.size() == 4U,
              "each graph keyframe owns one local transform per graph node");
        check(timeline.keyframes[0].frame.local_transforms[0](0U, 3U) == -0.45F
                  && timeline.keyframes[0].frame.local_transforms[2](0U, 3U) == 0.30F,
              "explicit local NODE records are reordered by graph node id rather than file order");
    }
    if (timeline.sample_times.size() == 4U) {
        check(timeline.sample_times[0] == 0.0F
                  && timeline.sample_times[1] == 1.0F
                  && timeline.sample_times[2] == 0.0F
                  && timeline.sample_times[3] == 2.0F,
              "graph timeline preserves repeated and out-of-order sample requests exactly");
    }

    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                fixtures / "transform_graph_timeline_ab.trgtimeline",
                detail::kMaxOfflineSceneEntries + 1U);
        },
        "graph timeline rejects expected scene ownership above the bounded entry limit");

    const std::filesystem::path root =
        std::filesystem::current_path()
        / "tiny_renderer_transform_graph_timeline_parser_fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::string camera =
        "0 0 3 0 0 0 0 1 0 0.8726646259971648 0.1 100";
    const std::string identity =
        "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";

    {
        std::ofstream duplicate(root / "duplicate_node.trgtimeline");
        duplicate
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node 0 root\n"
            << "node 0 root\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "duplicate_node.trgtimeline", 1U);
        },
        "graph timeline rejects duplicate explicit node ownership");

    {
        std::ofstream missing(root / "missing_node.trgtimeline");
        missing
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node 0 root\n"
            << "node 2 root\n"
            << "bind 0 0\n"
            << "keyframe 0 " << camera << "\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "missing_node.trgtimeline", 1U);
        },
        "graph timeline rejects a missing node inside the explicit contiguous node range");

    {
        std::ofstream parent_missing(root / "parent_missing.trgtimeline");
        parent_missing
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node 0 1\n"
            << "bind 0 0\n"
            << "keyframe 0 " << camera << "\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "parent_missing.trgtimeline", 1U);
        },
        "graph timeline rejects a parent reference to an undeclared node");

    {
        std::ofstream cycle(root / "cycle.trgtimeline");
        cycle
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node 1 0\n"
            << "node 0 1\n"
            << "bind 0 1\n"
            << "keyframe 0 " << camera << "\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "cycle.trgtimeline", 1U);
        },
        "graph timeline rejects cycles after complete arbitrary-order topology parsing");

    {
        std::ofstream duplicate_bind(root / "duplicate_bind.trgtimeline");
        duplicate_bind
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node 0 root\n"
            << "node 1 root\n"
            << "bind 0 0\n"
            << "bind 0 1\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "duplicate_bind.trgtimeline", 2U);
        },
        "graph timeline rejects duplicate prepared-entry bind ownership");

    {
        std::ofstream duplicate_target(root / "duplicate_bind_target.trgtimeline");
        duplicate_target
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node 0 root\n"
            << "node 1 root\n"
            << "bind 0 0\n"
            << "bind 1 0\n"
            << "keyframe 0 " << camera << "\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "duplicate_bind_target.trgtimeline", 2U);
        },
        "graph timeline rejects multiple render entries bound to one graph node");

    {
        std::ofstream missing_bind(root / "missing_bind.trgtimeline");
        missing_bind
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node 0 root\n"
            << "node 1 root\n"
            << "bind 0 0\n"
            << "keyframe 0 " << camera << "\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "missing_bind.trgtimeline", 2U);
        },
        "graph timeline rejects missing prepared-entry bindings before dynamic state");

    {
        std::ofstream node_limit(root / "node_limit.trgtimeline");
        node_limit
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node " << detail::kMaxOfflineTransformGraphNodes << " root\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "node_limit.trgtimeline", 0U);
        },
        "graph timeline rejects graph node ids outside the 512-node bound");

    {
        std::ofstream duplicate_local(root / "duplicate_local.trgtimeline");
        duplicate_local
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node 0 root\n"
            << "bind 0 0\n"
            << "keyframe 0 " << camera << "\n"
            << "local 0 " << identity << "\n"
            << "local 0 " << identity << "\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "duplicate_local.trgtimeline", 1U);
        },
        "graph timeline rejects duplicate local state for one graph node");

    {
        std::ofstream missing_local(root / "missing_local.trgtimeline");
        missing_local
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node 0 root\n"
            << "node 1 root\n"
            << "bind 0 0\n"
            << "keyframe 0 " << camera << "\n"
            << "local 0 " << identity << "\n"
            << "end\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "missing_local.trgtimeline", 1U);
        },
        "graph timeline rejects keyframes missing transform-only node local state");

    {
        std::ofstream projective(root / "projective_local.trgtimeline");
        projective
            << "tiny-renderer-transform-graph-timeline-v1\n"
            << "node 0 root\n"
            << "bind 0 0\n"
            << "keyframe 0 " << camera << "\n"
            << "local 0 1 0 0 0 0 1 0 0 0 0 1 0 0.1 0 0 1\n";
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)load_offline_transform_graph_timeline_sequence_file(
                root / "projective_local.trgtimeline", 1U);
        },
        "graph timeline rejects finite projective graph-local matrices");

    std::filesystem::remove_all(root, ignored);
}

void test_file_driven_transform_graph_timeline_matches_programmatic_m100() {
    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const OfflineSceneTransformGraphTimelineFile file_timeline =
        load_offline_transform_graph_timeline_sequence_file(
            fixtures / "transform_graph_timeline_ab.trgtimeline",
            3U);

    OfflineRenderSettings settings;
    settings.width = 64U;
    settings.height = 48U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.02F, 0.025F, 0.035F};

    const ModelAsset coverage =
        triangle_asset({0.85F, 0.15F, 0.10F}, 0.55F);
    const ModelAsset green =
        triangle_asset({0.10F, 0.75F, 0.20F}, 0.45F);
    const ModelAsset blue =
        triangle_asset({0.10F, 0.25F, 0.85F}, 0.55F);
    const std::array<OfflineSceneEntry, 3> entries{{
        OfflineSceneEntry{
            &coverage,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::AlphaToCoverage},
        OfflineSceneEntry{
            &green,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
        OfflineSceneEntry{
            &blue,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene prepared_scene =
        prepare_offline_mixed_scene(entries, settings);

    const OfflineSceneTransformGraph graph(
        {
            std::nullopt,
            std::optional<std::size_t>{0U},
            std::optional<std::size_t>{0U},
            std::nullopt,
        },
        {1U, 2U, 3U});
    const OfflineSceneCamera camera = fixture_frame_camera();
    const std::vector<Mat4> local_a{
        fixture_hierarchy_local_transform(1.0F, -0.45F, 0.0F),
        fixture_hierarchy_local_transform(1.0F, -0.30F, -0.10F),
        fixture_hierarchy_local_transform(1.0F, 0.30F, -0.20F),
        fixture_hierarchy_local_transform(1.0F, 0.45F, -0.05F),
    };
    const std::vector<Mat4> local_b{
        fixture_hierarchy_local_transform(1.5F, 0.25F, 0.0F),
        fixture_hierarchy_local_transform(1.0F, 0.20F, -0.10F),
        fixture_hierarchy_local_transform(1.0F, -0.30F, -0.20F),
        fixture_hierarchy_local_transform(1.0F, -0.45F, -0.15F),
    };
    const std::array<OfflineSceneTransformGraphTimelineKeyframe, 2>
        programmatic_keyframes{{
            OfflineSceneTransformGraphTimelineKeyframe{
                0.0F,
                OfflineSceneTransformGraphFrameState{camera, local_a}},
            OfflineSceneTransformGraphTimelineKeyframe{
                2.0F,
                OfflineSceneTransformGraphFrameState{camera, local_b}},
        }};
    const std::array<float, 4> samples{{0.0F, 1.0F, 0.0F, 2.0F}};

    const PreparedOfflineCameraSequence file_sequence =
        prepare_offline_transform_graph_timeline_sequence(
            prepared_scene,
            file_timeline.graph,
            file_timeline.keyframes,
            file_timeline.sample_times);
    const PreparedOfflineCameraSequence programmatic_sequence =
        prepare_offline_transform_graph_timeline_sequence(
            prepared_scene,
            graph,
            programmatic_keyframes,
            samples);

    check(file_sequence.frame_count() == 4U
              && file_sequence.frame_count() == programmatic_sequence.frame_count(),
          "file graph timeline and independent M100 state prepare the same sample count");
    if (file_sequence.frame_count() != programmatic_sequence.frame_count()) {
        return;
    }

    std::vector<Framebuffer> rendered;
    rendered.reserve(file_sequence.frame_count());
    for (std::size_t index = 0U; index < file_sequence.frame_count(); ++index) {
        const Framebuffer file_frame =
            render_prepared_camera_sequence_frame(file_sequence, index);
        const Framebuffer programmatic_frame =
            render_prepared_camera_sequence_frame(programmatic_sequence, index);
        check(
            exact_frame_equal(file_frame, programmatic_frame),
            "file graph timeline sample is exact resolved/hash and 4x RGB/depth/stencil equivalent to independent M100 state");
        rendered.push_back(file_frame);
    }
    if (rendered.size() == 4U) {
        check(exact_frame_equal(rendered[0], rendered[2]),
              "repeated file graph timeline sample is exactly deterministic");
    }
}

void test_file_driven_transform_graph_timeline_cli_transaction(const char* argv0) {
    const std::filesystem::path cli = render_cli_path(argv0);
    check(std::filesystem::exists(cli),
          "transform graph timeline integration locates tiny_renderer_render sibling executable");
    if (!std::filesystem::exists(cli)) {
        return;
    }

    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const std::filesystem::path scene =
        fixtures / "flat_scene_sequence_mixed.trscene";
    const std::filesystem::path model =
        fixtures / "opacity_texture_sequence.obj";
    const std::filesystem::path timeline =
        fixtures / "transform_graph_timeline_ab.trgtimeline";
    const std::filesystem::path invalid =
        fixtures / "transform_graph_timeline_invalid_later.trgtimeline";
    const std::filesystem::path hierarchy =
        fixtures / "hierarchy_timeline_ab.trhtimeline";

    const std::filesystem::path root =
        std::filesystem::current_path()
        / "tiny_renderer_transform_graph_timeline_cli_fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::filesystem::path base = root / "graph_timeline.ppm";
    check(
        run_transform_graph_timeline_sequence_cli(
            cli, scene, base, timeline) == 0,
        "strict file-driven transform graph timeline renders through the shared M100 transaction");
    const std::array<std::filesystem::path, 4> outputs{{
        indexed_output(base, 0U),
        indexed_output(base, 1U),
        indexed_output(base, 2U),
        indexed_output(base, 3U),
    }};
    for (const auto& output : outputs) {
        check(std::filesystem::exists(output),
              "transform graph timeline CLI creates every sampled indexed output");
    }
    check(!std::filesystem::exists(base),
          "transform graph timeline CLI never writes an ambiguous unsuffixed output");
    if (std::filesystem::exists(outputs[0])
        && std::filesystem::exists(outputs[2])) {
        check(read_binary_file(outputs[0]) == read_binary_file(outputs[2]),
              "repeated transform graph CLI sample is byte-identical");
    }

    const std::filesystem::path invalid_base = root / "invalid.ppm";
    check(
        run_transform_graph_timeline_sequence_cli(
            cli, scene, invalid_base, invalid) != 0,
        "later interior graph composition overflow rejects the complete CLI transaction");
    check(!std::filesystem::exists(invalid_base)
              && !std::filesystem::exists(indexed_output(invalid_base, 0U))
              && !std::filesystem::exists(indexed_output(invalid_base, 1U)),
          "later graph timeline composition failure leaves zero earlier indexed outputs");

    const std::filesystem::path conflict_base = root / "conflict.ppm";
    const std::string conflict_command =
        quote_path(cli)
        + " " + quote_path(scene)
        + " " + quote_path(conflict_base)
        + " 64 48 4 --transform-graph-timeline-sequence " + quote_path(timeline)
        + " --hierarchy-timeline-sequence " + quote_path(hierarchy);
    check(std::system(conflict_command.c_str()) != 0,
          "graph and hierarchical timeline modes are rejected as ambiguous together");
    check(!std::filesystem::exists(conflict_base)
              && !std::filesystem::exists(indexed_output(conflict_base, 0U)),
          "graph timeline option conflict rejects before output");

    const std::filesystem::path graph_obj_base = root / "graph_obj.ppm";
    check(
        run_transform_graph_timeline_sequence_cli(
            cli, model, graph_obj_base, timeline) != 0,
        "transform graph timeline mode explicitly rejects direct OBJ input");
    check(!std::filesystem::exists(graph_obj_base)
              && !std::filesystem::exists(indexed_output(graph_obj_base, 0U)),
          "direct OBJ graph timeline rejection occurs before output");

    const std::filesystem::path hierarchy_obj_base = root / "hierarchy_obj.ppm";
    check(
        run_hierarchical_timeline_sequence_cli(
            cli, model, hierarchy_obj_base, hierarchy) != 0,
        "hierarchical timeline mode explicitly rejects direct OBJ input");
    check(!std::filesystem::exists(hierarchy_obj_base)
              && !std::filesystem::exists(indexed_output(hierarchy_obj_base, 0U)),
          "direct OBJ hierarchical timeline rejection occurs before output");

    std::filesystem::remove_all(root, ignored);
}

void test_file_driven_hierarchical_timeline_matches_programmatic_m97() {
    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const OfflineSceneHierarchicalTimelineFile file_timeline =
        load_offline_hierarchical_timeline_sequence_file(
            fixtures / "hierarchy_timeline_ab.trhtimeline",
            3U);

    OfflineRenderSettings settings;
    settings.width = 64U;
    settings.height = 48U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.02F, 0.025F, 0.035F};

    const ModelAsset coverage =
        triangle_asset({0.85F, 0.15F, 0.10F}, 0.55F);
    const ModelAsset green =
        triangle_asset({0.10F, 0.75F, 0.20F}, 0.45F);
    const ModelAsset blue =
        triangle_asset({0.10F, 0.25F, 0.85F}, 0.55F);
    const std::array<OfflineSceneEntry, 3> entries{{
        OfflineSceneEntry{
            &coverage,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::AlphaToCoverage},
        OfflineSceneEntry{
            &green,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
        OfflineSceneEntry{
            &blue,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene prepared_scene =
        prepare_offline_mixed_scene(entries, settings);

    const OfflineSceneHierarchy hierarchy({
        std::optional<std::size_t>{2U},
        std::optional<std::size_t>{0U},
        std::nullopt,
    });
    const OfflineSceneCamera camera = fixture_frame_camera();
    const std::vector<Mat4> local_a{
        fixture_hierarchy_local_transform(1.0F, 0.35F, -0.10F),
        fixture_hierarchy_local_transform(1.0F, 0.35F, -0.20F),
        fixture_hierarchy_local_transform(1.0F, -0.45F, 0.0F),
    };
    const std::vector<Mat4> local_b{
        fixture_hierarchy_local_transform(1.0F, 0.20F, -0.10F),
        fixture_hierarchy_local_transform(1.0F, -0.30F, -0.20F),
        fixture_hierarchy_local_transform(1.5F, 0.25F, 0.0F),
    };
    const std::array<OfflineSceneHierarchicalTimelineKeyframe, 2>
        programmatic_keyframes{{
            OfflineSceneHierarchicalTimelineKeyframe{
                0.0F,
                OfflineSceneHierarchicalFrameState{camera, local_a}},
            OfflineSceneHierarchicalTimelineKeyframe{
                2.0F,
                OfflineSceneHierarchicalFrameState{camera, local_b}},
        }};
    const std::array<float, 4> samples{{0.0F, 1.0F, 0.0F, 2.0F}};

    const PreparedOfflineCameraSequence file_sequence =
        prepare_offline_hierarchy_timeline_sequence(
            prepared_scene,
            file_timeline.hierarchy,
            file_timeline.keyframes,
            file_timeline.sample_times);
    const PreparedOfflineCameraSequence programmatic_sequence =
        prepare_offline_hierarchy_timeline_sequence(
            prepared_scene,
            hierarchy,
            programmatic_keyframes,
            samples);

    check(file_sequence.frame_count() == 4U
              && file_sequence.frame_count() == programmatic_sequence.frame_count(),
          "file hierarchical timeline and independent M97 state prepare the same sample count");
    if (file_sequence.frame_count() != programmatic_sequence.frame_count()) {
        return;
    }

    std::vector<Framebuffer> rendered;
    rendered.reserve(file_sequence.frame_count());
    for (std::size_t index = 0U; index < file_sequence.frame_count(); ++index) {
        const Framebuffer file_frame =
            render_prepared_camera_sequence_frame(file_sequence, index);
        const Framebuffer programmatic_frame =
            render_prepared_camera_sequence_frame(programmatic_sequence, index);
        check(
            exact_frame_equal(file_frame, programmatic_frame),
            "file hierarchical sample is exact resolved/hash and 4x RGB/depth/stencil equivalent to independent M97 state");
        rendered.push_back(file_frame);
    }
    if (rendered.size() == 4U) {
        check(exact_frame_equal(rendered[0], rendered[2]),
              "repeated hierarchical file sample is exactly deterministic");
    }
}

void test_file_driven_hierarchical_timeline_cli_transaction(const char* argv0) {
    const std::filesystem::path cli = render_cli_path(argv0);
    check(std::filesystem::exists(cli),
          "hierarchical timeline integration locates tiny_renderer_render sibling executable");
    if (!std::filesystem::exists(cli)) {
        return;
    }

    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const std::filesystem::path scene =
        fixtures / "flat_scene_sequence_mixed.trscene";
    const std::filesystem::path timeline =
        fixtures / "hierarchy_timeline_ab.trhtimeline";
    const std::filesystem::path invalid =
        fixtures / "hierarchy_timeline_invalid_later.trhtimeline";
    const std::filesystem::path flat_timeline =
        fixtures / "timeline_sequence_ab.trtimeline";

    const std::filesystem::path root =
        std::filesystem::current_path()
        / "tiny_renderer_hierarchical_timeline_cli_fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::filesystem::path base = root / "hierarchy_timeline.ppm";
    check(
        run_hierarchical_timeline_sequence_cli(
            cli, scene, base, timeline) == 0,
        "strict file-driven hierarchical timeline renders through the shared M97 transaction");
    const std::array<std::filesystem::path, 4> outputs{{
        indexed_output(base, 0U),
        indexed_output(base, 1U),
        indexed_output(base, 2U),
        indexed_output(base, 3U),
    }};
    for (const auto& output : outputs) {
        check(std::filesystem::exists(output),
              "hierarchical timeline CLI creates every sampled indexed output");
    }
    check(!std::filesystem::exists(base),
          "hierarchical timeline CLI never writes an ambiguous unsuffixed output");
    if (std::filesystem::exists(outputs[0])
        && std::filesystem::exists(outputs[2])) {
        check(read_binary_file(outputs[0]) == read_binary_file(outputs[2]),
              "repeated hierarchical CLI sample is byte-identical");
    }

    const std::filesystem::path invalid_base = root / "invalid.ppm";
    check(
        run_hierarchical_timeline_sequence_cli(
            cli, scene, invalid_base, invalid) != 0,
        "later composed-world overflow rejects the complete hierarchical CLI transaction");
    check(!std::filesystem::exists(invalid_base)
              && !std::filesystem::exists(indexed_output(invalid_base, 0U))
              && !std::filesystem::exists(indexed_output(invalid_base, 1U)),
          "later hierarchical composition failure leaves zero earlier indexed outputs");

    const std::filesystem::path conflict_base = root / "conflict.ppm";
    const std::string conflict_command =
        quote_path(cli)
        + " " + quote_path(scene)
        + " " + quote_path(conflict_base)
        + " 64 48 4 --hierarchy-timeline-sequence " + quote_path(timeline)
        + " --timeline-sequence " + quote_path(flat_timeline);
    check(std::system(conflict_command.c_str()) != 0,
          "hierarchical and flat timeline modes are rejected as ambiguous together");
    check(!std::filesystem::exists(conflict_base)
              && !std::filesystem::exists(indexed_output(conflict_base, 0U)),
          "hierarchical timeline option conflict rejects before output");

    std::filesystem::remove_all(root, ignored);
}

void test_file_driven_timeline_matches_programmatic_m94() {
    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const OfflineSceneTimelineFile file_timeline =
        load_offline_timeline_sequence_file(
            fixtures / "timeline_sequence_ab.trtimeline",
            3U);

    OfflineRenderSettings settings;
    settings.width = 64U;
    settings.height = 48U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.02F, 0.025F, 0.035F};

    const ModelAsset coverage =
        triangle_asset({0.85F, 0.15F, 0.10F}, 0.55F);
    const ModelAsset green =
        triangle_asset({0.10F, 0.75F, 0.20F}, 0.45F);
    const ModelAsset blue =
        triangle_asset({0.10F, 0.25F, 0.85F}, 0.55F);

    const std::array<OfflineSceneEntry, 3> entries{{
        OfflineSceneEntry{
            &coverage,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::AlphaToCoverage},
        OfflineSceneEntry{
            &green,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
        OfflineSceneEntry{
            &blue,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene prepared_scene =
        prepare_offline_mixed_scene(entries, settings);

    OfflineSceneCamera camera_a = fixture_frame_camera();
    OfflineSceneCamera camera_b = camera_a;
    camera_b.eye = {1.0F, 0.25F, 3.0F};
    const std::vector<Mat4> transforms_a{
        fixture_affine_transform(0.55F, 0.25F),
        fixture_affine_transform(-0.65F, 0.0F),
        fixture_affine_transform(0.55F, -0.25F),
    };
    const std::vector<Mat4> transforms_b{
        fixture_affine_transform(-0.55F, 0.35F),
        fixture_affine_transform(0.65F, 0.0F),
        fixture_affine_transform(-0.55F, -0.35F),
    };
    const std::array<OfflineSceneTimelineKeyframe, 2> programmatic_keyframes{{
        OfflineSceneTimelineKeyframe{
            0.0F,
            OfflineSceneFrameState{camera_a, transforms_a}},
        OfflineSceneTimelineKeyframe{
            2.0F,
            OfflineSceneFrameState{camera_b, transforms_b}},
    }};
    const std::array<float, 4> programmatic_samples{{0.0F, 1.0F, 0.0F, 2.0F}};

    const PreparedOfflineCameraSequence file_sequence =
        prepare_offline_timeline_sequence(
            prepared_scene,
            file_timeline.keyframes,
            file_timeline.sample_times);
    const PreparedOfflineCameraSequence programmatic_sequence =
        prepare_offline_timeline_sequence(
            prepared_scene,
            programmatic_keyframes,
            programmatic_samples);

    check(file_sequence.frame_count() == 4U
              && file_sequence.frame_count() == programmatic_sequence.frame_count(),
          "file timeline and independent programmatic M94 timeline prepare the same sample count");
    if (file_sequence.frame_count() != programmatic_sequence.frame_count()) {
        return;
    }

    std::vector<Framebuffer> rendered;
    rendered.reserve(file_sequence.frame_count());
    for (std::size_t index = 0U; index < file_sequence.frame_count(); ++index) {
        const Framebuffer file_frame =
            render_prepared_camera_sequence_frame(file_sequence, index);
        const Framebuffer programmatic_frame =
            render_prepared_camera_sequence_frame(programmatic_sequence, index);
        check(
            exact_frame_equal(file_frame, programmatic_frame),
            "file timeline sample is exact resolved/hash and 4x RGB/depth/stencil equivalent to independent M94 state");
        rendered.push_back(file_frame);
    }
    if (rendered.size() == 4U) {
        check(exact_frame_equal(rendered[0], rendered[2]),
              "repeated file sample time reproduces exact deterministic output");
        check(!exact_frame_equal(rendered[0], rendered[1])
                  && !exact_frame_equal(rendered[1], rendered[3]),
              "file-driven interior sampling observably applies M94 camera/model interpolation");
    }
}

void test_file_driven_timeline_cli_transaction(const char* argv0) {
    const std::filesystem::path cli = render_cli_path(argv0);
    check(std::filesystem::exists(cli),
          "timeline integration locates tiny_renderer_render sibling executable");
    if (!std::filesystem::exists(cli)) {
        return;
    }

    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const std::filesystem::path scene =
        fixtures / "flat_scene_sequence_mixed.trscene";
    const std::filesystem::path timeline =
        fixtures / "timeline_sequence_ab.trtimeline";
    const std::filesystem::path invalid =
        fixtures / "timeline_sequence_invalid_later.trtimeline";
    const std::filesystem::path frames =
        fixtures / "frame_sequence_aba.trframes";

    const std::filesystem::path root =
        std::filesystem::current_path() / "tiny_renderer_timeline_cli_fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::filesystem::path base = root / "timeline.ppm";
    check(run_timeline_sequence_cli(cli, scene, base, timeline) == 0,
          "strict file-driven timeline renders through the shared sequence transaction");
    const std::array<std::filesystem::path, 4> outputs{{
        indexed_output(base, 0U),
        indexed_output(base, 1U),
        indexed_output(base, 2U),
        indexed_output(base, 3U),
    }};
    for (const auto& output : outputs) {
        check(std::filesystem::exists(output),
              "timeline CLI creates every explicit sampled indexed output");
    }
    check(!std::filesystem::exists(base),
          "timeline CLI never writes an ambiguous unsuffixed output");
    if (std::filesystem::exists(outputs[0])
        && std::filesystem::exists(outputs[1])
        && std::filesystem::exists(outputs[2])
        && std::filesystem::exists(outputs[3])) {
        const std::vector<char> endpoint = read_binary_file(outputs[0]);
        const std::vector<char> interior = read_binary_file(outputs[1]);
        const std::vector<char> repeated = read_binary_file(outputs[2]);
        const std::vector<char> final_frame = read_binary_file(outputs[3]);
        check(endpoint == repeated,
              "repeated timeline sample is byte-identical at the CLI boundary");
        check(endpoint != interior && interior != final_frame,
              "interior timeline output differs from both endpoint outputs");
    }

    const std::filesystem::path invalid_base = root / "invalid.ppm";
    check(run_timeline_sequence_cli(cli, scene, invalid_base, invalid) != 0,
          "later invalid timeline sample rejects the complete CLI transaction");
    check(!std::filesystem::exists(invalid_base)
              && !std::filesystem::exists(indexed_output(invalid_base, 0U))
              && !std::filesystem::exists(indexed_output(invalid_base, 1U)),
          "later invalid timeline state leaves zero earlier indexed outputs");

    const std::filesystem::path conflict_base = root / "conflict.ppm";
    const std::string conflict_command =
        quote_path(cli)
        + " " + quote_path(scene)
        + " " + quote_path(conflict_base)
        + " 64 48 4 --timeline-sequence " + quote_path(timeline)
        + " --frame-sequence " + quote_path(frames);
    check(std::system(conflict_command.c_str()) != 0,
          "timeline and exact-frame sidecars are rejected as ambiguous together");
    check(!std::filesystem::exists(conflict_base)
              && !std::filesystem::exists(indexed_output(conflict_base, 0U)),
          "timeline sidecar option conflict rejects before output");

    std::filesystem::remove_all(root, ignored);
}

void test_file_driven_frame_records_match_programmatic_equivalent() {
    const std::filesystem::path fixtures = source_dir() / "tests" / "fixtures";
    const std::vector<OfflineSceneFrameState> file_frames =
        load_offline_frame_sequence_file(
            fixtures / "frame_sequence_aba.trframes",
            3U);

    OfflineRenderSettings settings;
    settings.width = 64U;
    settings.height = 48U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.02F, 0.025F, 0.035F};

    const ModelAsset coverage =
        triangle_asset({0.85F, 0.15F, 0.10F}, 0.55F);
    const ModelAsset green =
        triangle_asset({0.10F, 0.75F, 0.20F}, 0.45F);
    const ModelAsset blue =
        triangle_asset({0.10F, 0.25F, 0.85F}, 0.55F);

    const std::array<OfflineSceneEntry, 3> entries{{
        OfflineSceneEntry{
            &coverage,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::AlphaToCoverage},
        OfflineSceneEntry{
            &green,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
        OfflineSceneEntry{
            &blue,
            Mat4::identity(),
            {},
            OfflineSceneTransparencyMode::SourceAlpha},
    }};
    const PreparedOfflineMixedScene prepared_scene =
        prepare_offline_mixed_scene(entries, settings);

    const OfflineSceneCamera camera = fixture_frame_camera();
    const std::vector<Mat4> transforms_a{
        fixture_affine_transform(0.55F, 0.25F),
        fixture_affine_transform(-0.65F, 0.0F),
        fixture_affine_transform(0.55F, -0.25F),
    };
    const std::vector<Mat4> transforms_b{
        fixture_affine_transform(-0.55F, 0.35F),
        fixture_affine_transform(0.65F, 0.0F),
        fixture_affine_transform(-0.55F, -0.35F),
    };
    const std::array<OfflineSceneFrameState, 3> programmatic_frames{{
        OfflineSceneFrameState{camera, transforms_a},
        OfflineSceneFrameState{camera, transforms_b},
        OfflineSceneFrameState{camera, transforms_a},
    }};

    const PreparedOfflineCameraSequence file_sequence =
        prepare_offline_frame_sequence(prepared_scene, file_frames);
    const PreparedOfflineCameraSequence programmatic_sequence =
        prepare_offline_frame_sequence(prepared_scene, programmatic_frames);

    check(
        file_sequence.frame_count() == programmatic_sequence.frame_count()
            && file_sequence.frame_count() == 3U,
        "file-driven and programmatic A-B-A frame records prepare the same bounded frame count");
    if (file_sequence.frame_count() != programmatic_sequence.frame_count()) {
        return;
    }

    for (std::size_t index = 0U; index < file_sequence.frame_count(); ++index) {
        const Framebuffer file_frame =
            render_prepared_camera_sequence_frame(file_sequence, index);
        const Framebuffer programmatic_frame =
            render_prepared_camera_sequence_frame(programmatic_sequence, index);
        check(
            exact_frame_equal(file_frame, programmatic_frame),
            "file-driven affine frame is exact resolved and per-sample RGB/depth/stencil equivalent to the same programmatic frame record");
    }
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
    test_strict_bounded_timeline_sequence_loader();
    test_strict_bounded_hierarchical_timeline_loader();
    test_strict_bounded_transform_graph_timeline_loader();
    test_file_driven_frame_records_match_programmatic_equivalent();
    test_file_driven_timeline_matches_programmatic_m94();
    test_file_driven_hierarchical_timeline_matches_programmatic_m97();
    test_file_driven_transform_graph_timeline_matches_programmatic_m100();
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        test_file_driven_cli_sequence_transaction(argv[0]);
        test_file_driven_affine_frame_sequence_transaction(argv[0]);
        test_file_driven_timeline_cli_transaction(argv[0]);
        test_file_driven_hierarchical_timeline_cli_transaction(argv[0]);
        test_file_driven_transform_graph_timeline_cli_transaction(argv[0]);
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
