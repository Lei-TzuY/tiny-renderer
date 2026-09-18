#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tiny_renderer/offline_render.hpp"

namespace tiny_renderer {
namespace detail {

// Keep a reusable sequence bounded as a complete in-memory result. This is an
// ownership/resource limit, not a performance claim. At the default 512x512
// preview size it permits up to 64 returned frames; a 4 MP preview permits four.
inline constexpr std::size_t kMaxOfflineSequenceResolvedPixels =
    16U * 1024U * 1024U;
inline constexpr std::size_t kMaxOfflineSequenceCameras = 256U;
inline constexpr std::string_view kOfflineCameraSequenceHeader =
    "tiny-renderer-camera-sequence-v1";
inline constexpr std::string_view kOfflineFrameSequenceHeader =
    "tiny-renderer-frame-sequence-v1";

[[nodiscard]] inline float offline_sequence_aspect(
    const OfflineRenderSettings& settings) {
    const double aspect = static_cast<double>(settings.width)
        / static_cast<double>(settings.height);
    const float result = static_cast<float>(aspect);
    if (!std::isfinite(result) || result <= 0.0F) {
        throw std::invalid_argument(
            "offline camera sequence aspect ratio is not representable");
    }
    return result;
}

[[nodiscard]] inline PreparedDrawExecutionOverrides offline_sequence_overrides(
    const OfflineRenderSettings& settings,
    const OfflineSceneCamera& camera) {
    PreparedDrawExecutionOverrides overrides;
    if (settings.environment_reflection) {
        overrides.environment_reflection_viewer_position = camera.eye;
    }
    return overrides;
}

[[noreturn]] inline void offline_camera_sequence_error(
    const std::filesystem::path& path,
    std::size_t line,
    const std::string& message) {
    throw std::invalid_argument(
        "offline camera sequence " + path.string() + ": line "
        + std::to_string(line) + ": " + message);
}

[[nodiscard]] inline bool offline_sequence_ignorable_line(
    const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t\r");
    return first == std::string::npos || line[first] == '#';
}

[[nodiscard]] inline float parse_offline_camera_sequence_float(
    const std::filesystem::path& path,
    std::size_t line,
    std::string_view token,
    const char* label) {
    std::size_t consumed = 0U;
    float value = 0.0F;
    try {
        value = std::stof(std::string(token), &consumed);
    } catch (const std::exception&) {
        offline_camera_sequence_error(
            path, line, std::string(label) + " must be a finite number");
    }
    if (consumed != token.size() || !std::isfinite(value)) {
        offline_camera_sequence_error(
            path, line, std::string(label) + " must be a finite number");
    }
    return value;
}

inline void reject_offline_camera_sequence_extra_tokens(
    const std::filesystem::path& path,
    std::size_t line,
    std::istringstream& input) {
    std::string extra;
    if (input >> extra) {
        offline_camera_sequence_error(
            path, line, "unexpected trailing token '" + extra + "'");
    }
}

}  // namespace detail

// Strict bounded sidecar format:
//   tiny-renderer-camera-sequence-v1
//   camera EX EY EZ TX TY TZ UX UY UZ VFOV_RADIANS NEAR FAR
//   camera ...
// Blank lines and full-line '#' comments are ignored. At least one camera is
// required and at most detail::kMaxOfflineSequenceCameras records are accepted.
// Each record is validated with the same OfflineSceneCamera contract used by
// the reusable prepared-scene execution path.
[[nodiscard]] inline std::vector<OfflineSceneCamera> load_offline_camera_sequence_file(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(
            "failed to open offline camera sequence: " + path.string());
    }

    std::vector<OfflineSceneCamera> cameras;
    bool header_seen = false;
    std::string line_text;
    std::size_t line_number = 0U;

