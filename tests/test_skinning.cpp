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

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/prepared_spatial.hpp"
#include "tiny_renderer/shadow_renderer.hpp"
#include "tiny_renderer/skinning.hpp"
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

template <typename Exception, typename Function>
void check_throws(Function&& function, const std::string& message) {
    bool threw = false;
    try {
        function();
    } catch (const Exception&) {
        threw = true;
    }
    check(threw, message);
}

Vertex vertex(const Vec3& position) {
    return Vertex::with_varyings(position, VaryingPack{0.25F});
}

Mesh base_mesh() {
    Mesh mesh;
    mesh.vertices = {
        vertex({-0.60F, -0.55F, 0.0F}),
        vertex({0.45F, -0.55F, 0.0F}),
        vertex({-0.05F, 0.60F, 0.0F}),
    };
    mesh.triangles = {{0U, 1U, 2U}};
    return mesh;
}

ModelAsset model_from_mesh(
    Mesh mesh,
    const Vec3& albedo = {0.8F, 0.2F, 0.1F}) {
    ModelAsset asset;
    asset.mesh = std::move(mesh);
    MaterialDraw draw;
    draw.range = {0U, asset.mesh.triangles.size()};
    draw.material.albedo = albedo;
    asset.draws = {draw};
    return asset;
}

VertexSkinBinding binding(std::initializer_list<SkinInfluence> influences) {
    return VertexSkinBinding(influences);
}

SkinningStatePtr identity_skin(std::size_t vertex_count) {
    std::vector<VertexSkinBinding> bindings;
    bindings.reserve(vertex_count);
    for (std::size_t i = 0U; i < vertex_count; ++i) {
        bindings.push_back(binding({SkinInfluence{0U, 1.0F}}));
    }
    return std::make_shared<const SkinningState>(
        std::move(bindings),
        std::vector<Mat4>{Mat4::identity()});
}

void check_same_framebuffer(
    const Framebuffer& left,
    const Framebuffer& right,
    const std::string& message) {
    check(left.width() == right.width()
              && left.height() == right.height()
              && left.sample_count() == right.sample_count(),
          message + " dimensions/sample-count");
    check(left.rgb8() == right.rgb8(), message + " resolved RGB");
    if (left.width() != right.width()
        || left.height() != right.height()
        || left.sample_count() != right.sample_count()) {
        return;
    }
    for (std::size_t y = 0U; y < left.height(); ++y) {
        for (std::size_t x = 0U; x < left.width(); ++x) {
            for (std::size_t sample = 0U;
                 sample < left.samples_per_pixel();
                 ++sample) {
                check(
                    left.sample_depth_at(x, y, sample)
                        == right.sample_depth_at(x, y, sample),
                    message + " per-sample depth");
                check(
                    left.sample_stencil_at(x, y, sample)
                        == right.sample_stencil_at(x, y, sample),
                    message + " per-sample stencil");
            }
        }
    }
}

class DoubleXProgram final : public VertexProgram {
public:
    VertexProgramOutput process(
        const VertexProgramInput& input) const noexcept override {
        VertexProgramOutput output{input.position, input.varyings};
        output.position.x *= 2.0F;
        return output;
    }
};

void test_identity_skin_exact_compatibility_and_prepared_lifetime() {
    const ModelAsset asset = model_from_mesh(base_mesh());

    Framebuffer baseline(41U, 41U, SampleCount::Four);
    Framebuffer skinned(41U, 41U, SampleCount::Four);
    baseline.clear({0.03F, 0.04F, 0.05F}, 1.0F, 7U);
    skinned.clear({0.03F, 0.04F, 0.05F}, 1.0F, 7U);

    draw_model_asset(baseline, asset, Mat4::identity());

    ModelRenderOptions options;
    auto skin = identity_skin(asset.mesh.vertices.size());
    options.skinning_state = skin;
    draw_model_asset(skinned, asset, Mat4::identity(), options);
    check_same_framebuffer(
        baseline,
        skinned,
        "one-joint identity skin is exact-equivalent to no-skin rendering");

    PreparedModelSubmission prepared = prepare_model_asset(asset, options);
    skin.reset();
    options.skinning_state.reset();
    check(
        static_cast<bool>(prepared.options().skinning_state),
        "prepared model retains immutable skinning state lifetime");

    Framebuffer prepared_fb(41U, 41U, SampleCount::Four);
    prepared_fb.clear({0.03F, 0.04F, 0.05F}, 1.0F, 7U);
    draw_prepared_model(prepared_fb, prepared, Mat4::identity());
    check_same_framebuffer(
        baseline,
        prepared_fb,
        "prepared identity skin remains exact after source skin handle release");
}

