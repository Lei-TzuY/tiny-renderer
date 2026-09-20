#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
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
inline constexpr std::size_t kMaxOfflineTimelineKeyframes = 256U;
inline constexpr std::size_t kMaxOfflineTimelineSamples = 256U;
inline constexpr std::size_t kMaxOfflineTransformGraphNodes = 512U;
inline constexpr std::string_view kOfflineCameraSequenceHeader =
    "tiny-renderer-camera-sequence-v1";
inline constexpr std::string_view kOfflineFrameSequenceHeader =
    "tiny-renderer-frame-sequence-v1";
inline constexpr std::string_view kOfflineTimelineSequenceHeader =
    "tiny-renderer-timeline-v1";
inline constexpr std::string_view kOfflineHierarchicalTimelineSequenceHeader =
    "tiny-renderer-hierarchy-timeline-v1";

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

// One programmatic keyframe in a bounded time domain. Time is an abstract
// finite scalar owned by the caller; this layer does not assign frame-rate,
// wall-clock, looping, or file-format semantics to it.
struct OfflineSceneTimelineKeyframe {
    float time{};
    OfflineSceneFrameState frame{};
};

// Strict file-facing timeline data is kept separate from sampled frame state.
// The loader owns syntax only; M94 remains the sole interpolation authority.
struct OfflineSceneTimelineFile {
    std::vector<OfflineSceneTimelineKeyframe> keyframes{};
    std::vector<float> sample_times{};
};

// Immutable bounded parent topology aligned by entry index. A missing parent
// marks a root. Construction validates the complete graph independently of any
// dynamic frame state so later local-transform evaluation cannot discover a
// structural cycle after earlier frames have been prepared.
class OfflineSceneHierarchy {
public:
    explicit OfflineSceneHierarchy(
        std::vector<std::optional<std::size_t>> parents)
        : parents_(std::move(parents)) {
        if (parents_.size() > detail::kMaxOfflineSceneEntries) {
            throw std::invalid_argument(
                "offline hierarchy entry count exceeds bounded scene entry limit");
        }

        for (std::size_t index = 0U; index < parents_.size(); ++index) {
            if (!parents_[index]) {
                continue;
            }
            if (*parents_[index] >= parents_.size()) {
                throw std::out_of_range(
                    "offline hierarchy parent index exceeds hierarchy entry count");
            }
            if (*parents_[index] == index) {
                throw std::invalid_argument(
                    "offline hierarchy entry cannot parent itself");
            }
        }

        std::vector<unsigned char> state(parents_.size(), 0U);
        const auto visit = [&](auto&& self, std::size_t index) -> void {
            if (state[index] == 2U) {
                return;
            }
            if (state[index] == 1U) {
                throw std::invalid_argument(
                    "offline hierarchy contains a parent cycle");
            }
            state[index] = 1U;
            if (parents_[index]) {
                self(self, *parents_[index]);
            }
            state[index] = 2U;
        };
        for (std::size_t index = 0U; index < parents_.size(); ++index) {
            visit(visit, index);
        }
    }

    [[nodiscard]] std::span<const std::optional<std::size_t>>
    parents() const noexcept {
        return {parents_.data(), parents_.size()};
    }

private:
    std::vector<std::optional<std::size_t>> parents_;
};

// Immutable transform graph whose topology is independent from prepared render
// entry ownership. Nodes may be transform-only groups/pivots. render_entry_nodes
// is aligned by prepared-scene entry index and binds each render entry to one
// unique graph node; all remaining nodes are non-renderable transform nodes.
class OfflineSceneTransformGraph {
public:
    OfflineSceneTransformGraph(
        std::vector<std::optional<std::size_t>> parents,
        std::vector<std::size_t> render_entry_nodes)
        : parents_(std::move(parents)),
          render_entry_nodes_(std::move(render_entry_nodes)) {
        if (parents_.size() > detail::kMaxOfflineTransformGraphNodes) {
            throw std::invalid_argument(
                "offline transform graph node count exceeds bounded graph limit");
        }
        if (render_entry_nodes_.size() > detail::kMaxOfflineSceneEntries) {
            throw std::invalid_argument(
                "offline transform graph render binding count exceeds bounded scene entry limit");
        }
        if (render_entry_nodes_.size() > parents_.size()) {
            throw std::invalid_argument(
                "offline transform graph cannot bind more render entries than graph nodes");
        }

        for (std::size_t index = 0U; index < parents_.size(); ++index) {
            if (!parents_[index]) {
                continue;
            }
            if (*parents_[index] >= parents_.size()) {
                throw std::out_of_range(
                    "offline transform graph parent index exceeds graph node count");
            }
            if (*parents_[index] == index) {
                throw std::invalid_argument(
                    "offline transform graph node cannot parent itself");
            }
        }

        std::vector<unsigned char> state(parents_.size(), 0U);
        const auto visit = [&](auto&& self, std::size_t index) -> void {
            if (state[index] == 2U) {
                return;
            }
            if (state[index] == 1U) {
                throw std::invalid_argument(
                    "offline transform graph contains a parent cycle");
            }
            state[index] = 1U;
            if (parents_[index]) {
                self(self, *parents_[index]);
            }
            state[index] = 2U;
        };
        for (std::size_t index = 0U; index < parents_.size(); ++index) {
            visit(visit, index);
        }

        std::vector<unsigned char> bound(parents_.size(), 0U);
        for (const std::size_t node : render_entry_nodes_) {
            if (node >= parents_.size()) {
                throw std::out_of_range(
                    "offline transform graph render binding index exceeds graph node count");
            }
            if (bound[node] != 0U) {
                throw std::invalid_argument(
                    "offline transform graph render entries must bind unique graph nodes");
            }
            bound[node] = 1U;
        }
    }

    [[nodiscard]] std::span<const std::optional<std::size_t>>
    parents() const noexcept {
        return {parents_.data(), parents_.size()};
    }

