#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/fragment_program.hpp"
#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/rasterizer.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check_near(float actual, float expected, const std::string& message, float epsilon = 1.0e-6F) {
    check(
        std::fabs(actual - expected) <= epsilon,
        message + " (actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected) + ")");
}

void check_color(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check_near(actual.x, expected.x, message + " red");
    check_near(actual.y, expected.y, message + " green");
    check_near(actual.z, expected.z, message + " blue");
}

Vertex varying_vertex(const Vec3& position, float value) {
    return Vertex::with_varyings(position, VaryingPack{value});
}

Triangle full_coverage_triangle(float varying = 0.25F) {
    return Triangle{{
        varying_vertex({-1.0F, -1.0F, 0.0F}, varying),
        varying_vertex({3.0F, -1.0F, 0.0F}, varying),
        varying_vertex({-1.0F, 3.0F, 0.0F}, varying),
    }};
}

Mesh full_coverage_mesh(float varying = 0.25F) {
    Mesh mesh;
    mesh.vertices = {
        varying_vertex({-1.0F, -1.0F, 0.0F}, varying),
        varying_vertex({3.0F, -1.0F, 0.0F}, varying),
        varying_vertex({-1.0F, 3.0F, 0.0F}, varying),
    };
    mesh.triangles = {{0U, 1U, 2U}};
    return mesh;
}

ModelAsset small_model(float varying, const Vec3& albedo = {1.0F, 1.0F, 1.0F}) {
    ModelAsset asset;
    asset.mesh.vertices = {
        varying_vertex({-0.28F, -0.28F, 0.0F}, varying),
        varying_vertex({0.28F, -0.28F, 0.0F}, varying),
        varying_vertex({0.0F, 0.28F, 0.0F}, varying),
    };
    asset.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material.albedo = albedo;
    asset.draws = {draw};
    return asset;
}

StencilState replace_stencil(std::uint8_t reference = 42U) {
    StencilState state;
    state.enabled = true;
    state.compare = StencilCompare::Always;
    state.reference = reference;
    state.pass = StencilOp::Replace;
    return state;
}

class IdentityProgram final : public FragmentProgram {
public:
    FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
        return {input.fixed_rgb, input.fixed_opacity, false};
    }
};

class VaryingColorProgram final : public FragmentProgram {
public:
    void validate(std::size_t varying_count) const override {
        if (varying_count < 1U) {
            throw std::invalid_argument("varying color program requires channel zero");
        }
    }

    FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
        const float value = input.varyings.values[0];
        return {{value, 1.0F - value, input.fixed_rgb.z}, input.fixed_opacity, false};
    }
};

class OpacityRewriteProgram final : public FragmentProgram {
public:
    explicit OpacityRewriteProgram(float opacity) : opacity_(opacity) {}

    FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
        return {input.fixed_rgb, opacity_, false};
    }

private:
    float opacity_{};
};

class DiscardProgram final : public FragmentProgram {
public:
    FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
        return {input.fixed_rgb, input.fixed_opacity, true};
    }
};

class SampleProgram final : public FragmentProgram {
public:
    FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
        const float local_x = input.sample_position.x - std::floor(input.sample_position.x);
        const float local_y = input.sample_position.y - std::floor(input.sample_position.y);
        const float index = static_cast<float>(input.sample_index) / 3.0F;
        return {{local_x, local_y, index}, input.fixed_opacity, false};
    }
};

class InvalidRgbProgram final : public FragmentProgram {
public:
    FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
        return {{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, input.fixed_opacity, false};
    }
};

class RequiresTwoVaryingsProgram final : public FragmentProgram {
public:
    void validate(std::size_t varying_count) const override {
        if (varying_count < 2U) {
            throw std::invalid_argument("program requires two varying channels");
        }
    }

    FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
        return {input.fixed_rgb, input.fixed_opacity, false};
    }
};

Rasterizer make_rasterizer(
    Framebuffer& framebuffer,
    FragmentProgramPtr program,
    float material_opacity = 1.0F,
    AlphaTestState alpha_test = {},
    AlphaToCoverageState alpha_to_coverage = {}) {
    MaterialState material;
    material.albedo = {1.0F, 0.0F, 0.0F};
    material.opacity = material_opacity;
    return Rasterizer(
        framebuffer,
        ColorBinding{99U, 99U, 99U},
        {},
        {},
        material,
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        DepthState{},
        {},
        replace_stencil(),
        {},
        alpha_to_coverage,
        {},
        alpha_test,
        std::move(program));
}

