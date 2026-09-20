#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tiny_renderer/affine_timeline.hpp"
#include "tiny_renderer/quaternion.hpp"
#include "tiny_renderer/skinning.hpp"

namespace tiny_renderer {

inline constexpr std::size_t kMaxSkeletalTrsTrackKeys = 256U;
inline constexpr std::size_t kMaxSkeletalTrsClipKeys = 4096U;
inline constexpr std::size_t kMaxSkeletalTrsSamples = 256U;

struct SkeletalTrs {
    Vec3 translation{0.0F, 0.0F, 0.0F};
    Quaternion rotation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};
};

struct SkeletalVec3Keyframe {
    float time{};
    Vec3 value{};
};

struct SkeletalQuaternionKeyframe {
    float time{};
    Quaternion value{};
};

struct SkeletalCubicVec3Keyframe {
    float time{};
    Vec3 in_tangent{};
    Vec3 value{};
    Vec3 out_tangent{};
};

struct SkeletalCubicQuaternionKeyframe {
    float time{};
    Vec4 in_tangent{};
    Quaternion value{};
    Vec4 out_tangent{};
};

enum class SkeletalInterpolationMode {
    Linear,
    Step,
    CubicSpline,
};

struct SkeletalTranslationTrack {
    std::size_t joint{};
    std::vector<SkeletalVec3Keyframe> keyframes{};
    SkeletalInterpolationMode interpolation{
        SkeletalInterpolationMode::Linear};
    std::vector<SkeletalCubicVec3Keyframe> cubic_keyframes{};
};

struct SkeletalRotationTrack {
    std::size_t joint{};
    std::vector<SkeletalQuaternionKeyframe> keyframes{};
    SkeletalInterpolationMode interpolation{
        SkeletalInterpolationMode::Linear};
    std::vector<SkeletalCubicQuaternionKeyframe> cubic_keyframes{};
};

struct SkeletalScaleTrack {
    std::size_t joint{};
    std::vector<SkeletalVec3Keyframe> keyframes{};
    SkeletalInterpolationMode interpolation{
        SkeletalInterpolationMode::Linear};
    std::vector<SkeletalCubicVec3Keyframe> cubic_keyframes{};
};