    [[nodiscard]] std::span<const std::size_t>
    render_entry_nodes() const noexcept {
        return {render_entry_nodes_.data(), render_entry_nodes_.size()};
    }

private:
    std::vector<std::optional<std::size_t>> parents_;
    std::vector<std::size_t> render_entry_nodes_;
};

// Dynamic hierarchical frame state owns local transforms only. The hierarchy
// remains fixed across a prepared sequence; resolved world transforms are
// temporary preparation data delegated into the established M92 transaction.
struct OfflineSceneHierarchicalFrameState {
    OfflineSceneCamera camera{};
    std::vector<Mat4> local_transforms{};
};

// One programmatic transform-graph frame owns one complete local affine matrix
// per graph node. Render-entry world transforms are derived transactionally
// from the immutable graph rather than stored redundantly in the frame.
struct OfflineSceneTransformGraphFrameState {
    OfflineSceneCamera camera{};
    std::vector<Mat4> local_transforms{};
};

// One programmatic keyframe over hierarchy-local state. The hierarchy topology
// is supplied separately and remains immutable for the complete timeline.
struct OfflineSceneHierarchicalTimelineKeyframe {
    float time{};
    OfflineSceneHierarchicalFrameState frame{};
};

// Strict file-facing M98 state. The sidecar owns one fixed validated hierarchy
// plus hierarchy-local keyframes and caller-ordered sample requests. Parsing
// does not interpolate local transforms or resolve world transforms.
struct OfflineSceneHierarchicalTimelineFile {
    OfflineSceneHierarchy hierarchy;
    std::vector<OfflineSceneHierarchicalTimelineKeyframe> keyframes{};
    std::vector<float> sample_times{};
};

namespace detail {

[[nodiscard]] inline std::vector<Mat4>
resolve_offline_hierarchy_world_transforms(
    const OfflineSceneHierarchy& hierarchy,
    std::span<const Mat4> local_transforms) {
    const std::span<const std::optional<std::size_t>> parents =
        hierarchy.parents();
    if (local_transforms.size() != parents.size()) {
        throw std::invalid_argument(
            "offline hierarchy local transform count must match hierarchy entry count");
    }

    for (const Mat4& local : local_transforms) {
        validate_spatial_affine_matrix(
            local,
            "offline hierarchy local transform");
    }

    std::vector<Mat4> world(
        local_transforms.size(),
        Mat4::identity());
    std::vector<unsigned char> resolved(local_transforms.size(), 0U);

    const auto resolve = [&](auto&& self, std::size_t index) -> const Mat4& {
        if (resolved[index] != 0U) {
            return world[index];
        }
        world[index] = parents[index]
            ? self(self, *parents[index]) * local_transforms[index]
            : local_transforms[index];
        validate_spatial_affine_matrix(
            world[index],
            "offline hierarchy composed world transform");
        resolved[index] = 1U;
        return world[index];
    };

    for (std::size_t index = 0U; index < local_transforms.size(); ++index) {
        (void)resolve(resolve, index);
    }
    return world;
}

[[nodiscard]] inline std::vector<Mat4>
resolve_offline_transform_graph_render_world_transforms(
    const OfflineSceneTransformGraph& graph,
    std::span<const Mat4> local_transforms) {
    const std::span<const std::optional<std::size_t>> parents =
        graph.parents();
    if (local_transforms.size() != parents.size()) {
        throw std::invalid_argument(
            "offline transform graph local transform count must match graph node count");
    }

    for (const Mat4& local : local_transforms) {
        validate_spatial_affine_matrix(
            local,
            "offline transform graph local transform");
    }

    std::vector<Mat4> world(
        local_transforms.size(),
        Mat4::identity());
    std::vector<unsigned char> resolved(local_transforms.size(), 0U);

    const auto resolve = [&](auto&& self, std::size_t index) -> const Mat4& {
        if (resolved[index] != 0U) {
            return world[index];
        }
        world[index] = parents[index]
            ? self(self, *parents[index]) * local_transforms[index]
            : local_transforms[index];
        validate_spatial_affine_matrix(
            world[index],
            "offline transform graph composed world transform");
        resolved[index] = 1U;
        return world[index];
    };

    // Resolve every node, including transform-only nodes that are not mapped
    // to render entries. Invalid hidden graph state must fail the transaction.
    for (std::size_t index = 0U; index < local_transforms.size(); ++index) {
        (void)resolve(resolve, index);
    }

    std::vector<Mat4> render_world;
    render_world.reserve(graph.render_entry_nodes().size());
    for (const std::size_t node : graph.render_entry_nodes()) {
        render_world.push_back(world[node]);
    }
    return render_world;
}

[[nodiscard]] inline float offline_timeline_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

[[nodiscard]] inline Vec3 offline_timeline_lerp(
    const Vec3& a,
    const Vec3& b,
    float t) {
    return {
        offline_timeline_lerp(a.x, b.x, t),
        offline_timeline_lerp(a.y, b.y, t),
        offline_timeline_lerp(a.z, b.z, t),
    };
}

[[nodiscard]] inline std::size_t validate_offline_timeline_keyframes(
    std::span<const OfflineSceneTimelineKeyframe> keyframes) {
    if (keyframes.size() < 2U) {
        throw std::invalid_argument(
            "offline timeline requires at least two keyframes");
    }
    if (keyframes.size() > kMaxOfflineTimelineKeyframes) {
        throw std::invalid_argument(
            "offline timeline keyframe count exceeds bounded limit");
    }

    const std::size_t model_count =
        keyframes.front().frame.model_transforms.size();
    if (model_count > kMaxOfflineSceneEntries) {
        throw std::invalid_argument(
            "offline timeline model transform count exceeds bounded scene entry limit");
    }
    for (std::size_t index = 0U; index < keyframes.size(); ++index) {
        const OfflineSceneTimelineKeyframe& keyframe = keyframes[index];
        if (!std::isfinite(keyframe.time)) {
            throw std::invalid_argument(
                "offline timeline keyframe time must be finite");
        }
        if (index > 0U && !(keyframe.time > keyframes[index - 1U].time)) {
            throw std::invalid_argument(
                "offline timeline keyframe times must be strictly increasing");
        }
        validate_offline_scene_camera(keyframe.frame.camera);
        if (keyframe.frame.model_transforms.size() != model_count) {
            throw std::invalid_argument(
                "offline timeline keyframes must use one consistent model transform count");
        }
        for (const Mat4& model : keyframe.frame.model_transforms) {
            validate_spatial_affine_matrix(
                model,
                "offline timeline keyframe model transform");
        }
    }
    return model_count;
}

[[nodiscard]] inline std::size_t
validate_offline_hierarchical_timeline_keyframes(
    const OfflineSceneHierarchy& hierarchy,
    std::span<const OfflineSceneHierarchicalTimelineKeyframe> keyframes) {
    if (keyframes.size() < 2U) {
        throw std::invalid_argument(
            "offline hierarchical timeline requires at least two keyframes");
    }
    if (keyframes.size() > kMaxOfflineTimelineKeyframes) {
        throw std::invalid_argument(
            "offline hierarchical timeline keyframe count exceeds bounded limit");
    }

    const std::size_t local_count = hierarchy.parents().size();
    for (std::size_t index = 0U; index < keyframes.size(); ++index) {
        const OfflineSceneHierarchicalTimelineKeyframe& keyframe =
            keyframes[index];
        if (!std::isfinite(keyframe.time)) {
            throw std::invalid_argument(
                "offline hierarchical timeline keyframe time must be finite");
        }
        if (index > 0U
            && !(keyframe.time > keyframes[index - 1U].time)) {
            throw std::invalid_argument(
                "offline hierarchical timeline keyframe times must be strictly increasing");
        }
        validate_offline_scene_camera(keyframe.frame.camera);
        if (keyframe.frame.local_transforms.size() != local_count) {
            throw std::invalid_argument(
                "offline hierarchical timeline keyframe local transform count must match hierarchy entry count");
        }
        for (const Mat4& local : keyframe.frame.local_transforms) {
            validate_spatial_affine_matrix(
                local,
                "offline hierarchical timeline keyframe local transform");
        }
    }
    return local_count;
}

[[nodiscard]] inline OfflineSceneCamera interpolate_offline_timeline_camera(
    const OfflineSceneCamera& a,
    const OfflineSceneCamera& b,
    float t) {
    OfflineSceneCamera camera;
    camera.eye = offline_timeline_lerp(a.eye, b.eye, t);
    camera.target = offline_timeline_lerp(a.target, b.target, t);
    camera.up = offline_timeline_lerp(a.up, b.up, t);
    camera.vertical_fov_radians =
        offline_timeline_lerp(a.vertical_fov_radians, b.vertical_fov_radians, t);
    camera.near_plane =
        offline_timeline_lerp(a.near_plane, b.near_plane, t);
    camera.far_plane =
        offline_timeline_lerp(a.far_plane, b.far_plane, t);
    validate_offline_scene_camera(camera);
    return camera;
}

[[nodiscard]] inline Mat4 interpolate_offline_timeline_affine(
    const Mat4& a,
    const Mat4& b,
    float t) {
    Mat4 result = Mat4::identity();
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            result(row, column) =
                offline_timeline_lerp(a(row, column), b(row, column), t);
        }
    }
    // The affine bottom row is semantic state, not an interpolated quantity.
    // Endpoints were validated above; interior samples preserve it exactly.
    result(3U, 0U) = 0.0F;
    result(3U, 1U) = 0.0F;
    result(3U, 2U) = 0.0F;
    result(3U, 3U) = 1.0F;
    validate_spatial_affine_matrix(
        result,
        "offline timeline interpolated model transform");
    return result;
}

