#pragma once

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

// Bounded fixed-function material shading contract. BlinnPhong preserves the
// renderer's historical diffuse + specular behavior; Lambert keeps diffuse,
// emissive, and diffuse-environment lighting while suppressing direct and
// environment specular contributions. This is a teaching-space boundary, not PBR.
enum class MaterialShadingModel {
    BlinnPhong,
    Lambert,
};

struct MaterialState {
    Vec3 albedo{1.0F, 1.0F, 1.0F};
    float opacity{1.0F};
    // Blinn-Phong specular reflectance in the renderer's current linear RGB
    // teaching space. Zero preserves the established Lambert-only path.
    Vec3 specular{0.0F, 0.0F, 0.0F};
    // Bounded teaching-space exponent used only when specular is non-zero.
    float shininess{32.0F};
    // Bounded self-emission in the renderer's current linear RGB teaching
    // space. Zero preserves every historical material result.
    Vec3 emissive{0.0F, 0.0F, 0.0F};
    // Trailing default preserves historical aggregate initialization and output.
    MaterialShadingModel shading_model{MaterialShadingModel::BlinnPhong};
};

}  // namespace tiny_renderer
