#pragma once

#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "tiny_renderer/prepared_spatial.hpp"

namespace tiny_renderer {

// A prepared scene separates immutable model/spatial ownership from the
// camera-dependent ordering and visibility decisions made for one evaluation.
// The two phases intentionally match the renderer's existing execution
// semantics: caller-order work executes first, then stable back-to-front work.
enum class PreparedScenePhase {
    CallerOrder,
    BackToFront,
};

struct PreparedScenePlanEntry {
    PreparedSpatialSubmission prepared;
    Mat4 model{Mat4::identity()};
    PreparedScenePhase phase{PreparedScenePhase::CallerOrder};
};

class PreparedScenePlan {
public:
    explicit PreparedScenePlan(std::vector<PreparedScenePlanEntry> entries)
        : entries_(std::move(entries)) {
        for (const PreparedScenePlanEntry& entry : entries_) {
            switch (entry.phase) {
                case PreparedScenePhase::CallerOrder:
                case PreparedScenePhase::BackToFront:
                    break;
                default:
                    throw std::invalid_argument("prepared scene entry uses an unknown execution phase");
            }
        }
    }

    // Evaluations contain pointers into these owned submissions. Keeping the
    // plan at a stable address makes that lifetime relationship explicit and
    // prevents a later move from invalidating a previously produced evaluation.
    PreparedScenePlan(const PreparedScenePlan&) = delete;
    PreparedScenePlan(PreparedScenePlan&&) = delete;
    PreparedScenePlan& operator=(const PreparedScenePlan&) = delete;
    PreparedScenePlan& operator=(PreparedScenePlan&&) = delete;

    [[nodiscard]] std::span<const PreparedScenePlanEntry> entries() const noexcept {
        return {entries_.data(), entries_.size()};
    }

private:
    std::vector<PreparedScenePlanEntry> entries_;
};

class PreparedSceneEvaluation {
public:
    PreparedSceneEvaluation(const PreparedSceneEvaluation&) = default;
    PreparedSceneEvaluation(PreparedSceneEvaluation&&) noexcept = default;
    PreparedSceneEvaluation& operator=(const PreparedSceneEvaluation&) = default;
    PreparedSceneEvaluation& operator=(PreparedSceneEvaluation&&) noexcept = default;

    [[nodiscard]] const Mat4& view() const noexcept { return view_; }
    [[nodiscard]] const Mat4& projection() const noexcept { return projection_; }

    [[nodiscard]] std::span<const PreparedDrawOrderEntry> caller_order_draws() const noexcept {
        return {caller_order_draws_.data(), caller_order_draws_.size()};
    }
    [[nodiscard]] std::span<const PreparedDrawOrderEntry> visible_caller_order_draws() const noexcept {
        return {visible_caller_order_draws_.data(), visible_caller_order_draws_.size()};
    }
    [[nodiscard]] std::span<const PreparedDrawOrderEntry> back_to_front_draws() const noexcept {
        return {back_to_front_draws_.data(), back_to_front_draws_.size()};
    }
    [[nodiscard]] std::span<const PreparedDrawOrderEntry> visible_back_to_front_draws() const noexcept {
        return {visible_back_to_front_draws_.data(), visible_back_to_front_draws_.size()};
    }

private:
    friend PreparedSceneEvaluation evaluate_prepared_scene_plan(
        const PreparedScenePlan& plan,
        const Mat4& view,
        const Mat4& projection);

    PreparedSceneEvaluation(
        Mat4 view,
        Mat4 projection,
        std::vector<PreparedDrawOrderEntry> caller_order_draws,
        std::vector<PreparedDrawOrderEntry> visible_caller_order_draws,
        std::vector<PreparedDrawOrderEntry> back_to_front_draws,
        std::vector<PreparedDrawOrderEntry> visible_back_to_front_draws)
        : view_(view),
          projection_(projection),
          caller_order_draws_(std::move(caller_order_draws)),
          visible_caller_order_draws_(std::move(visible_caller_order_draws)),
          back_to_front_draws_(std::move(back_to_front_draws)),
          visible_back_to_front_draws_(std::move(visible_back_to_front_draws)) {}

    Mat4 view_{Mat4::identity()};
    Mat4 projection_{Mat4::identity()};
    std::vector<PreparedDrawOrderEntry> caller_order_draws_;
    std::vector<PreparedDrawOrderEntry> visible_caller_order_draws_;
    std::vector<PreparedDrawOrderEntry> back_to_front_draws_;
    std::vector<PreparedDrawOrderEntry> visible_back_to_front_draws_;
};

// The plan must outlive every evaluation produced from it. Evaluation is
// camera-dependent: it recomputes view-depth ordering and conservative frustum
// selection without rebuilding or copying the owned prepared model snapshots.
[[nodiscard]] inline PreparedSceneEvaluation evaluate_prepared_scene_plan(
    const PreparedScenePlan& plan,
    const Mat4& view,
    const Mat4& projection) {
    std::vector<PreparedSpatialListEntry> caller_order_entries;
    std::vector<PreparedSpatialListEntry> back_to_front_entries;
    caller_order_entries.reserve(plan.entries().size());
    back_to_front_entries.reserve(plan.entries().size());

    for (const PreparedScenePlanEntry& entry : plan.entries()) {
        switch (entry.phase) {
            case PreparedScenePhase::CallerOrder:
                caller_order_entries.push_back({&entry.prepared, entry.model});
                break;
            case PreparedScenePhase::BackToFront:
                back_to_front_entries.push_back({&entry.prepared, entry.model});
                break;
            default:
                throw std::invalid_argument("prepared scene entry uses an unknown execution phase");
        }
    }

    std::vector<PreparedDrawOrderEntry> caller_order_draws =
        flatten_prepared_model_draws(caller_order_entries, view);
    std::vector<PreparedDrawOrderEntry> back_to_front_draws =
        order_prepared_model_draws_back_to_front(back_to_front_entries, view);

    std::vector<PreparedDrawOrderEntry> visible_caller_order_draws =
        filter_prepared_draw_order_to_frustum(caller_order_draws, view, projection);
    std::vector<PreparedDrawOrderEntry> visible_back_to_front_draws =
        filter_prepared_draw_order_to_frustum(back_to_front_draws, view, projection);

    return PreparedSceneEvaluation{
        view,
        projection,
        std::move(caller_order_draws),
        std::move(visible_caller_order_draws),
        std::move(back_to_front_draws),
        std::move(visible_back_to_front_draws),
    };
}

// Visibility is execution selection only. Target-dependent validation always
// covers both complete unfiltered phase plans before any combined execution may
// mutate the framebuffer.
inline void preflight_prepared_scene_evaluation(
    const Framebuffer& framebuffer,
    const PreparedSceneEvaluation& evaluation) {
    preflight_prepared_draw_order(framebuffer, evaluation.caller_order_draws());
    preflight_prepared_draw_order(framebuffer, evaluation.back_to_front_draws());
}

inline void draw_prepared_scene_evaluation(
    Framebuffer& framebuffer,
    const PreparedSceneEvaluation& evaluation) {
    preflight_prepared_scene_evaluation(framebuffer, evaluation);
    draw_prepared_draw_order(
        framebuffer,
        evaluation.visible_caller_order_draws(),
        evaluation.view(),
        evaluation.projection());
    draw_prepared_draw_order(
        framebuffer,
        evaluation.visible_back_to_front_draws(),
        evaluation.view(),
        evaluation.projection());
}

}  // namespace tiny_renderer