[[noreturn]] inline void offline_frame_sequence_error(
    const std::filesystem::path& path,
    std::size_t line,
    const std::string& message) {
    throw std::invalid_argument(
        "offline frame sequence " + path.string() + ": line "
        + std::to_string(line) + ": " + message);
}

[[noreturn]] inline void offline_timeline_sequence_error(
    const std::filesystem::path& path,
    std::size_t line,
    const std::string& message) {
    throw std::invalid_argument(
        "offline timeline " + path.string() + ": line "
        + std::to_string(line) + ": " + message);
}

[[noreturn]] inline void offline_hierarchical_timeline_sequence_error(
    const std::filesystem::path& path,
    std::size_t line,
    const std::string& message) {
    throw std::invalid_argument(
        "offline hierarchical timeline " + path.string() + ": line "
        + std::to_string(line) + ": " + message);
}

using OfflineSequenceErrorFunction = void (*)(
    const std::filesystem::path&,
    std::size_t,
    const std::string&);

[[nodiscard]] inline float parse_offline_sequence_float(
    const std::filesystem::path& path,
    std::size_t line,
    std::string_view token,
    const char* label,
    OfflineSequenceErrorFunction error) {
    std::size_t consumed = 0U;
    float value = 0.0F;
    try {
        value = std::stof(std::string(token), &consumed);
    } catch (const std::exception&) {
        error(path, line, std::string(label) + " must be a finite number");
    }
    if (consumed != token.size() || !std::isfinite(value)) {
        error(path, line, std::string(label) + " must be a finite number");
    }
    return value;
}

[[nodiscard]] inline std::size_t parse_offline_sequence_index(
    const std::filesystem::path& path,
    std::size_t line,
    std::string_view token,
    const char* label,
    OfflineSequenceErrorFunction error) {
    if (token.empty()) {
        error(path, line, std::string(label) + " must be a non-negative integer");
    }
    for (const char character : token) {
        if (character < '0' || character > '9') {
            error(path, line, std::string(label) + " must be a non-negative integer");
        }
    }

    std::size_t consumed = 0U;
    unsigned long long value = 0ULL;
    try {
        value = std::stoull(std::string(token), &consumed, 10);
    } catch (const std::exception&) {
        error(path, line, std::string(label) + " must be a non-negative integer");
    }
    if (consumed != token.size()
        || value > static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max())) {
        error(path, line, std::string(label) + " must be a non-negative integer");
    }
    return static_cast<std::size_t>(value);
}

