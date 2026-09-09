#include "tiny_renderer/offline_render.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {
namespace {

constexpr std::size_t kMaxPreviewPixels = 4U * 1024U * 1024U;
constexpr float kCameraDistance = 3.0F;
constexpr Vec3 kCameraEye{0.0F, 0.0F, kCameraDistance};
constexpr Vec3 kCameraTarget{0.0F, 0.0F, 0.0F};
constexpr Vec3 kCameraUp{0.0F, 1.0F, 0.0F};

bool finite_vec3(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void validate_settings(const OfflineRenderSettings& settings) {
    if (settings.width == 0U || settings.height == 0U) {
        throw std::invalid_argument("offline render dimensions must be non-zero");
    }
    if (settings.width > kMaxPreviewPixels / settings.height) {
        throw std::invalid_argument("offline render exceeds bounded pixel budget");
    }
    switch (settings.sample_count) {
        case SampleCount::One:
        case SampleCount::Four:
            break;
        default:
            throw std::invalid_argument("offline render uses an unsupported sample count");
    }
    if (!finite_vec3(settings.clear_color)) {
        throw std::invalid_argument("offline render clear color must be finite");
    }
    if (!std::isfinite(settings.vertical_fov_radians)
        || settings.vertical_fov_radians <= 0.0F
        || settings.vertical_fov_radians >= kPi) {
        throw std::invalid_argument("offline render vertical field of view must be finite and within (0, pi)");
    }
    if (!std::isfinite(settings.framing_margin)
        || settings.framing_margin < 1.0F
        || settings.framing_margin > 4.0F) {
        throw std::invalid_argument("offline render framing margin must be finite and within [1, 4]");
    }
    if (settings.environment) {
        validate_environment_background_state(*settings.environment);
    }
    if (settings.environment_lighting) {
        validate_environment_diffuse_state(settings.environment_lighting->environment);
    }
    if (settings.environment_reflection) {
        validate_environment_reflection_state(settings.environment_reflection->environment);
    }
}

void validate_scene_ordering(OfflineSceneOrdering ordering) {
    switch (ordering) {
        case OfflineSceneOrdering::InputOrder:
        case OfflineSceneOrdering::BackToFront:
            return;
    }
    throw std::invalid_argument("offline scene uses an unknown ordering policy");
}

ModelRenderOptions inject_offline_environment(
    const OfflineRenderSettings& settings,
    ModelRenderOptions options) {
    if (settings.environment_lighting) {
        if (options.fixed_lights.environment_diffuse) {
            throw std::invalid_argument(
                "offline render environment lighting conflicts with ModelRenderOptions environment diffuse lighting");
        }
        options.fixed_lights.environment_diffuse = settings.environment_lighting;
    }
    if (settings.environment_reflection) {
        if (options.fixed_lights.environment_reflection) {
            throw std::invalid_argument(
                "offline render environment reflection conflicts with ModelRenderOptions environment reflection");
        }
        EnvironmentReflectionLight reflection;
        reflection.normal = settings.environment_reflection->normal;
        reflection.viewer_position = kCameraEye;
        reflection.environment = settings.environment_reflection->environment;
        options.fixed_lights.environment_reflection = reflection;
    }
    return options;
}

struct ModelBounds {
    Vec3 center{};
    float radius{};
};

ModelBounds model_bounds(const ModelAsset& asset) {
    if (asset.mesh.vertices.empty() || asset.mesh.triangles.empty()) {
        throw std::invalid_argument("offline render requires a non-empty model mesh");
    }

    const Vec3 first = asset.mesh.vertices.front().position;
    if (!finite_vec3(first)) {
        throw std::invalid_argument("offline render model position must be finite");
    }

    double min_x = static_cast<double>(first.x);
    double min_y = static_cast<double>(first.y);
    double min_z = static_cast<double>(first.z);
    double max_x = min_x;
    double max_y = min_y;
    double max_z = min_z;

    for (const Vertex& vertex : asset.mesh.vertices) {
        if (!finite_vec3(vertex.position)) {
            throw std::invalid_argument("offline render model position must be finite");
        }
        const double x = static_cast<double>(vertex.position.x);
        const double y = static_cast<double>(vertex.position.y);
        const double z = static_cast<double>(vertex.position.z);
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        min_z = std::min(min_z, z);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
        max_z = std::max(max_z, z);
    }

    const double center_x = (min_x + max_x) * 0.5;
    const double center_y = (min_y + max_y) * 0.5;
    const double center_z = (min_z + max_z) * 0.5;
    double radius_squared = 0.0;
    for (const Vertex& vertex : asset.mesh.vertices) {
        const double dx = static_cast<double>(vertex.position.x) - center_x;
        const double dy = static_cast<double>(vertex.position.y) - center_y;
        const double dz = static_cast<double>(vertex.position.z) - center_z;
        radius_squared = std::max(radius_squared, dx * dx + dy * dy + dz * dz);
    }

    if (!std::isfinite(radius_squared) || radius_squared <= 0.0) {
        throw std::invalid_argument("offline render model bounds must have non-zero finite extent");
    }
    const double radius = std::sqrt(radius_squared);
    if (!std::isfinite(radius)
        || radius > static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::invalid_argument("offline render model bounds exceed finite float range");
    }

    const Vec3 center{
        static_cast<float>(center_x),
        static_cast<float>(center_y),
        static_cast<float>(center_z),
    };
    const float radius_float = static_cast<float>(radius);
    if (!finite_vec3(center) || !std::isfinite(radius_float) || radius_float <= 0.0F) {
        throw std::invalid_argument("offline render model bounds cannot be represented safely");
    }
    return {center, radius_float};
}

struct WorldPosition {
    double x{};
    double y{};
    double z{};
};

WorldPosition transformed_world_position(const Vertex& vertex, const Mat4& model) {
    if (!finite_vec3(vertex.position)) {
        throw std::invalid_argument("offline scene model position must be finite");
    }
    const Vec4 transformed = model * Vec4{
        vertex.position.x,
        vertex.position.y,
        vertex.position.z,
        1.0F,
    };
    if (!std::isfinite(transformed.x)
        || !std::isfinite(transformed.y)
        || !std::isfinite(transformed.z)
        || !std::isfinite(transformed.w)
        || std::fabs(transformed.w) <= kEpsilon) {
        throw std::invalid_argument(
            "offline scene model transform produced a non-finite or unprojectable position");
    }
    const double inv_w = 1.0 / static_cast<double>(transformed.w);
    const WorldPosition result{
        static_cast<double>(transformed.x) * inv_w,
        static_cast<double>(transformed.y) * inv_w,
        static_cast<double>(transformed.z) * inv_w,
    };
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) {
        throw std::invalid_argument("offline scene transformed position is non-finite");
    }
    return result;
}

std::optional<ModelBounds> scene_bounds(
    const std::vector<PreparedModelSubmission>& prepared,
    std::span<const OfflineSceneEntry> entries) {
    if (prepared.size() != entries.size()) {
        throw std::logic_error("offline scene prepared-entry count mismatch");
    }

    bool has_geometry = false;
    double min_x = 0.0;
    double min_y = 0.0;
    double min_z = 0.0;
    double max_x = 0.0;
    double max_y = 0.0;
    double max_z = 0.0;

    for (std::size_t i = 0U; i < prepared.size(); ++i) {
        const ModelAsset& asset = prepared[i].asset();
        if (asset.draws.empty()) {
            continue;
        }
        if (asset.mesh.vertices.empty()) {
            throw std::logic_error("non-empty offline scene draw list requires canonical vertices");
        }
        for (const Vertex& vertex : asset.mesh.vertices) {
            const WorldPosition position = transformed_world_position(vertex, entries[i].model);
            if (!has_geometry) {
                min_x = max_x = position.x;
                min_y = max_y = position.y;
                min_z = max_z = position.z;
                has_geometry = true;
            } else {
                min_x = std::min(min_x, position.x);
                min_y = std::min(min_y, position.y);
                min_z = std::min(min_z, position.z);
                max_x = std::max(max_x, position.x);
                max_y = std::max(max_y, position.y);
                max_z = std::max(max_z, position.z);
            }
        }
    }

    if (!has_geometry) {
        return std::nullopt;
    }

    const double center_x = (min_x + max_x) * 0.5;
    const double center_y = (min_y + max_y) * 0.5;
    const double center_z = (min_z + max_z) * 0.5;
    if (!std::isfinite(center_x) || !std::isfinite(center_y) || !std::isfinite(center_z)) {
        throw std::invalid_argument("offline scene bounds center is not finite");
    }

    double radius_squared = 0.0;
    for (std::size_t i = 0U; i < prepared.size(); ++i) {
        const ModelAsset& asset = prepared[i].asset();
        if (asset.draws.empty()) {
            continue;
        }
        for (const Vertex& vertex : asset.mesh.vertices) {
            const WorldPosition position = transformed_world_position(vertex, entries[i].model);
            const double dx = position.x - center_x;
            const double dy = position.y - center_y;
            const double dz = position.z - center_z;
            const double distance_squared = dx * dx + dy * dy + dz * dz;
            if (!std::isfinite(distance_squared)) {
                throw std::invalid_argument("offline scene bounds radius overflowed");
            }
            radius_squared = std::max(radius_squared, distance_squared);
        }
    }

    if (!std::isfinite(radius_squared) || radius_squared <= 0.0) {
        throw std::invalid_argument("offline scene bounds must have non-zero finite extent");
    }
    const double radius = std::sqrt(radius_squared);
    if (!std::isfinite(radius)
        || radius > static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::invalid_argument("offline scene bounds exceed finite float range");
    }

    const Vec3 center{
        static_cast<float>(center_x),
        static_cast<float>(center_y),
        static_cast<float>(center_z),
    };
    const float radius_float = static_cast<float>(radius);
    if (!finite_vec3(center) || !std::isfinite(radius_float) || radius_float <= 0.0F) {
        throw std::invalid_argument("offline scene bounds cannot be represented safely");
    }
    return ModelBounds{center, radius_float};
}

struct PreviewGeometryState {
    Mat4 fit{};
    Mat4 view{};
    Mat4 projection{};
    float aspect{};
};

PreviewGeometryState preview_geometry_state(
    const ModelBounds& bounds,
    const OfflineRenderSettings& settings) {
    const double aspect = static_cast<double>(settings.width) / static_cast<double>(settings.height);
    const double half_y = static_cast<double>(settings.vertical_fov_radians) * 0.5;
    const double half_x = std::atan(std::tan(half_y) * aspect);
    const double limiting_half_angle = std::min(half_x, half_y);
    const double target_radius =
        static_cast<double>(kCameraDistance) * std::sin(limiting_half_angle)
        / static_cast<double>(settings.framing_margin);
    const double scale = target_radius / static_cast<double>(bounds.radius);
    if (!std::isfinite(target_radius) || target_radius <= 0.0
        || !std::isfinite(scale) || scale <= 0.0
        || scale > static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::invalid_argument("offline render auto-fit transform is not representable");
    }
    const float scale_float = static_cast<float>(scale);
    if (!std::isfinite(scale_float) || scale_float <= 0.0F) {
        throw std::invalid_argument("offline render auto-fit scale is not representable");
    }

    const Mat4 fit = Mat4::scale({scale_float, scale_float, scale_float})
        * Mat4::translation({-bounds.center.x, -bounds.center.y, -bounds.center.z});
    const Mat4 view = Mat4::look_at(kCameraEye, kCameraTarget, kCameraUp);

    const double near_plane = std::max(
        0.001,
        static_cast<double>(kCameraDistance) - target_radius * 1.05);
    const double far_plane = static_cast<double>(kCameraDistance) + target_radius * 1.05;
    if (!(far_plane > near_plane)
        || far_plane > static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::invalid_argument("offline render preview clip range is not representable");
    }
    const float aspect_float = static_cast<float>(aspect);
    if (!std::isfinite(aspect_float) || aspect_float <= 0.0F) {
        throw std::invalid_argument("offline render aspect ratio is not representable");
    }
    const Mat4 projection = Mat4::perspective(
        settings.vertical_fov_radians,
        aspect_float,
        static_cast<float>(near_plane),
        static_cast<float>(far_plane));
    return {fit, view, projection, aspect_float};
}

float preview_aspect(const OfflineRenderSettings& settings) {
    const double aspect = static_cast<double>(settings.width) / static_cast<double>(settings.height);
    const float result = static_cast<float>(aspect);
    if (!std::isfinite(result) || result <= 0.0F) {
        throw std::invalid_argument("offline render aspect ratio is not representable");
    }
    return result;
}

void draw_preview_environment(
    Framebuffer& framebuffer,
    const OfflineRenderSettings& settings,
    float aspect) {
    if (!settings.environment) {
        return;
    }
    const PerspectiveCameraState camera{
        kCameraEye,
        kCameraTarget,
        kCameraUp,
        settings.vertical_fov_radians,
        aspect,
    };
    draw_environment_background(framebuffer, camera, *settings.environment);
}

}  // namespace

