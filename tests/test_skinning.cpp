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
#include "tiny_renderer/skeletal_timeline.hpp"
#include "tiny_renderer/skeletal_trs_timeline.hpp"
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
        vertex({-0.50F, -0.50F, 0.0F}),
        vertex({0.25F, -0.50F, 0.0F}),
        vertex({-0.125F, 0.50F, 0.0F}),
    };
    mesh.triangles = {{0U, 1U, 2U}};
    return mesh;
}


Vertex uv_normal_vertex(
    const Vec3& position,
    float u,
    float v,
    const Vec3& normal) {
    return Vertex::with_varyings(
        position,
        VaryingPack{u, v, normal.x, normal.y, normal.z});
}

Mesh lit_base_mesh() {
    Mesh mesh;
    const Vec3 normal{1.0F, 0.0F, 0.0F};
    mesh.vertices = {
        uv_normal_vertex(
            {-0.50F, -0.50F, 0.0F}, 0.0F, 0.0F, normal),
        uv_normal_vertex(
            {0.25F, -0.50F, 0.0F}, 1.0F, 0.0F, normal),
        uv_normal_vertex(
            {-0.125F, 0.50F, 0.0F}, 0.5F, 1.0F, normal),
    };
    mesh.triangles = {{0U, 1U, 2U}};
    return mesh;
}

DirectionalLight lit_directional_light(const Vec3& direction) {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {2U, 3U, 4U};
    light.direction_to_light = direction;
    light.ambient = 0.1F;
    light.diffuse = 0.9F;
    return light;
}

Mat4 exact_quarter_turn_z() {
    Mat4 result = Mat4::identity();
    result(0U, 0U) = 0.0F;
    result(0U, 1U) = -1.0F;
    result(1U, 0U) = 1.0F;
    result(1U, 1U) = 0.0F;
    return result;
}


Mat4 manual_affine_lerp(
    const Mat4& left,
    const Mat4& right,
    float t) {
    Mat4 result = Mat4::identity();
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            result(row, column) =
                left(row, column)
                + (right(row, column) - left(row, column)) * t;
        }
    }
    return result;
}

bool exact_matrix_equal(const Mat4& left, const Mat4& right) {
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            if (left(row, column) != right(row, column)) {
                return false;
            }
        }
    }
    return true;
}

bool exact_pose_equal(
    const SkeletalPoseState& left,
    const SkeletalPoseState& right) {
    const auto left_locals = left.local_transforms();
    const auto right_locals = right.local_transforms();
    if (left_locals.size() != right_locals.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < left_locals.size();
         ++index) {
        if (!exact_matrix_equal(
                left_locals[index],
                right_locals[index])) {
            return false;
        }
    }
    return true;
}