inline void reject_offline_sequence_extra_tokens(
    const std::filesystem::path& path,
    std::size_t line,
    std::istringstream& input,
    OfflineSequenceErrorFunction error) {
    std::string extra;
    if (input >> extra) {
        error(path, line, "unexpected trailing token '" + extra + "'");
    }
}

[[nodiscard]] inline OfflineSceneCamera parse_offline_sequence_camera(
    const std::filesystem::path& path,
    std::size_t line_number,
    std::istringstream& line,
    const char* record_label,
    OfflineSequenceErrorFunction error) {
    std::array<std::string, 12> tokens{};
    for (std::string& token : tokens) {
        if (!(line >> token)) {
            error(
                path,
                line_number,
                std::string(record_label)
                    + " requires EX EY EZ TX TY TZ UX UY UZ VFOV_RADIANS NEAR FAR");
        }
    }
    reject_offline_sequence_extra_tokens(path, line_number, line, error);

    OfflineSceneCamera camera;
    camera.eye = {
        parse_offline_sequence_float(path, line_number, tokens[0], "camera eye X", error),
        parse_offline_sequence_float(path, line_number, tokens[1], "camera eye Y", error),
        parse_offline_sequence_float(path, line_number, tokens[2], "camera eye Z", error),
    };
    camera.target = {
        parse_offline_sequence_float(path, line_number, tokens[3], "camera target X", error),
        parse_offline_sequence_float(path, line_number, tokens[4], "camera target Y", error),
        parse_offline_sequence_float(path, line_number, tokens[5], "camera target Z", error),
    };
    camera.up = {
        parse_offline_sequence_float(path, line_number, tokens[6], "camera up X", error),
        parse_offline_sequence_float(path, line_number, tokens[7], "camera up Y", error),
        parse_offline_sequence_float(path, line_number, tokens[8], "camera up Z", error),
    };
    camera.vertical_fov_radians = parse_offline_sequence_float(
        path, line_number, tokens[9], "camera vertical field of view", error);
    camera.near_plane = parse_offline_sequence_float(
        path, line_number, tokens[10], "camera near plane", error);
    camera.far_plane = parse_offline_sequence_float(
        path, line_number, tokens[11], "camera far plane", error);

    try {
        validate_offline_scene_camera(camera);
    } catch (const std::invalid_argument& caught) {
        error(path, line_number, caught.what());
    }
    return camera;
}

[[nodiscard]] inline Mat4 parse_offline_sequence_model_matrix(
    const std::filesystem::path& path,
    std::size_t line_number,
    std::istringstream& line,
    const char* transform_label,
    OfflineSequenceErrorFunction error) {
    std::array<std::string, 16> tokens{};
    for (std::string& token : tokens) {
        if (!(line >> token)) {
            error(
                path,
                line_number,
                "model requires 16 row-major affine matrix values");
        }
    }
    reject_offline_sequence_extra_tokens(path, line_number, line, error);

    Mat4 matrix{};
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            const std::size_t index = row * 4U + column;
            matrix(row, column) = parse_offline_sequence_float(
                path,
                line_number,
                tokens[index],
                "model matrix value",
                error);
        }
    }
    try {
        validate_spatial_affine_matrix(matrix, transform_label);
    } catch (const std::invalid_argument& caught) {
        error(path, line_number, caught.what());
    }
    return matrix;
}

// M93 compatibility wrappers preserve the original frame-sequence diagnostics
// while sharing numeric/camera/matrix parsing with the M95 timeline format.
[[nodiscard]] inline float parse_offline_frame_sequence_float(
    const std::filesystem::path& path,
    std::size_t line,
    std::string_view token,
    const char* label) {
    return parse_offline_sequence_float(
        path, line, token, label, offline_frame_sequence_error);
}

inline void reject_offline_frame_sequence_extra_tokens(
    const std::filesystem::path& path,
    std::size_t line,
    std::istringstream& input) {
    reject_offline_sequence_extra_tokens(
        path, line, input, offline_frame_sequence_error);
}

[[nodiscard]] inline OfflineSceneCamera parse_offline_frame_camera(
    const std::filesystem::path& path,
    std::size_t line_number,
    std::istringstream& line) {
    return parse_offline_sequence_camera(
        path,
        line_number,
        line,
        "frame",
        offline_frame_sequence_error);
}

[[nodiscard]] inline Mat4 parse_offline_frame_model_matrix(
    const std::filesystem::path& path,
    std::size_t line_number,
    std::istringstream& line) {
    return parse_offline_sequence_model_matrix(
        path,
        line_number,
        line,
        "offline frame model transform",
        offline_frame_sequence_error);
}

}  // namespace detail

