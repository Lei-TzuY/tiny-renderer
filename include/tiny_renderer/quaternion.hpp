#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

struct Quaternion {
    float x{};
    float y{};
    float z{};
    float w{1.0F};
};

[[nodiscard]] constexpr float dot(
    const Quaternion& left,
    const Quaternion& right) noexcept {
    return left.x * right.x
        + left.y * right.y
        + left.z * right.z
        + left.w * right.w;
}

namespace detail {

inline void validate_unit_quaternion(
    const Quaternion& value,
    std::string_view label) {
    if (!std::isfinite(value.x)
        || !std::isfinite(value.y)
        || !std::isfinite(value.z)
        || !std::isfinite(value.w)) {
        throw std::invalid_argument(
            std::string(label)
            + " must contain only finite values");
    }
    const double length_squared =
        static_cast<double>(value.x) * value.x
        + static_cast<double>(value.y) * value.y
        + static_cast<double>(value.z) * value.z
        + static_cast<double>(value.w) * value.w;
    if (!std::isfinite(length_squared)
        || std::fabs(length_squared - 1.0) > 1.0e-4) {
        throw std::invalid_argument(
            std::string(label)
            + " must be a unit quaternion");
    }
}

[[nodiscard]] inline Quaternion normalized_quaternion(
    const Quaternion& value,
    std::string_view label) {
    const double length_squared =
        static_cast<double>(value.x) * value.x
        + static_cast<double>(value.y) * value.y
        + static_cast<double>(value.z) * value.z
        + static_cast<double>(value.w) * value.w;
    if (!std::isfinite(length_squared)
        || length_squared <= 1.0e-16) {
        throw std::invalid_argument(
            std::string(label)
            + " cannot normalize a zero or non-finite quaternion");
    }
    const double inverse_length =
        1.0 / std::sqrt(length_squared);
    Quaternion result{
        static_cast<float>(
            static_cast<double>(value.x) * inverse_length),
        static_cast<float>(
            static_cast<double>(value.y) * inverse_length),
        static_cast<float>(
            static_cast<double>(value.z) * inverse_length),
        static_cast<float>(
            static_cast<double>(value.w) * inverse_length),
    };
    validate_unit_quaternion(result, label);
    return result;
}

}  // namespace detail

[[nodiscard]] inline Mat4 quaternion_rotation_matrix(
    const Quaternion& quaternion) {
    detail::validate_unit_quaternion(
        quaternion,
        "quaternion rotation");
    const float x = quaternion.x;
    const float y = quaternion.y;
    const float z = quaternion.z;
    const float w = quaternion.w;

    Mat4 result = Mat4::identity();
    result(0U, 0U) = 1.0F - 2.0F * (y * y + z * z);
    result(0U, 1U) = 2.0F * (x * y - z * w);
    result(0U, 2U) = 2.0F * (x * z + y * w);
    result(1U, 0U) = 2.0F * (x * y + z * w);
    result(1U, 1U) = 1.0F - 2.0F * (x * x + z * z);
    result(1U, 2U) = 2.0F * (y * z - x * w);
    result(2U, 0U) = 2.0F * (x * z - y * w);
    result(2U, 1U) = 2.0F * (y * z + x * w);
    result(2U, 2U) = 1.0F - 2.0F * (x * x + y * y);
    return result;
}

[[nodiscard]] inline Quaternion slerp_shortest(
    const Quaternion& left,
    const Quaternion& right,
    float t) {
    detail::validate_unit_quaternion(
        left,
        "left slerp quaternion");
    detail::validate_unit_quaternion(
        right,
        "right slerp quaternion");
    if (!std::isfinite(t) || !(t > 0.0F && t < 1.0F)) {
        throw std::invalid_argument(
            "slerp parameter must be finite and strictly within (0, 1)");
    }

    Quaternion adjusted = right;
    double cosine =
        static_cast<double>(dot(left, right));
    if (cosine < 0.0) {
        adjusted = {
            -right.x,
            -right.y,
            -right.z,
            -right.w,
        };
        cosine = -cosine;
    }
    cosine = std::clamp(cosine, 0.0, 1.0);

    if (cosine > 0.9995) {
        const double td = static_cast<double>(t);
        Quaternion blended{
            static_cast<float>(
                (1.0 - td) * static_cast<double>(left.x)
                + td * static_cast<double>(adjusted.x)),
            static_cast<float>(
                (1.0 - td) * static_cast<double>(left.y)
                + td * static_cast<double>(adjusted.y)),
            static_cast<float>(
                (1.0 - td) * static_cast<double>(left.z)
                + td * static_cast<double>(adjusted.z)),
            static_cast<float>(
                (1.0 - td) * static_cast<double>(left.w)
                + td * static_cast<double>(adjusted.w)),
        };
        return detail::normalized_quaternion(
            blended,
            "slerp result");
    }

    const double theta = std::acos(cosine);
    const double sine = std::sin(theta);
    if (!std::isfinite(theta)
        || !std::isfinite(sine)
        || sine <= 1.0e-12) {
        throw std::invalid_argument(
            "slerp produced an unstable spherical interpolation");
    }
    const double td = static_cast<double>(t);
    const double left_weight =
        std::sin((1.0 - td) * theta) / sine;
    const double right_weight =
        std::sin(td * theta) / sine;
    Quaternion result{
        static_cast<float>(
            left_weight * static_cast<double>(left.x)
            + right_weight * static_cast<double>(adjusted.x)),
        static_cast<float>(
            left_weight * static_cast<double>(left.y)
            + right_weight * static_cast<double>(adjusted.y)),
        static_cast<float>(
            left_weight * static_cast<double>(left.z)
            + right_weight * static_cast<double>(adjusted.z)),
        static_cast<float>(
            left_weight * static_cast<double>(left.w)
            + right_weight * static_cast<double>(adjusted.w)),
    };
    return detail::normalized_quaternion(
        result,
        "slerp result");
}

}  // namespace tiny_renderer
