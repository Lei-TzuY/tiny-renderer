#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

#include "tiny_renderer/offline_render.hpp"

namespace tiny_renderer {
namespace detail {

// Keep a reusable sequence bounded as a complete in-memory result. This is an
// ownership/resource limit, not a performance claim. At the default 512x512
// preview size it permits up to 64 returned frames; a 4 MP preview permits four.
inline constexpr std::size_t kMaxOfflineSequenceResolvedPixels =
    16U * 1024U * 1024U;

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

}  // namespace detail

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
