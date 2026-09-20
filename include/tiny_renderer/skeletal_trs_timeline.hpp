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

struct SkeletalTranslationTrack {
    std::size_t joint{};
    std::vector<SkeletalVec3Keyframe> keyframes{};
};

struct SkeletalRotationTrack {
    std::size_t joint{};
    std::vector<SkeletalQuaternionKeyframe> keyframes{};
};

struct SkeletalScaleTrack {
    std::size_t joint{};
    std::vector<SkeletalVec3Keyframe> keyframes{};
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

template <typename Keyframe, typename Value, typename ValueAccessor, typename Interpolate>
[[nodiscard]] inline Value sample_held_track(
    std::span<const Keyframe> keyframes,
    float sample_time,
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
    return sample_bounded_keyframe_value<Keyframe, Value>(
        keyframes,
        sample_time,
        label,
        value_of,
        interpolate);
}

template <typename Track, typename ValidateValue>
inline void validate_trs_tracks(
    const std::vector<Track>& tracks,
    std::size_t joint_count,
    float start_time,
    float end_time,
    std::string_view label,
    ValidateValue validate_value,
    std::size_t& aggregate_keys) {
    std::vector<bool> seen(joint_count, false);
    for (const Track& track : tracks) {
        if (track.joint >= joint_count) {
            throw std::out_of_range(
                std::string(label) + " joint index exceeds rig joint count");
        }
        if (seen[track.joint]) {
            throw std::invalid_argument(
                std::string(label) + " contains duplicate joint ownership");
        }
        seen[track.joint] = true;
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
        : rig_(std::move(rig)),
          start_time_(start_time),
          end_time_(end_time),
          default_pose_(std::move(default_pose)),
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

        std::vector<Mat4> default_locals;
        default_locals.reserve(joint_count);
        for (const SkeletalTrs& state : default_pose_) {
            default_locals.push_back(
                detail::compose_skeletal_trs(
                    state,
                    "skeletal TRS default local transform"));
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
            aggregate_keys);
    }

    [[nodiscard]] const SkeletalRig& rig() const noexcept {
        return *rig_;
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

    [[nodiscard]] std::vector<SkeletalPoseStatePtr> sample(
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

        std::vector<SkeletalPoseStatePtr> result;
        result.reserve(sample_times.size());

        for (const float sample_time : sample_times) {
            std::vector<SkeletalTrs> semantic_pose =
                default_pose_;

            for (const SkeletalTranslationTrack& track
                 : translation_tracks_) {
                semantic_pose[track.joint].translation =
                    detail::sample_held_track<
                        SkeletalVec3Keyframe,
                        Vec3>(
                        track.keyframes,
                        sample_time,
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
                    detail::sample_held_track<
                        SkeletalQuaternionKeyframe,
                        Quaternion>(
                        track.keyframes,
                        sample_time,
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
                    detail::sample_held_track<
                        SkeletalVec3Keyframe,
                        Vec3>(
                        track.keyframes,
                        sample_time,
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

            std::vector<Mat4> locals;
            locals.reserve(semantic_pose.size());
            for (const SkeletalTrs& state : semantic_pose) {
                locals.push_back(
                    detail::compose_skeletal_trs(
                        state,
                        "skeletal TRS sampled local transform"));
            }

            auto pose =
                std::make_shared<const SkeletalPoseState>(
                    rig_,
                    std::move(locals));
            // Transactional batch semantics: every sampled pose is fully
            // resolved here; result remains local until the entire loop wins.
            (void)pose->resolve();
            result.push_back(std::move(pose));
        }
        return result;
    }

private:
    SkeletalRigPtr rig_{};
    float start_time_{};
    float end_time_{};
    std::vector<SkeletalTrs> default_pose_{};
    std::vector<SkeletalTranslationTrack> translation_tracks_{};
    std::vector<SkeletalRotationTrack> rotation_tracks_{};
    std::vector<SkeletalScaleTrack> scale_tracks_{};
};

}  // namespace tiny_renderer