    while (std::getline(input, line_text)) {
        ++line_number;
        if (detail::offline_sequence_ignorable_line(line_text)) {
            continue;
        }

        std::istringstream line(line_text);
        std::string directive;
        line >> directive;

        if (!header_seen) {
            if (directive != detail::kOfflineCameraSequenceHeader) {
                detail::offline_camera_sequence_error(
                    path,
                    line_number,
                    "first non-comment line must be tiny-renderer-camera-sequence-v1");
            }
            detail::reject_offline_camera_sequence_extra_tokens(path, line_number, line);
            header_seen = true;
            continue;
        }

        if (directive != "camera") {
            detail::offline_camera_sequence_error(
                path, line_number, "unknown directive '" + directive + "'");
        }
        if (cameras.size() >= detail::kMaxOfflineSequenceCameras) {
            detail::offline_camera_sequence_error(
                path, line_number, "camera count exceeds bounded sequence limit");
        }

        std::array<std::string, 12> tokens{};
        for (std::string& token : tokens) {
            if (!(line >> token)) {
                detail::offline_camera_sequence_error(
                    path,
                    line_number,
                    "camera requires EX EY EZ TX TY TZ UX UY UZ VFOV_RADIANS NEAR FAR");
            }
        }
        detail::reject_offline_camera_sequence_extra_tokens(path, line_number, line);

        OfflineSceneCamera camera;
        camera.eye = {
            detail::parse_offline_camera_sequence_float(path, line_number, tokens[0], "camera eye X"),
            detail::parse_offline_camera_sequence_float(path, line_number, tokens[1], "camera eye Y"),
            detail::parse_offline_camera_sequence_float(path, line_number, tokens[2], "camera eye Z"),
        };
        camera.target = {
            detail::parse_offline_camera_sequence_float(path, line_number, tokens[3], "camera target X"),
            detail::parse_offline_camera_sequence_float(path, line_number, tokens[4], "camera target Y"),
            detail::parse_offline_camera_sequence_float(path, line_number, tokens[5], "camera target Z"),
        };
        camera.up = {
            detail::parse_offline_camera_sequence_float(path, line_number, tokens[6], "camera up X"),
            detail::parse_offline_camera_sequence_float(path, line_number, tokens[7], "camera up Y"),
            detail::parse_offline_camera_sequence_float(path, line_number, tokens[8], "camera up Z"),
        };
        camera.vertical_fov_radians = detail::parse_offline_camera_sequence_float(
            path, line_number, tokens[9], "camera vertical field of view");
        camera.near_plane = detail::parse_offline_camera_sequence_float(
            path, line_number, tokens[10], "camera near plane");
        camera.far_plane = detail::parse_offline_camera_sequence_float(
            path, line_number, tokens[11], "camera far plane");

        try {
            validate_offline_scene_camera(camera);
        } catch (const std::invalid_argument& error) {
            detail::offline_camera_sequence_error(path, line_number, error.what());
        }
        cameras.push_back(camera);
    }

    if (!header_seen) {
        throw std::invalid_argument(
            "offline camera sequence " + path.string()
            + ": missing tiny-renderer-camera-sequence-v1 header");
    }
    if (cameras.empty()) {
        throw std::invalid_argument(
            "offline camera sequence " + path.string()
            + ": at least one camera record is required");
    }
    return cameras;
}

// One programmatic frame state for reusable offline execution. The model
// transform record is aligned exactly with PreparedScenePlan::entries(); each
// matrix is a complete model transform for that frame, not a delta transform.
// Preparation copies camera-dependent evaluation results, so callers do not
// need to retain this record after the prepared sequence is created.
struct OfflineSceneFrameState {
    OfflineSceneCamera camera{};
    std::vector<Mat4> model_transforms{};
};

