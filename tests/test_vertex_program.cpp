#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/fragment_program.hpp"
#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/rasterizer.hpp"
#include "tiny_renderer/shadow_renderer.hpp"
#include "tiny_renderer/vertex_program.hpp"

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

Vertex vertex(const Vec3& position, float varying = 0.25F) {
    return Vertex::with_varyings(position, VaryingPack{varying});
}

Triangle base_triangle() {
    return Triangle{{
        vertex({-0.72F, -0.55F, 0.0F}),
        vertex({-0.18F, -0.55F, 0.0F}),
        vertex({-0.45F, 0.25F, 0.0F}),
    }};
}

Mesh base_mesh() {
    Mesh mesh;
    mesh.vertices = {
        vertex({-0.72F, -0.55F, 0.0F}),
        vertex({-0.18F, -0.55F, 0.0F}),
        vertex({-0.45F, 0.25F, 0.0F}),
        vertex({0.05F, 0.45F, 0.0F}),
    };
    mesh.triangles = {
        {0U, 1U, 2U},
        {1U, 3U, 2U},
    };
    return mesh;
}

Triangle translate_triangle(const Triangle& source, const Vec3& offset) {
    Triangle result = source;
    for (Vertex& v : result) {
        v.position = v.position + offset;
    }
    return result;
}

Mesh translate_mesh(const Mesh& source, const Vec3& offset) {
    Mesh result = source;
    for (Vertex& v : result.vertices) {
        v.position = v.position + offset;
    }
    return result;
}

ModelAsset model_from_mesh(Mesh mesh, const Vec3& albedo = {1.0F, 0.0F, 0.0F}) {
    ModelAsset asset;
    asset.mesh = std::move(mesh);
    MaterialDraw draw;
    draw.range = {0U, asset.mesh.triangles.size()};
    draw.material.albedo = albedo;
    asset.draws = {draw};
    return asset;
}

class IdentityVertexProgram final : public VertexProgram {
public:
    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        return {input.position, input.varyings};
    }
};

class TranslateVertexProgram final : public VertexProgram {
public:
    explicit TranslateVertexProgram(Vec3 offset) : offset_(offset) {}

    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        return {input.position + offset_, input.varyings};
    }

private:
    Vec3 offset_{};
};

class RewriteVaryingProgram final : public VertexProgram {
public:
    explicit RewriteVaryingProgram(float value) : value_(value) {}

    void validate(std::size_t varying_count) const override {
        if (varying_count < 1U) {
            throw std::invalid_argument("rewrite program requires varying channel zero");
        }
    }

    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        VertexProgramOutput output{input.position, input.varyings};
        output.varyings.values[0] = value_;
        return output;
    }

private:
    float value_{};
};

class VaryingFragmentProgram final : public FragmentProgram {
public:
    void validate(std::size_t varying_count) const override {
        if (varying_count < 1U) {
            throw std::invalid_argument("fragment program requires varying channel zero");
        }
    }

    FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
        const float value = input.varyings.values[0];
        return {{value, 1.0F - value, 0.0F}, input.fixed_opacity, false};
    }
};

class InvalidPositionProgram final : public VertexProgram {
public:
    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        return {
            {std::numeric_limits<float>::quiet_NaN(), input.position.y, input.position.z},
            input.varyings,
        };
    }
};

class InvalidLayoutProgram final : public VertexProgram {
public:
    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        VertexProgramOutput output{input.position, input.varyings};
        output.varyings.count = input.varyings.count + 1U;
        return output;
    }
};

class RequiresTwoVaryingsProgram final : public VertexProgram {
public:
    void validate(std::size_t varying_count) const override {
        if (varying_count < 2U) {
            throw std::invalid_argument("vertex program requires two varying channels");
        }
    }

    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        return {input.position, input.varyings};
    }
};

class CountingProgram final : public VertexProgram {
public:
    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        ++calls_;
        return {input.position, input.varyings};
    }

    [[nodiscard]] std::size_t calls() const noexcept { return calls_; }

private:
    mutable std::size_t calls_{0U};
};

Rasterizer constant_rasterizer(
    Framebuffer& framebuffer,
    VertexProgramPtr vertex_program = {},
    FragmentProgramPtr fragment_program = {}) {
    MaterialState material;
    material.albedo = {1.0F, 0.0F, 0.0F};
    return Rasterizer(
        framebuffer,
        {},
        {},
        {},
        material,
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
        std::move(fragment_program),
        std::move(vertex_program));
}

void check_same_framebuffer(
    const Framebuffer& a,
    const Framebuffer& b,
    const std::string& message) {
    check(a.rgb8() == b.rgb8(), message + " RGB");
    check(a.width() == b.width() && a.height() == b.height(), message + " dimensions");
    if (a.width() != b.width() || a.height() != b.height()) {
        return;
    }
    for (std::size_t y = 0U; y < a.height(); ++y) {
        for (std::size_t x = 0U; x < a.width(); ++x) {
            check(a.depth_at(x, y) == b.depth_at(x, y), message + " depth");
            check(a.stencil_at(x, y) == b.stencil_at(x, y), message + " stencil");
        }
    }
}