void test_multi_joint_pose_matches_manual_geometry() {
    const ModelAsset source = model_from_mesh(base_mesh());

    std::vector<VertexSkinBinding> bindings;
    bindings.push_back(binding({SkinInfluence{0U, 1.0F}}));
    bindings.push_back(binding({SkinInfluence{1U, 1.0F}}));
    bindings.push_back(binding({
        SkinInfluence{0U, 0.5F},
        SkinInfluence{1U, 0.5F},
    }));
    const auto skin = std::make_shared<const SkinningState>(
        std::move(bindings),
        std::vector<Mat4>{
            Mat4::translation({-0.25F, 0.125F, 0.0F}),
            Mat4::translation({0.25F, -0.125F, 0.0F}),
        });

    Mesh manual_mesh = source.mesh;
    manual_mesh.vertices[0].position =
        manual_mesh.vertices[0].position + Vec3{-0.25F, 0.125F, 0.0F};
    manual_mesh.vertices[1].position =
        manual_mesh.vertices[1].position + Vec3{0.25F, -0.125F, 0.0F};
    // Equal normalized weights over opposite translations leave vertex two
    // exactly at its canonical object-space position.
    const ModelAsset manual = model_from_mesh(
        std::move(manual_mesh));

    ModelRenderOptions options;
    options.skinning_state = skin;

    Framebuffer direct(47U, 47U, SampleCount::Four);
    Framebuffer reference(47U, 47U, SampleCount::Four);
    direct.clear({0.01F, 0.02F, 0.03F}, 1.0F, 0U);
    reference.clear({0.01F, 0.02F, 0.03F}, 1.0F, 0U);
    draw_model_asset(direct, source, Mat4::identity(), options);
    draw_model_asset(reference, manual, Mat4::identity());
    check_same_framebuffer(
        direct,
        reference,
        "multi-joint normalized LBS matches independently pre-deformed manual geometry");

    const PreparedModelSubmission prepared =
        prepare_model_asset(source, options);
    Framebuffer prepared_fb(47U, 47U, SampleCount::Four);
    prepared_fb.clear({0.01F, 0.02F, 0.03F}, 1.0F, 0U);
    draw_prepared_model(prepared_fb, prepared, Mat4::identity());
    check_same_framebuffer(
        prepared_fb,
        reference,
        "prepared multi-joint skin uses the same object-space geometry as direct rendering");
}

void test_skinning_precedes_vertex_program() {
    const ModelAsset source = model_from_mesh(base_mesh());
    std::vector<VertexSkinBinding> bindings;
    for (std::size_t i = 0U; i < source.mesh.vertices.size(); ++i) {
        bindings.push_back(binding({SkinInfluence{0U, 1.0F}}));
    }

    ModelRenderOptions options;
    options.skinning_state = std::make_shared<const SkinningState>(
        std::move(bindings),
        std::vector<Mat4>{Mat4::translation({0.25F, 0.0F, 0.0F})});
    options.vertex_program = std::make_shared<DoubleXProgram>();

    Mesh manual_mesh = source.mesh;
    for (Vertex& v : manual_mesh.vertices) {
        v.position.x = (v.position.x + 0.25F) * 2.0F;
    }

    Framebuffer composed(49U, 49U);
    Framebuffer manual(49U, 49U);
    composed.clear();
    manual.clear();
    draw_model_asset(composed, source, Mat4::identity(), options);
    draw_model_asset(
        manual,
        model_from_mesh(std::move(manual_mesh)),
        Mat4::identity());
    check_same_framebuffer(
        composed,
        manual,
        "skinning executes before the existing M35 object-space vertex program");
}

