#pragma once

#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/morph.hpp"
#include "tiny_renderer/semantic_interpolation.hpp"

namespace tiny_renderer {

inline constexpr std::size_t kMaxMorphWeightClipKeys = 256U;
inline constexpr std::size_t kMaxMorphWeightClipSamples = 256U;

struct MorphWeightKeyframe {
    float time{};
    std::vector<float> weights{};
};

// Dense complete-weight-vector animation over one immutable M117 target set.
// Exact key requests copy semantic coefficients bit-for-bit. M119 intentionally
// supports LINEAR and STEP only; CUBICSPLINE remains a later explicit contract.
class MorphWeightClip {
public:
    MorphWeightClip(
        MorphTargetSetPtr targets,
        std::vector<float> default_weights,
        std::vector<MorphWeightKeyframe> keyframes,
        SemanticInterpolationMode interpolation =
            SemanticInterpolationMode::Linear)
        : targets_(std::move(targets)),
          default_state_(std::make_shared<const MorphState>(
              targets_,
              std::move(default_weights))),
          keyframes_(std::move(keyframes)),
          interpolation_(interpolation) {
        if (!targets_) {
            throw std::invalid_argument(
                "morph-weight clip requires an immutable target set");
        }

        switch (interpolation_) {
            case SemanticInterpolationMode::Linear:
            case SemanticInterpolationMode::Step:
                break;
            case SemanticInterpolationMode::CubicSpline:
                throw std::invalid_argument(
                    "morph-weight clip CUBICSPLINE is not supported by this milestone");
            default:
                throw std::invalid_argument(
                    "morph-weight clip interpolation mode is unsupported");
        }

        if (keyframes_.size() < 2U
            || keyframes_.size() > kMaxMorphWeightClipKeys) {
            throw std::invalid_argument(
                "morph-weight clip requires between two and 256 keyframes");
        }

        const std::size_t target_count =
            targets_->targets().size();
        for (std::size_t index = 0U;
             index < keyframes_.size();
             ++index) {
            const MorphWeightKeyframe& keyframe =
                keyframes_[index];
            if (!std::isfinite(keyframe.time)) {
                throw std::invalid_argument(
                    "morph-weight key time must be finite");
            }
            if (index > 0U
                && !(keyframe.time
                    > keyframes_[index - 1U].time)) {
                throw std::invalid_argument(
                    "morph-weight key times must be strictly increasing");
            }
            if (keyframe.weights.size() != target_count) {
                throw std::invalid_argument(
                    "morph-weight key vector must match target count");
            }
            for (const float weight : keyframe.weights) {
                if (!std::isfinite(weight)) {
                    throw std::invalid_argument(
                        "morph-weight key coefficients must be finite");
                }
            }
        }
    }

    [[nodiscard]] const MorphTargetSet&
    target_set() const noexcept {
        return *targets_;
    }

    [[nodiscard]] std::span<const float>
    default_weights() const noexcept {
        return default_state_->weights();
    }

    [[nodiscard]] std::span<const MorphWeightKeyframe>
    keyframes() const noexcept {
        return {keyframes_.data(), keyframes_.size()};
    }

    [[nodiscard]] SemanticInterpolationMode
    interpolation() const noexcept {
        return interpolation_;
    }

    [[nodiscard]] float start_time() const noexcept {
        return keyframes_.front().time;
    }

    [[nodiscard]] float end_time() const noexcept {
        return keyframes_.back().time;
    }

    [[nodiscard]] std::vector<MorphStatePtr> sample(
        std::span<const float> sample_times) const {
        if (sample_times.size() > kMaxMorphWeightClipSamples) {
            throw std::invalid_argument(
                "morph-weight sample count exceeds bounded limit");
        }

        // Validate the complete request first so a later invalid time cannot
        // allow an earlier sampled state to escape to a caller for rendering.
        for (const float sample_time : sample_times) {
            if (!std::isfinite(sample_time)) {
                throw std::invalid_argument(
                    "morph-weight sample time must be finite");
            }
            if (sample_time < start_time()
                || sample_time > end_time()) {
                throw std::out_of_range(
                    "morph-weight sample time is outside the clip domain");
            }
        }

        std::vector<MorphStatePtr> result;
        result.reserve(sample_times.size());
        for (const float sample_time : sample_times) {
            result.push_back(
                std::make_shared<const MorphState>(
                    targets_,
                    sample_weights(sample_time)));
        }
        return result;
    }

private:
    [[nodiscard]] std::vector<float> sample_weights(
        float sample_time) const {
        if (sample_time == keyframes_.front().time) {
            return keyframes_.front().weights;
        }

        std::size_t upper = 1U;
        while (upper < keyframes_.size()
               && keyframes_[upper].time < sample_time) {
            ++upper;
        }
        if (upper < keyframes_.size()
            && sample_time == keyframes_[upper].time) {
            return keyframes_[upper].weights;
        }
        if (upper >= keyframes_.size()) {
            throw std::logic_error(
                "morph-weight clip failed to bracket an in-domain sample");
        }

        const MorphWeightKeyframe& left =
            keyframes_[upper - 1U];
        const MorphWeightKeyframe& right =
            keyframes_[upper];
        if (interpolation_ == SemanticInterpolationMode::Step) {
            return left.weights;
        }

        const double denominator =
            static_cast<double>(right.time)
            - static_cast<double>(left.time);
        const double alpha =
            (static_cast<double>(sample_time)
             - static_cast<double>(left.time))
            / denominator;
        if (!std::isfinite(alpha)
            || !(alpha > 0.0 && alpha < 1.0)) {
            throw std::logic_error(
                "morph-weight clip produced an invalid interpolation parameter");
        }

        std::vector<float> weights;
        weights.reserve(left.weights.size());
        for (std::size_t target = 0U;
             target < left.weights.size();
             ++target) {
            const double value =
                static_cast<double>(left.weights[target])
                + (static_cast<double>(right.weights[target])
                   - static_cast<double>(left.weights[target]))
                    * alpha;
            const float weight =
                static_cast<float>(value);
            if (!std::isfinite(weight)) {
                throw std::invalid_argument(
                    "morph-weight LINEAR interpolation is non-finite");
            }
            weights.push_back(weight);
        }
        return weights;
    }

    MorphTargetSetPtr targets_{};
    MorphStatePtr default_state_{};
    std::vector<MorphWeightKeyframe> keyframes_{};
    SemanticInterpolationMode interpolation_{
        SemanticInterpolationMode::Linear};
};

}  // namespace tiny_renderer