namespace detail {

inline void validate_trs_vec3(
    const Vec3& value,
    std::string_view label) {
    if (!std::isfinite(value.x)
        || !std::isfinite(value.y)
        || !std::isfinite(value.z)) {
        throw std::invalid_argument(
            std::string(label) + " must contain only finite values");
    }
}

inline void validate_skeletal_trs(
    const SkeletalTrs& state,
    std::string_view label) {
    validate_trs_vec3(
        state.translation,
        std::string(label) + " translation");
    validate_unit_quaternion(
        state.rotation,
        std::string(label) + " rotation");
    validate_trs_vec3(
        state.scale,
        std::string(label) + " scale");
}

[[nodiscard]] inline Mat4 compose_skeletal_trs(
    const SkeletalTrs& state,
    std::string_view label) {
    validate_skeletal_trs(state, label);
    const Mat4 result =
        Mat4::translation(state.translation)
        * quaternion_rotation_matrix(state.rotation)
        * Mat4::scale(state.scale);
    validate_bounded_affine_matrix(result, label);
    return result;
}

[[nodiscard]] inline Vec3 interpolate_vec3(
    const Vec3& left,
    const Vec3& right,
    float t,
    std::string_view label) {
    if (!std::isfinite(t) || !(t > 0.0F && t < 1.0F)) {
        throw std::invalid_argument(
            std::string(label)
            + " interpolation parameter must be finite and strictly within (0, 1)");
    }
    const Vec3 result{
        left.x + (right.x - left.x) * t,
        left.y + (right.y - left.y) * t,
        left.z + (right.z - left.z) * t,
    };
    validate_trs_vec3(result, label);
    return result;
}

inline void validate_trs_interpolation(
    SkeletalInterpolationMode mode,
    std::string_view label) {
    switch (mode) {
        case SkeletalInterpolationMode::Linear:
        case SkeletalInterpolationMode::Step:
        case SkeletalInterpolationMode::CubicSpline:
            return;
    }
    throw std::invalid_argument(
        std::string(label)
        + " interpolation mode is unsupported");
}

template <typename Keyframe, typename Value, typename ValueAccessor, typename Interpolate>
[[nodiscard]] inline Value sample_held_track(
    std::span<const Keyframe> keyframes,
    float sample_time,
    SkeletalInterpolationMode mode,
    std::string_view label,
    ValueAccessor value_of,
    Interpolate interpolate) {
    if (keyframes.empty()) {
        throw std::logic_error(
            std::string(label) + " has no keyframes");
    }
    if (sample_time <= keyframes.front().time) {
        return value_of(keyframes.front());
    }
    if (sample_time >= keyframes.back().time) {
        return value_of(keyframes.back());
    }

    validate_trs_interpolation(mode, label);
    if (mode == SkeletalInterpolationMode::Linear) {
        return sample_bounded_keyframe_value<Keyframe, Value>(
            keyframes,
            sample_time,
            label,
            value_of,
            interpolate);
    }

    std::size_t upper = 1U;
    while (upper < keyframes.size()
           && keyframes[upper].time < sample_time) {
        ++upper;
    }
    if (upper >= keyframes.size()) {
        throw std::logic_error(
            std::string(label)
            + " failed to bracket an in-domain STEP sample");
    }
    if (sample_time == keyframes[upper].time) {
        return value_of(keyframes[upper]);
    }
    return value_of(keyframes[upper - 1U]);
}

inline void validate_trs_vec4(
    const Vec4& value,
    std::string_view label) {
    if (!std::isfinite(value.x)
        || !std::isfinite(value.y)
        || !std::isfinite(value.z)
        || !std::isfinite(value.w)) {
        throw std::invalid_argument(
            std::string(label) + " must contain only finite values");
    }
}

[[nodiscard]] inline float hermite_scalar(
    float p0,
    float m0,
    float p1,
    float m1,
    float t,
    float duration,
    std::string_view label) {
    if (!std::isfinite(t) || !(t > 0.0F && t < 1.0F)
        || !std::isfinite(duration) || !(duration > 0.0F)) {
        throw std::invalid_argument(
            std::string(label) + " received invalid Hermite segment state");
    }
    const double td = static_cast<double>(t);
    const double t2 = td * td;
    const double t3 = t2 * td;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + td;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    const double value =
        h00 * static_cast<double>(p0)
        + h10 * static_cast<double>(duration)
            * static_cast<double>(m0)
        + h01 * static_cast<double>(p1)
        + h11 * static_cast<double>(duration)
            * static_cast<double>(m1);
    const float result = static_cast<float>(value);
    if (!std::isfinite(result)) {
        throw std::invalid_argument(
            std::string(label) + " Hermite interpolation is non-finite");
    }
    return result;
}

[[nodiscard]] inline Vec3 hermite_vec3(
    const Vec3& p0,
    const Vec3& m0,
    const Vec3& p1,
    const Vec3& m1,
    float t,
    float duration,
    std::string_view label) {
    const Vec3 result{
        hermite_scalar(p0.x, m0.x, p1.x, m1.x, t, duration, label),
        hermite_scalar(p0.y, m0.y, p1.y, m1.y, t, duration, label),
        hermite_scalar(p0.z, m0.z, p1.z, m1.z, t, duration, label),
    };
    validate_trs_vec3(result, label);
    return result;
}

[[nodiscard]] inline Quaternion hermite_quaternion(
    const Quaternion& p0,
    const Vec4& m0,
    const Quaternion& p1,
    const Vec4& m1,
    float t,
    float duration,
    std::string_view label) {
    const Quaternion raw{
        hermite_scalar(p0.x, m0.x, p1.x, m1.x, t, duration, label),
        hermite_scalar(p0.y, m0.y, p1.y, m1.y, t, duration, label),
        hermite_scalar(p0.z, m0.z, p1.z, m1.z, t, duration, label),
        hermite_scalar(p0.w, m0.w, p1.w, m1.w, t, duration, label),
    };
    return normalized_quaternion(raw, label);
}

template <typename Keyframe, typename Value, typename ValueAccessor, typename Interpolate>
[[nodiscard]] inline Value sample_cubic_held_track(
    std::span<const Keyframe> keyframes,
    float sample_time,
    std::string_view label,
    ValueAccessor value_of,
    Interpolate interpolate) {
    if (keyframes.empty()) {
        throw std::logic_error(
            std::string(label) + " has no cubic keyframes");
    }
    if (sample_time <= keyframes.front().time) {
        return value_of(keyframes.front());
    }
    if (sample_time >= keyframes.back().time) {
        return value_of(keyframes.back());
    }

    std::size_t upper = 1U;
    while (upper < keyframes.size()
           && keyframes[upper].time < sample_time) {
        ++upper;
    }
    if (upper >= keyframes.size()) {
        throw std::logic_error(
            std::string(label)
            + " failed to bracket an in-domain cubic sample");
    }
    if (sample_time == keyframes[upper].time) {
        return value_of(keyframes[upper]);
    }

    const Keyframe& left = keyframes[upper - 1U];
    const Keyframe& right = keyframes[upper];
    const float duration = right.time - left.time;
    const float t = (sample_time - left.time) / duration;
    if (!std::isfinite(t) || !(t > 0.0F && t < 1.0F)) {
        throw std::logic_error(
            std::string(label)
            + " produced an invalid cubic interpolation parameter");
    }
    return interpolate(left, right, t, duration);
}

template <typename Track, typename ValidateValue, typename ValidateCubic>
inline void validate_trs_tracks(
    const std::vector<Track>& tracks,
    std::size_t joint_count,
    float start_time,
    float end_time,
    std::string_view label,
    ValidateValue validate_value,
    ValidateCubic validate_cubic,
    std::size_t& aggregate_keys) {
    std::vector<bool> seen(joint_count, false);
    for (const Track& track : tracks) {
        validate_trs_interpolation(
            track.interpolation,
            label);
        if (track.joint >= joint_count) {
            throw std::out_of_range(
                std::string(label) + " joint index exceeds rig joint count");
        }
        if (seen[track.joint]) {
            throw std::invalid_argument(
                std::string(label) + " contains duplicate joint ownership");
        }
        seen[track.joint] = true;

        const bool cubic =
            track.interpolation == SkeletalInterpolationMode::CubicSpline;
        if (cubic) {
            if (!track.keyframes.empty()
                || track.cubic_keyframes.size() < 2U
                || track.cubic_keyframes.size()
                    > kMaxSkeletalTrsTrackKeys) {
                throw std::invalid_argument(
                    std::string(label)
                    + " CUBICSPLINE requires two to 256 cubic keys and no ordinary keys");
            }
            if (track.cubic_keyframes.size()
                > kMaxSkeletalTrsClipKeys - aggregate_keys) {
                throw std::invalid_argument(
                    "skeletal TRS clip aggregate key count exceeds bounded limit");
            }
            aggregate_keys += track.cubic_keyframes.size();
            for (std::size_t index = 0U;
                 index < track.cubic_keyframes.size();
                 ++index) {
                const auto& keyframe = track.cubic_keyframes[index];
                if (!std::isfinite(keyframe.time)
                    || keyframe.time < start_time
                    || keyframe.time > end_time) {
                    throw std::invalid_argument(
                        std::string(label)
                        + " cubic key time must be finite and inside the clip domain");
                }
                if (index > 0U
                    && !(keyframe.time
                        > track.cubic_keyframes[index - 1U].time)) {
                    throw std::invalid_argument(
                        std::string(label)
                        + " cubic key times must be strictly increasing");
                }
                validate_cubic(keyframe);
            }
            continue;
        }

        if (!track.cubic_keyframes.empty()) {
            throw std::invalid_argument(
                std::string(label)
                + " LINEAR/STEP tracks cannot own cubic key data");
        }
        if (track.keyframes.empty()
            || track.keyframes.size() > kMaxSkeletalTrsTrackKeys) {
            throw std::invalid_argument(
                std::string(label)
                + " requires between one and 256 keyframes per track");
        }
        if (track.keyframes.size()
            > kMaxSkeletalTrsClipKeys - aggregate_keys) {
            throw std::invalid_argument(
                "skeletal TRS clip aggregate key count exceeds bounded limit");
        }
        aggregate_keys += track.keyframes.size();
        for (std::size_t index = 0U;
             index < track.keyframes.size();
             ++index) {
            const auto& keyframe = track.keyframes[index];
            if (!std::isfinite(keyframe.time)
                || keyframe.time < start_time
                || keyframe.time > end_time) {
                throw std::invalid_argument(
                    std::string(label)
                    + " key time must be finite and inside the clip domain");
            }
            if (index > 0U
                && !(keyframe.time
                     > track.keyframes[index - 1U].time)) {
                throw std::invalid_argument(
                    std::string(label)
                    + " key times must be strictly increasing");
            }
            validate_value(keyframe.value);
        }
    }
}

}  // namespace detail