Mesh independently_materialize_skinned_lit_mesh(
    const Mesh& source,
    const SkinningState& skinning,
    const NormalBinding& normal_binding) {
    Mesh result = source;
    const auto bindings = skinning.vertex_bindings();
    const auto matrices = skinning.skin_matrices();
    std::vector<Mat3> normal_matrices;
    normal_matrices.reserve(matrices.size());
    for (const Mat4& matrix : matrices) {
        normal_matrices.push_back(normal_matrix(matrix));
    }

    for (std::size_t vertex_index = 0U;
         vertex_index < source.vertices.size();
         ++vertex_index) {
        const Vec3 source_position =
            source.vertices[vertex_index].position;
        const VaryingPack& pack =
            source.vertices[vertex_index].varyings;
        const Vec3 source_normal{
            pack.values[normal_binding.x],
            pack.values[normal_binding.y],
            pack.values[normal_binding.z],
        };

        double px = 0.0;
        double py = 0.0;
        double pz = 0.0;
        double nx = 0.0;
        double ny = 0.0;
        double nz = 0.0;
        double total = 0.0;
        for (const SkinInfluence influence :
             bindings[vertex_index].influences()) {
            const Vec4 position = matrices[influence.joint] * Vec4{
                source_position.x,
                source_position.y,
                source_position.z,
                1.0F,
            };
            const Vec3 normal =
                normal_matrices[influence.joint] * source_normal;
            const double weight =
                static_cast<double>(influence.weight);
            px += weight * static_cast<double>(position.x);
            py += weight * static_cast<double>(position.y);
            pz += weight * static_cast<double>(position.z);
            nx += weight * static_cast<double>(normal.x);
            ny += weight * static_cast<double>(normal.y);
            nz += weight * static_cast<double>(normal.z);
            total += weight;
        }

        result.vertices[vertex_index].position = {
            static_cast<float>(px / total),
            static_cast<float>(py / total),
            static_cast<float>(pz / total),
        };
        const double length_squared = nx * nx + ny * ny + nz * nz;
        const double inverse_length =
            1.0 / std::sqrt(length_squared);
        VaryingPack& output =
            result.vertices[vertex_index].varyings;
        output.values[normal_binding.x] =
            static_cast<float>(nx * inverse_length);
        output.values[normal_binding.y] =
            static_cast<float>(ny * inverse_length);
        output.values[normal_binding.z] =
            static_cast<float>(nz * inverse_length);
    }
    return result;
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


void test_identity_skin_with_fixed_lighting_is_exact() {
    const ModelAsset asset = model_from_mesh(lit_base_mesh());
    ModelRenderOptions baseline_options;
    baseline_options.directional_light =
        lit_directional_light({1.0F, 0.0F, 0.0F});
    ModelRenderOptions skinned_options = baseline_options;
    skinned_options.skinning_state =
        identity_skin(asset.mesh.vertices.size());

    Framebuffer baseline(47U, 47U, SampleCount::Four);
    Framebuffer skinned(47U, 47U, SampleCount::Four);
    baseline.clear({0.02F, 0.03F, 0.04F}, 1.0F, 3U);
    skinned.clear({0.02F, 0.03F, 0.04F}, 1.0F, 3U);
    draw_model_asset(
        baseline,
        asset,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        baseline_options);
    draw_model_asset(
        skinned,
        asset,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        skinned_options);
    check_same_framebuffer(
        baseline,
        skinned,
        "identity skin with active fixed lighting is exact-equivalent to canonical lighting");

    const PreparedModelSubmission prepared =
        prepare_model_asset(asset, skinned_options);
    Framebuffer prepared_fb(47U, 47U, SampleCount::Four);
    prepared_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 3U);
    draw_prepared_model(
        prepared_fb,
        prepared,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    check_same_framebuffer(
        baseline,
        prepared_fb,
        "prepared identity normal-aware skin preserves fixed-light output exactly");
}

void test_multi_joint_normal_skinning_matches_manual_and_normal_map() {
    const ModelAsset source = model_from_mesh(lit_base_mesh());
    Mat4 joint0 = Mat4::scale({2.0F, 1.0F, 0.5F});
    const Mat4 joint1 =
        exact_quarter_turn_z()
        * Mat4::scale({0.5F, 2.0F, 1.0F});

    std::vector<VertexSkinBinding> bindings;
    bindings.push_back(binding({SkinInfluence{0U, 1.0F}}));
    bindings.push_back(binding({SkinInfluence{1U, 1.0F}}));
    bindings.push_back(binding({
        SkinInfluence{0U, 0.5F},
        SkinInfluence{1U, 0.5F},
    }));
    const auto skin = std::make_shared<const SkinningState>(
        std::move(bindings),
        std::vector<Mat4>{joint0, joint1});

    const NormalBinding normal_binding{2U, 3U, 4U};
    ModelAsset manual = model_from_mesh(
        independently_materialize_skinned_lit_mesh(
            source.mesh,
            *skin,
            normal_binding));

    ModelRenderOptions skinned_options;
    skinned_options.skinning_state = skin;
    skinned_options.directional_light =
        lit_directional_light(
            normalize(Vec3{0.5F, 0.75F, 0.25F}));
    ModelRenderOptions manual_options = skinned_options;
    manual_options.skinning_state.reset();

    Framebuffer skinned_fb(53U, 53U, SampleCount::Four);
    Framebuffer manual_fb(53U, 53U, SampleCount::Four);
    skinned_fb.clear();
    manual_fb.clear();
    draw_model_asset(
        skinned_fb,
        source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        skinned_options);
    draw_model_asset(
        manual_fb,
        manual,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        manual_options);
    check_same_framebuffer(
        skinned_fb,
        manual_fb,
        "non-uniform multi-joint skinned positions and normals match independent manual materialization");

    const PreparedModelSubmission prepared =
        prepare_model_asset(source, skinned_options);
    Framebuffer prepared_fb(53U, 53U, SampleCount::Four);
    prepared_fb.clear();
    draw_prepared_model(
        prepared_fb,
        prepared,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    check_same_framebuffer(
        prepared_fb,
        manual_fb,
        "prepared multi-joint normal-aware skin matches manual lit geometry");

    const auto normal_map = std::make_shared<const Texture2D>(
        1U,
        1U,
        std::vector<Vec3>{{1.0F, 0.5F, 0.5F}});
    ModelAsset mapped_source = source;
    ModelAsset mapped_manual = manual;
    mapped_source.draws[0].normal_texture = normal_map;
    mapped_manual.draws[0].normal_texture = normal_map;

    Framebuffer mapped_skin(53U, 53U, SampleCount::Four);
    Framebuffer mapped_manual_fb(53U, 53U, SampleCount::Four);
    mapped_skin.clear();
    mapped_manual_fb.clear();
    draw_model_asset(
        mapped_skin,
        mapped_source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        skinned_options);
    draw_model_asset(
        mapped_manual_fb,
        mapped_manual,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        manual_options);
    check_same_framebuffer(
        mapped_skin,
        mapped_manual_fb,
        "normal mapping derives tangent frames from skinned geometry and consumes skinned geometric normals");
}

void test_normal_skinning_fail_closed_contract() {
    const ModelAsset source = model_from_mesh(lit_base_mesh());
    const ModelRenderOptions base_options = [] {
        ModelRenderOptions options;
        options.directional_light =
            lit_directional_light({1.0F, 0.0F, 0.0F});
        return options;
    }();

    {
        ModelRenderOptions bad_binding = base_options;
        bad_binding.directional_light.normal = {2U, 3U, 9U};
        bad_binding.skinning_state =
            identity_skin(source.mesh.vertices.size());
        const PreparedModelSubmission prepared =
            prepare_model_asset(source, bad_binding);
        Framebuffer framebuffer(31U, 31U);
        framebuffer.clear({0.1F, 0.2F, 0.3F}, 0.8F, 11U);
        const auto before = framebuffer.rgb8();
        check_throws<std::out_of_range>(
            [&] {
                draw_prepared_model(
                    framebuffer,
                    prepared,
                    Mat4::identity(),
                    Mat4::identity(),
                    Mat4::identity());
            },
            "normal-aware skinning rejects an out-of-range active normal binding");
        check(
            framebuffer.rgb8() == before,
            "invalid skinned normal binding rejects before framebuffer color ownership");
    }

    {
        Mat4 singular = Mat4::scale({1.0F, 1.0F, 0.0F});
        std::vector<VertexSkinBinding> bindings;
        for (std::size_t i = 0U;
             i < source.mesh.vertices.size();
             ++i) {
            bindings.push_back(
                binding({SkinInfluence{0U, 1.0F}}));
        }
        ModelRenderOptions singular_options = base_options;
        singular_options.skinning_state =
            std::make_shared<const SkinningState>(
                std::move(bindings),
                std::vector<Mat4>{singular});

        ModelRenderOptions valid_options = base_options;
        valid_options.skinning_state =
            identity_skin(source.mesh.vertices.size());
        const PreparedModelSubmission valid =
            prepare_model_asset(source, valid_options);
        const PreparedModelSubmission invalid =
            prepare_model_asset(source, singular_options);
        const std::array<PreparedModelListEntry, 2> entries{{
            {&valid, Mat4::identity()},
            {&invalid, Mat4::identity()},
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
            "later singular joint normal matrix rejects complete prepared list");
        check(
            framebuffer.rgb8() == before,
            "later singular normal matrix rejects before earlier color ownership");
        for (std::size_t sample = 0U;
             sample < framebuffer.samples_per_pixel();
             ++sample) {
            check(
                framebuffer.sample_depth_at(20U, 20U, sample)
                    == 0.81F,
                "later singular normal matrix rejects before earlier depth ownership");
            check(
                framebuffer.sample_stencil_at(20U, 20U, sample)
                    == 17U,
                "later singular normal matrix rejects before earlier stencil ownership");
        }
    }

    {
        Mat4 reflected = Mat4::identity();
        reflected(0U, 0U) = -1.0F;
        std::vector<VertexSkinBinding> bindings;
        for (std::size_t i = 0U;
             i < source.mesh.vertices.size();
             ++i) {
            bindings.push_back(binding({
                SkinInfluence{0U, 0.5F},
                SkinInfluence{1U, 0.5F},
            }));
        }
        ModelRenderOptions cancelling = base_options;
        cancelling.skinning_state =
            std::make_shared<const SkinningState>(
                std::move(bindings),
                std::vector<Mat4>{
                    Mat4::identity(),
                    reflected,
                });
        Framebuffer framebuffer(31U, 31U);
        framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.9F, 5U);
        const auto before = framebuffer.rgb8();
        check_throws<std::invalid_argument>(
            [&] {
                draw_model_asset(
                    framebuffer,
                    source,
                    Mat4::identity(),
                    Mat4::identity(),
                    Mat4::identity(),
                    cancelling);
            },
            "weighted joint normals that cancel to zero are rejected");
        check(
            framebuffer.rgb8() == before,
            "zero weighted skinned normal rejects before color ownership");
    }
}


void test_skeletal_rig_validation_contract() {
    const auto one_binding = [] {
        return std::vector<VertexSkinBinding>{
            binding({SkinInfluence{0U, 1.0F}}),
        };
    };

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalRig(
                std::vector<std::optional<std::size_t>>{},
                std::vector<Mat4>{},
                one_binding());
        },
        "skeletal rig rejects an empty joint topology");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalRig(
                std::vector<std::optional<std::size_t>>(
                    kMaxSkinJoints + 1U,
                    std::nullopt),
                std::vector<Mat4>(
                    kMaxSkinJoints + 1U,
                    Mat4::identity()),
                one_binding());
        },
        "skeletal rig enforces the bounded joint limit");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalRig(
                {std::nullopt, 0U},
                {Mat4::identity()},
                one_binding());
        },
        "skeletal rig requires one inverse bind per joint");

    check_throws<std::out_of_range>(
        [&] {
            (void)SkeletalRig(
                {2U, std::nullopt},
                {Mat4::identity(), Mat4::identity()},
                one_binding());
        },
        "skeletal rig rejects out-of-range parents");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalRig(
                {0U},
                {Mat4::identity()},
                one_binding());
        },
        "skeletal rig rejects self-parenting");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalRig(
                {1U, 0U},
                {Mat4::identity(), Mat4::identity()},
                one_binding());
        },
        "skeletal rig rejects parent cycles");

    Mat4 projective = Mat4::identity();
    projective(3U, 0U) = 0.25F;
    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalRig(
                {std::nullopt},
                {projective},
                one_binding());
        },
        "skeletal rig rejects projective inverse binds");

    check_throws<std::out_of_range>(
        [&] {
            (void)SkeletalRig(
                {std::nullopt},
                {Mat4::identity()},
                std::vector<VertexSkinBinding>{
                    binding({SkinInfluence{1U, 1.0F}}),
                });
        },
        "skeletal rig validates vertex influence ownership");

    const auto rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{std::nullopt},
        std::vector<Mat4>{Mat4::identity()},
        one_binding());

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalPoseState(
                SkeletalRigPtr{},
                {Mat4::identity()});
        },
        "skeletal pose requires an immutable rig");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalPoseState(rig, {});
        },
        "skeletal pose local count must match rig joint count");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalPoseState(rig, {projective});
        },
        "skeletal pose rejects projective local transforms");

    const ModelAsset asset = model_from_mesh(base_mesh());
    ModelRenderOptions both;
    both.skinning_state =
        identity_skin(asset.mesh.vertices.size());
    both.skeletal_pose_state =
        std::make_shared<const SkeletalPoseState>(
            rig,
            std::vector<Mat4>{Mat4::identity()});
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_model_asset(asset, both);
        },
        "model submission rejects simultaneous direct and skeletal skinning");

    ModelRenderOptions wrong_cardinality;
    wrong_cardinality.skeletal_pose_state =
        std::make_shared<const SkeletalPoseState>(
            rig,
            std::vector<Mat4>{Mat4::identity()});
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_model_asset(asset, wrong_cardinality);
        },
        "skeletal rig binding count must match canonical mesh vertex count");

    const auto composed_overflow_rig =
        std::make_shared<const SkeletalRig>(
            std::vector<std::optional<std::size_t>>{
                std::nullopt,
                0U,
            },
            std::vector<Mat4>{
                Mat4::identity(),
                Mat4::identity(),
            },
            std::vector<VertexSkinBinding>{
                binding({SkinInfluence{0U, 1.0F}}),
            });
    Mat4 huge = Mat4::identity();
    huge(0U, 0U) = 1.0e20F;
    const std::array<Mat4, 2> composed_overflow_pose{
        huge,
        huge,
    };
    check_throws<std::invalid_argument>(
        [&] {
            (void)composed_overflow_rig->resolve_pose(
                composed_overflow_pose);
        },
        "skeletal pose rejects finite locals whose parent composition overflows");

    const auto final_overflow_rig =
        std::make_shared<const SkeletalRig>(
            std::vector<std::optional<std::size_t>>{std::nullopt},
            std::vector<Mat4>{huge},
            one_binding());
    const std::array<Mat4, 1> final_overflow_pose{huge};
    check_throws<std::invalid_argument>(
        [&] {
            (void)final_overflow_rig->resolve_pose(
                final_overflow_pose);
        },
        "skeletal pose rejects world times inverse-bind overflow");
}