Framebuffer render_model_preview(
    const ModelAsset& asset,
    const OfflineRenderSettings& settings,
    ModelRenderOptions options) {
    validate_settings(settings);
    options = inject_offline_environment(settings, std::move(options));
    const ModelBounds bounds = model_bounds(asset);
    const PreviewGeometryState geometry = preview_geometry_state(bounds, settings);

    Framebuffer framebuffer(settings.width, settings.height, settings.sample_count);
    framebuffer.clear(settings.clear_color);
    draw_preview_environment(framebuffer, settings, geometry.aspect);
    draw_model_asset(
        framebuffer,
        asset,
        geometry.fit,
        geometry.view,
        geometry.projection,
        options);
    return framebuffer;
}

Framebuffer render_scene_preview(
    std::span<const OfflineSceneEntry> entries,
    const OfflineRenderSettings& settings,
    OfflineSceneOrdering ordering) {
    validate_settings(settings);
    validate_scene_ordering(ordering);

    std::vector<PreparedModelSubmission> prepared;
    prepared.reserve(entries.size());
    for (const OfflineSceneEntry& entry : entries) {
        if (entry.asset == nullptr) {
            throw std::invalid_argument("offline scene entry requires a model asset");
        }
        prepared.push_back(prepare_model_asset(
            *entry.asset,
            inject_offline_environment(settings, entry.options)));
    }

    const std::optional<ModelBounds> bounds = scene_bounds(prepared, entries);
    const float aspect = preview_aspect(settings);
    Framebuffer framebuffer(settings.width, settings.height, settings.sample_count);
    framebuffer.clear(settings.clear_color);
    draw_preview_environment(framebuffer, settings, aspect);
    if (!bounds) {
        return framebuffer;
    }

    const PreviewGeometryState geometry = preview_geometry_state(*bounds, settings);
    std::vector<PreparedModelListEntry> render_entries;
    render_entries.reserve(entries.size());
    for (std::size_t i = 0U; i < entries.size(); ++i) {
        render_entries.push_back({
            &prepared[i],
            geometry.fit * entries[i].model,
        });
    }

    const std::span<const PreparedModelListEntry> render_span{
        render_entries.data(), render_entries.size()};
    switch (ordering) {
        case OfflineSceneOrdering::InputOrder:
            draw_prepared_model_list(
                framebuffer,
                render_span,
                geometry.view,
                geometry.projection);
            break;
        case OfflineSceneOrdering::BackToFront:
            draw_prepared_model_list_back_to_front(
                framebuffer,
                render_span,
                geometry.view,
                geometry.projection);
            break;
    }
    return framebuffer;
}

}  // namespace tiny_renderer
