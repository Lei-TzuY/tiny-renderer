#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "tiny_renderer/framebuffer.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool same_color(const Vec3& a, const Vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

BlendState enabled_blend(
    BlendFactor source_factor,
    BlendFactor destination_factor,
    BlendOp operation = BlendOp::Add) {
    BlendState state;
    state.enabled = true;
    state.source_factor = source_factor;
    state.destination_factor = destination_factor;
    state.operation = operation;
    return state;
}

Vec3 run_blend(const Vec3& destination, const Vec3& source, BlendState state) {
    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear(destination, 1.0F);
    const bool passed = framebuffer.test_and_write(
        0U,
        0U,
        0.5F,
        source,
        DepthState{DepthCompare::Always, false},
        {},
        state);
    check(passed, "blend fixture passes fragment ownership");
    return framebuffer.color_at(0U, 0U);
}

void test_default_disabled_blend_preserves_replacement_behavior() {
    Framebuffer legacy(1U, 1U);
    Framebuffer explicit_disabled(1U, 1U);
    legacy.clear({0.25F, 0.5F, 0.75F}, 1.0F, 4U);
    explicit_disabled.clear({0.25F, 0.5F, 0.75F}, 1.0F, 4U);

    const Vec3 source{0.75F, 0.25F, 0.5F};
    const bool legacy_pass = legacy.depth_test_and_write(
        0U, 0U, 0.5F, source, DepthState{DepthCompare::Always, false});
    const bool explicit_pass = explicit_disabled.test_and_write(
        0U, 0U, 0.5F, source, DepthState{DepthCompare::Always, false}, {}, {});

    check(legacy_pass && explicit_pass,
          "legacy and explicit-disabled blend paths both accept the fragment");
    check(same_color(legacy.color_at(0U, 0U), explicit_disabled.color_at(0U, 0U))
              && legacy.rgb8() == explicit_disabled.rgb8(),
          "disabled blend is byte-identical to legacy replacement color ownership");
    check(legacy.depth_at(0U, 0U) == explicit_disabled.depth_at(0U, 0U)
              && legacy.stencil_at(0U, 0U) == explicit_disabled.stencil_at(0U, 0U),
          "disabled blend leaves existing depth/stencil ownership unchanged");
}

void test_color_write_mask_is_independent_of_blend_enable() {
    BlendState state;
    state.write_mask = {true, false, true};
    const Vec3 result = run_blend(
        {0.25F, 0.5F, 0.75F},
        {0.75F, 0.25F, 0.5F},
        state);
    check(same_color(result, {0.75F, 0.5F, 0.5F}),
          "disabled blending still applies the final RGB channel write mask");
}

void test_source_and_destination_color_factors() {
    BlendState state = enabled_blend(
        BlendFactor::SourceColor,
        BlendFactor::OneMinusSourceColor);
    const Vec3 result = run_blend(
        {0.25F, 0.5F, 0.75F},
        {0.5F, 0.25F, 0.75F},
        state);
    check(same_color(result, {0.375F, 0.4375F, 0.75F}),
          "source-color and inverse-source-color factors compose component-wise");

    state = enabled_blend(
        BlendFactor::DestinationColor,
        BlendFactor::OneMinusDestinationColor);
    const Vec3 destination_result = run_blend(
        {0.25F, 0.5F, 0.75F},
        {0.5F, 0.25F, 0.75F},
        state);
    check(same_color(destination_result, {0.5F, 0.375F, 0.75F}),
          "destination-color and inverse-destination-color factors compose component-wise");
}

void test_constant_color_factors() {
    BlendState state = enabled_blend(
        BlendFactor::ConstantColor,
        BlendFactor::OneMinusConstantColor);
    state.constant_color = {0.25F, 0.5F, 0.75F};
    const Vec3 result = run_blend(
        {0.25F, 0.25F, 0.25F},
        {0.5F, 0.5F, 0.5F},
        state);
    check(same_color(result, {0.3125F, 0.375F, 0.4375F}),
          "constant-color and inverse-constant-color factors use the validated RGB constant");
}