namespace detail {

[[noreturn]] inline void offline_frame_sequence_error(
    const std::filesystem::path& path,
    std::size_t line,
    const std::string& message) {
    throw std::invalid_argument(
        "offline frame sequence " + path.string() + ": line "
        + std::to_string(line) + ": " + message);
}

[[nodiscard]] inline float parse_offline_frame_sequence_float(
    const std::filesystem::path& path,
    std::size_t line,
    std::string_view token,
    const char* label) {
    std::size_t consumed = 0U;
    float value = 0.0F;
    try {
        value = std::stof(std::string(token), &consumed);
    } catch (const std::exception&) {
        offline_frame_sequence_error(
            path, line, std::string(label) + " must be a finite number");
    }
    if (consumed != token.size() || !std::isfinite(value)) {
        offline_frame_sequence_error(
            path, line, std::string(label) + " must be a finite number");
    }
    return value;
}

inline void reject_offline_frame_sequence_extra_tokens(
    const std::filesystem::path& path,
    std::size_t line,
    std::istringstream& input) {
    std::string extra;
    if (input >> extra) {
        offline_frame_sequence_error(
            path, line, "unexpected trailing token '" + extra + "'");
    }
}

[[nodiscard]] inline OfflineSceneCamera parse_offline_frame_camera(
    const std::filesystem::path& path,
    std::size_t line_number,
    std::istringstream& line) {
    std::array<std::string, 12> tokens{};
    for (std::string& token : tokens) {
        if (!(line >> token)) {
            offline_frame_sequence_error(
                path,
                line_number,
                "frame requires EX EY EZ TX TY TZ UX UY UZ VFOV_RADIANS NEAR FAR");
        }
    }
    reject_offline_frame_sequence_extra_tokens(path, line_number, line);

    OfflineSceneCamera camera;
    camera.eye = {
        parse_offline_frame_sequence_float(path, line_number, tokens[0], "camera eye X"),
        parse_offline_frame_sequence_float(path, line_number, tokens[1], "camera eye Y"),
        parse_offline_frame_sequence_float(path, line_number, tokens[2], "camera eye Z"),
    };
    camera.target = {
        parse_offline_frame_sequence_float(path, line_number, tokens[3], "camera target X"),
        parse_offline_frame_sequence_float(path, line_number, tokens[4], "camera target Y"),
        parse_offline_frame_sequence_float(path, line_number, tokens[5], "camera target Z"),
    };
    camera.up = {
        parse_offline_frame_sequence_float(path, line_number, tokens[6], "camera up X"),
        parse_offline_frame_sequence_float(path, line_number, tokens[7], "camera up Y"),
        parse_offline_frame_sequence_float(path, line_number, tokens[8], "camera up Z"),
    };
    camera.vertical_fov_radians = parse_offline_frame_sequence_float(
        path, line_number, tokens[9], "camera vertical field of view");
    camera.near_plane = parse_offline_frame_sequence_float(
        path, line_number, tokens[10], "camera near plane");
    camera.far_plane = parse_offline_frame_sequence_float(
        path, line_number, tokens[11], "camera far plane");

    try {
        validate_offline_scene_camera(camera);
    } catch (const std::invalid_argument& error) {
        offline_frame_sequence_error(path, line_number, error.what());
    }
    return camera;
}

[[nodiscard]] inline Mat4 parse_offline_frame_model_matrix(
    const std::filesystem::path& path,
    std::size_t line_number,
    std::istringstream& line) {
    std::array<std::string, 16> tokens{};
    for (std::string& token : tokens) {
        if (!(line >> token)) {
            offline_frame_sequence_error(
                path,
                line_number,
                "model requires 16 row-major affine matrix values");
        }
    }
    reject_offline_frame_sequence_extra_tokens(path, line_number, line);

    Mat4 matrix{};
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            const std::size_t index = row * 4U + column;
            matrix(row, column) = parse_offline_frame_sequence_float(
                path,
                line_number,
                tokens[index],
                "model matrix value");
        }
    }
    try {
        validate_spatial_affine_matrix(
            matrix,
            "offline frame model transform");
    } catch (const std::invalid_argument& error) {
        offline_frame_sequence_error(path, line_number, error.what());
    }
    return matrix;
}

}  // namespace detail

