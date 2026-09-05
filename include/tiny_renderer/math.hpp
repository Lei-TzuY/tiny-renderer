#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace tiny_renderer {

constexpr float kEpsilon = 1.0e-6F;
constexpr float kPi = 3.14159265358979323846F;

struct Vec2 {
    float x{};
    float y{};

    constexpr Vec2 operator+(const Vec2& rhs) const { return {x + rhs.x, y + rhs.y}; }
    constexpr Vec2 operator-(const Vec2& rhs) const { return {x - rhs.x, y - rhs.y}; }
    constexpr Vec2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
};

struct Vec3 {
    float x{};
    float y{};
    float z{};

    constexpr Vec3 operator+(const Vec3& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    constexpr Vec3 operator-(const Vec3& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    constexpr Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    constexpr Vec3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }
};

struct Vec4 {
    float x{};
    float y{};
    float z{};
    float w{};

    constexpr Vec4 operator+(const Vec4& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w}; }
    constexpr Vec4 operator-(const Vec4& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w}; }
    constexpr Vec4 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar, w * scalar}; }
};

constexpr float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(const Vec3& v) {
    const float len = length(v);
    if (len <= kEpsilon) {
        throw std::invalid_argument("cannot normalize a zero-length vector");
    }
    return v / len;
}

inline float radians(float degrees) { return degrees * (kPi / 180.0F); }

class Mat4 {
public:
    constexpr Mat4() = default;

    explicit constexpr Mat4(const std::array<float, 16>& values) : values_(values) {}

    static constexpr Mat4 identity() {
        return Mat4({
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        });
    }

    static constexpr Mat4 translation(const Vec3& t) {
        Mat4 result = identity();
        result(0, 3) = t.x;
        result(1, 3) = t.y;
        result(2, 3) = t.z;
        return result;
    }

    static constexpr Mat4 scale(const Vec3& s) {
        Mat4 result{};
        result(0, 0) = s.x;
        result(1, 1) = s.y;
        result(2, 2) = s.z;
        result(3, 3) = 1.0F;
        return result;
    }

    static Mat4 rotation_y(float angle_radians) {
        const float c = std::cos(angle_radians);
        const float s = std::sin(angle_radians);
        Mat4 result = identity();
        result(0, 0) = c;
        result(0, 2) = s;
        result(2, 0) = -s;
        result(2, 2) = c;
        return result;
    }

    static Mat4 perspective(float fov_y_radians, float aspect, float near_plane, float far_plane) {
        if (!(fov_y_radians > 0.0F && fov_y_radians < kPi) || aspect <= 0.0F || near_plane <= 0.0F || far_plane <= near_plane) {
            throw std::invalid_argument("invalid perspective projection parameters");
        }

        const float f = 1.0F / std::tan(fov_y_radians * 0.5F);
        Mat4 result{};
        result(0, 0) = f / aspect;
        result(1, 1) = f;
        result(2, 2) = (far_plane + near_plane) / (near_plane - far_plane);
        result(2, 3) = (2.0F * far_plane * near_plane) / (near_plane - far_plane);
        result(3, 2) = -1.0F;
        return result;
    }

    static Mat4 look_at(const Vec3& eye, const Vec3& target, const Vec3& up) {
        const Vec3 forward = normalize(target - eye);
        const Vec3 right = normalize(cross(forward, up));
        const Vec3 corrected_up = cross(right, forward);

        Mat4 result = identity();
        result(0, 0) = right.x;
        result(0, 1) = right.y;
        result(0, 2) = right.z;
        result(0, 3) = -dot(right, eye);

        result(1, 0) = corrected_up.x;
        result(1, 1) = corrected_up.y;
        result(1, 2) = corrected_up.z;
        result(1, 3) = -dot(corrected_up, eye);

        result(2, 0) = -forward.x;
        result(2, 1) = -forward.y;
        result(2, 2) = -forward.z;
        result(2, 3) = dot(forward, eye);
        return result;
    }

    constexpr float& operator()(std::size_t row, std::size_t column) { return values_[row * 4U + column]; }
    constexpr float operator()(std::size_t row, std::size_t column) const { return values_[row * 4U + column]; }

private:
    std::array<float, 16> values_{};
};

constexpr Vec4 operator*(const Mat4& m, const Vec4& v) {
    return {
        m(0, 0) * v.x + m(0, 1) * v.y + m(0, 2) * v.z + m(0, 3) * v.w,
        m(1, 0) * v.x + m(1, 1) * v.y + m(1, 2) * v.z + m(1, 3) * v.w,
        m(2, 0) * v.x + m(2, 1) * v.y + m(2, 2) * v.z + m(2, 3) * v.w,
        m(3, 0) * v.x + m(3, 1) * v.y + m(3, 2) * v.z + m(3, 3) * v.w,
    };
}

constexpr Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float value = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                value += a(row, k) * b(k, column);
            }
            result(row, column) = value;
        }
    }
    return result;
}

inline bool nearly_equal(float a, float b, float epsilon = 1.0e-5F) {
    return std::fabs(a - b) <= epsilon;
}

}  // namespace tiny_renderer
