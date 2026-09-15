#pragma once

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "tiny_renderer/offline_sequence.hpp"

namespace tiny_renderer {

struct OfflineBenchmarkConfig {
    std::size_t warmup_iterations{1U};
    std::size_t measured_iterations{5U};
};

struct OfflineBenchmarkSample {
    double preparation_microseconds{};
    double evaluation_preflight_microseconds{};
    // Includes canonical per-draw/range fail-closed validation plus fragment
    // submission/raster work. The scene-level transaction preflight is timed in
    // the preceding phase, but lower-level guards are intentionally not bypassed.
    double submission_raster_microseconds{};
    std::uint64_t sequence_hash{};
};

struct OfflineBenchmarkReport {
    std::size_t camera_count{};
    std::size_t warmup_iterations{};
    std::vector<OfflineBenchmarkSample> samples;
};

namespace detail {

inline constexpr std::size_t kMaxOfflineBenchmarkIterations = 1000U;
inline constexpr std::uint64_t kBenchmarkHashOffset = 14695981039346656037ULL;
inline constexpr std::uint64_t kBenchmarkHashPrime = 1099511628211ULL;

inline void validate_offline_benchmark_config(const OfflineBenchmarkConfig& config) {
    if (config.measured_iterations == 0U) {
        throw std::invalid_argument("offline benchmark requires at least one measured iteration");
    }
    if (config.warmup_iterations > kMaxOfflineBenchmarkIterations
        || config.measured_iterations > kMaxOfflineBenchmarkIterations) {
        throw std::invalid_argument("offline benchmark iteration budget exceeds 1000 total iterations");
    }
    if (config.warmup_iterations
        > kMaxOfflineBenchmarkIterations - config.measured_iterations) {
        throw std::invalid_argument("offline benchmark iteration budget exceeds 1000 total iterations");
    }
}

struct OfflineBenchmarkEvaluations {
    std::vector<PreparedSceneEvaluation> evaluations;
    std::vector<PreparedDrawExecutionOverrides> overrides;
};

[[nodiscard]] inline OfflineBenchmarkEvaluations benchmark_evaluate_and_preflight(
    const PreparedOfflineMixedScene& scene,
    std::span<const OfflineSceneCamera> cameras,
    Framebuffer& validation_target) {
    const OfflineRenderSettings& settings = scene.settings();
    const float aspect = offline_sequence_aspect(settings);

    OfflineBenchmarkEvaluations result;
    result.evaluations.reserve(cameras.size());
    result.overrides.reserve(cameras.size());

    for (const OfflineSceneCamera& camera : cameras) {
        validate_offline_scene_camera(camera);
        const Mat4 view = Mat4::look_at(camera.eye, camera.target, camera.up);
        const Mat4 projection = Mat4::perspective(
            camera.vertical_fov_radians,
            aspect,
            camera.near_plane,
            camera.far_plane);
        result.evaluations.push_back(evaluate_prepared_scene_plan(
            scene.plan(), view, projection));
        result.overrides.push_back(offline_sequence_overrides(settings, camera));
        preflight_prepared_scene_evaluation(
            validation_target,
            result.evaluations.back(),
            result.overrides.back());
    }
    return result;
}

inline std::uint64_t benchmark_execute_preflighted(
    const OfflineBenchmarkEvaluations& prepared,
    std::vector<Framebuffer>& targets) {
    if (prepared.evaluations.size() != prepared.overrides.size()
        || prepared.evaluations.size() != targets.size()) {
        throw std::logic_error("offline benchmark phase cardinality mismatch");
    }

    std::uint64_t sequence_hash = kBenchmarkHashOffset;
    for (std::size_t index = 0U; index < prepared.evaluations.size(); ++index) {
        execute_preflighted_prepared_scene_evaluation(
            targets[index],
            prepared.evaluations[index],
            prepared.overrides[index]);
        const std::uint64_t frame_hash = targets[index].fnv1a64();
        sequence_hash ^= frame_hash;
        sequence_hash *= kBenchmarkHashPrime;
    }
    return sequence_hash;
}

template <typename Clock, typename Function>
[[nodiscard]] double benchmark_microseconds(Function&& function) {
    const auto start = Clock::now();
    std::forward<Function>(function)();
    const auto finish = Clock::now();
    return std::chrono::duration<double, std::micro>(finish - start).count();
}

}  // namespace detail

// Controlled phase benchmark for the reusable explicit-camera mixed-scene path.
// This intentionally rejects environment *background* rendering so the final
// phase measures prepared geometry submission/raster work for every run;
// environment diffuse/reflection lighting remains valid because it is carried
// by prepared model state and camera execution overrides.
//
// The harness reports raw timings only. It makes no performance claim and CI
// timings are suitable only as execution smoke evidence, not benchmark data.
// For comparable measurements, use one fixed machine, a Release build, fixed
// workload/settings/cameras, and report the complete per-iteration samples.
template <typename Clock = std::chrono::steady_clock>
[[nodiscard]] OfflineBenchmarkReport benchmark_offline_mixed_scene(
    std::span<const OfflineSceneEntry> entries,
    OfflineRenderSettings settings,
    std::span<const OfflineSceneCamera> cameras,
    OfflineBenchmarkConfig config = {}) {
    static_assert(Clock::is_steady, "offline benchmark requires a steady clock");
    detail::validate_offline_benchmark_config(config);
    if (cameras.empty()) {
        throw std::invalid_argument("offline benchmark requires at least one camera");
    }
    if (settings.environment) {
        throw std::invalid_argument(
            "offline benchmark does not include environment background rasterization");
    }

    // Reuse the sequence output bound as the benchmark workload bound. This
    // prevents timing requests from bypassing the established in-memory camera
    // transaction resource limit even though targets are reused per iteration.
    if (settings.width == 0U || settings.height == 0U
        || settings.width > detail::kMaxOfflineSequenceResolvedPixels / settings.height) {
        throw std::invalid_argument("offline benchmark frame exceeds resolved-pixel budget");
    }
    const std::size_t frame_pixels = settings.width * settings.height;
    if (cameras.size() > detail::kMaxOfflineSequenceResolvedPixels / frame_pixels) {
        throw std::invalid_argument("offline benchmark camera set exceeds resolved-pixel budget");
    }

    OfflineBenchmarkReport report;
    report.camera_count = cameras.size();
    report.warmup_iterations = config.warmup_iterations;
    report.samples.reserve(config.measured_iterations);

    const auto run_warmup = [&] {
        PreparedOfflineMixedScene scene = prepare_offline_mixed_scene(entries, settings);
        Framebuffer validation_target(settings.width, settings.height, settings.sample_count);
        detail::OfflineBenchmarkEvaluations prepared =
            detail::benchmark_evaluate_and_preflight(scene, cameras, validation_target);

        std::vector<Framebuffer> targets;
        targets.reserve(cameras.size());
        for (std::size_t index = 0U; index < cameras.size(); ++index) {
            targets.emplace_back(settings.width, settings.height, settings.sample_count);
            targets.back().clear(settings.clear_color);
        }
        return detail::benchmark_execute_preflighted(prepared, targets);
    };

    for (std::size_t iteration = 0U; iteration < config.warmup_iterations; ++iteration) {
        (void)run_warmup();
    }

    std::uint64_t reference_hash = 0U;
    for (std::size_t iteration = 0U; iteration < config.measured_iterations; ++iteration) {
        std::optional<PreparedOfflineMixedScene> scene;
        const double preparation_us = detail::benchmark_microseconds<Clock>([&] {
            scene.emplace(prepare_offline_mixed_scene(entries, settings));
        });
        if (!scene) {
            throw std::logic_error("offline benchmark preparation did not produce a scene");
        }

        Framebuffer validation_target(settings.width, settings.height, settings.sample_count);
        detail::OfflineBenchmarkEvaluations prepared;
        const double evaluation_preflight_us = detail::benchmark_microseconds<Clock>([&] {
            prepared = detail::benchmark_evaluate_and_preflight(
                *scene, cameras, validation_target);
        });

        // Allocation and target clear intentionally stay outside the final
        // timing. The final phase includes canonical lower-level validation,
        // submission, shading, and sample ownership but not target allocation.
        std::vector<Framebuffer> targets;
        targets.reserve(cameras.size());
        for (std::size_t index = 0U; index < cameras.size(); ++index) {
            targets.emplace_back(settings.width, settings.height, settings.sample_count);
            targets.back().clear(settings.clear_color);
        }

        std::uint64_t sequence_hash = 0U;
        const double submission_raster_us = detail::benchmark_microseconds<Clock>([&] {
            sequence_hash = detail::benchmark_execute_preflighted(prepared, targets);
        });
        if (iteration == 0U) {
            reference_hash = sequence_hash;
        } else if (sequence_hash != reference_hash) {
            throw std::runtime_error("offline benchmark workload produced a non-deterministic sequence hash");
        }
        if (!std::isfinite(preparation_us)
            || !std::isfinite(evaluation_preflight_us)
            || !std::isfinite(submission_raster_us)
            || preparation_us < 0.0
            || evaluation_preflight_us < 0.0
            || submission_raster_us < 0.0) {
            throw std::runtime_error("offline benchmark steady-clock measurement is invalid");
        }

        report.samples.push_back({
            preparation_us,
            evaluation_preflight_us,
            submission_raster_us,
            sequence_hash,
        });
    }
    return report;
}

}  // namespace tiny_renderer
