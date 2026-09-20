#pragma once

#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/affine_timeline.hpp"
#include "tiny_renderer/skinning.hpp"

namespace tiny_renderer {

inline constexpr std::size_t kMaxSkeletalTimelineKeyframes = 256U;
inline constexpr std::size_t kMaxSkeletalTimelineSamples = 256U;

struct SkeletalPoseTimelineKeyframe {
    float time{};
    std::vector<Mat4> local_transforms{};
};

// Dense bounded joint-local timeline over one immutable M108 rig. Sampling
// always produces complete local poses and validates every requested pose
// through M108 before returning any result to the caller.
class SkeletalPoseTimeline {
public:
    SkeletalPoseTimeline(
        SkeletalRigPtr rig,
        std::vector<SkeletalPoseTimelineKeyframe> keyframes)
        : rig_(std::move(rig)),
          keyframes_(std::move(keyframes)) {
        if (!rig_) {
            throw std::invalid_argument(
                "skeletal timeline requires an immutable rig");
        }
        if (keyframes_.size() < 2U
            || keyframes_.size() > kMaxSkeletalTimelineKeyframes) {
            throw std::invalid_argument(
                "skeletal timeline requires between two and 256 keyframes");
        }

        const std::size_t joint_count = rig_->parents().size();
        for (std::size_t index = 0U; index < keyframes_.size(); ++index) {
            const SkeletalPoseTimelineKeyframe& keyframe =
                keyframes_[index];
            if (!std::isfinite(keyframe.time)) {
                throw std::invalid_argument(
                    "skeletal timeline keyframe time must be finite");
            }
            if (index > 0U
                && !(keyframe.time > keyframes_[index - 1U].time)) {
                throw std::invalid_argument(
                    "skeletal timeline keyframe times must be strictly increasing");
            }
            if (keyframe.local_transforms.size() != joint_count) {
                throw std::invalid_argument(
                    "skeletal timeline keyframe local transform count must match rig joint count");
            }
            for (const Mat4& local : keyframe.local_transforms) {
                detail::validate_bounded_affine_matrix(
                    local,
                    "skeletal timeline keyframe local transform");
            }
            // Endpoints themselves are executable M108 poses.
            (void)rig_->resolve_pose(keyframe.local_transforms);
        }
    }

    [[nodiscard]] const SkeletalRig& rig() const noexcept {
        return *rig_;
    }

    [[nodiscard]] std::span<const SkeletalPoseTimelineKeyframe>
    keyframes() const noexcept {
        return {keyframes_.data(), keyframes_.size()};
    }

    [[nodiscard]] std::vector<SkeletalPoseStatePtr> sample(
        std::span<const float> sample_times) const {
        if (sample_times.size() > kMaxSkeletalTimelineSamples) {
            throw std::invalid_argument(
                "skeletal timeline sample count exceeds bounded limit");
        }

        std::vector<SkeletalPoseStatePtr> sampled;
        sampled.reserve(sample_times.size());
        for (const float sample_time : sample_times) {
            std::vector<Mat4> locals =
                detail::sample_bounded_keyframe_value<
                    SkeletalPoseTimelineKeyframe,
                    std::vector<Mat4>>(
                    keyframes_,
                    sample_time,
                    "skeletal timeline",
                    [](const SkeletalPoseTimelineKeyframe& keyframe)
                        -> const std::vector<Mat4>& {
                        return keyframe.local_transforms;
                    },
                    [](const std::vector<Mat4>& left,
                       const std::vector<Mat4>& right,
                       float t) {
                        if (left.size() != right.size()) {
                            throw std::logic_error(
                                "skeletal timeline keyframe joint ownership changed after validation");
                        }
                        std::vector<Mat4> result;
                        result.reserve(left.size());
                        for (std::size_t joint = 0U;
                             joint < left.size();
                             ++joint) {
                            result.push_back(
                                detail::interpolate_bounded_affine(
                                    left[joint],
                                    right[joint],
                                    t,
                                    "skeletal timeline interpolated local transform"));
                        }
                        return result;
                    });

            auto pose = std::make_shared<const SkeletalPoseState>(
                rig_,
                std::move(locals));
            // Validate complete hierarchy + world*inverse-bind composition for
            // every requested sample before the batch is returned.
            (void)pose->resolve();
            sampled.push_back(std::move(pose));
        }
        return sampled;
    }

private:
    SkeletalRigPtr rig_{};
    std::vector<SkeletalPoseTimelineKeyframe> keyframes_{};
};

}  // namespace tiny_renderer