void test_identity_program_preserves_default_pipeline() {
    Framebuffer baseline(9U, 9U);
    Framebuffer programmed(9U, 9U);
    baseline.clear({0.1F, 0.2F, 0.3F}, 0.9F, 7U);
    programmed.clear({0.1F, 0.2F, 0.3F}, 0.9F, 7U);

    make_rasterizer(baseline, {}).draw_triangle(full_coverage_triangle(), Mat4::identity());
    make_rasterizer(programmed, std::make_shared<IdentityProgram>())
        .draw_triangle(full_coverage_triangle(), Mat4::identity());

    check(baseline.rgb8() == programmed.rgb8(),
          "identity program preserves default resolved RGB byte-for-byte");
    for (std::size_t y = 0U; y < baseline.height(); ++y) {
        for (std::size_t x = 0U; x < baseline.width(); ++x) {
            check(baseline.depth_at(x, y) == programmed.depth_at(x, y),
                  "identity program preserves default depth");
            check(baseline.stencil_at(x, y) == programmed.stencil_at(x, y),
                  "identity program preserves default stencil");
        }
    }
}

void test_varying_driven_color_transform() {
    Framebuffer framebuffer(5U, 5U);
    framebuffer.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
    make_rasterizer(framebuffer, std::make_shared<VaryingColorProgram>())
        .draw_triangle(full_coverage_triangle(0.25F), Mat4::identity());
    check_color(
        framebuffer.color_at(2U, 2U),
        {0.25F, 0.75F, 0.0F},
        "program sees interpolated varyings and fixed RGB");
}

void test_opacity_rewrite_feeds_alpha_test_and_a2c() {
    Framebuffer alpha_tested(5U, 5U);
    alpha_tested.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    make_rasterizer(
        alpha_tested,
        std::make_shared<OpacityRewriteProgram>(0.4F),
        1.0F,
        AlphaTestState{true, 0.5F})
        .draw_triangle(full_coverage_triangle(), Mat4::identity());
    check_color(alpha_tested.color_at(2U, 2U), {0.0F, 0.0F, 1.0F},
                "program opacity rewrite is tested by alpha test");
    check_near(alpha_tested.depth_at(2U, 2U), 0.9F,
               "alpha-test rejection after program leaves depth untouched");
    check(alpha_tested.stencil_at(2U, 2U) == 7U,
          "alpha-test rejection after program leaves stencil untouched");

    Framebuffer a2c(5U, 5U, SampleCount::Four);
    a2c.clear({0.0F, 0.0F, 1.0F}, 0.9F, 7U);
    make_rasterizer(
        a2c,
        std::make_shared<OpacityRewriteProgram>(0.5F),
        1.0F,
        {},
        AlphaToCoverageState{true})
        .draw_triangle(full_coverage_triangle(), Mat4::identity());
    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        const bool survives = sample < 2U;
        check_color(
            a2c.sample_color_at(2U, 2U, sample),
            survives ? Vec3{1.0F, 0.0F, 0.0F} : Vec3{0.0F, 0.0F, 1.0F},
            "program opacity rewrite feeds deterministic A2C sample mask");
        check_near(
            a2c.sample_depth_at(2U, 2U, sample),
            survives ? 0.5F : 0.9F,
            "A2C after program owns only surviving sample depth");
        check(
            a2c.sample_stencil_at(2U, 2U, sample) == (survives ? 42U : 7U),
            "A2C after program owns only surviving sample stencil");
    }
}

void test_program_discard_precedes_all_ownership() {
    Framebuffer framebuffer(5U, 5U, SampleCount::Four);
    framebuffer.clear({0.1F, 0.2F, 0.3F}, 0.9F, 7U);
    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    make_rasterizer(
        framebuffer,
        std::make_shared<DiscardProgram>(),
        1.0F,
        AlphaTestState{true, 0.0F},
        AlphaToCoverageState{true})
        .draw_triangle(full_coverage_triangle(), Mat4::identity());
    check(framebuffer.rgb8() == before, "program discard leaves resolved RGB untouched");
    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        check_near(framebuffer.sample_depth_at(2U, 2U, sample), 0.9F,
                   "program discard precedes depth ownership");
        check(framebuffer.sample_stencil_at(2U, 2U, sample) == 7U,
              "program discard precedes stencil ownership");
    }
}

