#pragma once

namespace tiny_renderer {

// Deterministic bounded depth-comparison policy for all shadow-map types.
// Hard preserves the historical one-texel comparison exactly. Pcf3x3 uses
// nine equal-weight comparisons around that same nearest texel; taps clamp to
// the current 2D map or already-selected cubemap face and never cross faces.
enum class ShadowSamplingMode {
    Hard,
    Pcf3x3,
};

}  // namespace tiny_renderer
