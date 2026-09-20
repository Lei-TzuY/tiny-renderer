#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

constexpr std::size_t kMaxSkinInfluences = 4U;
constexpr std::size_t kMaxSkinJoints = 256U;


namespace detail {

inline void validate_skin_affine_matrix(
    const Mat4& matrix,
    const char* label) {
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

}  // namespace detail

struct SkinInfluence {
    std::uint16_t joint{};
    float weight{};
};

// Immutable bounded influence list for one canonical mesh vertex.
class VertexSkinBinding {
public:
    VertexSkinBinding(std::initializer_list<SkinInfluence> influences) {
        if (influences.size() == 0U || influences.size() > kMaxSkinInfluences) {
            throw std::invalid_argument(
                "vertex skin binding requires between one and four influences");
        }

        double total = 0.0;
        std::size_t index = 0U;
        for (const SkinInfluence influence : influences) {
            if (!std::isfinite(influence.weight) || influence.weight < 0.0F) {
                throw std::invalid_argument(
                    "vertex skin influence weight must be finite and non-negative");
            }
            influences_[index++] = influence;
            total += static_cast<double>(influence.weight);
        }
        if (!std::isfinite(total) || total <= 0.0) {
            throw std::invalid_argument(
                "vertex skin binding requires a finite positive total weight");
        }
        count_ = influences.size();
    }

    [[nodiscard]] std::span<const SkinInfluence> influences() const noexcept {
        return {influences_.data(), count_};
    }

private:
    std::array<SkinInfluence, kMaxSkinInfluences> influences_{};
    std::size_t count_{};
};

// Immutable single-pose skinning state. Matrices are caller-supplied affine
// skin matrices (for example world_joint * inverse_bind); this first slice does
// not claim skeleton/import/inverse-bind construction semantics.
class SkinningState {
public:
    SkinningState(
        std::vector<VertexSkinBinding> vertex_bindings,
        std::vector<Mat4> skin_matrices)
        : vertex_bindings_(std::move(vertex_bindings)),
          skin_matrices_(std::move(skin_matrices)) {
        if (vertex_bindings_.empty()) {
            throw std::invalid_argument(
                "skinning state requires at least one vertex binding");
        }
        if (skin_matrices_.empty() || skin_matrices_.size() > kMaxSkinJoints) {
            throw std::invalid_argument(
                "skinning state joint palette must contain between one and 256 matrices");
        }

        for (const Mat4& matrix : skin_matrices_) {
            detail::validate_skin_affine_matrix(
                matrix,
                "skinning matrix");
        }

        for (const VertexSkinBinding& binding : vertex_bindings_) {
            for (const SkinInfluence influence : binding.influences()) {
                if (static_cast<std::size_t>(influence.joint)
                    >= skin_matrices_.size()) {
                    throw std::out_of_range(
                        "vertex skin influence references unavailable joint");
                }
            }
        }
    }

    [[nodiscard]] std::span<const VertexSkinBinding>
    vertex_bindings() const noexcept {
        return {vertex_bindings_.data(), vertex_bindings_.size()};
    }

    [[nodiscard]] std::span<const Mat4> skin_matrices() const noexcept {
        return {skin_matrices_.data(), skin_matrices_.size()};
    }

private:
    std::vector<VertexSkinBinding> vertex_bindings_{};
    std::vector<Mat4> skin_matrices_{};
};


using SkinningStatePtr = std::shared_ptr<const SkinningState>;

// Immutable bounded skeletal ownership. Joint declaration order is arbitrary;
// topology is validated once and remains fixed across every pose.
class SkeletalRig {
public:
    SkeletalRig(
        std::vector<std::optional<std::size_t>> parents,
        std::vector<Mat4> inverse_bind_matrices,
        std::vector<VertexSkinBinding> vertex_bindings)
        : parents_(std::move(parents)),
          inverse_bind_matrices_(std::move(inverse_bind_matrices)),
          vertex_bindings_(std::move(vertex_bindings)) {
        if (parents_.empty() || parents_.size() > kMaxSkinJoints) {
            throw std::invalid_argument(
                "skeletal rig joint count must be between one and 256");
        }
        if (inverse_bind_matrices_.size() != parents_.size()) {
            throw std::invalid_argument(
                "skeletal rig inverse-bind count must match joint count");
        }
        if (vertex_bindings_.empty()) {
            throw std::invalid_argument(
                "skeletal rig requires at least one vertex skin binding");
        }

        for (std::size_t joint = 0U; joint < parents_.size(); ++joint) {
            if (parents_[joint]) {
                if (*parents_[joint] >= parents_.size()) {
                    throw std::out_of_range(
                        "skeletal rig parent index exceeds joint count");
                }
                if (*parents_[joint] == joint) {
                    throw std::invalid_argument(
                        "skeletal rig joint cannot parent itself");
                }
            }
            detail::validate_skin_affine_matrix(
                inverse_bind_matrices_[joint],
                "skeletal rig inverse-bind matrix");
        }

        std::vector<unsigned char> state(parents_.size(), 0U);
        const auto visit = [&](auto&& self, std::size_t joint) -> void {
            if (state[joint] == 2U) {
                return;
            }
            if (state[joint] == 1U) {
                throw std::invalid_argument(
                    "skeletal rig contains a parent cycle");
            }
            state[joint] = 1U;
            if (parents_[joint]) {
                self(self, *parents_[joint]);
            }
            state[joint] = 2U;
        };
        for (std::size_t joint = 0U; joint < parents_.size(); ++joint) {
            visit(visit, joint);
        }

        for (const VertexSkinBinding& binding : vertex_bindings_) {
            for (const SkinInfluence influence : binding.influences()) {
                if (static_cast<std::size_t>(influence.joint)
                    >= parents_.size()) {
                    throw std::out_of_range(
                        "skeletal rig vertex binding references unavailable joint");
                }
            }
        }
    }