// Strict bounded sidecar for exact frame samples:
//
//   tiny-renderer-frame-sequence-v1
//   frame EX EY EZ TX TY TZ UX UY UZ VFOV_RADIANS NEAR FAR
//   model M00 M01 M02 M03 M10 M11 M12 M13 M20 M21 M22 M23 M30 M31 M32 M33
//   ... exactly expected_model_count model records ...
//   end
//
// Frames are independent samples: matrices are complete row-major affine model
// transforms, not deltas. Blank lines and full-line '#' comments are ignored.
// Parsing knows the prepared scene entry count so incomplete/extra transform
// records reject before preparation, rendering, or indexed output begins.
[[nodiscard]] inline std::vector<OfflineSceneFrameState>
load_offline_frame_sequence_file(
    const std::filesystem::path& path,
    std::size_t expected_model_count) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(
            "failed to open offline frame sequence: " + path.string());
    }

    std::vector<OfflineSceneFrameState> frames;
    std::optional<OfflineSceneFrameState> current;
    bool header_seen = false;
    std::string line_text;
    std::size_t line_number = 0U;

    while (std::getline(input, line_text)) {
        ++line_number;
        if (detail::offline_sequence_ignorable_line(line_text)) {
            continue;
        }

        std::istringstream line(line_text);
        std::string directive;
        line >> directive;

        if (!header_seen) {
            if (directive != detail::kOfflineFrameSequenceHeader) {
                detail::offline_frame_sequence_error(
                    path,
                    line_number,
                    "first non-comment line must be tiny-renderer-frame-sequence-v1");
            }
            detail::reject_offline_frame_sequence_extra_tokens(
                path, line_number, line);
            header_seen = true;
            continue;
        }

        if (directive == "frame") {
            if (current) {
                detail::offline_frame_sequence_error(
                    path,
                    line_number,
                    "new frame encountered before previous frame end");
            }
            if (frames.size() >= detail::kMaxOfflineSequenceCameras) {
                detail::offline_frame_sequence_error(
                    path,
                    line_number,
                    "frame count exceeds bounded sequence limit");
            }
            current.emplace();
            current->camera =
                detail::parse_offline_frame_camera(path, line_number, line);
            current->model_transforms.reserve(expected_model_count);
            continue;
        }

        if (directive == "model") {
            if (!current) {
                detail::offline_frame_sequence_error(
                    path, line_number, "model record requires an active frame");
            }
            if (current->model_transforms.size() >= expected_model_count) {
                detail::offline_frame_sequence_error(
                    path,
                    line_number,
                    "model transform count exceeds prepared scene entry count");
            }
            current->model_transforms.push_back(
                detail::parse_offline_frame_model_matrix(
                    path, line_number, line));
            continue;
        }

        if (directive == "end") {
            if (!current) {
                detail::offline_frame_sequence_error(
                    path, line_number, "end requires an active frame");
            }
            detail::reject_offline_frame_sequence_extra_tokens(
                path, line_number, line);
            if (current->model_transforms.size() != expected_model_count) {
                detail::offline_frame_sequence_error(
                    path,
                    line_number,
                    "frame model transform count must match prepared scene entry count");
            }
            frames.push_back(std::move(*current));
            current.reset();
            continue;
        }

        detail::offline_frame_sequence_error(
            path, line_number, "unknown directive '" + directive + "'");
    }

    if (!header_seen) {
        throw std::invalid_argument(
            "offline frame sequence " + path.string()
            + ": missing tiny-renderer-frame-sequence-v1 header");
    }
    if (current) {
        detail::offline_frame_sequence_error(
            path,
            line_number,
            "unterminated frame record requires end");
    }
    if (frames.empty()) {
        throw std::invalid_argument(
            "offline frame sequence " + path.string()
            + ": at least one frame record is required");
    }
    return frames;
}

// Prepared frame-sequence plan. It retains shared immutable ownership of the
// address-stable PreparedScenePlan plus a validated settings snapshot, so the
// source PreparedOfflineMixedScene may be moved or destroyed after preparation.
// Camera evaluations still borrow draw entries from that retained plan.
//
// Preparation validates every camera, evaluates camera-dependent ordering and
// conservative visibility, binds per-camera execution overrides, and
// target-preflights the complete sequence before this object can be returned.
// Rendering one indexed frame therefore does not repeat scene-level planning or
// transaction preflight; canonical lower-level draw/range guards remain active.
class PreparedOfflineCameraSequence {
public:
    PreparedOfflineCameraSequence(const PreparedOfflineCameraSequence&) = default;
    PreparedOfflineCameraSequence(PreparedOfflineCameraSequence&&) noexcept = default;
    PreparedOfflineCameraSequence& operator=(const PreparedOfflineCameraSequence&) = default;
    PreparedOfflineCameraSequence& operator=(PreparedOfflineCameraSequence&&) noexcept = default;

    [[nodiscard]] std::size_t frame_count() const noexcept { return cameras_.size(); }

private:
    friend PreparedOfflineCameraSequence prepare_offline_camera_sequence(
        const PreparedOfflineMixedScene& scene,
        std::span<const OfflineSceneCamera> cameras);
    friend PreparedOfflineCameraSequence prepare_offline_frame_sequence(
        const PreparedOfflineMixedScene& scene,
        std::span<const OfflineSceneFrameState> frames);
    friend Framebuffer render_prepared_camera_sequence_frame(
        const PreparedOfflineCameraSequence& sequence,
        std::size_t frame_index);
    friend std::vector<Framebuffer> render_prepared_scene_sequence(
        const PreparedOfflineMixedScene& scene,
        std::span<const OfflineSceneCamera> cameras);

    static PreparedOfflineCameraSequence prepare_impl(
        const PreparedOfflineMixedScene& scene,
        std::span<const OfflineSceneCamera> cameras,
        bool enforce_camera_limit);
    static PreparedOfflineCameraSequence prepare_frame_impl(
        const PreparedOfflineMixedScene& scene,
        std::span<const OfflineSceneFrameState> frames,
        bool enforce_frame_limit,
        bool require_model_transforms);

