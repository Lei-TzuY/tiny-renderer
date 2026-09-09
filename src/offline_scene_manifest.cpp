#include "tiny_renderer/offline_render.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace tiny_renderer {
namespace {

constexpr std::size_t kMaxOfflineSceneEntries = 256U;
constexpr std::string_view kSceneManifestHeader = "tiny-renderer-scene-v1";

[[noreturn]] void manifest_error(
    const std::filesystem::path& path,
    std::size_t line,
    const std::string& message) {
    throw std::invalid_argument(
        "offline scene manifest " + path.string() + ": line "
        + std::to_string(line) + ": " + message);
}

bool ignorable_manifest_line(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t\r");
    return first == std::string::npos || line[first] == '#';
}

float parse_manifest_float(
    const std::filesystem::path& path,
    std::size_t line,
    std::string_view token,
    const char* label) {
    std::size_t consumed = 0U;
    float value = 0.0F;
    try {
        value = std::stof(std::string(token), &consumed);
    } catch (const std::exception&) {
        manifest_error(path, line, std::string(label) + " must be a finite number");
    }
    if (consumed != token.size() || !std::isfinite(value)) {
        manifest_error(path, line, std::string(label) + " must be a finite number");
    }
    return value;
}

std::filesystem::path resolve_manifest_model_path(
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
        manifest_error(
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
        manifest_error(manifest_path, line, "model path must use the .obj extension");
    }

    return (manifest_path.parent_path() / relative).lexically_normal();
}

void reject_extra_tokens(
    const std::filesystem::path& path,
    std::size_t line,
    std::istringstream& input) {
    std::string extra;
    if (input >> extra) {
        manifest_error(path, line, "unexpected trailing token '" + extra + "'");
    }
}

}  // namespace

OfflineSceneManifest load_offline_scene_manifest_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open offline scene manifest: " + path.string());
    }

    OfflineSceneManifest manifest;
    bool header_seen = false;
    bool ordering_seen = false;
    std::string line_text;
    std::size_t line_number = 0U;

    while (std::getline(input, line_text)) {
        ++line_number;
        if (ignorable_manifest_line(line_text)) {
            continue;
        }

        std::istringstream line(line_text);
        std::string directive;
        line >> directive;

        if (!header_seen) {
            if (directive != kSceneManifestHeader) {
                manifest_error(
                    path,
                    line_number,
                    "first non-comment line must be tiny-renderer-scene-v1");
            }
            reject_extra_tokens(path, line_number, line);
            header_seen = true;
            continue;
        }

        if (directive == "ordering") {
            if (ordering_seen) {
                manifest_error(path, line_number, "ordering may be specified at most once");
            }
            std::string value;
            if (!(line >> value)) {
                manifest_error(path, line_number, "ordering requires input or back-to-front");
            }
            if (value == "input") {
                manifest.ordering = OfflineSceneOrdering::InputOrder;
            } else if (value == "back-to-front") {
                manifest.ordering = OfflineSceneOrdering::BackToFront;
            } else {
                manifest_error(path, line_number, "ordering must be input or back-to-front");
            }
            reject_extra_tokens(path, line_number, line);
            ordering_seen = true;
            continue;
        }

        if (directive == "model") {
            if (manifest.entries.size() >= kMaxOfflineSceneEntries) {
                manifest_error(path, line_number, "scene exceeds the 256-entry limit");
            }

            std::string model_token;
            std::string tx_token;
            std::string ty_token;
            std::string tz_token;
            std::string scale_token;
            std::string yaw_token;
            if (!(line >> model_token >> tx_token >> ty_token >> tz_token >> scale_token >> yaw_token)) {
                manifest_error(
                    path,
                    line_number,
                    "model requires FILE.obj TX TY TZ SCALE ROTATION_Y_RADIANS");
            }
            reject_extra_tokens(path, line_number, line);

            const float tx = parse_manifest_float(path, line_number, tx_token, "model translation X");
            const float ty = parse_manifest_float(path, line_number, ty_token, "model translation Y");
            const float tz = parse_manifest_float(path, line_number, tz_token, "model translation Z");
            const float scale = parse_manifest_float(path, line_number, scale_token, "model uniform scale");
            const float yaw = parse_manifest_float(path, line_number, yaw_token, "model Y rotation");
            if (scale <= 0.0F) {
                manifest_error(path, line_number, "model uniform scale must be greater than zero");
            }

            const Mat4 model = Mat4::translation({tx, ty, tz})
                * Mat4::rotation_y(yaw)
                * Mat4::scale({scale, scale, scale});
            manifest.entries.push_back({
                resolve_manifest_model_path(path, line_number, model_token),
                model,
            });
            continue;
        }

        manifest_error(path, line_number, "unknown directive '" + directive + "'");
    }

    if (!header_seen) {
        throw std::invalid_argument(
            "offline scene manifest " + path.string()
            + ": missing tiny-renderer-scene-v1 header");
    }
    return manifest;
}

}  // namespace tiny_renderer