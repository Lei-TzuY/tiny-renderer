#pragma once

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

struct MaterialState {
    Vec3 albedo{1.0F, 1.0F, 1.0F};
    float opacity{1.0F};
    // Blinn-Phong specular reflectance in the renderer's current linear RGB
    // teaching space. Zero preserves the established Lambert-only path.
    Vec3 specular{0.0F, 0.0F, 0.0F};
    // Bounded teaching-space exponent used only when specular is non-zero.
    float shininess{32.0F};
};

}  // namespace tiny_renderer