struct SkeletalTrsSampledState {
    std::vector<SkeletalTrs> semantic_pose{};
    SkeletalPoseStatePtr resolved_pose{};
};

// Sparse semantic joint-property animation over one immutable M108 rig.
// Track domains may be narrower than the clip domain; outside a property's
// first/last key the nearest semantic endpoint is held. Each requested batch is
// fully materialized and resolved through M108 before any result is returned.
class SkeletalTrsClip {
public:
    SkeletalTrsClip(
        SkeletalRigPtr rig,
        float start_time,
        float end_time,
        std::vector<SkeletalTrs> default_pose,
        std::vector<SkeletalTranslationTrack> translation_tracks,
        std::vector<SkeletalRotationTrack> rotation_tracks,
        std::vector<SkeletalScaleTrack> scale_tracks)
        : SkeletalTrsClip(
              std::move(rig),
              start_time,
              end_time,
              std::move(default_pose),
              {},
              std::move(translation_tracks),
              std::move(rotation_tracks),
              std::move(scale_tracks)) {}

    SkeletalTrsClip(
        SkeletalRigPtr rig,
        float start_time,
        float end_time,
        std::vector<SkeletalTrs> default_pose,
        std::vector<Mat4> local_prefixes,
        std::vector<SkeletalTranslationTrack> translation_tracks,
        std::vector<SkeletalRotationTrack> rotation_tracks,
        std::vector<SkeletalScaleTrack> scale_tracks)
        : rig_(std::move(rig)),
          start_time_(start_time),
          end_time_(end_time),
          default_pose_(std::move(default_pose)),
          local_prefixes_(std::move(local_prefixes)),
          translation_tracks_(std::move(translation_tracks)),
          rotation_tracks_(std::move(rotation_tracks)),
          scale_tracks_(std::move(scale_tracks)) {
        if (!rig_) {
            throw std::invalid_argument(
                "skeletal TRS clip requires an immutable rig");
        }
        if (!std::isfinite(start_time_)
            || !std::isfinite(end_time_)
            || !(end_time_ > start_time_)) {
            throw std::invalid_argument(
                "skeletal TRS clip requires a finite increasing domain");
        }

        const std::size_t joint_count = rig_->parents().size();
        if (default_pose_.size() != joint_count) {
            throw std::invalid_argument(
                "skeletal TRS default pose count must match rig joint count");
        }
        if (local_prefixes_.empty()) {
            local_prefixes_.assign(
                joint_count,
                Mat4::identity());
        }
        if (local_prefixes_.size() != joint_count) {
            throw std::invalid_argument(
                "skeletal TRS local prefix count must match rig joint count");
        }
        for (const Mat4& prefix : local_prefixes_) {
            detail::validate_bounded_affine_matrix(
                prefix,
                "skeletal TRS immutable local prefix");
        }

        std::vector<Mat4> default_locals;
        default_locals.reserve(joint_count);
        for (std::size_t joint = 0U;
             joint < default_pose_.size();
             ++joint) {
            const Mat4 semantic =
                detail::compose_skeletal_trs(
                    default_pose_[joint],
                    "skeletal TRS default semantic local transform");
            const Mat4 local =
                local_prefixes_[joint] * semantic;
            detail::validate_bounded_affine_matrix(
                local,
                "skeletal TRS prefixed default local transform");
            default_locals.push_back(local);
        }
        (void)rig_->resolve_pose(default_locals);

        std::size_t aggregate_keys = 0U;
        detail::validate_trs_tracks(
            translation_tracks_,
            joint_count,
            start_time_,
            end_time_,
            "skeletal translation track",
            [](const Vec3& value) {
                detail::validate_trs_vec3(
                    value,
                    "skeletal translation key");
            },
            [](const SkeletalCubicVec3Keyframe& key) {
                detail::validate_trs_vec3(
                    key.in_tangent,
                    "skeletal translation cubic in tangent");
                detail::validate_trs_vec3(
                    key.value,
                    "skeletal translation cubic value");
                detail::validate_trs_vec3(
                    key.out_tangent,
                    "skeletal translation cubic out tangent");
            },
            aggregate_keys);
        detail::validate_trs_tracks(
            rotation_tracks_,
            joint_count,
            start_time_,
            end_time_,
            "skeletal rotation track",
            [](const Quaternion& value) {
                detail::validate_unit_quaternion(
                    value,
                    "skeletal rotation key");
            },
            [](const SkeletalCubicQuaternionKeyframe& key) {
                detail::validate_trs_vec4(
                    key.in_tangent,
                    "skeletal rotation cubic in tangent");
                detail::validate_unit_quaternion(
                    key.value,
                    "skeletal rotation cubic value");
                detail::validate_trs_vec4(
                    key.out_tangent,
                    "skeletal rotation cubic out tangent");
            },
            aggregate_keys);
        detail::validate_trs_tracks(
            scale_tracks_,
            joint_count,
            start_time_,
            end_time_,
            "skeletal scale track",
            [](const Vec3& value) {
                detail::validate_trs_vec3(
                    value,
                    "skeletal scale key");
            },
            [](const SkeletalCubicVec3Keyframe& key) {
                detail::validate_trs_vec3(
                    key.in_tangent,
                    "skeletal scale cubic in tangent");
                detail::validate_trs_vec3(
                    key.value,
                    "skeletal scale cubic value");
                detail::validate_trs_vec3(
                    key.out_tangent,
                    "skeletal scale cubic out tangent");
            },
            aggregate_keys);
    }