void test_skeletal_pose_matches_precomposed_m107_skinning() {
    const ModelAsset source = model_from_mesh(lit_base_mesh());

    {
        std::vector<VertexSkinBinding> bindings;
        for (std::size_t i = 0U;
             i < source.mesh.vertices.size();
             ++i) {
            bindings.push_back(
                binding({SkinInfluence{0U, 1.0F}}));
        }
        const Mat4 root =
            Mat4::translation({0.125F, -0.25F, 0.0F})
            * Mat4::scale({2.0F, 1.0F, 0.5F});
        const auto rig = std::make_shared<const SkeletalRig>(
            std::vector<std::optional<std::size_t>>{std::nullopt},
            std::vector<Mat4>{Mat4::identity()},
            bindings);

        ModelRenderOptions skeletal;
        skeletal.directional_light =
            lit_directional_light(
                normalize(Vec3{0.75F, 0.5F, 0.25F}));
        skeletal.skeletal_pose_state =
            std::make_shared<const SkeletalPoseState>(
                rig,
                std::vector<Mat4>{root});

        ModelRenderOptions direct = skeletal;
        direct.skeletal_pose_state.reset();
        direct.skinning_state =
            std::make_shared<const SkinningState>(
                bindings,
                std::vector<Mat4>{root});

        Framebuffer skeletal_fb(53U, 53U, SampleCount::Four);
        Framebuffer direct_fb(53U, 53U, SampleCount::Four);
        skeletal_fb.clear({0.01F, 0.02F, 0.03F}, 1.0F, 1U);
        direct_fb.clear({0.01F, 0.02F, 0.03F}, 1.0F, 1U);
        draw_model_asset(
            skeletal_fb,
            source,
            Mat4::identity(),
            Mat4::identity(),
            Mat4::identity(),
            skeletal);
        draw_model_asset(
            direct_fb,
            source,
            Mat4::identity(),
            Mat4::identity(),
            Mat4::identity(),
            direct);
        check_same_framebuffer(
            skeletal_fb,
            direct_fb,
            "root-only skeletal resolution is exact-equivalent to precomposed M107 skin matrices");
    }

    std::vector<VertexSkinBinding> chain_bindings{
        binding({SkinInfluence{0U, 1.0F}}),
        binding({SkinInfluence{1U, 1.0F}}),
        binding({SkinInfluence{2U, 1.0F}}),
    };
    const std::vector<std::optional<std::size_t>> parents{
        2U,
        std::nullopt,
        1U,
    };
    const std::vector<Mat4> locals{
        Mat4::translation({0.125F, 0.0F, 0.0F}),
        Mat4::scale({1.0F, 2.0F, 1.0F}),
        exact_quarter_turn_z(),
    };
    const Mat4 world1 = locals[1];
    const Mat4 world2 = world1 * locals[2];
    const Mat4 world0 = world2 * locals[0];

    auto rig = std::make_shared<const SkeletalRig>(
        parents,
        std::vector<Mat4>{
            Mat4::identity(),
            Mat4::identity(),
            Mat4::identity(),
        },
        chain_bindings);
    auto pose = std::make_shared<const SkeletalPoseState>(
        rig,
        locals);

    ModelRenderOptions skeletal;
    skeletal.directional_light =
        lit_directional_light(
            normalize(Vec3{0.25F, 0.75F, 0.5F}));
    skeletal.skeletal_pose_state = pose;

    ModelRenderOptions direct = skeletal;
    direct.skeletal_pose_state.reset();
    direct.skinning_state =
        std::make_shared<const SkinningState>(
            chain_bindings,
            std::vector<Mat4>{world0, world1, world2});

    const PreparedModelSubmission prepared_skeletal =
        prepare_model_asset(source, skeletal);
    const PreparedModelSubmission prepared_direct =
        prepare_model_asset(source, direct);

    rig.reset();
    pose.reset();
    skeletal.skeletal_pose_state.reset();

    Framebuffer skeletal_fb(55U, 55U, SampleCount::Four);
    Framebuffer direct_fb(55U, 55U, SampleCount::Four);
    skeletal_fb.clear();
    direct_fb.clear();
    draw_prepared_model(
        skeletal_fb,
        prepared_skeletal,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    draw_prepared_model(
        direct_fb,
        prepared_direct,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    check_same_framebuffer(
        skeletal_fb,
        direct_fb,
        "arbitrary-order skeletal chain matches independently precomposed M107 skin matrices after caller rig lifetime ends");

    const std::array<PreparedModelListEntry, 1> skeletal_entry{{
        {&prepared_skeletal, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> direct_entry{{
        {&prepared_direct, Mat4::identity()},
    }};
    const auto skeletal_shadow = render_directional_shadow_map(
        skeletal_entry,
        Mat4::identity(),
        DirectionalShadowMapOptions{
            41U,
            41U,
            CullMode::None,
            FrontFace::CounterClockwise,
        });
    const auto direct_shadow = render_directional_shadow_map(
        direct_entry,
        Mat4::identity(),
        DirectionalShadowMapOptions{
            41U,
            41U,
            CullMode::None,
            FrontFace::CounterClockwise,
        });
    for (std::size_t y = 0U; y < skeletal_shadow->height(); ++y) {
        for (std::size_t x = 0U; x < skeletal_shadow->width(); ++x) {
            check(
                skeletal_shadow->depth_at(x, y)
                    == direct_shadow->depth_at(x, y),
                "skeletal shadow silhouette matches precomposed M107 skin state");
        }
    }
}

void test_skeletal_bind_pose_reduces_to_identity_skinning() {
    const ModelAsset source = model_from_mesh(lit_base_mesh());
    const std::vector<VertexSkinBinding> bindings{
        binding({SkinInfluence{0U, 1.0F}}),
        binding({SkinInfluence{1U, 1.0F}}),
        binding({
            SkinInfluence{0U, 0.5F},
            SkinInfluence{1U, 0.5F},
        }),
    };

    const Mat4 root_local =
        Mat4::translation({0.25F, 0.0F, 0.0F});
    const Mat4 child_local =
        Mat4::translation({0.125F, 0.0F, 0.0F});
    const auto rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{
            std::nullopt,
            0U,
        },
        std::vector<Mat4>{
            Mat4::translation({-0.25F, 0.0F, 0.0F}),
            Mat4::translation({-0.375F, 0.0F, 0.0F}),
        },
        bindings);

    ModelRenderOptions skeletal;
    skeletal.directional_light =
        lit_directional_light({1.0F, 0.0F, 0.0F});
    skeletal.skeletal_pose_state =
        std::make_shared<const SkeletalPoseState>(
            rig,
            std::vector<Mat4>{root_local, child_local});

    ModelRenderOptions canonical = skeletal;
    canonical.skeletal_pose_state.reset();

    Framebuffer skeletal_fb(47U, 47U, SampleCount::Four);
    Framebuffer canonical_fb(47U, 47U, SampleCount::Four);
    skeletal_fb.clear({0.04F, 0.03F, 0.02F}, 1.0F, 2U);
    canonical_fb.clear({0.04F, 0.03F, 0.02F}, 1.0F, 2U);
    draw_model_asset(
        skeletal_fb,
        source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        skeletal);
    draw_model_asset(
        canonical_fb,
        source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        canonical);
    check_same_framebuffer(
        skeletal_fb,
        canonical_fb,
        "bind-compatible local pose times inverse binds reduces exactly to canonical geometry");
}

void test_skeletal_prepared_list_fail_closed_on_later_pose_overflow() {
    const ModelAsset source = model_from_mesh(base_mesh());
    std::vector<VertexSkinBinding> bindings;
    for (std::size_t i = 0U;
         i < source.mesh.vertices.size();
         ++i) {
        bindings.push_back(
            binding({SkinInfluence{0U, 1.0F}}));
    }

    const auto valid_rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{std::nullopt},
        std::vector<Mat4>{Mat4::identity()},
        bindings);
    ModelRenderOptions valid_options;
    valid_options.skeletal_pose_state =
        std::make_shared<const SkeletalPoseState>(
            valid_rig,
            std::vector<Mat4>{Mat4::identity()});
    const PreparedModelSubmission valid =
        prepare_model_asset(source, valid_options);

    Mat4 huge = Mat4::identity();
    huge(0U, 0U) = 1.0e20F;
    const auto invalid_rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{std::nullopt},
        std::vector<Mat4>{huge},
        bindings);
    ModelRenderOptions invalid_options;
    invalid_options.skeletal_pose_state =
        std::make_shared<const SkeletalPoseState>(
            invalid_rig,
            std::vector<Mat4>{huge});
    const PreparedModelSubmission invalid =
        prepare_model_asset(source, invalid_options);

    const std::array<PreparedModelListEntry, 2> entries{{
        {&valid, Mat4::identity()},
        {&invalid, Mat4::identity()},
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
        "later skeletal world-times-inverse-bind overflow rejects complete prepared list");
    check(
        framebuffer.rgb8() == before,
        "later skeletal pose overflow rejects before earlier RGB ownership");
    for (std::size_t sample = 0U;
         sample < framebuffer.samples_per_pixel();
         ++sample) {
        check(
            framebuffer.sample_depth_at(20U, 20U, sample)
                == 0.81F,
            "later skeletal pose overflow rejects before earlier depth ownership");
        check(
            framebuffer.sample_stencil_at(20U, 20U, sample)
                == 17U,
            "later skeletal pose overflow rejects before earlier stencil ownership");
    }

    const std::array<PreparedModelListEntry, 1> painter_entries{{
        {&valid, Mat4::identity()},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)order_prepared_model_list_back_to_front(
                painter_entries,
                Mat4::identity());
        },
        "canonical painter ordering rejects deferred skeletal poses");

    const PreparedSpatialSubmission spatial =
        prepare_spatial_submission(valid);
    const std::array<PreparedSpatialListEntry, 1> spatial_entries{{
        {&spatial, Mat4::identity()},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)flatten_prepared_model_draws(
                spatial_entries,
                Mat4::identity());
        },
        "prepared spatial planning rejects deferred skeletal poses");
}


void test_skeletal_timeline_validation_and_determinism() {
    const auto rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{std::nullopt},
        std::vector<Mat4>{Mat4::identity()},
        std::vector<VertexSkinBinding>{
            binding({SkinInfluence{0U, 1.0F}}),
        });

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalPoseTimeline(
                SkeletalRigPtr{},
                std::vector<SkeletalPoseTimelineKeyframe>{
                    {0.0F, {Mat4::identity()}},
                    {1.0F, {Mat4::identity()}},
                });
        },
        "skeletal timeline requires an immutable rig");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalPoseTimeline(
                rig,
                std::vector<SkeletalPoseTimelineKeyframe>{});
        },
        "skeletal timeline rejects fewer than two keyframes");

    std::vector<SkeletalPoseTimelineKeyframe> too_many;
    too_many.reserve(kMaxSkeletalTimelineKeyframes + 1U);
    for (std::size_t index = 0U;
         index <= kMaxSkeletalTimelineKeyframes;
         ++index) {
        too_many.push_back({
            static_cast<float>(index),
            {Mat4::identity()},
        });
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalPoseTimeline(rig, too_many);
        },
        "skeletal timeline bounds keyframe count");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalPoseTimeline(
                rig,
                std::vector<SkeletalPoseTimelineKeyframe>{
                    {
                        std::numeric_limits<float>::quiet_NaN(),
                        {Mat4::identity()},
                    },
                    {1.0F, {Mat4::identity()}},
                });
        },
        "skeletal timeline rejects non-finite keyframe time");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalPoseTimeline(
                rig,
                std::vector<SkeletalPoseTimelineKeyframe>{
                    {0.0F, {Mat4::identity()}},
                    {0.0F, {Mat4::identity()}},
                });
        },
        "skeletal timeline requires strictly increasing keyframe times");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalPoseTimeline(
                rig,
                std::vector<SkeletalPoseTimelineKeyframe>{
                    {0.0F, {}},
                    {1.0F, {Mat4::identity()}},
                });
        },
        "skeletal timeline keyframe local count must match rig");

    Mat4 projective = Mat4::identity();
    projective(3U, 2U) = 0.25F;
    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalPoseTimeline(
                rig,
                std::vector<SkeletalPoseTimelineKeyframe>{
                    {0.0F, {Mat4::identity()}},
                    {1.0F, {projective}},
                });
        },
        "skeletal timeline rejects projective keyframe locals");

    const Mat4 start =
        Mat4::translation({0.125F, -0.25F, 0.0F});
    const Mat4 finish =
        Mat4::translation({0.625F, 0.25F, 0.0F})
        * Mat4::scale({2.0F, 1.0F, 0.5F});
    const SkeletalPoseTimeline timeline(
        rig,
        std::vector<SkeletalPoseTimelineKeyframe>{
            {0.0F, {start}},
            {1.0F, {finish}},
        });

    const std::array<float, 4> sample_times{
        1.0F,
        0.5F,
        0.0F,
        0.5F,
    };
    const auto sampled = timeline.sample(sample_times);
    check(
        sampled.size() == sample_times.size(),
        "skeletal timeline preserves caller sample order and multiplicity");
    check(
        exact_matrix_equal(
            sampled[0]->local_transforms()[0],
            finish),
        "exact final-key sample preserves stored local matrix bit-for-bit");
    check(
        exact_matrix_equal(
            sampled[2]->local_transforms()[0],
            start),
        "exact first-key sample preserves stored local matrix bit-for-bit");
    check(
        exact_pose_equal(*sampled[1], *sampled[3]),
        "repeated skeletal timeline samples are deterministic");
    const Mat4 expected_mid =
        manual_affine_lerp(start, finish, 0.5F);
    check(
        exact_matrix_equal(
            sampled[1]->local_transforms()[0],
            expected_mid),
        "interior skeletal timeline sample matches independent affine top-3x4 interpolation");

    const std::array<float, 1> nonfinite_sample{
        std::numeric_limits<float>::infinity(),
    };
    check_throws<std::invalid_argument>(
        [&] {
            (void)timeline.sample(nonfinite_sample);
        },
        "skeletal timeline rejects non-finite sample time");

    const std::array<float, 1> before_domain{-0.25F};
    check_throws<std::out_of_range>(
        [&] {
            (void)timeline.sample(before_domain);
        },
        "skeletal timeline rejects sample before domain");

    const std::array<float, 1> after_domain{1.25F};
    check_throws<std::out_of_range>(
        [&] {
            (void)timeline.sample(after_domain);
        },
        "skeletal timeline rejects sample after domain");

    std::vector<float> too_many_samples(
        kMaxSkeletalTimelineSamples + 1U,
        0.5F);
    check_throws<std::invalid_argument>(
        [&] {
            (void)timeline.sample(too_many_samples);
        },
        "skeletal timeline bounds sample count");
}