void test_validation_and_lighting_contract() {
    check_throws<std::invalid_argument>(
        [] {
            (void)VertexSkinBinding({
                SkinInfluence{0U, 1.0F},
                SkinInfluence{0U, 0.0F},
                SkinInfluence{0U, 0.0F},
                SkinInfluence{0U, 0.0F},
                SkinInfluence{0U, 0.0F},
            });
        },
        "skin binding rejects more than four influences");
    check_throws<std::invalid_argument>(
        [] {
            (void)VertexSkinBinding({
                SkinInfluence{0U, -0.1F},
            });
        },
        "skin binding rejects negative weight");
    check_throws<std::invalid_argument>(
        [] {
            (void)VertexSkinBinding({
                SkinInfluence{
                    0U,
                    std::numeric_limits<float>::quiet_NaN(),
                },
            });
        },
        "skin binding rejects non-finite weight");
    check_throws<std::invalid_argument>(
        [] {
            (void)VertexSkinBinding({
                SkinInfluence{0U, 0.0F},
                SkinInfluence{1U, 0.0F},
            });
        },
        "skin binding rejects zero total weight");

    check_throws<std::invalid_argument>(
        [] {
            (void)SkinningState({}, {Mat4::identity()});
        },
        "skinning state rejects empty vertex ownership");

    std::vector<Mat4> too_many_matrices(
        kMaxSkinJoints + 1U,
        Mat4::identity());
    check_throws<std::invalid_argument>(
        [&] {
            (void)SkinningState(
                {binding({SkinInfluence{0U, 1.0F}})},
                too_many_matrices);
        },
        "skinning state bounds joint palette");

    Mat4 projective = Mat4::identity();
    projective(3U, 2U) = 0.25F;
    check_throws<std::invalid_argument>(
        [&] {
            (void)SkinningState(
                {binding({SkinInfluence{0U, 1.0F}})},
                {projective});
        },
        "skinning state rejects projective joint matrix");

    Mat4 nonfinite = Mat4::identity();
    nonfinite(0U, 0U) = std::numeric_limits<float>::infinity();
    check_throws<std::invalid_argument>(
        [&] {
            (void)SkinningState(
                {binding({SkinInfluence{0U, 1.0F}})},
                {nonfinite});
        },
        "skinning state rejects non-finite joint matrix");

    check_throws<std::out_of_range>(
        [] {
            (void)SkinningState(
                {binding({SkinInfluence{1U, 1.0F}})},
                {Mat4::identity()});
        },
        "skinning state rejects unavailable joint index");

    const ModelAsset asset = model_from_mesh(base_mesh());
    ModelRenderOptions wrong_count;
    wrong_count.skinning_state = std::make_shared<const SkinningState>(
        std::vector<VertexSkinBinding>{
            binding({SkinInfluence{0U, 1.0F}}),
        },
        std::vector<Mat4>{Mat4::identity()});
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_model_asset(asset, wrong_count);
        },
        "prepared model rejects skin binding cardinality mismatch");

    ModelRenderOptions lit;
    lit.skinning_state = identity_skin(asset.mesh.vertices.size());
    lit.directional_light.enabled = true;
    lit.directional_light.normal = {0U, 0U, 0U};
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_model_asset(asset, lit);
        },
        "single-pose skinning rejects fixed lighting until normal deformation exists");

    PreparedModelSubmission skinned = prepare_model_asset(
        asset,
        ModelRenderOptions{
            .skinning_state = identity_skin(asset.mesh.vertices.size()),
        });
    const std::array<PreparedModelListEntry, 1> entry{{
        {&skinned, Mat4::identity()},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)order_prepared_model_list_back_to_front(
                entry,
                Mat4::identity());
        },
        "canonical prepared list ordering rejects position-changing skinning");

    PreparedSpatialSubmission spatial =
        prepare_spatial_submission(skinned);
    const std::array<PreparedSpatialListEntry, 1> spatial_entry{{
        {&spatial, Mat4::identity()},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)flatten_prepared_model_draws(
                spatial_entry,
                Mat4::identity());
        },
        "prepared spatial planning rejects skinning with stale canonical bounds");
}