    [[nodiscard]] const SkeletalRig& rig() const noexcept {
        return *rig_;
    }

    [[nodiscard]] const SkeletalRigPtr& rig_ptr() const noexcept {
        return rig_;
    }

    [[nodiscard]] float start_time() const noexcept {
        return start_time_;
    }

    [[nodiscard]] float end_time() const noexcept {
        return end_time_;
    }

    [[nodiscard]] std::span<const SkeletalTrs>
    default_pose() const noexcept {
        return {
            default_pose_.data(),
            default_pose_.size(),
        };
    }

    [[nodiscard]] std::span<const Mat4>
    local_prefixes() const noexcept {
        return {
            local_prefixes_.data(),
            local_prefixes_.size(),
        };
    }

    [[nodiscard]] std::vector<SkeletalTrsSampledState> sample_states(
        std::span<const float> sample_times) const {
        validate_sample_times(sample_times);

        std::vector<SkeletalTrsSampledState> result;
        result.reserve(sample_times.size());
        for (const float sample_time : sample_times) {
            std::vector<SkeletalTrs> semantic_pose =
                sample_semantic_pose_at(sample_time);
            SkeletalPoseStatePtr pose =
                materialize_resolved_pose(semantic_pose);
            result.push_back({
                std::move(semantic_pose),
                std::move(pose),
            });
        }
        return result;
    }