// Samples a bounded programmatic timeline in exact caller request order.
// Keyframe endpoints are copied without arithmetic so exact keyframe samples
// preserve stored camera/matrix state bit-for-bit. Interior camera components
// and the affine top 3x4 are linearly interpolated; every resulting state is
// revalidated before it can enter prepared-scene evaluation.
[[nodiscard]] inline std::vector<OfflineSceneFrameState>
sample_offline_frame_timeline(
    std::span<const OfflineSceneTimelineKeyframe> keyframes,
    std::span<const float> sample_times) {
    const std::size_t model_count =
        detail::validate_offline_timeline_keyframes(keyframes);
    if (sample_times.size() > detail::kMaxOfflineTimelineSamples) {
        throw std::invalid_argument(
            "offline timeline sample count exceeds bounded limit");
    }

    std::vector<OfflineSceneFrameState> frames;
    frames.reserve(sample_times.size());
    for (const float sample_time : sample_times) {
        if (!std::isfinite(sample_time)) {
            throw std::invalid_argument(
                "offline timeline sample time must be finite");
        }
        if (sample_time < keyframes.front().time
            || sample_time > keyframes.back().time) {
            throw std::out_of_range(
                "offline timeline sample time is outside the keyframe domain");
        }

        if (sample_time == keyframes.front().time) {
            frames.push_back(keyframes.front().frame);
            continue;
        }

        std::size_t upper = 1U;
        while (upper < keyframes.size()
               && keyframes[upper].time < sample_time) {
            ++upper;
        }
        if (upper < keyframes.size()
            && sample_time == keyframes[upper].time) {
            frames.push_back(keyframes[upper].frame);
            continue;
        }
        if (upper >= keyframes.size()) {
            throw std::logic_error(
                "offline timeline failed to bracket an in-domain sample");
        }

        const OfflineSceneTimelineKeyframe& left = keyframes[upper - 1U];
        const OfflineSceneTimelineKeyframe& right = keyframes[upper];
        const float denominator = right.time - left.time;
        const float t = (sample_time - left.time) / denominator;
        if (!std::isfinite(t) || !(t > 0.0F && t < 1.0F)) {
            throw std::logic_error(
                "offline timeline produced an invalid interpolation parameter");
        }

        OfflineSceneFrameState frame;
        frame.camera = detail::interpolate_offline_timeline_camera(
            left.frame.camera,
            right.frame.camera,
            t);
        frame.model_transforms.reserve(model_count);
        for (std::size_t model_index = 0U;
             model_index < model_count;
             ++model_index) {
            frame.model_transforms.push_back(
                detail::interpolate_offline_timeline_affine(
                    left.frame.model_transforms[model_index],
                    right.frame.model_transforms[model_index],
                    t));
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

// Samples hierarchy-local state in exact caller request order. Exact keyframe
// requests copy camera/local state without interpolation arithmetic; interior
// samples reuse M94 camera and affine interpolation before any local-to-world
// composition occurs.
[[nodiscard]] inline std::vector<OfflineSceneHierarchicalFrameState>
sample_offline_hierarchical_timeline(
    const OfflineSceneHierarchy& hierarchy,
    std::span<const OfflineSceneHierarchicalTimelineKeyframe> keyframes,
    std::span<const float> sample_times) {
    const std::size_t local_count =
        detail::validate_offline_hierarchical_timeline_keyframes(
            hierarchy,
            keyframes);
    if (sample_times.size() > detail::kMaxOfflineTimelineSamples) {
        throw std::invalid_argument(
            "offline hierarchical timeline sample count exceeds bounded limit");
    }

    std::vector<OfflineSceneHierarchicalFrameState> frames;
    frames.reserve(sample_times.size());
    for (const float sample_time : sample_times) {
        if (!std::isfinite(sample_time)) {
            throw std::invalid_argument(
                "offline hierarchical timeline sample time must be finite");
        }
        if (sample_time < keyframes.front().time
            || sample_time > keyframes.back().time) {
            throw std::out_of_range(
                "offline hierarchical timeline sample time is outside the keyframe domain");
        }

        if (sample_time == keyframes.front().time) {
            frames.push_back(keyframes.front().frame);
            continue;
        }

        std::size_t upper = 1U;
        while (upper < keyframes.size()
               && keyframes[upper].time < sample_time) {
            ++upper;
        }
        if (upper < keyframes.size()
            && sample_time == keyframes[upper].time) {
            frames.push_back(keyframes[upper].frame);
            continue;
        }
        if (upper >= keyframes.size()) {
            throw std::logic_error(
                "offline hierarchical timeline failed to bracket an in-domain sample");
        }

        const OfflineSceneHierarchicalTimelineKeyframe& left =
            keyframes[upper - 1U];
        const OfflineSceneHierarchicalTimelineKeyframe& right =
            keyframes[upper];
        const float denominator = right.time - left.time;
        const float t = (sample_time - left.time) / denominator;
        if (!std::isfinite(t) || !(t > 0.0F && t < 1.0F)) {
            throw std::logic_error(
                "offline hierarchical timeline produced an invalid interpolation parameter");
        }

        OfflineSceneHierarchicalFrameState frame;
        frame.camera = detail::interpolate_offline_timeline_camera(
            left.frame.camera,
            right.frame.camera,
            t);
        frame.local_transforms.reserve(local_count);
        for (std::size_t local_index = 0U;
             local_index < local_count;
             ++local_index) {
            frame.local_transforms.push_back(
                detail::interpolate_offline_timeline_affine(
                    left.frame.local_transforms[local_index],
                    right.frame.local_transforms[local_index],
                    t));
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

// Strict bounded sidecar for programmatic M94 timeline state:
//
//   tiny-renderer-timeline-v1
//   keyframe TIME EX EY EZ TX TY TZ UX UY UZ VFOV_RADIANS NEAR FAR
//   model M00 M01 M02 M03 M10 M11 M12 M13 M20 M21 M22 M23 M30 M31 M32 M33
//   ... exactly expected_model_count model records ...
//   end
//   ... at least two strictly increasing keyframes ...
//   sample TIME
//   ... one or more sample requests in caller output order ...
//
// Keyframes must precede all sample directives. Parsing validates bounded
// syntax, camera state, affine model records, keyframe ordering, and sample
// domain membership, but performs no interpolation. M94 sampling/preparation
// remains the only owner of interpolation semantics.
[[nodiscard]] inline OfflineSceneTimelineFile load_offline_timeline_sequence_file(
    const std::filesystem::path& path,
    std::size_t expected_model_count) {
    if (expected_model_count > detail::kMaxOfflineSceneEntries) {
        throw std::invalid_argument(
            "offline timeline expected model count exceeds bounded scene entry limit");
    }

    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(
            "failed to open offline timeline: " + path.string());
    }

    OfflineSceneTimelineFile result;
    std::optional<OfflineSceneTimelineKeyframe> current;
    bool header_seen = false;
    bool samples_started = false;
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
            if (directive != detail::kOfflineTimelineSequenceHeader) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "first non-comment line must be tiny-renderer-timeline-v1");
            }
            detail::reject_offline_sequence_extra_tokens(
                path,
                line_number,
                line,
                detail::offline_timeline_sequence_error);
            header_seen = true;
            continue;
        }

        if (directive == "keyframe") {
            if (samples_started) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "keyframes must precede all sample directives");
            }
            if (current) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "new keyframe encountered before previous keyframe end");
            }
            if (result.keyframes.size() >= detail::kMaxOfflineTimelineKeyframes) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "keyframe count exceeds bounded timeline limit");
            }

            std::string time_token;
            if (!(line >> time_token)) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "keyframe requires TIME followed by camera state");
            }
            const float time = detail::parse_offline_sequence_float(
                path,
                line_number,
                time_token,
                "keyframe time",
                detail::offline_timeline_sequence_error);
            if (!result.keyframes.empty()
                && !(time > result.keyframes.back().time)) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "keyframe times must be strictly increasing");
            }

            current.emplace();
            current->time = time;
            current->frame.camera = detail::parse_offline_sequence_camera(
                path,
                line_number,
                line,
                "keyframe",
                detail::offline_timeline_sequence_error);
            current->frame.model_transforms.reserve(expected_model_count);
            continue;
        }

        if (directive == "model") {
            if (!current) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "model record requires an active keyframe");
            }
            if (current->frame.model_transforms.size() >= expected_model_count) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "model transform count exceeds prepared scene entry count");
            }
            current->frame.model_transforms.push_back(
                detail::parse_offline_sequence_model_matrix(
                    path,
                    line_number,
                    line,
                    "offline timeline keyframe model transform",
                    detail::offline_timeline_sequence_error));
            continue;
        }

        if (directive == "end") {
            if (!current) {
                detail::offline_timeline_sequence_error(
                    path, line_number, "end requires an active keyframe");
            }
            detail::reject_offline_sequence_extra_tokens(
                path,
                line_number,
                line,
                detail::offline_timeline_sequence_error);
            if (current->frame.model_transforms.size() != expected_model_count) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "keyframe model transform count must match prepared scene entry count");
            }
            result.keyframes.push_back(std::move(*current));
            current.reset();
            continue;
        }

        if (directive == "sample") {
            if (current) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "sample cannot appear inside an active keyframe");
            }
            if (result.keyframes.size() < 2U) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "samples require at least two completed keyframes");
            }
            samples_started = true;
            if (result.sample_times.size() >= detail::kMaxOfflineTimelineSamples) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "sample count exceeds bounded timeline limit");
            }
            std::string time_token;
            if (!(line >> time_token)) {
                detail::offline_timeline_sequence_error(
                    path, line_number, "sample requires TIME");
            }
            detail::reject_offline_sequence_extra_tokens(
                path,
                line_number,
                line,
                detail::offline_timeline_sequence_error);
            const float sample_time = detail::parse_offline_sequence_float(
                path,
                line_number,
                time_token,
                "sample time",
                detail::offline_timeline_sequence_error);
            if (sample_time < result.keyframes.front().time
                || sample_time > result.keyframes.back().time) {
                detail::offline_timeline_sequence_error(
                    path,
                    line_number,
                    "sample time is outside the keyframe domain");
            }
            result.sample_times.push_back(sample_time);
            continue;
        }

        detail::offline_timeline_sequence_error(
            path, line_number, "unknown directive '" + directive + "'");
    }

    if (!header_seen) {
        throw std::invalid_argument(
            "offline timeline " + path.string()
            + ": missing tiny-renderer-timeline-v1 header");
    }
    if (current) {
        detail::offline_timeline_sequence_error(
            path,
            line_number,
            "unterminated keyframe record requires end");
    }
    if (result.keyframes.size() < 2U) {
        throw std::invalid_argument(
            "offline timeline " + path.string()
            + ": at least two keyframe records are required");
    }
    if (result.sample_times.empty()) {
        throw std::invalid_argument(
            "offline timeline " + path.string()
            + ": at least one sample record is required");
    }

    // Reuse M94's semantic keyframe validator without sampling or allocating
    // interpolated frames. The parser therefore cannot diverge from the
    // programmatic time-domain contract.
    (void)detail::validate_offline_timeline_keyframes(result.keyframes);
    return result;
}


