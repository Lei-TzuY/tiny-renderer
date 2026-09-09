#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "tiny_renderer/environment.hpp"
#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model.hpp"
#include "tiny_renderer/model_renderer.hpp"

namespace tiny_renderer {

// Headless reflection intentionally omits a viewer position. The fixed preview
// camera is authoritative, so render_model_preview/render_scene_preview bind
// the M58 reflection light to the exact same camera eye used to build the view
// matrix.
struct OfflineEnvironmentReflectionState {
    NormalBinding normal{};
    EnvironmentReflectionState environment{};
};

// Deterministic headless preview settings. The preview camera is fixed at
// +Z looking at the origin; model/scene geometry is translated/scaled from its
// finite world-space bounds so the complete bounding sphere fits the limiting
// horizontal/vertical field of view.
struct OfflineRenderSettings {
    std::size_t width{512U};
    std::size_t height{512U};
    SampleCount sample_count{SampleCount::Four};
    Vec3 clear_color{0.02F, 0.025F, 0.035F};
    float vertical_fov_radians{radians(50.0F)};
    float framing_margin{1.10F};
    // Optional borrowed linear-HDR environment background. When present it is
    // rendered before geometry using the exact same fixed preview camera/FOV/aspect.
    std::optional<EnvironmentBackgroundState> environment{};
    // Optional borrowed diffuse environment light. Background visibility and
    // lighting are independent; callers may enable either, both, or neither.
    // The normal binding is explicit because OfflineRenderSettings is also a
    // library API and cannot infer arbitrary caller-owned varying layouts.
    std::optional<EnvironmentDiffuseLight> environment_lighting{};
    // Optional borrowed perfect-mirror environment reflection. The preview
    // owns viewer/camera consistency; callers supply only normal binding and
    // the borrowed environment state.
    std::optional<OfflineEnvironmentReflectionState> environment_reflection{};
};

enum class OfflineSceneOrdering {
    InputOrder,
    BackToFront,
};

// One borrowed entry in a bounded flat scene. The asset and render options are
// snapshotted into PreparedModelSubmission objects before any returned render
// can exist; there is deliberately no hierarchy, persistent scene graph, or
// alternate model/raster ownership path.
struct OfflineSceneEntry {
    const ModelAsset* asset{nullptr};
    Mat4 model{Mat4::identity()};
    ModelRenderOptions options{};
};

// Renders a bounded, auto-framed preview through the existing model/raster
// path. ModelRenderOptions are forwarded unchanged except that optional
// environment lighting/reflection settings are injected into the existing
// fixed-light collection. No alternate material, texture, lighting, depth,
// environment, or framebuffer implementation is introduced.
[[nodiscard]] Framebuffer render_model_preview(
    const ModelAsset& asset,
    const OfflineRenderSettings& settings = {},
    ModelRenderOptions options = {});

// Renders an ordered heterogeneous flat scene using the canonical prepared
// model list executor. InputOrder preserves caller entry order. BackToFront
// delegates to the established deterministic painter-order helper and inherits
// its bounded entry-level semantics and vertex-program rejection. Empty scenes
// are valid clear/environment-only renders.
[[nodiscard]] Framebuffer render_scene_preview(
    std::span<const OfflineSceneEntry> entries,
    const OfflineRenderSettings& settings = {},
    OfflineSceneOrdering ordering = OfflineSceneOrdering::InputOrder);

}  // namespace tiny_renderer