    [[nodiscard]] std::vector<SkeletalPoseStatePtr> sample(
        std::span<const float> sample_times) const {
        std::vector<SkeletalTrsSampledState> states =
            sample_states(sample_times);
        std::vector<SkeletalPoseStatePtr> result;
        result.reserve(states.size());
        for (SkeletalTrsSampledState& state : states) {
            result.push_back(std::move(state.resolved_pose));
        }
        return result;
    }

private:
    void validate_sample_times(
        std::span<const float> sample_times) const {
        if (sample_times.size() > kMaxSkeletalTrsSamples) {
            throw std::invalid_argument(
                "skeletal TRS sample count exceeds bounded limit");
        }
        for (const float sample_time : sample_times) {
            if (!std::isfinite(sample_time)) {
                throw std::invalid_argument(
                    "skeletal TRS sample time must be finite");
            }
            if (sample_time < start_time_
                || sample_time > end_time_) {
                throw std::out_of_range(
                    "skeletal TRS sample time is outside clip domain");
            }
        }
    }

    [[nodiscard]] std::vector<SkeletalTrs> sample_semantic_pose_at(
        float sample_time) const {
        std::vector<SkeletalTrs> semantic_pose =
            default_pose_;

        for (const SkeletalTranslationTrack& track
             : translation_tracks_) {
            semantic_pose[track.joint].translation =
                track.interpolation == SkeletalInterpolationMode::CubicSpline
                ? detail::sample_cubic_held_track<
                    SkeletalCubicVec3Keyframe,
                    Vec3>(
                    track.cubic_keyframes,
                    sample_time,
                    "skeletal translation cubic track",
                    [](const SkeletalCubicVec3Keyframe& keyframe) {
                        return keyframe.value;
                    },
                    [](const SkeletalCubicVec3Keyframe& left,
                       const SkeletalCubicVec3Keyframe& right,
                       float t,
                       float duration) {
                        return detail::hermite_vec3(
                            left.value,
                            left.out_tangent,
                            right.value,
                            right.in_tangent,
                            t,
                            duration,
                            "skeletal translation cubic");
                    })
                : detail::sample_held_track<
                    SkeletalVec3Keyframe,
                    Vec3>(
                    track.keyframes,
                    sample_time,
                    track.interpolation,
                    "skeletal translation track",
                    [](const SkeletalVec3Keyframe& keyframe) {
                        return keyframe.value;
                    },
                    [](const Vec3& left,
                       const Vec3& right,
                       float t) {
                        return detail::interpolate_vec3(
                            left,
                            right,
                            t,
                            "skeletal translation");
                    });
        }
        for (const SkeletalRotationTrack& track
             : rotation_tracks_) {
            semantic_pose[track.joint].rotation =
                track.interpolation == SkeletalInterpolationMode::CubicSpline
                ? detail::sample_cubic_held_track<
                    SkeletalCubicQuaternionKeyframe,
                    Quaternion>(
                    track.cubic_keyframes,
                    sample_time,
                    "skeletal rotation cubic track",
                    [](const SkeletalCubicQuaternionKeyframe& keyframe) {
                        return keyframe.value;
                    },
                    [](const SkeletalCubicQuaternionKeyframe& left,
                       const SkeletalCubicQuaternionKeyframe& right,
                       float t,
                       float duration) {
                        return detail::hermite_quaternion(
                            left.value,
                            left.out_tangent,
                            right.value,
                            right.in_tangent,
                            t,
                            duration,
                            "skeletal rotation cubic");
                    })
                : detail::sample_held_track<
                    SkeletalQuaternionKeyframe,
                    Quaternion>(
                    track.keyframes,
                    sample_time,
                    track.interpolation,
                    "skeletal rotation track",
                    [](const SkeletalQuaternionKeyframe& keyframe) {
                        return keyframe.value;
                    },
                    [](const Quaternion& left,
                       const Quaternion& right,
                       float t) {
                        return slerp_shortest(left, right, t);
                    });
        }
        for (const SkeletalScaleTrack& track
             : scale_tracks_) {
            semantic_pose[track.joint].scale =
                track.interpolation == SkeletalInterpolationMode::CubicSpline
                ? detail::sample_cubic_held_track<
                    SkeletalCubicVec3Keyframe,
                    Vec3>(
                    track.cubic_keyframes,
                    sample_time,
                    "skeletal scale cubic track",
                    [](const SkeletalCubicVec3Keyframe& keyframe) {
                        return keyframe.value;
                    },
                    [](const SkeletalCubicVec3Keyframe& left,
                       const SkeletalCubicVec3Keyframe& right,
                       float t,
                       float duration) {
                        return detail::hermite_vec3(
                            left.value,
                            left.out_tangent,
                            right.value,
                            right.in_tangent,
                            t,
                            duration,
                            "skeletal scale cubic");
                    })
                : detail::sample_held_track<
                    SkeletalVec3Keyframe,
                    Vec3>(
                    track.keyframes,
                    sample_time,
                    track.interpolation,
                    "skeletal scale track",
                    [](const SkeletalVec3Keyframe& keyframe) {
                        return keyframe.value;
                    },
                    [](const Vec3& left,
                       const Vec3& right,
                       float t) {
                        return detail::interpolate_vec3(
                            left,
                            right,
                            t,
                            "skeletal scale");
                    });
        }
        return semantic_pose;
    }