    [[nodiscard]] std::span<const std::optional<std::size_t>>
    parents() const noexcept {
        return {parents_.data(), parents_.size()};
    }

    [[nodiscard]] std::span<const Mat4>
    inverse_bind_matrices() const noexcept {
        return {
            inverse_bind_matrices_.data(),
            inverse_bind_matrices_.size(),
        };
    }

    [[nodiscard]] std::span<const VertexSkinBinding>
    vertex_bindings() const noexcept {
        return {vertex_bindings_.data(), vertex_bindings_.size()};
    }

    [[nodiscard]] SkinningStatePtr resolve_pose(
        std::span<const Mat4> local_transforms) const {
        if (local_transforms.size() != parents_.size()) {
            throw std::invalid_argument(
                "skeletal pose local transform count must match joint count");
        }
        for (const Mat4& local : local_transforms) {
            detail::validate_skin_affine_matrix(
                local,
                "skeletal pose local transform");
        }

        std::vector<Mat4> world(
            local_transforms.size(),
            Mat4::identity());
        std::vector<unsigned char> resolved(
            local_transforms.size(),
            0U);
        const auto resolve = [&](auto&& self, std::size_t joint)
            -> const Mat4& {
            if (resolved[joint] != 0U) {
                return world[joint];
            }
            world[joint] = parents_[joint]
                ? self(self, *parents_[joint]) * local_transforms[joint]
                : local_transforms[joint];
            detail::validate_skin_affine_matrix(
                world[joint],
                "skeletal pose composed world joint transform");
            resolved[joint] = 1U;
            return world[joint];
        };

        for (std::size_t joint = 0U; joint < parents_.size(); ++joint) {
            (void)resolve(resolve, joint);
        }

        std::vector<Mat4> skin_matrices;
        skin_matrices.reserve(parents_.size());
        for (std::size_t joint = 0U; joint < parents_.size(); ++joint) {
            Mat4 skin =
                world[joint] * inverse_bind_matrices_[joint];
            detail::validate_skin_affine_matrix(
                skin,
                "skeletal pose resolved skin matrix");
            skin_matrices.push_back(skin);
        }
        return std::make_shared<const SkinningState>(
            vertex_bindings_,
            std::move(skin_matrices));
    }

private:
    std::vector<std::optional<std::size_t>> parents_{};
    std::vector<Mat4> inverse_bind_matrices_{};
    std::vector<VertexSkinBinding> vertex_bindings_{};
};

using SkeletalRigPtr = std::shared_ptr<const SkeletalRig>;

// One deferred single-pose request. Intrinsic local-state validation is done at
// construction; hierarchy composition is intentionally delayed until object-
// space preparation so heterogeneous prepared lists remain fail-closed.
class SkeletalPoseState {
public:
    SkeletalPoseState(
        SkeletalRigPtr rig,
        std::vector<Mat4> local_transforms)
        : rig_(std::move(rig)),
          local_transforms_(std::move(local_transforms)) {
        if (!rig_) {
            throw std::invalid_argument(
                "skeletal pose requires an immutable rig");
        }
        if (local_transforms_.size() != rig_->parents().size()) {
            throw std::invalid_argument(
                "skeletal pose local transform count must match rig joint count");
        }
        for (const Mat4& local : local_transforms_) {
            detail::validate_skin_affine_matrix(
                local,
                "skeletal pose local transform");
        }
    }

    [[nodiscard]] const SkeletalRig& rig() const noexcept {
        return *rig_;
    }

    [[nodiscard]] std::span<const Mat4>
    local_transforms() const noexcept {
        return {local_transforms_.data(), local_transforms_.size()};
    }

    [[nodiscard]] SkinningStatePtr resolve() const {
        return rig_->resolve_pose(local_transforms_);
    }

private:
    SkeletalRigPtr rig_{};
    std::vector<Mat4> local_transforms_{};
};

using SkeletalPoseStatePtr =
    std::shared_ptr<const SkeletalPoseState>;

}  // namespace tiny_renderer
