#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer::detail {

inline void validate_bounded_affine_matrix(
    const Mat4& matrix,
    std::string_view label) {
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            if (!std::isfinite(matrix(row, column))) {
                throw std::invalid_argument(
                    std::string(label) + " must contain only finite values");
            }
        }
    }
    if (std::fabs(matrix(3U, 0U)) > kEpsilon
        || std::fabs(matrix(3U, 1U)) > kEpsilon
        || std::fabs(matrix(3U, 2U)) > kEpsilon
        || std::fabs(matrix(3U, 3U) - 1.0F) > kEpsilon) {
        throw std::invalid_argument(
            std::string(label) + " must be affine");
    }
}

[[nodiscard]] inline Mat4 interpolate_bounded_affine(
    const Mat4& left,
    const Mat4& right,
    float t,
    std::string_view label) {
    if (!std::isfinite(t) || !(t > 0.0F && t < 1.0F)) {
        throw std::invalid_argument(
            std::string(label)
            + " interpolation parameter must be finite and strictly within (0, 1)");
    }

    Mat4 result = Mat4::identity();
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            result(row, column) =
                left(row, column)
                + (right(row, column) - left(row, column)) * t;
        }
    }
    result(3U, 0U) = 0.0F;
    result(3U, 1U) = 0.0F;
    result(3U, 2U) = 0.0F;
    result(3U, 3U) = 1.0F;
    validate_bounded_affine_matrix(result, label);
    return result;
}

template <
    typename Keyframe,
    typename Value,
    typename ValueAccessor,
    typename InterpolateValue>
[[nodiscard]] inline Value sample_bounded_keyframe_value(
    std::span<const Keyframe> keyframes,
    float sample_time,
    std::string_view timeline_label,
    ValueAccessor value_of,
    InterpolateValue interpolate_value) {
    if (!std::isfinite(sample_time)) {
        throw std::invalid_argument(
            std::string(timeline_label) + " sample time must be finite");
    }
    if (keyframes.empty()) {
        throw std::logic_error(
            std::string(timeline_label) + " has no keyframes");
    }
    if (sample_time < keyframes.front().time
        || sample_time > keyframes.back().time) {
        throw std::out_of_range(
            std::string(timeline_label)
            + " sample time is outside the keyframe domain");
    }

    if (sample_time == keyframes.front().time) {
        return value_of(keyframes.front());
    }

    std::size_t upper = 1U;
    while (upper < keyframes.size()
           && keyframes[upper].time < sample_time) {
        ++upper;
    }
    if (upper < keyframes.size()
        && sample_time == keyframes[upper].time) {
        return value_of(keyframes[upper]);
    }
    if (upper >= keyframes.size()) {
        throw std::logic_error(
            std::string(timeline_label)
            + " failed to bracket an in-domain sample");
    }

    const Keyframe& left = keyframes[upper - 1U];
    const Keyframe& right = keyframes[upper];
    const float denominator = right.time - left.time;
    const float t = (sample_time - left.time) / denominator;
    if (!std::isfinite(t) || !(t > 0.0F && t < 1.0F)) {
        throw std::logic_error(
            std::string(timeline_label)
            + " produced an invalid interpolation parameter");
    }
    return interpolate_value(
        value_of(left),
        value_of(right),
        t);
}

}  // namespace tiny_renderer::detail