    PreparedOfflineCameraSequence(
        const PreparedOfflineMixedScene& scene,
        std::vector<OfflineSceneCamera> cameras,
        std::vector<PreparedSceneEvaluation> evaluations,
        std::vector<PreparedDrawExecutionOverrides> overrides)
        : plan_owner_(scene.plan_),
          settings_(scene.settings_),
          cameras_(std::move(cameras)),
          evaluations_(std::move(evaluations)),
          overrides_(std::move(overrides)) {}

    std::shared_ptr<const PreparedScenePlan> plan_owner_{};
    OfflineRenderSettings settings_{};
    std::vector<OfflineSceneCamera> cameras_;
    std::vector<PreparedSceneEvaluation> evaluations_;
    std::vector<PreparedDrawExecutionOverrides> overrides_;
};

// Builds one reusable prepared frame transaction without allocating or
// rasterizing any output framebuffer. Every camera, optional per-entry affine
// model transform overlay, camera-dependent draw plan, and complete unfiltered
// target preflight is accepted before this object can be returned.
inline PreparedOfflineCameraSequence PreparedOfflineCameraSequence::prepare_frame_impl(
    const PreparedOfflineMixedScene& scene,
    std::span<const OfflineSceneFrameState> frames,
    bool enforce_frame_limit,
    bool require_model_transforms) {
    if (enforce_frame_limit
        && frames.size() > detail::kMaxOfflineSequenceCameras) {
        throw std::invalid_argument(
            "offline prepared frame sequence exceeds bounded frame limit");
    }

    const OfflineRenderSettings& settings = scene.settings();
    if (settings.width == 0U || settings.height == 0U) {
        throw std::logic_error(
            "prepared offline scene contains invalid zero-sized render settings");
    }

    const std::size_t scene_entry_count = scene.plan().entries().size();
    const float aspect = detail::offline_sequence_aspect(settings);
    Framebuffer validation_target(
        settings.width,
        settings.height,
        settings.sample_count);

    std::vector<OfflineSceneCamera> owned_cameras;
    std::vector<PreparedSceneEvaluation> evaluations;
    std::vector<PreparedDrawExecutionOverrides> overrides;
    owned_cameras.reserve(frames.size());
    evaluations.reserve(frames.size());
    overrides.reserve(frames.size());

    for (const OfflineSceneFrameState& frame : frames) {
        if (require_model_transforms) {
            if (frame.model_transforms.size() != scene_entry_count) {
                throw std::invalid_argument(
                    "offline frame model transform count must match prepared scene entry count");
            }
        } else if (!frame.model_transforms.empty()) {
            throw std::logic_error(
                "camera-only sequence preparation received unexpected model transforms");
        }

        const OfflineSceneCamera& camera = frame.camera;
        validate_offline_scene_camera(camera);
        const Mat4 view = Mat4::look_at(camera.eye, camera.target, camera.up);
        const Mat4 projection = Mat4::perspective(
            camera.vertical_fov_radians,
            aspect,
            camera.near_plane,
            camera.far_plane);

        PreparedSceneEvaluation evaluation = require_model_transforms
            ? evaluate_prepared_scene_plan(
                  scene.plan(),
                  std::span<const Mat4>{
                      frame.model_transforms.data(),
                      frame.model_transforms.size()},
                  view,
                  projection)
            : evaluate_prepared_scene_plan(
                  scene.plan(),
                  view,
                  projection);
        PreparedDrawExecutionOverrides execution_overrides =
            detail::offline_sequence_overrides(settings, camera);
        preflight_prepared_scene_evaluation(
            validation_target,
            evaluation,
            execution_overrides);

        owned_cameras.push_back(camera);
        evaluations.push_back(std::move(evaluation));
        overrides.push_back(std::move(execution_overrides));
    }

    return PreparedOfflineCameraSequence{
        scene,
        std::move(owned_cameras),
        std::move(evaluations),
        std::move(overrides),
    };
}

// Camera-only compatibility preparation delegates to the same frame transaction
// with no transform overlay, preserving every M91 ownership and validation rule.
inline PreparedOfflineCameraSequence PreparedOfflineCameraSequence::prepare_impl(
    const PreparedOfflineMixedScene& scene,
    std::span<const OfflineSceneCamera> cameras,
    bool enforce_camera_limit) {
    std::vector<OfflineSceneFrameState> frames;
    frames.reserve(cameras.size());
    for (const OfflineSceneCamera& camera : cameras) {
        frames.push_back(OfflineSceneFrameState{camera, {}});
    }
    return prepare_frame_impl(
        scene,
        frames,
        enforce_camera_limit,
        false);
}

