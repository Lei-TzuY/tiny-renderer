#pragma once

#include <cstddef>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model.hpp"
#include "tiny_renderer/model_renderer.hpp"

namespace tiny_renderer {

// Deterministic headless preview settings. The preview camera is fixed at
// +Z looking at the origin; the model is translated/scaled from its finite
// object-space bounds so the complete bounding sphere fits the limiting
// horizontal/vertical field of view.
struct OfflineRenderSettings {
    std::size_t width{512U};
    std::size_t height{512U};
    SampleCount sample_count{SampleCount::Four};
    Vec3 clear_color{0.02F, 0.025F, 0.035F};
    float vertical_fov_radians{radians(50.0F)};
    float framing_margin{1.10F};
};

// Renders a bounded, auto-framed preview through the existing model/raster
// path. ModelRenderOptions are forwarded unchanged; no alternate material,
// texture, lighting, depth, or framebuffer implementation is introduced.
[[nodiscard]] Framebuffer render_model_preview(
    const ModelAsset& asset,
    const OfflineRenderSettings& settings = {},
    ModelRenderOptions options = {});

}  // namespace tiny_renderer
