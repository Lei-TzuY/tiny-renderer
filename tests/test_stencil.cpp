#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

StencilState stencil_state(
    StencilCompare compare,
    std::uint8_t reference,
    StencilOp stencil_fail = StencilOp::Keep,
    StencilOp depth_fail = StencilOp::Keep,
    StencilOp pass = StencilOp::Keep,
    std::uint8_t read_mask = 0xFFU,
    std::uint8_t write_mask = 0xFFU) {
    StencilState state;
    state.enabled = true;
    state.compare = compare;
    state.reference = reference;
    state.read_mask = read_mask;
    state.write_mask = write_mask;
    state.stencil_fail = stencil_fail;
    state.depth_fail = depth_fail;
    state.pass = pass;
    return state;
}

bool run_compare(
    StencilCompare compare,
    std::uint8_t reference,
    std::uint8_t stored,
    std::uint8_t read_mask = 0xFFU) {
    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({}, 0.5F, stored);
    StencilState state = stencil_state(compare, reference);
    state.read_mask = read_mask;
    state.write_mask = 0U;
    return framebuffer.test_and_write(
        0U,
        0U,
        0.25F,
        {1.0F, 0.0F, 0.0F},
        DepthState{DepthCompare::Always, false},
        state);
}

std::uint8_t run_stencil_fail_operation(
    StencilOp operation,
    std::uint8_t stored,
    std::uint8_t reference,
    std::uint8_t write_mask = 0xFFU) {
    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({}, 0.5F, stored);
    const StencilState state = stencil_state(
        StencilCompare::Never,
        reference,
        operation,
        StencilOp::Keep,
        StencilOp::Keep,
        0xFFU,
        write_mask);
    const bool passed = framebuffer.test_and_write(
        0U,
        0U,
        0.25F,
        {1.0F, 0.0F, 0.0F},
        DepthState{DepthCompare::Always, false},
        state);
    check(!passed, "stencil-fail operation fixture rejects the fragment");
    return framebuffer.stencil_at(0U, 0U);
}

void test_clear_and_compare_semantics() {
    Framebuffer framebuffer(2U, 2U);
    framebuffer.clear({0.1F, 0.2F, 0.3F}, 0.75F, 0x5AU);
    check(framebuffer.stencil_at(0U, 0U) == 0x5AU
              && framebuffer.stencil_at(1U, 1U) == 0x5AU,
          "stencil clear initializes the complete 8-bit attachment");

    check(!run_compare(StencilCompare::Never, 3U, 5U), "StencilCompare::Never rejects");
    check(run_compare(StencilCompare::Less, 3U, 5U), "StencilCompare::Less compares reference < stored");
    check(!run_compare(StencilCompare::Less, 5U, 3U), "StencilCompare::Less rejects reversed ordering");
    check(run_compare(StencilCompare::LessEqual, 5U, 5U), "StencilCompare::LessEqual accepts equality");
    check(run_compare(StencilCompare::Greater, 7U, 5U), "StencilCompare::Greater compares reference > stored");
    check(!run_compare(StencilCompare::Greater, 3U, 5U), "StencilCompare::Greater rejects reversed ordering");
    check(run_compare(StencilCompare::GreaterEqual, 5U, 5U), "StencilCompare::GreaterEqual accepts equality");
    check(run_compare(StencilCompare::Equal, 5U, 5U), "StencilCompare::Equal accepts equality");
    check(!run_compare(StencilCompare::Equal, 4U, 5U), "StencilCompare::Equal rejects inequality");
    check(run_compare(StencilCompare::NotEqual, 4U, 5U), "StencilCompare::NotEqual accepts inequality");
    check(!run_compare(StencilCompare::NotEqual, 5U, 5U), "StencilCompare::NotEqual rejects equality");
    check(run_compare(StencilCompare::Always, 0U, 255U), "StencilCompare::Always accepts");
}

void test_read_mask_applies_to_reference_and_stored_values() {
    check(run_compare(StencilCompare::Equal, 0x25U, 0xA5U, 0x0FU),
          "stencil read mask compares only selected low bits");
    check(!run_compare(StencilCompare::Equal, 0x25U, 0xA5U, 0xF0U),
          "stencil read mask exposes differing selected high bits");
}

