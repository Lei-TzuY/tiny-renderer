#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "tiny_renderer/environment.hpp"
#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model.hpp"
#include "tiny_renderer/model_renderer.hpp"

namespace tiny_renderer {

// Headless reflection intentionally omits a viewer position. The active preview
// camera is authoritative, so render_model_preview/render_scene_preview bind
// the M58 reflection light to the exact same camera eye used to build the view
// matrix.
struct OfflineEnvironmentReflectionState {
    NormalBinding normal{};
    EnvironmentReflectionState environment{};
};

// Deterministic headless preview settings. Model preview and the default flat-
// scene path keep the historical fixed +Z camera plus finite-bounds auto-fit.
// An explicit flat-scene camera is supplied separately so the default settings
// remain source- and byte-compatible.
struct OfflineRenderSettings {
    std::size_t width{512U};
    std::size_t height{512U};
    SampleCount sample_count{SampleCount::Four};
    Vec3 clear_color{0.02F, 0.025F, 0.035F};
    float vertical_fov_radians{radians(50.0F)};
    float framing_margin{1.10F};
    // Optional borrowed linear-HDR environment background. When present it is
    // rendered before geometry using the same active preview camera/FOV/aspect.
    std::optional<EnvironmentBackgroundState> environment{};
    // Optional borrowed diffuse environment light. Background visibility and
    // lighting are independent; callers may enable either, both, or neither.
    // The normal binding is explicit because OfflineRenderSettings is also a
    // library API and cannot infer arbitrary caller-owned varying layouts.
    std::optional<EnvironmentDiffuseLight> environment_lighting{};
    // Optional borrowed perfect-mirror environment reflection. The preview
    // owns viewer/camera consistency; callers supply only normal binding and
    // the borrowed environment state.
    std::optional<OfflineEnvironmentReflectionState> environment_reflection{};
};

enum class OfflineSceneOrdering {
    InputOrder,
    BackToFront,
};

// Optional explicit world-space perspective camera for flat-scene rendering.
// When absent, render_scene_preview preserves the historical combined-bounds
// auto-fit path. When present, model transforms remain in caller world space
// and no auto-fit transform is injected.
struct OfflineSceneCamera {
    Vec3 eye{0.0F, 0.0F, 3.0F};
    Vec3 target{0.0F, 0.0F, 0.0F};
    Vec3 up{0.0F, 1.0F, 0.0F};
    float vertical_fov_radians{radians(50.0F)};
    float near_plane{0.1F};
    float far_plane{100.0F};
};

void validate_offline_scene_camera(const OfflineSceneCamera& camera);

// One borrowed entry in a bounded flat scene. The asset and render options are
// snapshotted into PreparedModelSubmission objects before any returned render
// can exist; there is deliberately no hierarchy, persistent scene graph, or
// alternate model/raster ownership path.
struct OfflineSceneEntry {
    const ModelAsset* asset{nullptr};
    Mat4 model{Mat4::identity()};
    ModelRenderOptions options{};
};

// Parsed CLI-facing flat-scene description. Paths are resolved as sibling OBJ
// files of the manifest itself. Each record may optionally request one bounded
// material-model override for its scene-owned asset snapshot. The override is
// tooling configuration only: canonical ModelAsset/MaterialState semantics and
// render_scene_preview remain the execution path.
struct OfflineSceneManifestEntry {
    std::filesystem::path model_path{};
    Mat4 model{Mat4::identity()};
    std::optional<MaterialShadingModel> shading_model_override{};
};

struct OfflineSceneManifest {
    OfflineSceneOrdering ordering{OfflineSceneOrdering::InputOrder};
    std::optional<OfflineSceneCamera> camera{};
    std::vector<OfflineSceneManifestEntry> entries{};
};

namespace detail {

inline constexpr std::size_t kMaxOfflineSceneEntries = 256U;
inline constexpr std::string_view kOfflineSceneManifestHeader = "tiny-renderer-scene-v1";

[[noreturn]] inline void offline_scene_manifest_error(
    const std::filesystem::path& path,
    std::size_t line,
    const std::string& message) {
    throw std::invalid_argument(
        "offline scene manifest " + path.string() + ": line "
        + std::to_string(line) + ": " + message);
}

[[nodiscard]] inline bool offline_scene_ignorable_line(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t\r");
    return first == std::string::npos || line[first] == '#';
}

[[nodiscard]] inline float parse_offline_scene_float(
    const std::filesystem::path& path,
    std::size_t line,
    std::string_view token,
    const char* label) {
    std::size_t consumed = 0U;
    float value = 0.0F;
    try {
        value = std::stof(std::string(token), &consumed);
    } catch (const std::exception&) {
        offline_scene_manifest_error(path, line, std::string(label) + " must be a finite number");
    }
    if (consumed != token.size() || !std::isfinite(value)) {
        offline_scene_manifest_error(path, line, std::string(label) + " must be a finite number");
    }
    return value;
}

[[nodiscard]] inline std::filesystem::path resolve_offline_scene_model_path(
    const std::filesystem::path& manifest_path,
    std::size_t line,
    const std::string& token) {
    const std::filesystem::path relative(token);
    if (token.empty()
        || relative.is_absolute()
        || relative.has_root_name()
        || relative.has_root_directory()
        || !relative.parent_path().empty()
        || relative.filename().empty()
        || relative.filename() == "."
        || relative.filename() == "..") {
        offline_scene_manifest_error(
            manifest_path,
            line,
            "model path must name one sibling OBJ file without traversal or subdirectories");
    }

    std::string extension = relative.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension != ".obj") {
        offline_scene_manifest_error(manifest_path, line, "model path must use the .obj extension");
    }
    return (manifest_path.parent_path() / relative).lexically_normal();
}

[[nodiscard]] inline std::optional<MaterialShadingModel> parse_offline_scene_shading_model(
    const std::filesystem::path& path,
    std::size_t line,
    std::string_view token) {
    if (token == "inherit") {
        return std::nullopt;
    }
    if (token == "lambert") {
        return MaterialShadingModel::Lambert;
    }
    if (token == "blinn-phong") {
        return MaterialShadingModel::BlinnPhong;
    }
    offline_scene_manifest_error(
        path,
        line,
        "model shading mode must be inherit, lambert, or blinn-phong");
}

inline void reject_offline_scene_extra_tokens(
    const std::filesystem::path& path,
    std::size_t line,
    std::istringstream& input) {
    std::string extra;
    if (input >> extra) {
        offline_scene_manifest_error(path, line, "unexpected trailing token '" + extra + "'");
    }
}

}  // namespace detail

