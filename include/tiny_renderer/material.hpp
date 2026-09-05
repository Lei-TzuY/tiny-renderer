#pragma once

#include "tiny_renderer/math.hpp"

namespace tiny_renderer {

struct MaterialState {
    Vec3 albedo{1.0F, 1.0F, 1.0F};
    float opacity{1.0F};
};

}  // namespace tiny_renderer