void test_identity_and_position_deformation() {
    Framebuffer baseline(33U, 33U);
    Framebuffer identity(33U, 33U);
    baseline.clear({0.1F, 0.2F, 0.3F}, 1.0F, 5U);
    identity.clear({0.1F, 0.2F, 0.3F}, 1.0F, 5U);
    constant_rasterizer(baseline).draw_triangle(base_triangle(), Mat4::identity());
    constant_rasterizer(identity, std::make_shared<IdentityVertexProgram>())
        .draw_triangle(base_triangle(), Mat4::identity());
    check_same_framebuffer(baseline, identity, "identity vertex program preserves fixed pipeline");

    const Vec3 offset{0.8F, 0.0F, 0.0F};
    Framebuffer programmed(33U, 33U);
    Framebuffer manual(33U, 33U);
    programmed.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
    manual.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
    constant_rasterizer(programmed, std::make_shared<TranslateVertexProgram>(offset))
        .draw_triangle(base_triangle(), Mat4::identity());
    constant_rasterizer(manual)
        .draw_triangle(translate_triangle(base_triangle(), offset), Mat4::identity());
    check_same_framebuffer(programmed, manual, "object-space deformation matches manual geometry");
}

void test_varying_rewrite_reaches_fragment_program() {
    Framebuffer framebuffer(17U, 17U);
    framebuffer.clear({0.0F, 0.0F, 0.0F}, 1.0F, 0U);
    constant_rasterizer(
        framebuffer,
        std::make_shared<RewriteVaryingProgram>(0.8F),
        std::make_shared<VaryingFragmentProgram>())
        .draw_triangle(base_triangle(), Mat4::identity());

    const Vec3 center = framebuffer.color_at(4U, 10U);
    check_near(center.x, 0.8F, "fragment program observes rewritten vertex varying red");
    check_near(center.y, 0.2F, "fragment program observes rewritten vertex varying green");
}

void test_program_runs_once_per_source_vertex() {
    const Mesh mesh = base_mesh();

    auto mesh_counter = std::make_shared<CountingProgram>();
    Framebuffer mesh_fb(17U, 17U);
    mesh_fb.clear();
    constant_rasterizer(mesh_fb, mesh_counter).draw_mesh(mesh, Mat4::identity());
    check(mesh_counter->calls() == mesh.vertices.size(),
          "mesh submission executes vertex program once per source vertex");

    auto range_counter = std::make_shared<CountingProgram>();
    Framebuffer range_fb(17U, 17U);
    range_fb.clear();
    constant_rasterizer(range_fb, range_counter)
        .draw_mesh_range(mesh, DrawRange{0U, 1U}, Mat4::identity());
    check(range_counter->calls() == mesh.vertices.size(),
          "range submission prepares each canonical source vertex exactly once");
}

void test_deformation_precedes_homogeneous_clipping() {
    const Vec3 offset{0.9F, 0.0F, 0.0F};
    Triangle crossing = base_triangle();
    crossing[1].position.x = 0.65F;

    Framebuffer programmed(25U, 25U);
    Framebuffer manual(25U, 25U);
    programmed.clear();
    manual.clear();
    constant_rasterizer(programmed, std::make_shared<TranslateVertexProgram>(offset))
        .draw_triangle(crossing, Mat4::identity());
    constant_rasterizer(manual)
        .draw_triangle(translate_triangle(crossing, offset), Mat4::identity());
    check_same_framebuffer(programmed, manual,
                           "programmed geometry is clipped by the existing homogeneous clip path");
}

void test_invalid_outputs_fail_before_framebuffer_ownership() {
    for (const VertexProgramPtr& program : std::array<VertexProgramPtr, 2>{
             std::make_shared<InvalidPositionProgram>(),
             std::make_shared<InvalidLayoutProgram>()}) {
        Framebuffer framebuffer(17U, 17U);
        framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.77F, 19U);
        const auto before = framebuffer.rgb8();
        bool threw = false;
        try {
            constant_rasterizer(framebuffer, program).draw_mesh(base_mesh(), Mat4::identity());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "invalid vertex program output is rejected");
        check(framebuffer.rgb8() == before, "invalid vertex output leaves RGB untouched");
        for (std::size_t y = 0U; y < framebuffer.height(); ++y) {
            for (std::size_t x = 0U; x < framebuffer.width(); ++x) {
                check_near(framebuffer.depth_at(x, y), 0.77F,
                           "invalid vertex output leaves depth untouched");
                check(framebuffer.stencil_at(x, y) == 19U,
                      "invalid vertex output leaves stencil untouched");
            }
        }
    }
}