// Strict bounded text format:
//   tiny-renderer-scene-v1
//   ordering input|back-to-front        # optional, at most once
//   camera EX EY EZ TX TY TZ UX UY UZ VFOV NEAR FAR  # optional, at most once
//   model FILE.obj TX TY TZ SCALE RY [inherit|lambert|blinn-phong]
//                                      # repeat, max 256 entries
// Blank lines and full-line '#' comments are ignored. FILE.obj must be a
// sibling filename (no absolute path, parent traversal, or subdirectory).
// Omitting the final shading token is identical to explicit `inherit`.
[[nodiscard]] inline OfflineSceneManifest load_offline_scene_manifest_file(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open offline scene manifest: " + path.string());
    }

    OfflineSceneManifest manifest;
    bool header_seen = false;
    bool ordering_seen = false;
    bool camera_seen = false;
    std::string line_text;
    std::size_t line_number = 0U;

    while (std::getline(input, line_text)) {
        ++line_number;
        if (detail::offline_scene_ignorable_line(line_text)) {
            continue;
        }

        std::istringstream line(line_text);
        std::string directive;
        line >> directive;

        if (!header_seen) {
            if (directive != detail::kOfflineSceneManifestHeader) {
                detail::offline_scene_manifest_error(
                    path,
                    line_number,
                    "first non-comment line must be tiny-renderer-scene-v1");
            }
            detail::reject_offline_scene_extra_tokens(path, line_number, line);
            header_seen = true;
            continue;
        }

        if (directive == "ordering") {
            if (ordering_seen) {
                detail::offline_scene_manifest_error(path, line_number, "ordering may be specified at most once");
            }
            std::string value;
            if (!(line >> value)) {
                detail::offline_scene_manifest_error(
                    path,
                    line_number,
                    "ordering requires input or back-to-front");
            }
            if (value == "input") {
                manifest.ordering = OfflineSceneOrdering::InputOrder;
            } else if (value == "back-to-front") {
                manifest.ordering = OfflineSceneOrdering::BackToFront;
            } else {
                detail::offline_scene_manifest_error(
                    path,
                    line_number,
                    "ordering must be input or back-to-front");
            }
            detail::reject_offline_scene_extra_tokens(path, line_number, line);
            ordering_seen = true;
            continue;
        }

        if (directive == "camera") {
            if (camera_seen) {
                detail::offline_scene_manifest_error(path, line_number, "camera may be specified at most once");
            }
            std::array<std::string, 12> tokens{};
            for (std::string& token : tokens) {
                if (!(line >> token)) {
                    detail::offline_scene_manifest_error(
                        path,
                        line_number,
                        "camera requires EX EY EZ TX TY TZ UX UY UZ VFOV_RADIANS NEAR FAR");
                }
            }
            detail::reject_offline_scene_extra_tokens(path, line_number, line);

            OfflineSceneCamera camera;
            camera.eye = {
                detail::parse_offline_scene_float(path, line_number, tokens[0], "camera eye X"),
                detail::parse_offline_scene_float(path, line_number, tokens[1], "camera eye Y"),
                detail::parse_offline_scene_float(path, line_number, tokens[2], "camera eye Z"),
            };
            camera.target = {
                detail::parse_offline_scene_float(path, line_number, tokens[3], "camera target X"),
                detail::parse_offline_scene_float(path, line_number, tokens[4], "camera target Y"),
                detail::parse_offline_scene_float(path, line_number, tokens[5], "camera target Z"),
            };
            camera.up = {
                detail::parse_offline_scene_float(path, line_number, tokens[6], "camera up X"),
                detail::parse_offline_scene_float(path, line_number, tokens[7], "camera up Y"),
                detail::parse_offline_scene_float(path, line_number, tokens[8], "camera up Z"),
            };
            camera.vertical_fov_radians = detail::parse_offline_scene_float(
                path, line_number, tokens[9], "camera vertical field of view");
            camera.near_plane = detail::parse_offline_scene_float(
                path, line_number, tokens[10], "camera near plane");
            camera.far_plane = detail::parse_offline_scene_float(
                path, line_number, tokens[11], "camera far plane");
            try {
                validate_offline_scene_camera(camera);
            } catch (const std::invalid_argument& error) {
                detail::offline_scene_manifest_error(path, line_number, error.what());
            }
            manifest.camera = camera;
            camera_seen = true;
            continue;
        }

        if (directive == "model") {
            if (manifest.entries.size() >= detail::kMaxOfflineSceneEntries) {
                detail::offline_scene_manifest_error(path, line_number, "scene exceeds the 256-entry limit");
            }

            std::string model_token;
            std::string tx_token;
            std::string ty_token;
            std::string tz_token;
            std::string scale_token;
            std::string yaw_token;
            if (!(line >> model_token >> tx_token >> ty_token >> tz_token >> scale_token >> yaw_token)) {
                detail::offline_scene_manifest_error(
                    path,
                    line_number,
                    "model requires FILE.obj TX TY TZ SCALE ROTATION_Y_RADIANS [SHADING_MODE]");
            }

            std::optional<MaterialShadingModel> shading_model_override;
            std::string shading_token;
            if (line >> shading_token) {
                shading_model_override = detail::parse_offline_scene_shading_model(
                    path, line_number, shading_token);
            }
            detail::reject_offline_scene_extra_tokens(path, line_number, line);

            const float tx = detail::parse_offline_scene_float(
                path, line_number, tx_token, "model translation X");
            const float ty = detail::parse_offline_scene_float(
                path, line_number, ty_token, "model translation Y");
            const float tz = detail::parse_offline_scene_float(
                path, line_number, tz_token, "model translation Z");
            const float scale = detail::parse_offline_scene_float(
                path, line_number, scale_token, "model uniform scale");
            const float yaw = detail::parse_offline_scene_float(
                path, line_number, yaw_token, "model Y rotation");
            if (scale <= 0.0F) {
                detail::offline_scene_manifest_error(
                    path,
                    line_number,
                    "model uniform scale must be greater than zero");
            }

            const Mat4 model = Mat4::translation({tx, ty, tz})
                * Mat4::rotation_y(yaw)
                * Mat4::scale({scale, scale, scale});
            manifest.entries.push_back({
                detail::resolve_offline_scene_model_path(path, line_number, model_token),
                model,
                shading_model_override,
            });
            continue;
        }

        detail::offline_scene_manifest_error(
            path,
            line_number,
            "unknown directive '" + directive + "'");
    }

    if (!header_seen) {
        throw std::invalid_argument(
            "offline scene manifest " + path.string()
            + ": missing tiny-renderer-scene-v1 header");
    }
    return manifest;
}

// Renders a bounded, auto-framed preview through the existing model/raster
// path. ModelRenderOptions are forwarded unchanged except that optional
// environment lighting/reflection settings are injected into the existing
// fixed-light collection. No alternate material, texture, lighting, depth,
// environment, or framebuffer implementation is introduced.
[[nodiscard]] Framebuffer render_model_preview(
    const ModelAsset& asset,
    const OfflineRenderSettings& settings = {},
    ModelRenderOptions options = {});

// Renders an ordered heterogeneous flat scene using the canonical prepared
// model list executor. InputOrder preserves caller entry order. BackToFront
// delegates to the established deterministic painter-order helper and inherits
// its bounded entry-level semantics and vertex-program rejection. A supplied
// camera preserves entry transforms in world space; absent camera preserves the
// combined-bounds auto-fit contract. Empty scenes are valid clear/environment-
// only renders.
[[nodiscard]] Framebuffer render_scene_preview(
    std::span<const OfflineSceneEntry> entries,
    const OfflineRenderSettings& settings = {},
    OfflineSceneOrdering ordering = OfflineSceneOrdering::InputOrder,
    std::optional<OfflineSceneCamera> camera = std::nullopt);

}  // namespace tiny_renderer