    [[nodiscard]] SkeletalPoseStatePtr materialize_resolved_pose(
        std::span<const SkeletalTrs> semantic_pose) const {
        if (semantic_pose.size() != local_prefixes_.size()) {
            throw std::logic_error(
                "skeletal TRS semantic pose ownership changed after validation");
        }
        std::vector<Mat4> locals;
        locals.reserve(semantic_pose.size());
        for (std::size_t joint = 0U;
             joint < semantic_pose.size();
             ++joint) {
            const Mat4 semantic =
                detail::compose_skeletal_trs(
                    semantic_pose[joint],
                    "skeletal TRS sampled semantic local transform");
            const Mat4 local =
                local_prefixes_[joint] * semantic;
            detail::validate_bounded_affine_matrix(
                local,
                "skeletal TRS prefixed sampled local transform");
            locals.push_back(local);
        }

        auto pose =
            std::make_shared<const SkeletalPoseState>(
                rig_,
                std::move(locals));
        (void)pose->resolve();
        return pose;
    }

    SkeletalRigPtr rig_{};
    float start_time_{};
    float end_time_{};
    std::vector<SkeletalTrs> default_pose_{};
    std::vector<Mat4> local_prefixes_{};
    std::vector<SkeletalTranslationTrack> translation_tracks_{};
    std::vector<SkeletalRotationTrack> rotation_tracks_{};