// Strict bounded sidecar for M97 hierarchical timeline state:
//
//   tiny-renderer-hierarchy-timeline-v1
//   parent ENTRY root
//   parent ENTRY PARENT_ENTRY
//   ... exactly one parent record per prepared scene entry ...
//   keyframe TIME EX EY EZ TX TY TZ UX UY UZ VFOV_RADIANS NEAR FAR
//   local M00 M01 M02 M03 M10 M11 M12 M13 M20 M21 M22 M23 M30 M31 M32 M33
//   ... exactly one local transform per prepared scene entry ...
//   end
//   ... at least two strictly increasing keyframes ...
//   sample TIME
//   ... one or more caller-ordered sample requests ...
//
// Parent records may appear in arbitrary order because ENTRY is explicit, but
// all topology records must precede keyframes. The loader validates strict
// syntax, complete/unique topology ownership, camera/local affine state,
// keyframe ordering, and sample-domain membership. It performs no interpolation
// and no local-to-world composition; M97 remains the sole semantic authority.
[[nodiscard]] inline OfflineSceneHierarchicalTimelineFile
load_offline_hierarchical_timeline_sequence_file(
    const std::filesystem::path& path,
    std::size_t expected_model_count) {
    if (expected_model_count > detail::kMaxOfflineSceneEntries) {
        throw std::invalid_argument(
            "offline hierarchical timeline expected model count exceeds bounded scene entry limit");
    }

    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(
            "failed to open offline hierarchical timeline: " + path.string());
    }

    std::vector<std::optional<std::size_t>> parents(expected_model_count);
    std::vector<unsigned char> parent_seen(expected_model_count, 0U);
    std::size_t parent_count = 0U;
    std::optional<OfflineSceneHierarchy> hierarchy;
    std::vector<OfflineSceneHierarchicalTimelineKeyframe> keyframes;
    std::vector<float> sample_times;
    std::optional<OfflineSceneHierarchicalTimelineKeyframe> current;
    bool header_seen = false;
    bool samples_started = false;
    std::string line_text;
    std::size_t line_number = 0U;

    const auto finalize_hierarchy = [&](std::size_t diagnostic_line) {
        if (hierarchy) {
            return;
        }
        if (parent_count != expected_model_count) {
            detail::offline_hierarchical_timeline_sequence_error(
                path,
                diagnostic_line,
                "hierarchy requires exactly one parent record per prepared scene entry before keyframes");
        }
        try {
            hierarchy.emplace(parents);
        } catch (const std::exception& caught) {
            detail::offline_hierarchical_timeline_sequence_error(
                path, diagnostic_line, caught.what());
        }
    };

    while (std::getline(input, line_text)) {
        ++line_number;
        if (detail::offline_sequence_ignorable_line(line_text)) {
            continue;
        }

        std::istringstream line(line_text);
        std::string directive;
        line >> directive;

        if (!header_seen) {
            if (directive != detail::kOfflineHierarchicalTimelineSequenceHeader) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "first non-comment line must be tiny-renderer-hierarchy-timeline-v1");
            }
            detail::reject_offline_sequence_extra_tokens(
                path,
                line_number,
                line,
                detail::offline_hierarchical_timeline_sequence_error);
            header_seen = true;
            continue;
        }

        if (directive == "parent") {
            if (hierarchy || current || !keyframes.empty() || samples_started) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "parent records must precede all keyframes and samples");
            }

            std::string entry_token;
            std::string parent_token;
            if (!(line >> entry_token >> parent_token)) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "parent requires ENTRY followed by root or PARENT_ENTRY");
            }
            detail::reject_offline_sequence_extra_tokens(
                path,
                line_number,
                line,
                detail::offline_hierarchical_timeline_sequence_error);

            const std::size_t entry = detail::parse_offline_sequence_index(
                path,
                line_number,
                entry_token,
                "hierarchy entry index",
                detail::offline_hierarchical_timeline_sequence_error);
            if (entry >= expected_model_count) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "hierarchy entry index exceeds prepared scene entry count");
            }
            if (parent_seen[entry] != 0U) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "duplicate parent record for hierarchy entry");
            }

            if (parent_token == "root") {
                parents[entry] = std::nullopt;
            } else {
                const std::size_t parent = detail::parse_offline_sequence_index(
                    path,
                    line_number,
                    parent_token,
                    "hierarchy parent index",
                    detail::offline_hierarchical_timeline_sequence_error);
                if (parent >= expected_model_count) {
                    detail::offline_hierarchical_timeline_sequence_error(
                        path,
                        line_number,
                        "hierarchy parent index exceeds prepared scene entry count");
                }
                if (parent == entry) {
                    detail::offline_hierarchical_timeline_sequence_error(
                        path,
                        line_number,
                        "offline hierarchy entry cannot parent itself");
                }
                parents[entry] = parent;
            }
            parent_seen[entry] = 1U;
            ++parent_count;
            if (parent_count == expected_model_count) {
                finalize_hierarchy(line_number);
            }
            continue;
        }

        if (directive == "keyframe") {
            if (samples_started) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "keyframes must precede all sample directives");
            }
            if (current) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "new keyframe encountered before previous keyframe end");
            }
            finalize_hierarchy(line_number);
            if (keyframes.size() >= detail::kMaxOfflineTimelineKeyframes) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "keyframe count exceeds bounded timeline limit");
            }

            std::string time_token;
            if (!(line >> time_token)) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "keyframe requires TIME followed by camera state");
            }
            const float time = detail::parse_offline_sequence_float(
                path,
                line_number,
                time_token,
                "keyframe time",
                detail::offline_hierarchical_timeline_sequence_error);
            if (!keyframes.empty() && !(time > keyframes.back().time)) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "keyframe times must be strictly increasing");
            }

            current.emplace();
            current->time = time;
            current->frame.camera = detail::parse_offline_sequence_camera(
                path,
                line_number,
                line,
                "keyframe",
                detail::offline_hierarchical_timeline_sequence_error);
            current->frame.local_transforms.reserve(expected_model_count);
            continue;
        }

        if (directive == "local") {
            if (!current) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "local record requires an active keyframe");
            }
            if (current->frame.local_transforms.size() >= expected_model_count) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "local transform count exceeds prepared scene entry count");
            }
            current->frame.local_transforms.push_back(
                detail::parse_offline_sequence_model_matrix(
                    path,
                    line_number,
                    line,
                    "offline hierarchical timeline keyframe local transform",
                    detail::offline_hierarchical_timeline_sequence_error));
            continue;
        }

        if (directive == "end") {
            if (!current) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path, line_number, "end requires an active keyframe");
            }
            detail::reject_offline_sequence_extra_tokens(
                path,
                line_number,
                line,
                detail::offline_hierarchical_timeline_sequence_error);
            if (current->frame.local_transforms.size() != expected_model_count) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "keyframe local transform count must match prepared scene entry count");
            }
            keyframes.push_back(std::move(*current));
            current.reset();
            continue;
        }

        if (directive == "sample") {
            if (current) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "sample cannot appear inside an active keyframe");
            }
            if (keyframes.size() < 2U) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "samples require at least two completed keyframes");
            }
            samples_started = true;
            if (sample_times.size() >= detail::kMaxOfflineTimelineSamples) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "sample count exceeds bounded timeline limit");
            }

            std::string time_token;
            if (!(line >> time_token)) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path, line_number, "sample requires TIME");
            }
            detail::reject_offline_sequence_extra_tokens(
                path,
                line_number,
                line,
                detail::offline_hierarchical_timeline_sequence_error);
            const float sample_time = detail::parse_offline_sequence_float(
                path,
                line_number,
                time_token,
                "sample time",
                detail::offline_hierarchical_timeline_sequence_error);
            if (sample_time < keyframes.front().time
                || sample_time > keyframes.back().time) {
                detail::offline_hierarchical_timeline_sequence_error(
                    path,
                    line_number,
                    "sample time is outside the keyframe domain");
            }
            sample_times.push_back(sample_time);
            continue;
        }

        detail::offline_hierarchical_timeline_sequence_error(
            path, line_number, "unknown directive '" + directive + "'");
    }

    if (!header_seen) {
        throw std::invalid_argument(
            "offline hierarchical timeline " + path.string()
            + ": missing tiny-renderer-hierarchy-timeline-v1 header");
    }
    if (current) {
        detail::offline_hierarchical_timeline_sequence_error(
            path,
            line_number,
            "unterminated keyframe record requires end");
    }
    finalize_hierarchy(line_number);
    if (keyframes.size() < 2U) {
        throw std::invalid_argument(
            "offline hierarchical timeline " + path.string()
            + ": at least two keyframe records are required");
    }
    if (sample_times.empty()) {
        throw std::invalid_argument(
            "offline hierarchical timeline " + path.string()
            + ": at least one sample record is required");
    }

    (void)detail::validate_offline_hierarchical_timeline_keyframes(
        *hierarchy,
        keyframes);
    return OfflineSceneHierarchicalTimelineFile{
        std::move(*hierarchy),
        std::move(keyframes),
        std::move(sample_times),
    };
}

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
    if (expected_model_count > detail::kMaxOfflineSceneEntries) {
        throw std::invalid_argument(
            "offline frame sequence expected model count exceeds bounded scene entry limit");
    }

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