void test_all_stencil_operations_and_write_mask() {
    check(run_stencil_fail_operation(StencilOp::Keep, 0xA5U, 0x3CU) == 0xA5U,
          "StencilOp::Keep preserves stored value");
    check(run_stencil_fail_operation(StencilOp::Zero, 0xA5U, 0x3CU) == 0U,
          "StencilOp::Zero writes zero");
    check(run_stencil_fail_operation(StencilOp::Replace, 0xA5U, 0x3CU) == 0x3CU,
          "StencilOp::Replace writes the reference value");
    check(run_stencil_fail_operation(StencilOp::IncrementClamp, 0x10U, 0U) == 0x11U,
          "StencilOp::IncrementClamp increments a non-maximum value");
    check(run_stencil_fail_operation(StencilOp::IncrementClamp, 0xFFU, 0U) == 0xFFU,
          "StencilOp::IncrementClamp saturates at 255");
    check(run_stencil_fail_operation(StencilOp::DecrementClamp, 0x10U, 0U) == 0x0FU,
          "StencilOp::DecrementClamp decrements a non-zero value");
    check(run_stencil_fail_operation(StencilOp::DecrementClamp, 0U, 0U) == 0U,
          "StencilOp::DecrementClamp saturates at zero");
    check(run_stencil_fail_operation(StencilOp::Invert, 0xA5U, 0U) == 0x5AU,
          "StencilOp::Invert flips all eight bits");
    check(run_stencil_fail_operation(StencilOp::Replace, 0xA5U, 0x3CU, 0x0FU) == 0xACU,
          "stencil write mask merges only selected result bits into stored value");
}

void test_stencil_then_depth_ordering_and_operation_paths() {
    const Vec3 original_color{0.1F, 0.2F, 0.3F};

    {
        Framebuffer framebuffer(1U, 1U);
        framebuffer.clear(original_color, 0.5F, 1U);
        const StencilState state = stencil_state(
            StencilCompare::Equal,
            2U,
            StencilOp::Replace,
            StencilOp::IncrementClamp,
            StencilOp::Invert);
        const bool passed = framebuffer.test_and_write(
            0U, 0U, 0.25F, {1.0F, 0.0F, 0.0F}, {}, state);
        check(!passed, "stencil rejection stops before depth/color ownership");
        check(framebuffer.stencil_at(0U, 0U) == 2U,
              "stencil rejection applies stencil-fail operation");
        check(framebuffer.depth_at(0U, 0U) == 0.5F && same_color(framebuffer.color_at(0U, 0U), original_color),
              "stencil rejection preserves depth and color");
    }

    {
        Framebuffer framebuffer(1U, 1U);
        framebuffer.clear(original_color, 0.5F, 1U);
        const StencilState state = stencil_state(
            StencilCompare::Equal,
            1U,
            StencilOp::Replace,
            StencilOp::IncrementClamp,
            StencilOp::Invert);
        const bool passed = framebuffer.test_and_write(
            0U, 0U, 0.75F, {1.0F, 0.0F, 0.0F}, DepthState{DepthCompare::Less, true}, state);
        check(!passed, "depth rejection occurs only after stencil acceptance");
        check(framebuffer.stencil_at(0U, 0U) == 2U,
              "depth rejection applies depth-fail stencil operation");
        check(framebuffer.depth_at(0U, 0U) == 0.5F && same_color(framebuffer.color_at(0U, 0U), original_color),
              "depth rejection preserves depth and color");
    }

    {
        Framebuffer framebuffer(1U, 1U);
        framebuffer.clear(original_color, 0.5F, 1U);
        const StencilState state = stencil_state(
            StencilCompare::Equal,
            1U,
            StencilOp::Replace,
            StencilOp::IncrementClamp,
            StencilOp::Invert);
        const Vec3 incoming_color{0.8F, 0.4F, 0.2F};
        const bool passed = framebuffer.test_and_write(
            0U, 0U, 0.25F, incoming_color, DepthState{DepthCompare::Less, true}, state);
        check(passed, "fragment passes when both stencil and depth accept");
        check(framebuffer.stencil_at(0U, 0U) == 0xFEU,
              "full pass applies pass stencil operation");
        check(framebuffer.depth_at(0U, 0U) == 0.25F && same_color(framebuffer.color_at(0U, 0U), incoming_color),
              "full pass owns depth and color");
    }
}

