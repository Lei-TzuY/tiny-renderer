#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/model_renderer.hpp"

namespace tiny_renderer {

// Immutable object-space spatial summary for one canonical MaterialDraw. The
// bounds include every indexed triangle corner referenced by the draw range.
// They deliberately ignore unused mesh vertices because they cannot contribute
// fragments for this draw.
struct PreparedDrawSpatialMetadata {
    DrawRange range{};
    Vec3 min{};
    Vec3 max{};
    Vec3 center{};
};

// A prepared model plus spatial metadata derived once from the exact owned
// canonical snapshot. This keeps spatial scheduling state coupled to the same
// mesh/material/texture lifetime as PreparedModelSubmission without copying the
// mesh per draw.
class PreparedSpatialSubmission {
public:
    PreparedSpatialSubmission(const PreparedSpatialSubmission&) = default;
    PreparedSpatialSubmission(PreparedSpatialSubmission&&) noexcept = default;
    PreparedSpatialSubmission& operator=(const PreparedSpatialSubmission&) = default;
    PreparedSpatialSubmission& operator=(PreparedSpatialSubmission&&) noexcept = default;

    [[nodiscard]] const PreparedModelSubmission& prepared() const noexcept {
        return prepared_;
    }

    [[nodiscard]] std::span<const PreparedDrawSpatialMetadata> draws() const noexcept {
        return {draws_.data(), draws_.size()};
    }

private:
    friend PreparedSpatialSubmission prepare_spatial_submission(
        PreparedModelSubmission prepared);

    PreparedSpatialSubmission(
        PreparedModelSubmission prepared,
        std::vector<PreparedDrawSpatialMetadata> draws)
        : prepared_(std::move(prepared)), draws_(std::move(draws)) {}

