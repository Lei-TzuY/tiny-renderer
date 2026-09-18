#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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

[[nodiscard]] inline bool offline_camera_sequence_ignorable_line(
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
        if (detail::offline_camera_sequence_ignorable_line(line_text)) {
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

// Render one reusable prepared mixed scene from an ordered camera sequence.
// The complete camera list is validated, evaluated, and target-preflighted
// before the first frame is rasterized. Returned frame order exactly matches
// caller camera order. Each camera receives a fresh scene evaluation and
// camera-dependent reflection viewer override while canonical prepared
// model/material/texture/spatial ownership remains shared by `scene`.
//
// The complete returned sequence is bounded by
// detail::kMaxOfflineSequenceResolvedPixels resolved pixels. This API makes no
// throughput or speedup claim; it exposes reusable multi-camera execution and a
// sequence-wide fail-closed transaction boundary.
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

    const float aspect = detail::offline_sequence_aspect(settings);
    Framebuffer validation_target(
        settings.width,
        settings.height,
        settings.sample_count);

    std::vector<PreparedSceneEvaluation> evaluations;
    std::vector<PreparedDrawExecutionOverrides> overrides;
    evaluations.reserve(cameras.size());
    overrides.reserve(cameras.size());

    // First pass: no framebuffer mutation. A malformed or target-invalid later
    // camera rejects the whole sequence before an earlier valid frame can shade
    // or write any sample.
    for (const OfflineSceneCamera& camera : cameras) {
        validate_offline_scene_camera(camera);
        const Mat4 view = Mat4::look_at(camera.eye, camera.target, camera.up);
        const Mat4 projection = Mat4::perspective(
            camera.vertical_fov_radians,
            aspect,
            camera.near_plane,
            camera.far_plane);
        evaluations.push_back(evaluate_prepared_scene_plan(
            scene.plan(), view, projection));
        overrides.push_back(detail::offline_sequence_overrides(settings, camera));
        preflight_prepared_scene_evaluation(
            validation_target,
            evaluations.back(),
            overrides.back());
    }

    // Second pass: execute only after the complete sequence has converged.
    // Reuse the established single-camera consumer so environment background,
    // complete-plan validation, visibility selection, and raster ownership stay
    // on one canonical path.
    std::vector<Framebuffer> frames;
    frames.reserve(cameras.size());
    for (const OfflineSceneCamera& camera : cameras) {
        frames.push_back(render_prepared_scene_preview(scene, camera));
    }
    return frames;
}

}  // namespace tiny_renderer