void test_depth_write_disable_does_not_disable_stencil_pass_operation() {
    Framebuffer framebuffer(1U, 1U);
    framebuffer.clear({0.1F, 0.2F, 0.3F}, 0.5F, 4U);
    const StencilState state = stencil_state(
        StencilCompare::Equal,
        4U,
        StencilOp::Keep,
        StencilOp::Keep,
        StencilOp::IncrementClamp);
    const Vec3 incoming_color{0.7F, 0.5F, 0.25F};
    const bool passed = framebuffer.test_and_write(
        0U,
        0U,
        0.25F,
        incoming_color,
        DepthState{DepthCompare::Less, false},
        state);
    check(passed, "depth-write-disabled fragment still passes depth testing");
    check(framebuffer.depth_at(0U, 0U) == 0.5F,
          "depth-write-disabled pass preserves stored depth");
    check(framebuffer.stencil_at(0U, 0U) == 5U,
          "depth-write-disabled pass still applies stencil pass operation");
    check(same_color(framebuffer.color_at(0U, 0U), incoming_color),
          "depth-write-disabled pass still updates color");
}

void test_disabled_stencil_preserves_depth_test_wrapper_behavior() {
    Framebuffer wrapper(1U, 1U);
    Framebuffer explicit_disabled(1U, 1U);
    wrapper.clear({}, 0.5F, 17U);
    explicit_disabled.clear({}, 0.5F, 17U);

    const Vec3 color{0.6F, 0.3F, 0.2F};
    const bool wrapper_pass = wrapper.depth_test_and_write(
        0U, 0U, 0.25F, color, DepthState{DepthCompare::Less, true});
    const bool explicit_pass = explicit_disabled.test_and_write(
        0U, 0U, 0.25F, color, DepthState{DepthCompare::Less, true}, {});

    check(wrapper_pass == explicit_pass,
          "legacy depth_test_and_write wrapper matches explicit disabled stencil path");
    check(wrapper.rgb8() == explicit_disabled.rgb8()
              && wrapper.depth_at(0U, 0U) == explicit_disabled.depth_at(0U, 0U),
          "disabled stencil leaves legacy color/depth ownership unchanged");
    check(wrapper.stencil_at(0U, 0U) == 17U && explicit_disabled.stencil_at(0U, 0U) == 17U,
          "disabled stencil leaves stencil attachment untouched");
}

void test_invalid_stencil_state_fails_closed() {
    const Vec3 original_color{0.1F, 0.2F, 0.3F};

    {
        Framebuffer framebuffer(1U, 1U);
        framebuffer.clear(original_color, 0.5F, 9U);
        StencilState state;
        state.enabled = true;
        state.compare = static_cast<StencilCompare>(99);
        bool threw = false;
        try {
            (void)framebuffer.test_and_write(0U, 0U, 0.25F, {1.0F, 0.0F, 0.0F}, {}, state);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "unknown stencil comparison mode is rejected");
        check(framebuffer.depth_at(0U, 0U) == 0.5F
                  && framebuffer.stencil_at(0U, 0U) == 9U
                  && same_color(framebuffer.color_at(0U, 0U), original_color),
              "unknown stencil comparison rejection is fail-closed");
    }

    {
        Framebuffer framebuffer(1U, 1U);
        framebuffer.clear(original_color, 0.5F, 9U);
        StencilState state;
        state.enabled = true;
        state.pass = static_cast<StencilOp>(99);
        bool threw = false;
        try {
            (void)framebuffer.test_and_write(0U, 0U, 0.25F, {1.0F, 0.0F, 0.0F}, {}, state);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "unknown stencil operation is rejected");
        check(framebuffer.depth_at(0U, 0U) == 0.5F
                  && framebuffer.stencil_at(0U, 0U) == 9U
                  && same_color(framebuffer.color_at(0U, 0U), original_color),
              "unknown stencil operation rejection is fail-closed");
    }
}

}  // namespace

int main() {
    try {
        test_clear_and_compare_semantics();
        test_read_mask_applies_to_reference_and_stored_values();
        test_all_stencil_operations_and_write_mask();
        test_stencil_then_depth_ordering_and_operation_paths();
        test_depth_write_disable_does_not_disable_stencil_pass_operation();
        test_disabled_stencil_preserves_depth_test_wrapper_behavior();
        test_invalid_stencil_state_fails_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " stencil test(s) failed\n";
        return 1;
    }

    std::cout << "all stencil tests passed\n";
    return 0;
}