void test_all_blend_operations() {
    const Vec3 destination{0.25F, 0.5F, 0.75F};
    const Vec3 source{0.5F, 0.75F, 0.25F};

    check(same_color(
              run_blend(destination, source, enabled_blend(BlendFactor::One, BlendFactor::One, BlendOp::Add)),
              {0.75F, 1.25F, 1.0F}),
          "BlendOp::Add sums factored source and destination without hidden clamping");
    check(same_color(
              run_blend(destination, source, enabled_blend(BlendFactor::One, BlendFactor::One, BlendOp::Subtract)),
              {0.25F, 0.25F, -0.5F}),
          "BlendOp::Subtract computes source minus destination");
    check(same_color(
              run_blend(destination, source, enabled_blend(BlendFactor::One, BlendFactor::One, BlendOp::ReverseSubtract)),
              {-0.25F, -0.25F, 0.5F}),
          "BlendOp::ReverseSubtract computes destination minus source");

    BlendState min_state = enabled_blend(BlendFactor::Zero, BlendFactor::Zero, BlendOp::Min);
    BlendState max_state = enabled_blend(BlendFactor::Zero, BlendFactor::Zero, BlendOp::Max);
    check(same_color(run_blend(destination, source, min_state), {0.25F, 0.5F, 0.25F}),
          "BlendOp::Min compares unfactored source/destination components by contract");
    check(same_color(run_blend(destination, source, max_state), {0.5F, 0.75F, 0.75F}),
          "BlendOp::Max compares unfactored source/destination components by contract");
}

void test_color_mask_does_not_suppress_depth_or_stencil_side_effects() {
    Framebuffer framebuffer(1U, 1U);
    const Vec3 original_color{0.25F, 0.5F, 0.75F};
    framebuffer.clear(original_color, 0.75F, 3U);

    StencilState stencil;
    stencil.enabled = true;
    stencil.compare = StencilCompare::Equal;
    stencil.reference = 3U;
    stencil.pass = StencilOp::IncrementClamp;

    BlendState blend;
    blend.write_mask = {false, false, false};

    const bool passed = framebuffer.test_and_write(
        0U,
        0U,
        0.25F,
        {1.0F, 0.0F, 0.0F},
        DepthState{DepthCompare::Less, true},
        stencil,
        blend);

    check(passed, "fully masked color store still reports a passing fragment");
    check(same_color(framebuffer.color_at(0U, 0U), original_color),
          "fully masked color store preserves all destination RGB channels");
    check(framebuffer.depth_at(0U, 0U) == 0.25F,
          "fully masked color store does not suppress an enabled depth write");
    check(framebuffer.stencil_at(0U, 0U) == 4U,
          "fully masked color store does not suppress the stencil pass operation");
}

void test_invalid_blend_state_fails_closed() {
    const Vec3 original_color{0.25F, 0.5F, 0.75F};

    const auto expect_rejection = [&](BlendState state, const std::string& context) {
        Framebuffer framebuffer(1U, 1U);
        framebuffer.clear(original_color, 0.75F, 9U);
        bool threw = false;
        try {
            (void)framebuffer.test_and_write(
                0U,
                0U,
                0.25F,
                {1.0F, 0.0F, 0.0F},
                DepthState{DepthCompare::Less, true},
                {},
                state);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, context + " is rejected");
        check(same_color(framebuffer.color_at(0U, 0U), original_color)
                  && framebuffer.depth_at(0U, 0U) == 0.75F
                  && framebuffer.stencil_at(0U, 0U) == 9U,
              context + " rejection is fail-closed before framebuffer mutation");
    };

    BlendState bad_source;
    bad_source.source_factor = static_cast<BlendFactor>(99);
    expect_rejection(bad_source, "unknown source blend factor");

    BlendState bad_destination;
    bad_destination.destination_factor = static_cast<BlendFactor>(99);
    expect_rejection(bad_destination, "unknown destination blend factor");

    BlendState bad_operation;
    bad_operation.operation = static_cast<BlendOp>(99);
    expect_rejection(bad_operation, "unknown blend operation");

    BlendState bad_constant;
    bad_constant.constant_color.x = std::numeric_limits<float>::quiet_NaN();
    expect_rejection(bad_constant, "non-finite blend constant");

    BlendState out_of_range_constant;
    out_of_range_constant.constant_color.z = 1.25F;
    expect_rejection(out_of_range_constant, "out-of-range blend constant");
}

}  // namespace

int main() {
    try {
        test_default_disabled_blend_preserves_replacement_behavior();
        test_color_write_mask_is_independent_of_blend_enable();
        test_source_and_destination_color_factors();
        test_constant_color_factors();
        test_all_blend_operations();
        test_color_mask_does_not_suppress_depth_or_stencil_side_effects();
        test_invalid_blend_state_fails_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " blend test(s) failed\n";
        return 1;
    }

    std::cout << "all blend tests passed\n";
    return 0;
}