    std::vector<SkeletalScaleTrack> scale_tracks_{};
};

struct SkeletalTrsBlendRequest {
    float left_time{};
    float right_time{};
    float weight{};
};

namespace detail {

[[nodiscard]] inline bool exact_affine_equal(
    const Mat4& left,
    const Mat4& right) noexcept {
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            if (left(row, column) != right(row, column)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] inline bool exact_skin_binding_equal(
    const VertexSkinBinding& left,
    const VertexSkinBinding& right) noexcept {
    const auto left_influences = left.influences();
    const auto right_influences = right.influences();
    if (left_influences.size() != right_influences.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < left_influences.size();
         ++index) {
        if (left_influences[index].joint
                != right_influences[index].joint
            || left_influences[index].weight
                != right_influences[index].weight) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool exactly_compatible_rigs(
    const SkeletalRig& left,
    const SkeletalRig& right) noexcept {
    const auto left_parents = left.parents();
    const auto right_parents = right.parents();
    if (left_parents.size() != right_parents.size()) {
        return false;
    }
    for (std::size_t joint = 0U;
         joint < left_parents.size();
         ++joint) {
        if (left_parents[joint] != right_parents[joint]) {
            return false;
        }
    }

    const auto left_inverse = left.inverse_bind_matrices();
    const auto right_inverse = right.inverse_bind_matrices();
    if (left_inverse.size() != right_inverse.size()) {
        return false;
    }
    for (std::size_t joint = 0U;
         joint < left_inverse.size();
         ++joint) {
        if (!exact_affine_equal(
                left_inverse[joint],
                right_inverse[joint])) {
            return false;
        }
    }

    const auto left_bindings = left.vertex_bindings();
    const auto right_bindings = right.vertex_bindings();
    if (left_bindings.size() != right_bindings.size()) {
        return false;
    }
    for (std::size_t vertex = 0U;
         vertex < left_bindings.size();
         ++vertex) {
        if (!exact_skin_binding_equal(
                left_bindings[vertex],
                right_bindings[vertex])) {
            return false;
        }
    }
    return true;
}

inline void validate_semantic_blend_compatibility(
    const SkeletalTrsClip& left,
    const SkeletalTrsClip& right) {
    if (!exactly_compatible_rigs(left.rig(), right.rig())) {
        throw std::invalid_argument(
            "skeletal TRS blend requires exactly compatible rig ownership");
    }
    const auto left_prefixes = left.local_prefixes();
    const auto right_prefixes = right.local_prefixes();
    if (left_prefixes.size() != right_prefixes.size()) {
        throw std::invalid_argument(
            "skeletal TRS blend requires identical local-prefix ownership");
    }
    for (std::size_t joint = 0U;
         joint < left_prefixes.size();
         ++joint) {
        if (!exact_affine_equal(
                left_prefixes[joint],
                right_prefixes[joint])) {
            throw std::invalid_argument(
                "skeletal TRS blend requires identical immutable local prefixes");
        }
    }
}

[[nodiscard]] inline SkeletalPoseStatePtr materialize_blended_semantic_pose(
    const SkeletalTrsClip& clip,
    std::span<const SkeletalTrs> semantic_pose) {
    const auto prefixes = clip.local_prefixes();
    if (semantic_pose.size() != prefixes.size()) {
        throw std::logic_error(
            "skeletal TRS blended semantic ownership changed after validation");
    }

    std::vector<Mat4> locals;
    locals.reserve(semantic_pose.size());
    for (std::size_t joint = 0U;
         joint < semantic_pose.size();
         ++joint) {
        const Mat4 semantic =
            compose_skeletal_trs(
                semantic_pose[joint],
                "skeletal TRS blended semantic local transform");
        const Mat4 local = prefixes[joint] * semantic;
        validate_bounded_affine_matrix(
            local,
            "skeletal TRS prefixed blended local transform");
        locals.push_back(local);
    }
    auto pose = std::make_shared<const SkeletalPoseState>(
        clip.rig_ptr(),
        std::move(locals));
    (void)pose->resolve();
    return pose;
}

}  // namespace detail

[[nodiscard]] inline std::vector<SkeletalPoseStatePtr>
blend_skeletal_trs_clips(
    const SkeletalTrsClip& left,
    const SkeletalTrsClip& right,
    std::span<const SkeletalTrsBlendRequest> requests) {
    if (requests.size() > kMaxSkeletalTrsSamples) {
        throw std::invalid_argument(
            "skeletal TRS blend request count exceeds bounded limit");
    }
    detail::validate_semantic_blend_compatibility(left, right);

    std::vector<float> left_times;
    std::vector<float> right_times;
    left_times.reserve(requests.size());
    right_times.reserve(requests.size());
    for (const SkeletalTrsBlendRequest& request : requests) {
        if (!std::isfinite(request.weight)
            || request.weight < 0.0F
            || request.weight > 1.0F) {
            throw std::invalid_argument(
                "skeletal TRS blend weight must be finite within [0, 1]");
        }
        left_times.push_back(request.left_time);
        right_times.push_back(request.right_time);
    }

    // Both complete source batches are sampled and M108-resolved before any
    // output blend exists, including exact endpoint-weight requests.
    const std::vector<SkeletalTrsSampledState> left_states =
        left.sample_states(left_times);
    const std::vector<SkeletalTrsSampledState> right_states =
        right.sample_states(right_times);

    std::vector<SkeletalPoseStatePtr> result;
    result.reserve(requests.size());
    for (std::size_t index = 0U;
         index < requests.size();
         ++index) {
        const float weight = requests[index].weight;
        if (weight == 0.0F) {
            result.push_back(left_states[index].resolved_pose);
            continue;
        }
        if (weight == 1.0F) {
            result.push_back(right_states[index].resolved_pose);
            continue;
        }

        const auto& left_semantic =
            left_states[index].semantic_pose;
        const auto& right_semantic =
            right_states[index].semantic_pose;
        if (left_semantic.size() != right_semantic.size()) {
            throw std::logic_error(
                "skeletal TRS blend source ownership changed after compatibility validation");
        }

        std::vector<SkeletalTrs> blended;
        blended.reserve(left_semantic.size());
        for (std::size_t joint = 0U;
             joint < left_semantic.size();
             ++joint) {
            SkeletalTrs state;
            state.translation = detail::interpolate_vec3(
                left_semantic[joint].translation,
                right_semantic[joint].translation,
                weight,
                "skeletal TRS blend translation");
            state.scale = detail::interpolate_vec3(
                left_semantic[joint].scale,
                right_semantic[joint].scale,
                weight,
                "skeletal TRS blend scale");
            state.rotation = slerp_shortest(
                left_semantic[joint].rotation,
                right_semantic[joint].rotation,
                weight);
            blended.push_back(state);
        }
        result.push_back(
            detail::materialize_blended_semantic_pose(
                left,
                blended));
    }
    return result;
}

}  // namespace tiny_renderer
