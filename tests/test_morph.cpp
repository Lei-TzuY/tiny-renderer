#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/morph.hpp"
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

Vertex lit_vertex(const Vec3& position, const Vec3& normal) {
    return Vertex::with_varyings(
        position,
        VaryingPack{normal.x, normal.y, normal.z});
}

Mesh base_mesh() {
    Mesh mesh;
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    mesh.vertices = {
        lit_vertex({-0.50F, -0.50F, 0.0F}, normal),
        lit_vertex({0.50F, -0.50F, 0.0F}, normal),
        lit_vertex({0.00F, 0.50F, 0.0F}, normal),
    };
    mesh.triangles = {{0U, 1U, 2U}};
    return mesh;
}

ModelAsset model_from_mesh(Mesh mesh) {
    ModelAsset asset;
    asset.mesh = std::move(mesh);
    MaterialDraw draw;
    draw.range = {0U, asset.mesh.triangles.size()};
    draw.material.albedo = {0.7F, 0.3F, 0.15F};
    asset.draws = {draw};
    return asset;
}

DirectionalLight fixed_light() {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.direction_to_light =
        normalize(Vec3{0.25F, 0.5F, 1.0F});
    light.ambient = 0.15F;
    light.diffuse = 0.85F;
    return light;
}

void check_same_framebuffer(
    const Framebuffer& left,
    const Framebuffer& right,
    const std::string& message) {
    check(
        left.width() == right.width()
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

void check_same_shadow(
    const DepthTexture2D& left,
    const DepthTexture2D& right,
    const std::string& message) {
    check(
        left.width() == right.width()
            && left.height() == right.height(),
        message + " dimensions");
    if (left.width() != right.width()
        || left.height() != right.height()) {
        return;
    }
    for (std::size_t y = 0U; y < left.height(); ++y) {
        for (std::size_t x = 0U; x < left.width(); ++x) {
            check(
                left.depth_at(x, y) == right.depth_at(x, y),
                message + " depth");
        }
    }
}

MorphTargetSetPtr make_targets(bool include_normals = true) {
    MorphTarget first;
    first.position_deltas = {
        {0.25F, 0.00F, 0.00F},
        {0.00F, 0.25F, 0.00F},
        {-0.125F, 0.00F, 0.00F},
    };
    if (include_normals) {
        first.normal_deltas = std::vector<Vec3>{
            {0.25F, 0.00F, 0.00F},
            {0.00F, 0.25F, 0.00F},
            {0.125F, 0.125F, 0.00F},
        };
    }

    MorphTarget second;
    second.position_deltas = {
        {0.00F, 0.125F, 0.00F},
        {-0.25F, 0.00F, 0.00F},
        {0.125F, -0.125F, 0.00F},
    };
    if (include_normals) {
        second.normal_deltas = std::vector<Vec3>{
            {0.00F, -0.25F, 0.00F},
            {-0.25F, 0.00F, 0.00F},
            {0.00F, 0.125F, 0.00F},
        };
    }
    return std::make_shared<const MorphTargetSet>(
        std::vector<MorphTarget>{
            std::move(first),
            std::move(second),
        });
}

Mesh manually_morph(
    const Mesh& source,
    const MorphTargetSet& target_set,
    std::span<const float> weights,
    bool deform_normals) {
    Mesh result = source;
    const auto targets = target_set.targets();
    for (std::size_t vertex = 0U;
         vertex < source.vertices.size();
         ++vertex) {
        double x = source.vertices[vertex].position.x;
        double y = source.vertices[vertex].position.y;
        double z = source.vertices[vertex].position.z;
        for (std::size_t target = 0U;
             target < targets.size();
             ++target) {
            x += static_cast<double>(weights[target])
                * targets[target].position_deltas[vertex].x;
            y += static_cast<double>(weights[target])
                * targets[target].position_deltas[vertex].y;
            z += static_cast<double>(weights[target])
                * targets[target].position_deltas[vertex].z;
        }
        result.vertices[vertex].position = {
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(z),
        };

        if (!deform_normals) {
            continue;
        }
        const VaryingPack& input =
            source.vertices[vertex].varyings;
        double nx = input.values[0U];
        double ny = input.values[1U];
        double nz = input.values[2U];
        for (std::size_t target = 0U;
             target < targets.size();
             ++target) {
            const Vec3 delta =
                (*targets[target].normal_deltas)[vertex];
            nx += static_cast<double>(weights[target]) * delta.x;
            ny += static_cast<double>(weights[target]) * delta.y;
            nz += static_cast<double>(weights[target]) * delta.z;
        }
        const double inverse_length =
            1.0 / std::sqrt(nx * nx + ny * ny + nz * nz);
        VaryingPack& output =
            result.vertices[vertex].varyings;
        output.values[0U] =
            static_cast<float>(nx * inverse_length);
        output.values[1U] =
            static_cast<float>(ny * inverse_length);
        output.values[2U] =
            static_cast<float>(nz * inverse_length);
    }
    return result;
}

SkeletalPoseStatePtr nontrivial_pose(std::size_t vertex_count) {
    std::vector<VertexSkinBinding> bindings;
    bindings.reserve(vertex_count);
    for (std::size_t vertex = 0U;
         vertex < vertex_count;
         ++vertex) {
        bindings.emplace_back(
            std::initializer_list<SkinInfluence>{
                SkinInfluence{0U, 1.0F}});
    }
    const auto rig = std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{
            std::nullopt,
        },
        std::vector<Mat4>{
            Mat4::identity(),
        },
        std::move(bindings));
    return std::make_shared<const SkeletalPoseState>(
        rig,
        std::vector<Mat4>{
            Mat4::translation({0.125F, -0.125F, 0.0F})
                * Mat4::scale({1.5F, 0.75F, 1.0F}),
        });
}

class DoubleXProgram final : public VertexProgram {
public:
    VertexProgramOutput process(
        const VertexProgramInput& input) const noexcept override {
        VertexProgramOutput output{
            input.position,
            input.varyings,
        };
        output.position.x *= 2.0F;
        return output;
    }
};

void test_morph_validation_contract() {
    check_throws<std::invalid_argument>(
        [] {
            (void)MorphTargetSet({});
        },
        "morph target set rejects zero targets");

    std::vector<MorphTarget> too_many;
    for (std::size_t target = 0U;
         target <= kMaxMorphTargets;
         ++target) {
        MorphTarget value;
        value.position_deltas = {{0.0F, 0.0F, 0.0F}};
        too_many.push_back(std::move(value));
    }
    check_throws<std::invalid_argument>(
        [&] {
            (void)MorphTargetSet(too_many);
        },
        "morph target set enforces bounded target count");

    check_throws<std::invalid_argument>(
        [] {
            MorphTarget target;
            (void)MorphTargetSet({target});
        },
        "morph target set rejects zero canonical vertices");

    check_throws<std::invalid_argument>(
        [] {
            MorphTarget first;
            first.position_deltas = {
                {0.0F, 0.0F, 0.0F},
                {0.0F, 0.0F, 0.0F},
            };
            MorphTarget second;
            second.position_deltas = {
                {0.0F, 0.0F, 0.0F},
            };
            (void)MorphTargetSet({first, second});
        },
        "morph target set requires aligned position ownership");

    check_throws<std::invalid_argument>(
        [] {
            MorphTarget target;
            target.position_deltas = {
                {0.0F, 0.0F, 0.0F},
                {0.0F, 0.0F, 0.0F},
            };
            target.normal_deltas =
                std::vector<Vec3>{{0.0F, 0.0F, 0.0F}};
            (void)MorphTargetSet({target});
        },
        "morph target set rejects incomplete normal ownership");

    check_throws<std::invalid_argument>(
        [] {
            MorphTarget target;
            target.position_deltas = {{
                std::numeric_limits<float>::infinity(),
                0.0F,
                0.0F,
            }};
            (void)MorphTargetSet({target});
        },
        "morph target set rejects non-finite position deltas");

    check_throws<std::invalid_argument>(
        [] {
            MorphTarget target;
            target.position_deltas = {{0.0F, 0.0F, 0.0F}};
            target.normal_deltas =
                std::vector<Vec3>{{
                    0.0F,
                    std::numeric_limits<float>::quiet_NaN(),
                    0.0F,
                }};
            (void)MorphTargetSet({target});
        },
        "morph target set rejects non-finite normal deltas");

    const MorphTargetSetPtr targets = make_targets();
    check_throws<std::invalid_argument>(
        [] {
            (void)MorphState(
                MorphTargetSetPtr{},
                std::vector<float>{});
        },
        "morph state requires immutable target ownership");

    check_throws<std::invalid_argument>(
        [&] {
            (void)MorphState(targets, {1.0F});
        },
        "morph state requires one weight per target");

    check_throws<std::invalid_argument>(
        [&] {
            (void)MorphState(
                targets,
                {
                    0.0F,
                    std::numeric_limits<float>::infinity(),
                });
        },
        "morph state rejects non-finite weights");

    MorphTarget wrong;
    wrong.position_deltas = {
        {0.0F, 0.0F, 0.0F},
    };
    ModelRenderOptions wrong_options;
    wrong_options.morph_state =
        std::make_shared<const MorphState>(
            std::make_shared<const MorphTargetSet>(
                std::vector<MorphTarget>{wrong}),
            std::vector<float>{1.0F});
    check_throws<std::invalid_argument>(
        [&] {
            (void)prepare_model_asset(
                model_from_mesh(base_mesh()),
                wrong_options);
        },
        "prepared model rejects morph/canonical vertex ownership mismatch");
}

void test_zero_weight_morph_is_exact_and_retained() {
    const ModelAsset source =
        model_from_mesh(base_mesh());
    MorphTarget target;
    target.position_deltas = {
        {0.25F, 0.0F, 0.0F},
        {0.0F, 0.25F, 0.0F},
        {-0.25F, 0.0F, 0.0F},
    };
    auto state = std::make_shared<const MorphState>(
        std::make_shared<const MorphTargetSet>(
            std::vector<MorphTarget>{std::move(target)}),
        std::vector<float>{0.0F});

    ModelRenderOptions baseline_options;
    baseline_options.directional_light = fixed_light();
    ModelRenderOptions morph_options = baseline_options;
    morph_options.morph_state = state;

    Framebuffer baseline(47U, 47U, SampleCount::Four);
    Framebuffer morphed(47U, 47U, SampleCount::Four);
    baseline.clear({0.03F, 0.04F, 0.05F}, 1.0F, 7U);
    morphed.clear({0.03F, 0.04F, 0.05F}, 1.0F, 7U);
    draw_model_asset(
        baseline,
        source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        baseline_options);
    draw_model_asset(
        morphed,
        source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        morph_options);
    check_same_framebuffer(
        baseline,
        morphed,
        "zero-weight morph is exact-equivalent under fixed lighting");

    PreparedModelSubmission prepared =
        prepare_model_asset(source, morph_options);
    state.reset();
    morph_options.morph_state.reset();
    check(
        prepared.options().morph_state != nullptr,
        "prepared model retains immutable morph ownership");
    Framebuffer prepared_fb(47U, 47U, SampleCount::Four);
    prepared_fb.clear({0.03F, 0.04F, 0.05F}, 1.0F, 7U);
    draw_prepared_model(
        prepared_fb,
        prepared,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity());
    check_same_framebuffer(
        baseline,
        prepared_fb,
        "prepared zero-weight morph remains canonical after caller releases source state");

    const PreparedModelSubmission baseline_prepared =
        prepare_model_asset(source, baseline_options);
    const std::array<PreparedModelListEntry, 1> morph_entry{{
        {&prepared, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> baseline_entry{{
        {&baseline_prepared, Mat4::identity()},
    }};
    const auto morph_shadow = render_directional_shadow_map(
        morph_entry,
        Mat4::identity(),
        DirectionalShadowMapOptions{
            41U, 41U, CullMode::None, FrontFace::CounterClockwise});
    const auto baseline_shadow = render_directional_shadow_map(
        baseline_entry,
        Mat4::identity(),
        DirectionalShadowMapOptions{
            41U, 41U, CullMode::None, FrontFace::CounterClockwise});
    check_same_shadow(
        *morph_shadow,
        *baseline_shadow,
        "zero-weight morph shadow is exact-equivalent to canonical geometry");

    const auto ordered =
        order_prepared_model_list_back_to_front(
            morph_entry,
            Mat4::identity());
    check(
        ordered.size() == 1U,
        "zero-weight morph remains valid for canonical painter ordering");
}

void test_active_morph_matches_manual_geometry() {
    const ModelAsset source =
        model_from_mesh(base_mesh());
    const MorphTargetSetPtr targets = make_targets();
    const std::array<std::array<float, 2>, 2> weights{{
        {{1.0F, 0.0F}},
        {{0.75F, -0.25F}},
    }};

    for (const auto& weight_pair : weights) {
        auto state = std::make_shared<const MorphState>(
            targets,
            std::vector<float>{
                weight_pair[0],
                weight_pair[1],
            });
        const std::array<float, 2> manual_weights{
            weight_pair[0],
            weight_pair[1],
        };
        const ModelAsset manual = model_from_mesh(
            manually_morph(
                source.mesh,
                *targets,
                manual_weights,
                true));

        ModelRenderOptions morph_options;
        morph_options.directional_light = fixed_light();
        morph_options.morph_state = state;
        ModelRenderOptions manual_options;
        manual_options.directional_light = fixed_light();

        Framebuffer morph_fb(53U, 53U, SampleCount::Four);
        Framebuffer manual_fb(53U, 53U, SampleCount::Four);
        morph_fb.clear();
        manual_fb.clear();
        draw_model_asset(
            morph_fb,
            source,
            Mat4::identity(),
            Mat4::identity(),
            Mat4::identity(),
            morph_options);
        draw_model_asset(
            manual_fb,
            manual,
            Mat4::identity(),
            Mat4::identity(),
            Mat4::identity(),
            manual_options);
        check_same_framebuffer(
            morph_fb,
            manual_fb,
            "active morph matches independent manual position/normal deformation");

        const PreparedModelSubmission prepared =
            prepare_model_asset(source, morph_options);
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
            "prepared active morph matches independent manual deformation");
    }
}

void test_morph_precedes_skinning_and_vertex_program() {
    const ModelAsset source =
        model_from_mesh(base_mesh());
    const MorphTargetSetPtr targets = make_targets();
    const std::vector<float> weights{0.5F, 0.25F};
    const auto morph =
        std::make_shared<const MorphState>(targets, weights);
    const SkeletalPoseStatePtr pose =
        nontrivial_pose(source.mesh.vertices.size());
    const auto program =
        std::make_shared<DoubleXProgram>();

    ModelRenderOptions composed;
    composed.directional_light = fixed_light();
    composed.morph_state = morph;
    composed.skeletal_pose_state = pose;
    composed.vertex_program = program;

    const ModelAsset manual = model_from_mesh(
        manually_morph(
            source.mesh,
            *targets,
            weights,
            true));
    ModelRenderOptions reference;
    reference.directional_light = fixed_light();
    reference.skeletal_pose_state = pose;
    reference.vertex_program = program;

    Framebuffer composed_fb(57U, 57U, SampleCount::Four);
    Framebuffer reference_fb(57U, 57U, SampleCount::Four);
    composed_fb.clear({0.01F, 0.02F, 0.03F}, 1.0F, 3U);
    reference_fb.clear({0.01F, 0.02F, 0.03F}, 1.0F, 3U);
    draw_model_asset(
        composed_fb,
        source,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        composed);
    draw_model_asset(
        reference_fb,
        manual,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        reference);
    check_same_framebuffer(
        composed_fb,
        reference_fb,
        "shared object-space gateway executes morph before M108 skinning and M35 vertex program");

    const PreparedModelSubmission composed_prepared =
        prepare_model_asset(source, composed);
    const PreparedModelSubmission reference_prepared =
        prepare_model_asset(manual, reference);
    const std::array<PreparedModelListEntry, 1> composed_entry{{
        {&composed_prepared, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> reference_entry{{
        {&reference_prepared, Mat4::identity()},
    }};
    const auto composed_shadow = render_directional_shadow_map(
        composed_entry,
        Mat4::identity(),
        DirectionalShadowMapOptions{
            47U, 47U, CullMode::None, FrontFace::CounterClockwise});
    const auto reference_shadow = render_directional_shadow_map(
        reference_entry,
        Mat4::identity(),
        DirectionalShadowMapOptions{
            47U, 47U, CullMode::None, FrontFace::CounterClockwise});
    check_same_shadow(
        *composed_shadow,
        *reference_shadow,
        "shadow capture consumes morph-before-skin-before-M35 geometry");
}

void test_lit_morph_requires_normal_semantics_but_shadow_does_not() {
    const ModelAsset source =
        model_from_mesh(base_mesh());
    const MorphTargetSetPtr targets =
        make_targets(false);
    ModelRenderOptions options;
    options.directional_light = fixed_light();
    options.morph_state =
        std::make_shared<const MorphState>(
            targets,
            std::vector<float>{1.0F, 0.0F});

    Framebuffer framebuffer(41U, 41U, SampleCount::Four);
    framebuffer.clear({0.13F, 0.23F, 0.33F}, 0.77F, 11U);
    const auto before = framebuffer.rgb8();
    check_throws<std::invalid_argument>(
        [&] {
            draw_model_asset(
                framebuffer,
                source,
                Mat4::identity(),
                Mat4::identity(),
                Mat4::identity(),
                options);
        },
        "active lit morph without normal deltas fails closed");
    check(
        framebuffer.rgb8() == before,
        "missing lit morph normal semantics reject before color mutation");

    const PreparedModelSubmission prepared =
        prepare_model_asset(source, options);
    const std::array<PreparedModelListEntry, 1> entries{{
        {&prepared, Mat4::identity()},
    }};
    const auto shadow = render_directional_shadow_map(
        entries,
        Mat4::identity(),
        DirectionalShadowMapOptions{
            41U, 41U, CullMode::None, FrontFace::CounterClockwise});

    const std::array<float, 2> weights{1.0F, 0.0F};
    const ModelAsset manual = model_from_mesh(
        manually_morph(
            source.mesh,
            *targets,
            weights,
            false));
    ModelRenderOptions manual_options;
    manual_options.directional_light = fixed_light();
    const PreparedModelSubmission manual_prepared =
        prepare_model_asset(manual, manual_options);
    const std::array<PreparedModelListEntry, 1> manual_entries{{
        {&manual_prepared, Mat4::identity()},
    }};
    const auto manual_shadow = render_directional_shadow_map(
        manual_entries,
        Mat4::identity(),
        DirectionalShadowMapOptions{
            41U, 41U, CullMode::None, FrontFace::CounterClockwise});
    check_same_shadow(
        *shadow,
        *manual_shadow,
        "shadow position deformation remains valid without morph normal deltas");
}

void test_later_invalid_morph_is_list_fail_closed() {
    const ModelAsset source =
        model_from_mesh(base_mesh());

    ModelRenderOptions valid_options;
    valid_options.morph_state =
        std::make_shared<const MorphState>(
            make_targets(),
            std::vector<float>{0.5F, 0.0F});
    const PreparedModelSubmission valid =
        prepare_model_asset(source, valid_options);

    MorphTarget huge;
    huge.position_deltas = {
        {6.0e19F, 0.0F, 0.0F},
        {6.0e19F, 0.0F, 0.0F},
        {6.0e19F, 0.0F, 0.0F},
    };
    ModelRenderOptions invalid_options;
    invalid_options.morph_state =
        std::make_shared<const MorphState>(
            std::make_shared<const MorphTargetSet>(
                std::vector<MorphTarget>{std::move(huge)}),
            std::vector<float>{2.0F});
    const PreparedModelSubmission invalid =
        prepare_model_asset(source, invalid_options);

    const std::array<PreparedModelListEntry, 2> entries{{
        {&valid, Mat4::identity()},
        {&invalid, Mat4::identity()},
    }};

    Framebuffer framebuffer(41U, 41U, SampleCount::Four);
    framebuffer.clear({0.12F, 0.22F, 0.32F}, 0.81F, 17U);
    const auto before = framebuffer.rgb8();
    check_throws<std::invalid_argument>(
        [&] {
            draw_prepared_model_list(
                framebuffer,
                entries,
                Mat4::identity(),
                Mat4::identity());
        },
        "later unsafe morph result rejects complete prepared list");
    check(
        framebuffer.rgb8() == before,
        "later invalid morph rejects before earlier RGB ownership");
    for (std::size_t sample = 0U;
         sample < framebuffer.samples_per_pixel();
         ++sample) {
        check(
            framebuffer.sample_depth_at(20U, 20U, sample)
                == 0.81F,
            "later invalid morph rejects before earlier depth ownership");
        check(
            framebuffer.sample_stencil_at(20U, 20U, sample)
                == 17U,
            "later invalid morph rejects before earlier stencil ownership");
    }
}

void test_active_morph_rejected_by_canonical_spatial_paths() {
    const ModelAsset source =
        model_from_mesh(base_mesh());
    ModelRenderOptions options;
    options.morph_state =
        std::make_shared<const MorphState>(
            make_targets(),
            std::vector<float>{1.0F, 0.0F});
    const PreparedModelSubmission prepared =
        prepare_model_asset(source, options);
    const std::array<PreparedModelListEntry, 1> entries{{
        {&prepared, Mat4::identity()},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)order_prepared_model_list_back_to_front(
                entries,
                Mat4::identity());
        },
        "active morph rejects canonical painter ordering");

    const PreparedSpatialSubmission spatial =
        prepare_spatial_submission(prepared);
    const std::array<PreparedSpatialListEntry, 1> spatial_entries{{
        {&spatial, Mat4::identity()},
    }};
    check_throws<std::invalid_argument>(
        [&] {
            (void)flatten_prepared_model_draws(
                spatial_entries,
                Mat4::identity());
        },
        "active morph rejects canonical prepared spatial planning");
}

}  // namespace

int main() {
    test_morph_validation_contract();
    test_zero_weight_morph_is_exact_and_retained();
    test_active_morph_matches_manual_geometry();
    test_morph_precedes_skinning_and_vertex_program();
    test_lit_morph_requires_normal_semantics_but_shadow_does_not();
    test_later_invalid_morph_is_list_fail_closed();
    test_active_morph_rejected_by_canonical_spatial_paths();

    if (failures != 0) {
        std::cerr << failures << " morph test(s) failed\n";
        return 1;
    }
    std::cout << "morph tests passed\n";
    return 0;
}