    PreparedModelSubmission prepared_;
    std::vector<PreparedDrawSpatialMetadata> draws_;
};

struct PreparedSpatialListEntry {
    const PreparedSpatialSubmission* prepared{nullptr};
    Mat4 model{Mat4::identity()};
};

// One immutable planning result. draw_index indexes both ModelAsset::draws and
// PreparedSpatialSubmission::draws() for the referenced prepared snapshot.
struct PreparedDrawOrderEntry {
    const PreparedSpatialSubmission* prepared{nullptr};
    Mat4 model{Mat4::identity()};
    std::size_t draw_index{};
    double view_depth{};
};

namespace detail {

[[nodiscard]] inline bool finite_spatial_vec3(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline void validate_spatial_matrix_finite(const Mat4& matrix, const char* label) {
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            if (!std::isfinite(matrix(row, column))) {
                throw std::invalid_argument(std::string(label) + " must contain only finite values");
            }
        }
    }
}

inline void validate_spatial_affine_matrix(const Mat4& matrix, const char* label) {
    validate_spatial_matrix_finite(matrix, label);
    if (std::fabs(matrix(3U, 0U)) > kEpsilon
        || std::fabs(matrix(3U, 1U)) > kEpsilon
        || std::fabs(matrix(3U, 2U)) > kEpsilon
        || std::fabs(matrix(3U, 3U) - 1.0F) > kEpsilon) {
        throw std::invalid_argument(
            std::string(label) + " must be affine for prepared spatial ordering");
    }
}

[[nodiscard]] inline Vec3 safe_spatial_midpoint(const Vec3& min, const Vec3& max) {
    const double x = (static_cast<double>(min.x) + static_cast<double>(max.x)) * 0.5;
    const double y = (static_cast<double>(min.y) + static_cast<double>(max.y)) * 0.5;
    const double z = (static_cast<double>(min.z) + static_cast<double>(max.z)) * 0.5;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
        || std::fabs(x) > static_cast<double>(std::numeric_limits<float>::max())
        || std::fabs(y) > static_cast<double>(std::numeric_limits<float>::max())
        || std::fabs(z) > static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::invalid_argument("prepared draw bounds center is not representable");
    }
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

[[nodiscard]] inline PreparedDrawSpatialMetadata prepare_draw_spatial_metadata(
    const ModelAsset& asset,
    const MaterialDraw& draw) {
    if (draw.range.first_triangle > asset.mesh.triangles.size()
        || draw.range.triangle_count > asset.mesh.triangles.size() - draw.range.first_triangle
        || draw.range.triangle_count == 0U) {
        throw std::logic_error("prepared draw spatial metadata requires a valid non-empty draw range");
    }

    bool initialized = false;
    Vec3 min{};
    Vec3 max{};
    const std::size_t end = draw.range.first_triangle + draw.range.triangle_count;
    for (std::size_t triangle_index = draw.range.first_triangle;
         triangle_index < end;
         ++triangle_index) {
        for (const std::uint32_t index : asset.mesh.triangles[triangle_index]) {
            if (static_cast<std::size_t>(index) >= asset.mesh.vertices.size()) {
                throw std::logic_error("prepared draw spatial metadata encountered an invalid canonical index");
            }
            const Vec3 position = asset.mesh.vertices[index].position;
            if (!finite_spatial_vec3(position)) {
                throw std::invalid_argument(
                    "prepared draw spatial metadata requires finite referenced positions");
            }
            if (!initialized) {
                min = position;
                max = position;
                initialized = true;
                continue;
            }
            min.x = std::min(min.x, position.x);
            min.y = std::min(min.y, position.y);
            min.z = std::min(min.z, position.z);
            max.x = std::max(max.x, position.x);
            max.y = std::max(max.y, position.y);
            max.z = std::max(max.z, position.z);
        }
    }
    if (!initialized) {
        throw std::logic_error("prepared draw spatial metadata found no referenced geometry");
    }
    return {draw.range, min, max, safe_spatial_midpoint(min, max)};
}

[[nodiscard]] inline const PreparedDrawSpatialMetadata& validate_prepared_draw_spatial_entry(
    const PreparedDrawOrderEntry& entry) {
    if (entry.prepared == nullptr) {
        throw std::invalid_argument("prepared draw order entry requires a prepared spatial submission");
    }
    if (!std::isfinite(entry.view_depth)) {
        throw std::invalid_argument("prepared draw order entry requires a finite view depth");
    }
    validate_spatial_affine_matrix(
        entry.model,
        "prepared draw execution model transform");

    const PreparedModelSubmission& prepared = entry.prepared->prepared();
    if (prepared.options().vertex_program) {
        throw std::invalid_argument(
            "prepared draw execution does not support position-changing vertex programs");
    }
    const auto metadata = entry.prepared->draws();
    const ModelAsset& asset = prepared.asset();
    if (entry.draw_index >= metadata.size() || entry.draw_index >= asset.draws.size()) {
        throw std::out_of_range("prepared draw order entry references an unavailable material draw");
    }
    const MaterialDraw& draw = asset.draws[entry.draw_index];
    const PreparedDrawSpatialMetadata& spatial = metadata[entry.draw_index];
    if (spatial.range.first_triangle != draw.range.first_triangle
        || spatial.range.triangle_count != draw.range.triangle_count) {
        throw std::logic_error("prepared draw spatial metadata is inconsistent with the owned material draw");
    }
    return spatial;
}

[[nodiscard]] inline std::array<Vec3, 8> spatial_bounds_corners(
    const PreparedDrawSpatialMetadata& metadata) {
    return {{
        {metadata.min.x, metadata.min.y, metadata.min.z},
        {metadata.max.x, metadata.min.y, metadata.min.z},
        {metadata.min.x, metadata.max.y, metadata.min.z},
        {metadata.max.x, metadata.max.y, metadata.min.z},
        {metadata.min.x, metadata.min.y, metadata.max.z},
        {metadata.max.x, metadata.min.y, metadata.max.z},
        {metadata.min.x, metadata.max.y, metadata.max.z},
        {metadata.max.x, metadata.max.y, metadata.max.z},
    }};
}

[[nodiscard]] inline double clip_plane_distance(const Vec4& clip, std::size_t plane) {
    const double x = static_cast<double>(clip.x);
    const double y = static_cast<double>(clip.y);
    const double z = static_cast<double>(clip.z);
    const double w = static_cast<double>(clip.w);
    switch (plane) {
        case 0U: return w + x;
        case 1U: return w - x;
        case 2U: return w + y;
        case 3U: return w - y;
        case 4U: return w + z;
        case 5U: return w - z;
        default: throw std::logic_error("unknown homogeneous clip plane");
    }
}

[[nodiscard]] inline double clip_plane_scale(const Vec4& clip, std::size_t plane) {
    double component = 0.0;
    switch (plane) {
        case 0U:
        case 1U:
            component = std::fabs(static_cast<double>(clip.x));
            break;
        case 2U:
        case 3U:
            component = std::fabs(static_cast<double>(clip.y));
            break;
        case 4U:
        case 5U:
            component = std::fabs(static_cast<double>(clip.z));
            break;
        default:
            throw std::logic_error("unknown homogeneous clip plane");
    }
    return std::max({1.0, std::fabs(static_cast<double>(clip.w)), component});
}

[[nodiscard]] inline bool prepared_draw_fully_outside_clip_volume(
    const PreparedDrawSpatialMetadata& metadata,
    const Mat4& model,
    const Mat4& view,
    const Mat4& projection) {
    const Mat4 clip_from_object = projection * view * model;
    validate_spatial_matrix_finite(
        clip_from_object,
        "prepared draw clip transform");

    std::array<Vec4, 8> clip_corners{};
    const auto corners = spatial_bounds_corners(metadata);
    for (std::size_t i = 0U; i < corners.size(); ++i) {
        const Vec3 corner = corners[i];
        clip_corners[i] = clip_from_object * Vec4{corner.x, corner.y, corner.z, 1.0F};
        const Vec4& clip = clip_corners[i];
        if (!std::isfinite(clip.x)
            || !std::isfinite(clip.y)
            || !std::isfinite(clip.z)
            || !std::isfinite(clip.w)) {
            throw std::invalid_argument(
                "prepared draw visibility produced a non-finite clip-space bound corner");
        }
    }

    // The transformed AABB is convex. If every corner is strictly outside the
    // same homogeneous clip half-space then the complete box, and therefore
    // every referenced triangle inside it, is outside that plane. A relative
    // epsilon deliberately biases boundary/uncertain cases toward retention.
    for (std::size_t plane = 0U; plane < 6U; ++plane) {
        const bool all_outside = std::all_of(
            clip_corners.begin(),
            clip_corners.end(),
            [&](const Vec4& clip) {
                const double tolerance = static_cast<double>(kEpsilon)
                    * clip_plane_scale(clip, plane);
                return clip_plane_distance(clip, plane) < -tolerance;
            });
        if (all_outside) {
            return true;
        }
    }
    return false;
}

}  // namespace detail

