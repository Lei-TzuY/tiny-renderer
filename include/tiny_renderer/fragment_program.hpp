#pragma once

#include <cstddef>
#include <memory>

#include "tiny_renderer/math.hpp"
#include "tiny_renderer/mesh.hpp"

namespace tiny_renderer {

struct FragmentProgramInput {
    VaryingPack varyings{};
    Vec3 fixed_rgb{};
    float fixed_opacity{1.0F};
    Vec2 sample_position{};
    std::size_t sample_index{0U};
    float depth{0.0F};
};

struct FragmentProgramOutput {
    Vec3 rgb{};
    float opacity{1.0F};
    bool discard{false};
};

// A bounded CPU fragment extension point. Programs cannot access the
// framebuffer and shade() is noexcept; configuration that can be rejected
// statically belongs in validate(), which submission paths call before writes.
class FragmentProgram {
public:
    virtual ~FragmentProgram() = default;

    virtual void validate(std::size_t varying_count) const {
        (void)varying_count;
    }

    [[nodiscard]] virtual FragmentProgramOutput shade(
        const FragmentProgramInput& input) const noexcept = 0;
};

using FragmentProgramPtr = std::shared_ptr<const FragmentProgram>;

}  // namespace tiny_renderer
