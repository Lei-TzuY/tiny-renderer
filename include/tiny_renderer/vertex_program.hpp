#pragma once

#include <cstddef>
#include <memory>

#include "tiny_renderer/mesh.hpp"

namespace tiny_renderer {

struct VertexProgramInput {
    Vec3 position{};
    VaryingPack varyings{};
};

struct VertexProgramOutput {
    Vec3 position{};
    VaryingPack varyings{};
};

// Bounded object-space vertex extension point. Implementations can deform the
// source position and varying values, but the renderer retains ownership of
// model/view/projection, normal transforms, clipping, shadow transforms, and
// all raster/framebuffer stages.
class VertexProgram {
public:
    virtual ~VertexProgram() = default;

    // Static configuration validation. This runs before submission writes.
    virtual void validate(std::size_t varying_count) const {
        (void)varying_count;
    }

    [[nodiscard]] virtual VertexProgramOutput process(
        const VertexProgramInput& input) const noexcept = 0;
};

using VertexProgramPtr = std::shared_ptr<const VertexProgram>;

}  // namespace tiny_renderer