void test_prepared_lifetime_and_list_fail_closed() {
    ModelRenderOptions valid_options;
    auto translating = std::make_shared<TranslateVertexProgram>(Vec3{0.55F, 0.0F, 0.0F});
    valid_options.vertex_program = translating;
    PreparedModelSubmission valid = prepare_model_asset(
        model_from_mesh(base_mesh()), valid_options);
    translating.reset();

    Framebuffer prepared_fb(33U, 33U);
    Framebuffer manual_fb(33U, 33U);
    prepared_fb.clear();
    manual_fb.clear();
    draw_prepared_model(prepared_fb, valid, Mat4::identity());
    draw_model_asset(
        manual_fb,
        model_from_mesh(translate_mesh(base_mesh(), {0.55F, 0.0F, 0.0F})),
        Mat4::identity());
    check_same_framebuffer(prepared_fb, manual_fb,
                           "prepared plan retains vertex program and matches manual geometry");

    ModelRenderOptions invalid_options;
    invalid_options.vertex_program = std::make_shared<InvalidPositionProgram>();
    PreparedModelSubmission invalid = prepare_model_asset(
        model_from_mesh(base_mesh(), {0.0F, 1.0F, 0.0F}), invalid_options);
    const std::array<PreparedModelListEntry, 2> entries{{
        {&valid, Mat4::identity()},
        {&invalid, Mat4::identity()},
    }};

    Framebuffer list_fb(33U, 33U);
    list_fb.clear({0.15F, 0.25F, 0.35F}, 0.88F, 11U);
    const auto before = list_fb.rgb8();
    bool threw = false;
    try {
        draw_prepared_model_list(list_fb, entries, Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "later invalid vertex program rejects heterogeneous prepared list");
    check(list_fb.rgb8() == before,
          "later invalid vertex program rejects list before earlier RGB writes");
    check_near(list_fb.depth_at(16U, 16U), 0.88F,
               "later invalid vertex program rejects list before depth writes");
    check(list_fb.stencil_at(16U, 16U) == 11U,
          "later invalid vertex program rejects list before stencil writes");
}

void test_static_validation_happens_at_prepare() {
    ModelRenderOptions options;
    options.vertex_program = std::make_shared<RequiresTwoVaryingsProgram>();
    bool threw = false;
    try {
        (void)prepare_model_asset(model_from_mesh(base_mesh()), options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "prepared model construction validates vertex program configuration");
}

void test_camera_and_shadow_use_same_programmed_silhouette() {
    const Vec3 offset{0.62F, 0.0F, 0.0F};
    ModelRenderOptions programmed_options;
    programmed_options.vertex_program = std::make_shared<TranslateVertexProgram>(offset);
    PreparedModelSubmission programmed = prepare_model_asset(
        model_from_mesh(base_mesh()), programmed_options);
    PreparedModelSubmission manual = prepare_model_asset(
        model_from_mesh(translate_mesh(base_mesh(), offset)));

    const std::array<PreparedModelListEntry, 1> programmed_entry{{
        {&programmed, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> manual_entry{{
        {&manual, Mat4::identity()},
    }};

    const auto programmed_shadow = render_directional_shadow_map(
        programmed_entry,
        Mat4::identity(),
        DirectionalShadowMapOptions{33U, 33U});
    const auto manual_shadow = render_directional_shadow_map(
        manual_entry,
        Mat4::identity(),
        DirectionalShadowMapOptions{33U, 33U});
    for (std::size_t y = 0U; y < programmed_shadow->height(); ++y) {
        for (std::size_t x = 0U; x < programmed_shadow->width(); ++x) {
            check(
                programmed_shadow->depth_at(x, y) == manual_shadow->depth_at(x, y),
                "shadow pass uses the same programmed object-space silhouette as manual geometry");
        }
    }

    Framebuffer camera_programmed(33U, 33U);
    Framebuffer camera_manual(33U, 33U);
    camera_programmed.clear();
    camera_manual.clear();
    draw_prepared_model(camera_programmed, programmed, Mat4::identity());
    draw_prepared_model(camera_manual, manual, Mat4::identity());
    check_same_framebuffer(camera_programmed, camera_manual,
                           "camera pass uses the same programmed object-space silhouette");
}

}  // namespace

int main() {
    test_identity_and_position_deformation();
    test_varying_rewrite_reaches_fragment_program();
    test_program_runs_once_per_source_vertex();
    test_deformation_precedes_homogeneous_clipping();
    test_invalid_outputs_fail_before_framebuffer_ownership();
    test_prepared_lifetime_and_list_fail_closed();
    test_static_validation_happens_at_prepare();
    test_camera_and_shadow_use_same_programmed_silhouette();

    if (failures != 0) {
        std::cerr << failures << " vertex-program test(s) failed\n";
        return 1;
    }
    std::cout << "vertex-program tests passed\n";
    return 0;
}