void test_sample_position_and_index_are_per_sample() {
    Framebuffer framebuffer(5U, 5U, SampleCount::Four);
    framebuffer.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
    make_rasterizer(framebuffer, std::make_shared<SampleProgram>())
        .draw_triangle(full_coverage_triangle(), Mat4::identity());

    const std::array<Vec3, 4> expected{{
        {0.25F, 0.25F, 0.0F},
        {0.75F, 0.25F, 1.0F / 3.0F},
        {0.25F, 0.75F, 2.0F / 3.0F},
        {0.75F, 0.75F, 1.0F},
    }};
    for (std::size_t sample = 0U; sample < 4U; ++sample) {
        check_color(
            framebuffer.sample_color_at(2U, 2U, sample),
            expected[sample],
            "program receives deterministic sample position/index");
    }
}

void test_invalid_program_output_fails_before_ownership() {
    Framebuffer framebuffer(5U, 5U);
    framebuffer.clear({0.1F, 0.2F, 0.3F}, 0.9F, 7U);
    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    bool threw = false;
    try {
        make_rasterizer(framebuffer, std::make_shared<InvalidRgbProgram>())
            .draw_triangle(full_coverage_triangle(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "non-finite program RGB output is rejected");
    check(framebuffer.rgb8() == before, "invalid program output preserves framebuffer RGB");
    check_near(framebuffer.depth_at(2U, 2U), 0.9F,
               "invalid program output precedes depth ownership");
    check(framebuffer.stencil_at(2U, 2U) == 7U,
          "invalid program output precedes stencil ownership");
}

void test_range_preflights_program_configuration() {
    Framebuffer framebuffer(5U, 5U);
    framebuffer.clear({0.1F, 0.2F, 0.3F}, 0.9F, 7U);
    const std::vector<std::uint8_t> before = framebuffer.rgb8();
    Rasterizer rasterizer(
        framebuffer,
        ColorBinding{99U, 99U, 99U},
        {},
        {},
        {},
        BaseColorSource::ConstantWhite,
        CullMode::None,
        FrontFace::CounterClockwise,
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        std::make_shared<RequiresTwoVaryingsProgram>());
    bool threw = false;
    try {
        rasterizer.draw_mesh_range(
            full_coverage_mesh(),
            DrawRange{0U, 1U},
            Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "range draw validates fragment program before execution");
    check(framebuffer.rgb8() == before, "range program validation failure preserves RGB");
}

void test_prepared_plan_retains_program_and_list_matches_sequential() {
    std::weak_ptr<const FragmentProgram> weak;
    PreparedModelSubmission prepared = [&]() {
        auto program = std::make_shared<VaryingColorProgram>();
        weak = program;
        ModelRenderOptions options;
        options.fragment_program = program;
        return prepare_model_asset(small_model(0.25F), options);
    }();
    check(!weak.expired(), "prepared plan retains fragment program lifetime");

    const Mat4 left = Mat4::translation({-0.35F, 0.0F, 0.0F});
    const Mat4 right = Mat4::translation({0.35F, 0.0F, 0.0F});
    Framebuffer sequential(33U, 33U);
    Framebuffer listed(33U, 33U);
    sequential.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
    listed.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);

    draw_prepared_model(sequential, prepared, left, Mat4::identity(), Mat4::identity());
    draw_prepared_model(sequential, prepared, right, Mat4::identity(), Mat4::identity());
    const std::array<PreparedModelListEntry, 2> entries{{
        {&prepared, left},
        {&prepared, right},
    }};
    draw_prepared_model_list(listed, entries, Mat4::identity(), Mat4::identity());
    check(listed.rgb8() == sequential.rgb8(),
          "prepared list fragment program matches sequential prepared execution");
}

void test_prepare_rejects_invalid_program_configuration() {
    ModelRenderOptions options;
    options.fragment_program = std::make_shared<RequiresTwoVaryingsProgram>();
    bool threw = false;
    try {
        (void)prepare_model_asset(small_model(0.25F), options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "prepared model rejects incompatible fragment program statically");
}

}  // namespace

int main() {
    test_identity_program_preserves_default_pipeline();
    test_varying_driven_color_transform();
    test_opacity_rewrite_feeds_alpha_test_and_a2c();
    test_program_discard_precedes_all_ownership();
    test_sample_position_and_index_are_per_sample();
    test_invalid_program_output_fails_before_ownership();
    test_range_preflights_program_configuration();
    test_prepared_plan_retains_program_and_list_matches_sequential();
    test_prepare_rejects_invalid_program_configuration();

    if (failures != 0) {
        std::cerr << failures << " fragment program test(s) failed\n";
        return 1;
    }
    std::cout << "fragment program tests passed\n";
    return 0;
}