[[nodiscard]] inline PreparedSpatialSubmission prepare_spatial_submission(
    PreparedModelSubmission prepared) {
    const ModelAsset& asset = prepared.asset();
    std::vector<PreparedDrawSpatialMetadata> draws;
    draws.reserve(asset.draws.size());
    for (const MaterialDraw& draw : asset.draws) {
        draws.push_back(detail::prepare_draw_spatial_metadata(asset, draw));
    }
    return PreparedSpatialSubmission{std::move(prepared), std::move(draws)};
}

[[nodiscard]] inline PreparedSpatialSubmission prepare_spatial_model(
    ModelAsset asset,
    ModelRenderOptions options = {}) {
    return prepare_spatial_submission(
        prepare_model_asset(std::move(asset), std::move(options)));
}

// Flattens every material draw from the supplied prepared models and returns a
// deterministic stable far-to-near plan. The key is the view-space Z of the
// prepared object-space AABB center. More-negative view Z sorts first. Equal
// depths preserve caller entry order and canonical material-draw order.
//
// The first spatial slice intentionally accepts affine model/view transforms
// only. This keeps the prepared AABB-center contract meaningful and avoids
// pretending a projective model transform has an affine object-space bound.
// Vertex programs are rejected because their post-program positions are not
// represented by the prepared canonical bounds.
[[nodiscard]] inline std::vector<PreparedDrawOrderEntry>
order_prepared_model_draws_back_to_front(
    std::span<const PreparedSpatialListEntry> entries,
    const Mat4& view) {
    detail::validate_spatial_affine_matrix(view, "prepared spatial view transform");

    std::vector<PreparedDrawOrderEntry> ordered;
    std::size_t total_draws = 0U;
    for (const PreparedSpatialListEntry& entry : entries) {
        if (entry.prepared == nullptr) {
            throw std::invalid_argument("prepared spatial list entry requires a prepared submission");
        }
        if (entry.prepared->prepared().options().vertex_program) {
            throw std::invalid_argument(
                "prepared draw spatial ordering does not support position-changing vertex programs");
        }
        detail::validate_spatial_affine_matrix(
            entry.model,
            "prepared spatial model transform");
        if (entry.prepared->draws().size() > std::numeric_limits<std::size_t>::max() - total_draws) {
            throw std::overflow_error("prepared spatial draw-plan size overflow");
        }
        total_draws += entry.prepared->draws().size();
    }
    ordered.reserve(total_draws);

    for (const PreparedSpatialListEntry& entry : entries) {
        const Mat4 view_model = view * entry.model;
        const auto metadata = entry.prepared->draws();
        for (std::size_t draw_index = 0U; draw_index < metadata.size(); ++draw_index) {
            const Vec3 center = metadata[draw_index].center;
            const Vec4 position = view_model * Vec4{center.x, center.y, center.z, 1.0F};
            if (!std::isfinite(position.x)
                || !std::isfinite(position.y)
                || !std::isfinite(position.z)
                || !std::isfinite(position.w)
                || std::fabs(position.w) <= kEpsilon) {
                throw std::invalid_argument(
                    "prepared draw spatial ordering produced a non-finite view-space center");
            }
            const double depth = static_cast<double>(position.z)
                / static_cast<double>(position.w);
            if (!std::isfinite(depth)) {
                throw std::invalid_argument(
                    "prepared draw spatial ordering produced a non-finite view depth");
            }
            ordered.push_back({entry.prepared, entry.model, draw_index, depth});
        }
    }

    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const PreparedDrawOrderEntry& left, const PreparedDrawOrderEntry& right) {
            return left.view_depth < right.view_depth;
        });
    return ordered;
}