void test_skeletal_timeline_matches_manual_m108_execution() {
    const ModelAsset source = model_from_mesh(lit_base_mesh());
    std::vector<VertexSkinBinding> bindings;
    for (std::size_t vertex_index = 0U;
         vertex_index < source.mesh.vertices.size();
         ++vertex_index) {
        bindings.push_back(
            binding({SkinInfluence{0U, 1.0F}}));
    }
    const auto rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{std::nullopt},
        std::vector<Mat4>{Mat4::identity()},
        bindings);

    const Mat4 left =
        Mat4::translation({-0.25F, 0.0F, 0.0F})
        * Mat4::scale({1.0F, 2.0F, 1.0F});
    const Mat4 right =
        Mat4::translation({0.25F, 0.25F, 0.0F})
        * Mat4::scale({2.0F, 1.0F, 0.5F});
    const SkeletalPoseTimeline timeline(
        rig,
        std::vector<SkeletalPoseTimelineKeyframe>{
            {0.0F, {left}},
            {1.0F, {right}},
        });

    const std::array<float, 4> sample_times{
        1.0F,
        0.5F,
        0.0F,
        0.5F,
    };
    const auto sampled = timeline.sample(sample_times);

    for (std::size_t sample_index = 0U;
         sample_index < sample_times.size();
         ++sample_index) {
        const float time = sample_times[sample_index];
        const Mat4 manual_local =
            time == 0.0F
                ? left
                : (time == 1.0F
                    ? right
                    : manual_affine_lerp(left, right, time));
        auto manual_pose =
            std::make_shared<const SkeletalPoseState>(
                rig,
                std::vector<Mat4>{manual_local});

        ModelRenderOptions timeline_options;
        timeline_options.directional_light =
            lit_directional_light(
                normalize(Vec3{0.5F, 0.75F, 0.25F}));
        timeline_options.skeletal_pose_state =
            sampled[sample_index];

        ModelRenderOptions manual_options =
            timeline_options;
        manual_options.skeletal_pose_state =
            std::move(manual_pose);

        Framebuffer timeline_fb(51U, 51U, SampleCount::Four);
        Framebuffer manual_fb(51U, 51U, SampleCount::Four);
        timeline_fb.clear(
            {0.01F, 0.02F, 0.03F},
            1.0F,
            4U);
        manual_fb.clear(
            {0.01F, 0.02F, 0.03F},
            1.0F,
            4U);
        draw_model_asset(
            timeline_fb,
            source,
            Mat4::identity(),
            Mat4::identity(),
            Mat4::identity(),
            timeline_options);
        draw_model_asset(
            manual_fb,
            source,
            Mat4::identity(),
            Mat4::identity(),
            Mat4::identity(),
            manual_options);
        check_same_framebuffer(
            timeline_fb,
            manual_fb,
            "skeletal timeline sample renders exactly like independently sampled-local M108 pose");
    }

    ModelRenderOptions timeline_shadow_options;
    timeline_shadow_options.skeletal_pose_state = sampled[1];
    ModelRenderOptions manual_shadow_options;
    manual_shadow_options.skeletal_pose_state =
        std::make_shared<const SkeletalPoseState>(
            rig,
            std::vector<Mat4>{
                manual_affine_lerp(left, right, 0.5F),
            });
    const PreparedModelSubmission timeline_prepared =
        prepare_model_asset(source, timeline_shadow_options);
    const PreparedModelSubmission manual_prepared =
        prepare_model_asset(source, manual_shadow_options);
    const std::array<PreparedModelListEntry, 1> timeline_entry{{
        {&timeline_prepared, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> manual_entry{{
        {&manual_prepared, Mat4::identity()},
    }};
    const auto timeline_shadow = render_directional_shadow_map(
        timeline_entry,
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
    for (std::size_t y = 0U; y < timeline_shadow->height(); ++y) {
        for (std::size_t x = 0U; x < timeline_shadow->width(); ++x) {
            check(
                timeline_shadow->depth_at(x, y)
                    == manual_shadow->depth_at(x, y),
                "skeletal timeline shadow matches independently sampled-local M108 pose");
        }
    }
}

void test_skeletal_timeline_interpolates_local_before_world() {
    const ModelAsset source = model_from_mesh(lit_base_mesh());
    // Joint 0 is the child while joint 1 is its root, deliberately declaring
    // the parent after the child to lock arbitrary-order hierarchy semantics.
    const std::vector<VertexSkinBinding> bindings{
        binding({SkinInfluence{1U, 1.0F}}),
        binding({SkinInfluence{0U, 1.0F}}),
        binding({SkinInfluence{0U, 1.0F}}),
    };
    const auto rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{
            1U,
            std::nullopt,
        },
        std::vector<Mat4>{
            Mat4::identity(),
            Mat4::identity(),
        },
        bindings);

    const Mat4 child_left =
        Mat4::translation({0.0F, 0.0F, 0.0F});
    const Mat4 parent_left =
        Mat4::scale({1.0F, 1.0F, 1.0F});
    const Mat4 child_right =
        Mat4::translation({2.0F, 0.0F, 0.0F});
    const Mat4 parent_right =
        Mat4::scale({3.0F, 1.0F, 1.0F});

    const SkeletalPoseTimeline timeline(
        rig,
        std::vector<SkeletalPoseTimelineKeyframe>{
            {
                0.0F,
                {child_left, parent_left},
            },
            {
                1.0F,
                {child_right, parent_right},
            },
        });
    const std::array<float, 1> midpoint_time{0.5F};
    const auto sampled = timeline.sample(midpoint_time);
    const SkinningStatePtr resolved =
        sampled.front()->resolve();
    const auto skin_matrices = resolved->skin_matrices();

    check(
        skin_matrices.size() == 2U,
        "skeletal timeline midpoint resolves complete arbitrary-order joint palette");
    if (skin_matrices.size() == 2U) {
        check(
            skin_matrices[0](0U, 3U) == 2.0F,
            "skeletal timeline interpolates child and later-declared parent locals before hierarchy composition");
        check(
            skin_matrices[0](0U, 3U) != 3.0F,
            "skeletal timeline does not interpolate endpoint child world transforms");
    }

    const Mat4 manual_child =
        manual_affine_lerp(
            child_left,
            child_right,
            0.5F);
    const Mat4 manual_parent =
        manual_affine_lerp(
            parent_left,
            parent_right,
            0.5F);

    ModelRenderOptions timeline_options;
    timeline_options.directional_light =
        lit_directional_light(
            normalize(Vec3{0.5F, 0.75F, 0.25F}));
    timeline_options.skeletal_pose_state =
        sampled.front();
    ModelRenderOptions manual_options =
        timeline_options;
    manual_options.skeletal_pose_state =
        std::make_shared<const SkeletalPoseState>(
            rig,
            std::vector<Mat4>{
                manual_child,
                manual_parent,
            });

    Framebuffer timeline_fb(49U, 49U, SampleCount::Four);
    Framebuffer manual_fb(49U, 49U, SampleCount::Four);
    timeline_fb.clear();
    manual_fb.clear();
    draw_model_asset(
        timeline_fb,
        source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        timeline_options);
    draw_model_asset(
        manual_fb,
        source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        manual_options);
    check_same_framebuffer(
        timeline_fb,
        manual_fb,
        "arbitrary-order skeletal timeline local-before-world midpoint matches independent normal-aware M108 reference");

    const PreparedModelSubmission timeline_prepared =
        prepare_model_asset(source, timeline_options);
    const PreparedModelSubmission manual_prepared =
        prepare_model_asset(source, manual_options);
    const std::array<PreparedModelListEntry, 1> timeline_entry{{
        {&timeline_prepared, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> manual_entry{{
        {&manual_prepared, Mat4::identity()},
    }};
    const auto timeline_shadow = render_directional_shadow_map(
        timeline_entry,
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
    for (std::size_t y = 0U; y < timeline_shadow->height(); ++y) {
        for (std::size_t x = 0U; x < timeline_shadow->width(); ++x) {
            check(
                timeline_shadow->depth_at(x, y)
                    == manual_shadow->depth_at(x, y),
                "arbitrary-order skeletal timeline shadow matches independent local-before-world M108 reference");
        }
    }
}

void test_skeletal_timeline_later_midpoint_overflow_is_batch_fail_closed() {
    std::vector<VertexSkinBinding> bindings{
        binding({SkinInfluence{0U, 1.0F}}),
    };
    const auto rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{
            std::nullopt,
            0U,
        },
        std::vector<Mat4>{
            Mat4::identity(),
            Mat4::identity(),
        },
        bindings);

    // M^2 remains finite, while 1.25*M^2 exceeds float max. Endpoint
    // parent*child products below have coefficients no larger than 1.0, but
    // local-space midpoint interpolation creates a 1.25 coefficient in one
    // composed world entry. This isolates overflow to the interior sample.
    constexpr float magnitude = 1.7e19F;

    Mat4 parent_left = Mat4::identity();
    parent_left(0U, 0U) = -magnitude;
    parent_left(0U, 1U) = 0.0F;
    parent_left(1U, 0U) = -magnitude;
    parent_left(1U, 1U) = -magnitude;
    Mat4 child_left = Mat4::identity();
    child_left(0U, 0U) = -magnitude;
    child_left(0U, 1U) = 0.0F;
    child_left(1U, 0U) = 0.0F;
    child_left(1U, 1U) = 0.0F;

    Mat4 parent_right = Mat4::identity();
    parent_right(0U, 0U) = -magnitude;
    parent_right(0U, 1U) = 0.0F;
    parent_right(1U, 0U) = -magnitude;
    parent_right(1U, 1U) = 0.0F;
    Mat4 child_right = Mat4::identity();
    child_right(0U, 0U) = -magnitude;
    child_right(0U, 1U) = magnitude;
    child_right(1U, 0U) = -magnitude;
    child_right(1U, 1U) = magnitude;

    // Construction resolves both endpoint poses, proving they are valid.
    const SkeletalPoseTimeline timeline(
        rig,
        std::vector<SkeletalPoseTimelineKeyframe>{
            {
                0.0F,
                {parent_left, child_left},
            },
            {
                1.0F,
                {parent_right, child_right},
            },
        });

    Framebuffer framebuffer(31U, 31U, SampleCount::Four);
    framebuffer.clear(
        {0.17F, 0.27F, 0.37F},
        0.73F,
        19U);
    const auto before = framebuffer.rgb8();
    const std::array<float, 2> samples{
        0.0F,
        0.5F,
    };
    check_throws<std::invalid_argument>(
        [&] {
            const auto poses = timeline.sample(samples);
            for (const SkeletalPoseStatePtr& pose : poses) {
                ModelRenderOptions options;
                options.skeletal_pose_state = pose;
                draw_model_asset(
                    framebuffer,
                    model_from_mesh(base_mesh()),
                    Mat4::identity(),
                    options);
            }
        },
        "later skeletal timeline midpoint hierarchy overflow rejects complete sample batch");
    check(
        framebuffer.rgb8() == before,
        "later skeletal timeline midpoint failure occurs before earlier requested sample can own framebuffer color");
    for (std::size_t sample = 0U;
         sample < framebuffer.samples_per_pixel();
         ++sample) {
        check(
            framebuffer.sample_depth_at(15U, 15U, sample)
                == 0.73F,
            "later skeletal timeline midpoint failure occurs before depth ownership");
        check(
            framebuffer.sample_stencil_at(15U, 15U, sample)
                == 19U,
            "later skeletal timeline midpoint failure occurs before stencil ownership");
    }
}


Quaternion quarter_turn_z_quaternion() {
    constexpr float root_half = 0.70710678118654752440F;
    return {0.0F, 0.0F, root_half, root_half};
}

Mat4 independent_quaternion_matrix(const Quaternion& quaternion) {
    const float x = quaternion.x;
    const float y = quaternion.y;
    const float z = quaternion.z;
    const float w = quaternion.w;
    Mat4 result = Mat4::identity();
    result(0U, 0U) = 1.0F - 2.0F * (y * y + z * z);
    result(0U, 1U) = 2.0F * (x * y - z * w);
    result(0U, 2U) = 2.0F * (x * z + y * w);
    result(1U, 0U) = 2.0F * (x * y + z * w);
    result(1U, 1U) = 1.0F - 2.0F * (x * x + z * z);
    result(1U, 2U) = 2.0F * (y * z - x * w);
    result(2U, 0U) = 2.0F * (x * z - y * w);
    result(2U, 1U) = 2.0F * (y * z + x * w);
    result(2U, 2U) = 1.0F - 2.0F * (x * x + y * y);
    return result;
}

void test_skeletal_trs_validation_sampling_and_shortest_path() {
    const auto rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{std::nullopt},
        std::vector<Mat4>{Mat4::identity()},
        std::vector<VertexSkinBinding>{
            binding({SkinInfluence{0U, 1.0F}}),
        });

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalTrsClip(
                SkeletalRigPtr{},
                0.0F,
                1.0F,
                {SkeletalTrs{}},
                {},
                {},
                {});
        },
        "semantic TRS clip requires an immutable rig");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalTrsClip(
                rig,
                1.0F,
                1.0F,
                {SkeletalTrs{}},
                {},
                {},
                {});
        },
        "semantic TRS clip requires a strictly increasing finite domain");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalTrsClip(
                rig,
                0.0F,
                1.0F,
                {},
                {},
                {},
                {});
        },
        "semantic TRS default pose cardinality matches rig");

    SkeletalTrs bad_default;
    bad_default.rotation = {0.0F, 0.0F, 0.0F, 2.0F};
    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalTrsClip(
                rig,
                0.0F,
                1.0F,
                {bad_default},
                {},
                {},
                {});
        },
        "semantic TRS default pose rejects non-unit quaternion");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalTrsClip(
                rig,
                0.0F,
                1.0F,
                {SkeletalTrs{}},
                {
                    {0U, {{0.0F, {0.0F, 0.0F, 0.0F}}}},
                    {0U, {{1.0F, {1.0F, 0.0F, 0.0F}}}},
                },
                {},
                {});
        },
        "semantic TRS clip rejects duplicate property ownership for one joint");

    check_throws<std::out_of_range>(
        [&] {
            (void)SkeletalTrsClip(
                rig,
                0.0F,
                1.0F,
                {SkeletalTrs{}},
                {
                    {1U, {{0.0F, {0.0F, 0.0F, 0.0F}}}},
                },
                {},
                {});
        },
        "semantic TRS track joint index is bounded by rig ownership");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalTrsClip(
                rig,
                0.0F,
                1.0F,
                {SkeletalTrs{}},
                {
                    {
                        0U,
                        {
                            {0.5F, {0.0F, 0.0F, 0.0F}},
                            {0.5F, {1.0F, 0.0F, 0.0F}},
                        },
                    },
                },
                {},
                {});
        },
        "semantic TRS property keys require strictly increasing time");

    check_throws<std::invalid_argument>(
        [&] {
            (void)SkeletalTrsClip(
                rig,
                0.0F,
                1.0F,
                {SkeletalTrs{}},
                {},
                {
                    {
                        0U,
                        {
                            {0.0F, {0.0F, 0.0F, 0.0F, 1.0F}},
                            {1.0F, {0.0F, 0.0F, 0.0F, 2.0F}},
                        },
                    },
                },
                {});
        },
        "semantic TRS rotation track rejects non-unit key quaternion");

    const Quaternion identity{};
    const Quaternion half_turn_z{0.0F, 0.0F, 1.0F, 0.0F};
    const SkeletalTrsClip clip(
        rig,
        0.0F,
        1.0F,
        {SkeletalTrs{}},
        {
            {
                0U,
                {
                    {0.25F, {0.0F, 0.0F, 0.0F}},
                    {0.75F, {2.0F, 0.0F, 0.0F}},
                },
            },
        },
        {
            {
                0U,
                {
                    {0.0F, identity},
                    {1.0F, half_turn_z},
                },
            },
        },
        {
            {
                0U,
                {
                    {0.5F, {1.0F, 1.0F, 1.0F}},
                    {1.0F, {2.0F, 1.0F, 1.0F}},
                },
            },
        });

    const std::array<float, 6> times{
        0.0F,
        0.25F,
        0.5F,
        0.75F,
        1.0F,
        0.5F,
    };
    const auto poses = clip.sample(times);
    check(
        poses.size() == times.size(),
        "semantic TRS clip preserves caller order and multiplicity");
    check(
        exact_pose_equal(*poses[2], *poses[5]),
        "repeated semantic TRS samples are deterministic");

    const auto at_start = poses[0]->local_transforms();
    check(
        at_start[0](0U, 3U) == 0.0F
            && at_start[0](0U, 0U) == 1.0F,
        "semantic property tracks hold their first key before a narrower property domain");

    const auto at_translation_key = poses[3]->local_transforms();
    check(
        at_translation_key[0](0U, 3U) == 2.0F,
        "exact semantic translation key preserves stored translation without interpolation drift");

    const Mat4 midpoint = poses[2]->local_transforms()[0];
    const Quaternion expected_mid_rotation = quarter_turn_z_quaternion();
    const Mat4 expected_mid =
        Mat4::translation({1.0F, 0.0F, 0.0F})
        * independent_quaternion_matrix(expected_mid_rotation)
        * Mat4::scale({1.0F, 1.0F, 1.0F});
    check(
        exact_matrix_equal(midpoint, expected_mid),
        "semantic midpoint composes independent translation lerp, quaternion slerp, and held scale as T*R*S");

    Mat4 half_turn_matrix = Mat4::identity();
    half_turn_matrix(0U, 0U) = -1.0F;
    half_turn_matrix(1U, 1U) = -1.0F;
    const Mat4 matrix_lerp =
        manual_affine_lerp(
            Mat4::identity(),
            half_turn_matrix,
            0.5F);
    check(
        std::fabs(midpoint(0U, 1U) - matrix_lerp(0U, 1U))
            > 0.5F,
        "semantic quaternion midpoint is observably different from affine matrix-element interpolation");

    const Quaternion antipodal{
        -expected_mid_rotation.x,
        -expected_mid_rotation.y,
        -expected_mid_rotation.z,
        -expected_mid_rotation.w,
    };
    const Quaternion shortest =
        slerp_shortest(
            expected_mid_rotation,
            antipodal,
            0.5F);
    const Mat4 shortest_matrix =
        quaternion_rotation_matrix(shortest);
    const Mat4 equivalent_matrix =
        quaternion_rotation_matrix(expected_mid_rotation);
    check(
        exact_matrix_equal(shortest_matrix, equivalent_matrix),
        "shortest-path slerp canonicalizes equivalent antipodal quaternion endpoints deterministically");

    const std::array<float, 1> nonfinite{
        std::numeric_limits<float>::quiet_NaN(),
    };
    check_throws<std::invalid_argument>(
        [&] {
            (void)clip.sample(nonfinite);
        },
        "semantic TRS sampling rejects non-finite request time");

    const std::array<float, 1> outside{1.25F};
    check_throws<std::out_of_range>(
        [&] {
            (void)clip.sample(outside);
        },
        "semantic TRS sampling rejects time outside clip domain");

    std::vector<float> too_many(
        kMaxSkeletalTrsSamples + 1U,
        0.5F);
    check_throws<std::invalid_argument>(
        [&] {
            (void)clip.sample(too_many);
        },
        "semantic TRS sampling bounds requested batch size");
}