[[nodiscard]] inline PreparedOfflineCameraSequence prepare_offline_camera_sequence(
    const PreparedOfflineMixedScene& scene,
    std::span<const OfflineSceneCamera> cameras) {
    return PreparedOfflineCameraSequence::prepare_impl(
        scene, cameras, true);
}

// Programmatic M92 path: each frame supplies one explicit affine model
// transform per prepared scene entry. Geometry/material/texture snapshots and
// object-space draw bounds stay owned by the original PreparedScenePlan.
[[nodiscard]] inline PreparedOfflineCameraSequence prepare_offline_frame_sequence(
    const PreparedOfflineMixedScene& scene,
    std::span<const OfflineSceneFrameState> frames) {
    return PreparedOfflineCameraSequence::prepare_frame_impl(
        scene,
        frames,
        true,
        true);
}

// Executes exactly one already-prepared camera entry. Only one framebuffer is
// owned by this call. Environment background and canonical prepared-draw
// submission remain on the existing production paths. Scene-level evaluation
// and preflight were completed transactionally by prepare_offline_camera_sequence.
[[nodiscard]] inline Framebuffer render_prepared_camera_sequence_frame(
    const PreparedOfflineCameraSequence& sequence,
    std::size_t frame_index) {
    if (frame_index >= sequence.frame_count()) {
        throw std::out_of_range(
            "offline prepared camera sequence frame index out of range");
    }
    if (!sequence.plan_owner_
        || sequence.evaluations_.size() != sequence.frame_count()
        || sequence.overrides_.size() != sequence.frame_count()) {
        throw std::logic_error(
            "offline prepared camera sequence has inconsistent owned state");
    }

    const OfflineRenderSettings& settings = sequence.settings_;
    const OfflineSceneCamera& camera = sequence.cameras_[frame_index];

    Framebuffer framebuffer(
        settings.width,
        settings.height,
        settings.sample_count);
    framebuffer.clear(settings.clear_color);

    if (settings.environment) {
        const PerspectiveCameraState perspective{
            camera.eye,
            camera.target,
            camera.up,
            camera.vertical_fov_radians,
            detail::offline_sequence_aspect(settings),
        };
        draw_environment_background(
            framebuffer,
            perspective,
            *settings.environment);
    }

    detail::execute_preflighted_prepared_scene_evaluation(
        framebuffer,
        sequence.evaluations_[frame_index],
        sequence.overrides_[frame_index]);
    return framebuffer;
}

// Compatibility helper that still materializes every returned framebuffer.
// The historical total resolved-pixel bound remains on this vector-producing
// API. New frame-at-a-time callers should prepare once and execute indexed
// frames instead.
[[nodiscard]] inline std::vector<Framebuffer> render_prepared_scene_sequence(
    const PreparedOfflineMixedScene& scene,
    std::span<const OfflineSceneCamera> cameras) {
    if (cameras.empty()) {
        return {};
    }

    const OfflineRenderSettings& settings = scene.settings();
    if (settings.width == 0U || settings.height == 0U) {
        throw std::logic_error(
            "prepared offline scene contains invalid zero-sized render settings");
    }
    if (settings.width > detail::kMaxOfflineSequenceResolvedPixels / settings.height) {
        throw std::invalid_argument(
            "offline camera sequence single frame exceeds total resolved-pixel budget");
    }
    const std::size_t frame_pixels = settings.width * settings.height;
    if (cameras.size()
        > detail::kMaxOfflineSequenceResolvedPixels / frame_pixels) {
        throw std::invalid_argument(
            "offline camera sequence exceeds total resolved-pixel budget");
    }

    // Preserve the historical library contract: this compatibility API is
    // bounded by aggregate returned framebuffer ownership, not by the 256-camera
    // file-sidecar/prepared-plan metadata limit. Preparation still uses the same
    // camera evaluation and complete target-preflight implementation.
    const PreparedOfflineCameraSequence prepared =
        PreparedOfflineCameraSequence::prepare_impl(scene, cameras, false);
    std::vector<Framebuffer> frames;
    frames.reserve(prepared.frame_count());
    for (std::size_t frame_index = 0U;
         frame_index < prepared.frame_count();
         ++frame_index) {
        frames.push_back(render_prepared_camera_sequence_frame(
            prepared,
            frame_index));
    }
    return frames;
}

}  // namespace tiny_renderer