// Returns the input draw plan with only draws that are provably outside one
// homogeneous clip half-space removed. Retained records preserve exact caller
// order. The test transforms all eight prepared AABB corners and rejects only
// when every corner is beyond the same clip plane; boundary and ambiguous cases
// remain visible. Model/view must stay affine under the prepared spatial
// contract while projection may be a finite perspective matrix.
[[nodiscard]] inline std::vector<PreparedDrawOrderEntry>
filter_prepared_draw_order_to_frustum(
    std::span<const PreparedDrawOrderEntry> entries,
    const Mat4& view,
    const Mat4& projection) {
    detail::validate_spatial_affine_matrix(
        view,
        "prepared draw visibility view transform");
    detail::validate_spatial_matrix_finite(
        projection,
        "prepared draw visibility projection transform");

    std::vector<PreparedDrawOrderEntry> visible;
    visible.reserve(entries.size());
    for (const PreparedDrawOrderEntry& entry : entries) {
        const PreparedDrawSpatialMetadata& metadata =
            detail::validate_prepared_draw_spatial_entry(entry);
        if (!detail::prepared_draw_fully_outside_clip_volume(
                metadata,
                entry.model,
                view,
                projection)) {
            visible.push_back(entry);
        }
    }
    return visible;
}

// Validates every selected prepared draw without submitting fragments. This is
// the transactional gate for higher-level schedulers that must prove a complete
// draw plan against the target before framebuffer or environment mutation.
void preflight_prepared_draw_order(
    const Framebuffer& framebuffer,
    std::span<const PreparedDrawOrderEntry> entries);

// Executes the supplied draw sequence exactly in caller order. The executor
// uses the canonical prepared-model material/raster mapping and selected
// draw_mesh_range path; it does not re-sort or create a parallel raster path.
// Complete plan preflight occurs before the first fragment submission.
void draw_prepared_draw_order(
    Framebuffer& framebuffer,
    std::span<const PreparedDrawOrderEntry> entries,
    const Mat4& view,
    const Mat4& projection);

}  // namespace tiny_renderer