// Programmatic M96 path: validates one immutable parent topology and resolves
// every frame's local transforms into complete world transforms before
// delegating the entire batch to M92. Ordering, visibility, camera-dependent
// overrides, target preflight, and indexed execution therefore stay owned by
// the existing prepared-frame transaction.
[[nodiscard]] inline PreparedOfflineCameraSequence
prepare_offline_hierarchy_sequence(
    const PreparedOfflineMixedScene& scene,
    const OfflineSceneHierarchy& hierarchy,
    std::span<const OfflineSceneHierarchicalFrameState> frames) {
    if (hierarchy.parents().size() != scene.plan().entries().size()) {
        throw std::invalid_argument(
            "offline hierarchy entry count must match prepared scene entry count");
    }
    if (frames.size() > detail::kMaxOfflineSequenceCameras) {
        throw std::invalid_argument(
            "offline hierarchy frame sequence exceeds bounded frame limit");
    }

    std::vector<OfflineSceneFrameState> world_frames;
    world_frames.reserve(frames.size());
    for (const OfflineSceneHierarchicalFrameState& frame : frames) {
        world_frames.push_back(OfflineSceneFrameState{
            frame.camera,
            detail::resolve_offline_hierarchy_world_transforms(
                hierarchy,
                frame.local_transforms),
        });
    }

    return prepare_offline_frame_sequence(scene, world_frames);
}