void test_skeletal_trs_local_before_world_matches_manual_render_and_shadow() {
    const ModelAsset source = model_from_mesh(lit_base_mesh());
    const std::vector<VertexSkinBinding> bindings{
        binding({SkinInfluence{1U, 1.0F}}),
        binding({SkinInfluence{0U, 1.0F}}),
        binding({SkinInfluence{0U, 1.0F}}),
    };
    // Child joint 0 is declared before its parent joint 1.
    const auto rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{
            1U,
            std::nullopt,
        },
        std::vector<Mat4>{
            Mat4::identity(),
            Mat4::identity(),
        },
        bindings);

    const SkeletalTrsClip clip(
        rig,
        0.0F,
        1.0F,
        {
            SkeletalTrs{},
            SkeletalTrs{},
        },
        {
            {
                0U,
                {
                    {0.0F, {0.0F, 0.0F, 0.0F}},
                    {1.0F, {0.4F, 0.0F, 0.0F}},
                },
            },
        },
        {
            {
                1U,
                {
                    {0.0F, Quaternion{}},
                    {1.0F, {0.0F, 0.0F, 1.0F, 0.0F}},
                },
            },
        },
        {
            {
                1U,
                {
                    {0.0F, {1.0F, 1.0F, 1.0F}},
                    {1.0F, {1.5F, 1.0F, 1.0F}},
                },
            },
        });

    const std::array<float, 1> midpoint_time{0.5F};
    const auto sampled = clip.sample(midpoint_time);
    const Mat4 manual_child =
        Mat4::translation({0.2F, 0.0F, 0.0F});
    const Mat4 manual_parent =
        independent_quaternion_matrix(
            quarter_turn_z_quaternion())
        * Mat4::scale({1.25F, 1.0F, 1.0F});
    const auto sampled_locals =
        sampled.front()->local_transforms();
    check(
        exact_matrix_equal(sampled_locals[0], manual_child)
            && exact_matrix_equal(sampled_locals[1], manual_parent),
        "semantic TRS clip samples complete joint-local state before arbitrary-order hierarchy composition");

    const SkinningStatePtr resolved =
        sampled.front()->resolve();
    const Mat4 expected_child_world =
        manual_parent * manual_child;
    check(
        exact_matrix_equal(
            resolved->skin_matrices()[0],
            expected_child_world),
        "M108 composes semantic child local under later-declared parent after TRS interpolation");

    auto manual_pose =
        std::make_shared<const SkeletalPoseState>(
            rig,
            std::vector<Mat4>{
                manual_child,
                manual_parent,
            });

    ModelRenderOptions semantic_options;
    semantic_options.directional_light =
        lit_directional_light(
            normalize(Vec3{0.5F, 0.75F, 0.25F}));
    semantic_options.skeletal_pose_state =
        sampled.front();

    ModelRenderOptions manual_options =
        semantic_options;
    manual_options.skeletal_pose_state =
        std::move(manual_pose);

    Framebuffer semantic_fb(53U, 53U, SampleCount::Four);
    Framebuffer manual_fb(53U, 53U, SampleCount::Four);
    semantic_fb.clear({0.01F, 0.02F, 0.03F}, 1.0F, 4U);
    manual_fb.clear({0.01F, 0.02F, 0.03F}, 1.0F, 4U);
    draw_model_asset(
        semantic_fb,
        source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        semantic_options);
    draw_model_asset(
        manual_fb,
        source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        manual_options);
    check_same_framebuffer(
        semantic_fb,
        manual_fb,
        "semantic TRS skeletal midpoint matches independent normal-aware M108 fixed-light execution");

    const PreparedModelSubmission semantic_prepared =
        prepare_model_asset(source, semantic_options);
    const PreparedModelSubmission manual_prepared =
        prepare_model_asset(source, manual_options);
    const std::array<PreparedModelListEntry, 1> semantic_entry{{
        {&semantic_prepared, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> manual_entry{{
        {&manual_prepared, Mat4::identity()},
    }};
    const auto semantic_shadow =
        render_directional_shadow_map(
            semantic_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                43U,
                43U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    const auto manual_shadow =
        render_directional_shadow_map(
            manual_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                43U,
                43U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    for (std::size_t y = 0U;
         y < semantic_shadow->height();
         ++y) {
        for (std::size_t x = 0U;
             x < semantic_shadow->width();
             ++x) {
            check(
                semantic_shadow->depth_at(x, y)
                    == manual_shadow->depth_at(x, y),
                "semantic TRS shadow silhouette matches independent local-before-world M108 reference");
        }
    }
}

void test_skeletal_trs_later_world_overflow_is_batch_fail_closed() {
    const auto rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{
            std::nullopt,
            0U,
        },
        std::vector<Mat4>{
            Mat4::identity(),
            Mat4::identity(),
        },
        std::vector<VertexSkinBinding>{
            binding({SkinInfluence{0U, 1.0F}}),
        });

    const SkeletalTrsClip clip(
        rig,
        0.0F,
        1.0F,
        {
            SkeletalTrs{},
            SkeletalTrs{},
        },
        {},
        {},
        {
            {
                0U,
                {
                    {0.0F, {1.0F, 1.0F, 1.0F}},
                    {1.0F, {1.0e20F, 1.0F, 1.0F}},
                },
            },
            {
                1U,
                {
                    {0.0F, {1.0F, 1.0F, 1.0F}},
                    {1.0F, {1.0e20F, 1.0F, 1.0F}},
                },
            },
        });

    Framebuffer framebuffer(31U, 31U, SampleCount::Four);
    framebuffer.clear(
        {0.17F, 0.27F, 0.37F},
        0.73F,
        19U);
    const auto before = framebuffer.rgb8();
    const ModelAsset source = model_from_mesh(base_mesh());
    const std::array<float, 2> samples{
        0.0F,
        1.0F,
    };
    check_throws<std::invalid_argument>(
        [&] {
            const auto poses = clip.sample(samples);
            for (const SkeletalPoseStatePtr& pose : poses) {
                ModelRenderOptions options;
                options.skeletal_pose_state = pose;
                draw_model_asset(
                    framebuffer,
                    source,
                    Mat4::identity(),
                    options);
            }
        },
        "later semantic TRS hierarchy overflow rejects the complete requested batch");
    check(
        framebuffer.rgb8() == before,
        "later semantic TRS failure occurs before any earlier requested pose owns framebuffer color");
    for (std::size_t sample = 0U;
         sample < framebuffer.samples_per_pixel();
         ++sample) {
        check(
            framebuffer.sample_depth_at(15U, 15U, sample)
                == 0.73F,
            "later semantic TRS failure occurs before framebuffer depth ownership");
        check(
            framebuffer.sample_stencil_at(15U, 15U, sample)
                == 19U,
            "later semantic TRS failure occurs before framebuffer stencil ownership");
    }
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
            (void)SkinningState(
                std::vector<VertexSkinBinding>{},
                std::vector<Mat4>{Mat4::identity()});
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

    ModelRenderOptions spatial_options;
    spatial_options.skinning_state =
        identity_skin(asset.mesh.vertices.size());
    PreparedModelSubmission skinned = prepare_model_asset(
        asset,
        spatial_options);
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
    test_identity_skin_with_fixed_lighting_is_exact();
    test_multi_joint_normal_skinning_matches_manual_and_normal_map();
    test_normal_skinning_fail_closed_contract();
    test_skeletal_rig_validation_contract();
    test_skeletal_pose_matches_precomposed_m107_skinning();
    test_skeletal_bind_pose_reduces_to_identity_skinning();
    test_skeletal_prepared_list_fail_closed_on_later_pose_overflow();
    test_skeletal_timeline_validation_and_determinism();
    test_skeletal_timeline_matches_manual_m108_execution();
    test_skeletal_timeline_interpolates_local_before_world();
    test_skeletal_timeline_later_midpoint_overflow_is_batch_fail_closed();
    test_skeletal_trs_validation_sampling_and_shortest_path();
    test_skeletal_trs_local_before_world_matches_manual_render_and_shadow();
    test_skeletal_trs_later_world_overflow_is_batch_fail_closed();
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
