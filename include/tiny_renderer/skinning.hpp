#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

constexpr std::size_t kMaxSkinInfluences = 4U;
constexpr std::size_t kMaxSkinJoints = 256U;

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
            for (std::size_t row = 0U; row < 4U; ++row) {
                for (std::size_t column = 0U; column < 4U; ++column) {
                    if (!std::isfinite(matrix(row, column))) {
                        throw std::invalid_argument(
                            "skinning matrix must contain only finite values");
                    }
                }
            }
            if (std::fabs(matrix(3U, 0U)) > kEpsilon
                || std::fabs(matrix(3U, 1U)) > kEpsilon
                || std::fabs(matrix(3U, 2U)) > kEpsilon
                || std::fabs(matrix(3U, 3U) - 1.0F) > kEpsilon) {
                throw std::invalid_argument(
                    "skinning matrix must be affine");
            }
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

}  // namespace tiny_renderer