// Programmatic M99 path: resolves every transform-graph node first, including
// non-renderable groups/pivots, then extracts one world matrix per prepared
// render entry in prepared-entry order and delegates the complete batch to M92.
// No graph-specific ordering, visibility, material, or raster path exists.
[[nodiscard]] inline PreparedOfflineCameraSequence
prepare_offline_transform_graph_sequence(
    const PreparedOfflineMixedScene& scene,
    const OfflineSceneTransformGraph& graph,
    std::span<const OfflineSceneTransformGraphFrameState> frames) {
    if (graph.render_entry_nodes().size() != scene.plan().entries().size()) {
        throw std::invalid_argument(
            "offline transform graph render binding count must match prepared scene entry count");
    }
    if (frames.size() > detail::kMaxOfflineSequenceCameras) {
        throw std::invalid_argument(
            "offline transform graph frame sequence exceeds bounded frame limit");
    }

    std::vector<OfflineSceneFrameState> world_frames;
    world_frames.reserve(frames.size());
    for (const OfflineSceneTransformGraphFrameState& frame : frames) {
        world_frames.push_back(OfflineSceneFrameState{
            frame.camera,
            detail::resolve_offline_transform_graph_render_world_transforms(
                graph,
                frame.local_transforms),
        });
    }

    return prepare_offline_frame_sequence(scene, world_frames);
}

// Programmatic M97 path: samples camera and local affine state with M94's
// semantics, resolves every sampled frame through M96's immutable hierarchy,
// then delegates the complete world-frame batch to M92.
[[nodiscard]] inline PreparedOfflineCameraSequence
prepare_offline_hierarchy_timeline_sequence(
    const PreparedOfflineMixedScene& scene,
    const OfflineSceneHierarchy& hierarchy,
    std::span<const OfflineSceneHierarchicalTimelineKeyframe> keyframes,
    std::span<const float> sample_times) {
    if (hierarchy.parents().size() != scene.plan().entries().size()) {
        throw std::invalid_argument(
            "offline hierarchical timeline entry count must match prepared scene entry count");
    }

    const std::vector<OfflineSceneHierarchicalFrameState> sampled_frames =
        sample_offline_hierarchical_timeline(
            hierarchy,
            keyframes,
            sample_times);
    return prepare_offline_hierarchy_sequence(
        scene,
        hierarchy,
        sampled_frames);
}

// Samples and prepares a complete bounded timeline transaction through M92's
// existing affine-frame preparation path. All keyframes are checked against the
// prepared scene entry count even when the requested sample span is empty.
[[nodiscard]] inline PreparedOfflineCameraSequence prepare_offline_timeline_sequence(
    const PreparedOfflineMixedScene& scene,
    std::span<const OfflineSceneTimelineKeyframe> keyframes,
    std::span<const float> sample_times) {
    const std::size_t scene_entry_count = scene.plan().entries().size();
    for (const OfflineSceneTimelineKeyframe& keyframe : keyframes) {
        if (keyframe.frame.model_transforms.size() != scene_entry_count) {
            throw std::invalid_argument(
                "offline timeline keyframe model transform count must match prepared scene entry count");
        }
    }

    const std::vector<OfflineSceneFrameState> sampled_frames =
        sample_offline_frame_timeline(keyframes, sample_times);
    return prepare_offline_frame_sequence(scene, sampled_frames);
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
