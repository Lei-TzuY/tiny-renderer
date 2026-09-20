#pragma once

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

inline constexpr std::size_t kMaxMorphTargets = 8U;

struct MorphTarget {
    std::vector<Vec3> position_deltas{};
    std::optional<std::vector<Vec3>> normal_deltas{};
};

// Immutable bounded target ownership aligned to one canonical vertex array.
class MorphTargetSet {
public:
    explicit MorphTargetSet(std::vector<MorphTarget> targets)
        : targets_(std::move(targets)) {
        if (targets_.empty() || targets_.size() > kMaxMorphTargets) {
            throw std::invalid_argument(
                "morph target set requires between one and eight targets");
        }
        vertex_count_ = targets_.front().position_deltas.size();
        if (vertex_count_ == 0U) {
            throw std::invalid_argument(
                "morph target set requires at least one canonical vertex delta");
        }

        for (const MorphTarget& target : targets_) {
            if (target.position_deltas.size() != vertex_count_) {
                throw std::invalid_argument(
                    "morph target position delta count must match target-set vertex ownership");
            }
            if (target.normal_deltas
                && target.normal_deltas->size() != vertex_count_) {
                throw std::invalid_argument(
                    "morph target normal delta count must match target-set vertex ownership");
            }
            for (const Vec3& delta : target.position_deltas) {
                validate_finite_delta(delta, "morph position delta");
            }
            if (target.normal_deltas) {
                for (const Vec3& delta : *target.normal_deltas) {
                    validate_finite_delta(delta, "morph normal delta");
                }
            }
        }
    }

    [[nodiscard]] std::size_t vertex_count() const noexcept {
        return vertex_count_;
    }

    [[nodiscard]] std::span<const MorphTarget> targets() const noexcept {
        return {targets_.data(), targets_.size()};
    }

private:
    static void validate_finite_delta(
        const Vec3& delta,
        const char* label) {
        if (!std::isfinite(delta.x)
            || !std::isfinite(delta.y)
            || !std::isfinite(delta.z)) {
            throw std::invalid_argument(
                std::string(label) + " must contain only finite values");
        }
    }

    std::vector<MorphTarget> targets_{};
    std::size_t vertex_count_{};
};

using MorphTargetSetPtr = std::shared_ptr<const MorphTargetSet>;

// Immutable single morph state. Weights are semantic coefficients: they may be
// negative and do not need to sum to one, but every coefficient must be finite.
class MorphState {
public:
    MorphState(
        MorphTargetSetPtr targets,
        std::vector<float> weights)
        : targets_(std::move(targets)),
          weights_(std::move(weights)) {
        if (!targets_) {
            throw std::invalid_argument(
                "morph state requires an immutable target set");
        }
        if (weights_.size() != targets_->targets().size()) {
            throw std::invalid_argument(
                "morph state weight count must match target count");
        }
        for (const float weight : weights_) {
            if (!std::isfinite(weight)) {
                throw std::invalid_argument(
                    "morph state weights must be finite");
            }
        }
    }

    [[nodiscard]] const MorphTargetSet& target_set() const noexcept {
        return *targets_;
    }

    [[nodiscard]] std::span<const float> weights() const noexcept {
        return {weights_.data(), weights_.size()};
    }

    [[nodiscard]] bool has_active_weights() const noexcept {
        for (const float weight : weights_) {
            if (weight != 0.0F) {
                return true;
            }
        }
        return false;
    }

private:
    MorphTargetSetPtr targets_{};
    std::vector<float> weights_{};
};

using MorphStatePtr = std::shared_ptr<const MorphState>;

}  // namespace tiny_renderer