void test_prepared_list_fail_closed_on_later_unsafe_skin() {
    const ModelAsset valid_asset = model_from_mesh(base_mesh());
    ModelRenderOptions valid_options;
    valid_options.skinning_state =
        identity_skin(valid_asset.mesh.vertices.size());
    PreparedModelSubmission valid =
        prepare_model_asset(valid_asset, valid_options);

    Mesh unsafe_mesh = base_mesh();
    unsafe_mesh.vertices[0].position.x = 2.0F;
    const ModelAsset unsafe_asset = model_from_mesh(
        std::move(unsafe_mesh),
        {0.1F, 0.8F, 0.2F});
    std::vector<VertexSkinBinding> unsafe_bindings;
    for (std::size_t i = 0U;
         i < unsafe_asset.mesh.vertices.size();
         ++i) {
        unsafe_bindings.push_back(
            binding({SkinInfluence{0U, 1.0F}}));
    }
    Mat4 huge = Mat4::identity();
    huge(0U, 0U) = 1.0e20F;
    ModelRenderOptions unsafe_options;
    unsafe_options.skinning_state =
        std::make_shared<const SkinningState>(
            std::move(unsafe_bindings),
            std::vector<Mat4>{huge});
    PreparedModelSubmission unsafe =
        prepare_model_asset(unsafe_asset, unsafe_options);

    const std::array<PreparedModelListEntry, 2> entries{{
        {&valid, Mat4::identity()},
        {&unsafe, Mat4::identity()},
    }};

    Framebuffer framebuffer(41U, 41U, SampleCount::Four);
    framebuffer.clear({0.12F, 0.23F, 0.34F}, 0.81F, 17U);
    const auto before = framebuffer.rgb8();
    check_throws<std::invalid_argument>(
        [&] {
            draw_prepared_model_list(
                framebuffer,
                entries,
                Mat4::identity(),
                Mat4::identity());
        },
        "later unsafe skinned prepared entry rejects complete heterogeneous list");
    check(
        framebuffer.rgb8() == before,
        "later unsafe skin rejects before earlier RGB ownership");
    for (std::size_t sample = 0U;
         sample < framebuffer.samples_per_pixel();
         ++sample) {
        check(
            framebuffer.sample_depth_at(20U, 20U, sample) == 0.81F,
            "later unsafe skin rejects before earlier depth ownership");
        check(
            framebuffer.sample_stencil_at(20U, 20U, sample) == 17U,
            "later unsafe skin rejects before earlier stencil ownership");
    }
}

void test_camera_and_shadow_share_skinned_silhouette() {
    const ModelAsset source = model_from_mesh(base_mesh());
    std::vector<VertexSkinBinding> bindings;
    for (std::size_t i = 0U; i < source.mesh.vertices.size(); ++i) {
        bindings.push_back(binding({SkinInfluence{0U, 1.0F}}));
    }
    const Vec3 offset{0.35F, -0.125F, 0.0F};
    ModelRenderOptions options;
    options.skinning_state = std::make_shared<const SkinningState>(
        std::move(bindings),
        std::vector<Mat4>{Mat4::translation(offset)});
    PreparedModelSubmission skinned =
        prepare_model_asset(source, options);

    Mesh manual_mesh = source.mesh;
    for (Vertex& v : manual_mesh.vertices) {
        v.position = v.position + offset;
    }
    PreparedModelSubmission manual =
        prepare_model_asset(model_from_mesh(std::move(manual_mesh)));

    const std::array<PreparedModelListEntry, 1> skinned_entry{{
        {&skinned, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> manual_entry{{
        {&manual, Mat4::identity()},
    }};

    const auto skinned_shadow = render_directional_shadow_map(
        skinned_entry,
        Mat4::identity(),
        DirectionalShadowMapOptions{
            41U,
            41U,
            CullMode::None,
            FrontFace::CounterClockwise,
        });
    const auto manual_shadow = render_directional_shadow_map(
        manual_entry,
        Mat4::identity(),
        DirectionalShadowMapOptions{
            41U,
            41U,
            CullMode::None,
            FrontFace::CounterClockwise,
        });
    check(
        skinned_shadow->width() == manual_shadow->width()
            && skinned_shadow->height() == manual_shadow->height(),
        "skinned and manual shadow resources preserve dimensions");
    for (std::size_t y = 0U; y < skinned_shadow->height(); ++y) {
        for (std::size_t x = 0U; x < skinned_shadow->width(); ++x) {
            check(
                skinned_shadow->depth_at(x, y)
                    == manual_shadow->depth_at(x, y),
                "shadow capture consumes the same skinned object-space silhouette as manual geometry");
        }
    }

    Framebuffer camera_skinned(41U, 41U);
    Framebuffer camera_manual(41U, 41U);
    camera_skinned.clear();
    camera_manual.clear();
    draw_prepared_model(
        camera_skinned,
        skinned,
        Mat4::identity());
    draw_prepared_model(
        camera_manual,
        manual,
        Mat4::identity());
    check_same_framebuffer(
        camera_skinned,
        camera_manual,
        "camera rendering consumes the same skinned object-space silhouette as shadow capture");
}

}  // namespace

int main() {
    test_identity_skin_exact_compatibility_and_prepared_lifetime();
    test_multi_joint_pose_matches_manual_geometry();
    test_skinning_precedes_vertex_program();
    test_validation_and_lighting_contract();
    test_prepared_list_fail_closed_on_later_unsafe_skin();
    test_camera_and_shadow_share_skinned_silhouette();

    if (failures != 0) {
        std::cerr << failures << " skinning test(s) failed\n";
        return 1;
    }
    std::cout << "skinning tests passed\n";
    return 0;
}
