#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/offline_render.hpp"
#include "tiny_renderer/prepared_spatial.hpp"
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

ModelAsset unit_triangle_asset(
    const Vec3& color = {0.8F, 0.3F, 0.1F},
    float opacity = 1.0F) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.5F, -0.5F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({0.5F, -0.5F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({0.0F, 0.5F, 0.0F}, VaryingPack{}),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material.albedo = color;
    draw.material.opacity = opacity;
    asset.draws.push_back(draw);
    return asset;
}

class IdentityVisibilityVertexProgram final : public VertexProgram {
public:
    VertexProgramOutput process(const VertexProgramInput& input) const noexcept override {
        return {input.position, input.varyings};
    }
};

bool same_float(float left, float right) {
    return left == right || (std::isnan(left) && std::isnan(right));
}

void check_framebuffers_equal(
    const Framebuffer& left,
    const Framebuffer& right,
    const std::string& context) {
    check(left.width() == right.width() && left.height() == right.height(),
          context + " has matching dimensions");
    check(left.sample_count() == right.sample_count(),
          context + " has matching sample count");
    if (left.width() != right.width()
        || left.height() != right.height()
        || left.sample_count() != right.sample_count()) {
        return;
    }

    for (std::size_t y = 0U; y < left.height(); ++y) {
        for (std::size_t x = 0U; x < left.width(); ++x) {
            for (std::size_t sample = 0U; sample < left.samples_per_pixel(); ++sample) {
                const Vec3& a = left.sample_color_at(x, y, sample);
                const Vec3& b = right.sample_color_at(x, y, sample);
                check(
                    same_float(a.x, b.x) && same_float(a.y, b.y) && same_float(a.z, b.z),
                    context + " preserves exact sample color");
                check(
                    same_float(left.sample_depth_at(x, y, sample), right.sample_depth_at(x, y, sample)),
                    context + " preserves exact sample depth");
                check(
                    left.sample_stencil_at(x, y, sample) == right.sample_stencil_at(x, y, sample),
                    context + " preserves exact sample stencil");
            }
        }
    }
}

void test_six_clip_planes_reject_provably_outside_draws() {
    const PreparedSpatialSubmission spatial = prepare_spatial_model(unit_triangle_asset());
    const std::array<PreparedDrawOrderEntry, 6> outside{{
        {&spatial, Mat4::translation({-2.0F, 0.0F, 0.0F}), 0U, -1.0},
        {&spatial, Mat4::translation({2.0F, 0.0F, 0.0F}), 0U, -1.0},
        {&spatial, Mat4::translation({0.0F, -2.0F, 0.0F}), 0U, -1.0},
        {&spatial, Mat4::translation({0.0F, 2.0F, 0.0F}), 0U, -1.0},
        {&spatial, Mat4::translation({0.0F, 0.0F, -2.0F}), 0U, -2.0},
        {&spatial, Mat4::translation({0.0F, 0.0F, 2.0F}), 0U, 2.0},
    }};

    const std::vector<PreparedDrawOrderEntry> visible =
        filter_prepared_draw_order_to_frustum(outside, Mat4::identity(), Mat4::identity());
    check(visible.empty(),
          "prepared visibility rejects draws provably outside each of the six homogeneous clip planes");
}

void test_boundary_crossing_and_caller_order_are_conservative() {
    const PreparedSpatialSubmission spatial = prepare_spatial_model(unit_triangle_asset());
    const std::array<PreparedDrawOrderEntry, 4> plan{{
        {&spatial, Mat4::translation({0.0F, 0.0F, 0.0F}), 0U, -4.0},
        // min x is exactly -1: touching the clip boundary must be retained.
        {&spatial, Mat4::translation({-0.5F, 0.0F, 0.0F}), 0U, -3.0},
        // Bounds straddle the left plane: ambiguous/partially visible stays retained.
        {&spatial, Mat4::translation({-0.75F, 0.0F, 0.0F}), 0U, -2.0},
        {&spatial, Mat4::translation({3.0F, 0.0F, 0.0F}), 0U, -1.0},
    }};

    const std::vector<PreparedDrawOrderEntry> visible =
        filter_prepared_draw_order_to_frustum(plan, Mat4::identity(), Mat4::identity());
    check(visible.size() == 3U,
          "prepared visibility retains interior, boundary-touching, and frustum-crossing draws");
    if (visible.size() == 3U) {
        check(visible[0].view_depth == -4.0
                  && visible[1].view_depth == -3.0
                  && visible[2].view_depth == -2.0,
              "prepared visibility preserves exact caller order and planning records for retained draws");
    }
}

void test_perspective_filter_execution_is_attachment_equivalent() {
    const PreparedSpatialSubmission spatial = prepare_spatial_model(unit_triangle_asset());
    const Mat4 view = Mat4::look_at(
        {0.0F, 0.0F, 3.0F},
        {0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F});
    const Mat4 projection = Mat4::perspective(radians(60.0F), 1.0F, 0.1F, 20.0F);

    const std::array<PreparedSpatialListEntry, 2> entries{{
        {&spatial, Mat4::identity()},
        {&spatial, Mat4::translation({20.0F, 0.0F, 0.0F})},
    }};
    const std::vector<PreparedDrawOrderEntry> plan =
        order_prepared_model_draws_back_to_front(entries, view);
    const std::vector<PreparedDrawOrderEntry> visible =
        filter_prepared_draw_order_to_frustum(plan, view, projection);

    check(plan.size() == 2U, "perspective reference plan contains visible and off-frustum draws");
    check(visible.size() == 1U,
          "perspective visibility removes only the provably off-frustum prepared draw");

    Framebuffer unfiltered(65U, 65U, SampleCount::Four);
    Framebuffer filtered(65U, 65U, SampleCount::Four);
    unfiltered.clear({0.125F, 0.25F, 0.375F}, 1.0F, 17U);
    filtered.clear({0.125F, 0.25F, 0.375F}, 1.0F, 17U);
    draw_prepared_draw_order(unfiltered, plan, view, projection);
    draw_prepared_draw_order(filtered, visible, view, projection);

    check(
        unfiltered.rgb8() == filtered.rgb8() && unfiltered.fnv1a64() == filtered.fnv1a64(),
        "visibility-filtered execution preserves resolved color byte/hash output");
    check_framebuffers_equal(
        unfiltered,
        filtered,
        "visibility-filtered prepared draw execution");
}

void test_conservative_large_bounds_are_not_false_rejected() {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-4.0F, -4.0F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({4.0F, -4.0F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({0.0F, 4.0F, 0.0F}, VaryingPack{}),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material.albedo = {0.2F, 0.7F, 0.4F};
    asset.draws.push_back(draw);

    const PreparedSpatialSubmission spatial = prepare_spatial_model(std::move(asset));
    const std::array<PreparedDrawOrderEntry, 1> plan{{
        {&spatial, Mat4::identity(), 0U, -1.0},
    }};
    const std::vector<PreparedDrawOrderEntry> visible =
        filter_prepared_draw_order_to_frustum(plan, Mat4::identity(), Mat4::identity());
    check(visible.size() == 1U,
          "prepared visibility retains a draw whose bounds cross multiple clip planes but overlap the volume");
}

void test_visibility_filter_fails_closed_on_unrepresentable_state() {
    const PreparedSpatialSubmission spatial = prepare_spatial_model(unit_triangle_asset());
    const std::array<PreparedDrawOrderEntry, 1> valid_plan{{
        {&spatial, Mat4::identity(), 0U, -1.0},
    }};

    Mat4 non_finite_projection = Mat4::identity();
    non_finite_projection(0U, 0U) = std::numeric_limits<float>::quiet_NaN();
    bool projection_threw = false;
    try {
        (void)filter_prepared_draw_order_to_frustum(
            valid_plan, Mat4::identity(), non_finite_projection);
    } catch (const std::invalid_argument&) {
        projection_threw = true;
    }
    check(projection_threw, "prepared visibility rejects non-finite projection state");

    bool projective_view_threw = false;
    try {
        (void)filter_prepared_draw_order_to_frustum(
            valid_plan,
            Mat4::perspective(radians(60.0F), 1.0F, 0.1F, 10.0F),
            Mat4::identity());
    } catch (const std::invalid_argument&) {
        projective_view_threw = true;
    }
    check(projective_view_threw, "prepared visibility rejects a projective view transform");

    const std::array<PreparedDrawOrderEntry, 1> projective_model{{
        {&spatial, Mat4::perspective(radians(60.0F), 1.0F, 0.1F, 10.0F), 0U, -1.0},
    }};
    bool projective_model_threw = false;
    try {
        (void)filter_prepared_draw_order_to_frustum(
            projective_model, Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        projective_model_threw = true;
    }
    check(projective_model_threw, "prepared visibility rejects a projective model transform");

    const std::array<PreparedDrawOrderEntry, 1> invalid_index{{
        {&spatial, Mat4::identity(), 4U, -1.0},
    }};
    bool index_threw = false;
    try {
        (void)filter_prepared_draw_order_to_frustum(
            invalid_index, Mat4::identity(), Mat4::identity());
    } catch (const std::out_of_range&) {
        index_threw = true;
    }
    check(index_threw, "prepared visibility rejects unavailable draw indices");

    const std::array<PreparedDrawOrderEntry, 1> non_finite_depth{{
        {&spatial, Mat4::identity(), 0U, std::numeric_limits<double>::infinity()},
    }};
    bool depth_threw = false;
    try {
        (void)filter_prepared_draw_order_to_frustum(
            non_finite_depth, Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        depth_threw = true;
    }
    check(depth_threw, "prepared visibility rejects non-finite inherited planning depth");

    ModelRenderOptions programmed_options;
    programmed_options.vertex_program = std::make_shared<IdentityVisibilityVertexProgram>();
    const PreparedSpatialSubmission programmed = prepare_spatial_model(
        unit_triangle_asset(), programmed_options);
    const std::array<PreparedDrawOrderEntry, 1> programmed_plan{{
        {&programmed, Mat4::identity(), 0U, -1.0},
    }};
    bool programmed_threw = false;
    try {
        (void)filter_prepared_draw_order_to_frustum(
            programmed_plan, Mat4::identity(), Mat4::identity());
    } catch (const std::invalid_argument&) {
        programmed_threw = true;
    }
    check(programmed_threw,
          "prepared visibility rejects vertex-program geometry outside canonical spatial metadata");
}

void test_empty_filter_is_deterministic() {
    const std::span<const PreparedDrawOrderEntry> empty{};
    const std::vector<PreparedDrawOrderEntry> visible =
        filter_prepared_draw_order_to_frustum(empty, Mat4::identity(), Mat4::identity());
    check(visible.empty(), "empty prepared visibility input is a deterministic empty result");
}

OfflineSceneCamera explicit_visibility_camera() {
    OfflineSceneCamera camera;
    camera.eye = {0.0F, 0.0F, 3.0F};
    camera.target = {0.0F, 0.0F, 0.0F};
    camera.up = {0.0F, 1.0F, 0.0F};
    camera.vertical_fov_radians = radians(60.0F);
    camera.near_plane = 0.1F;
    camera.far_plane = 20.0F;
    return camera;
}

OfflineRenderSettings visibility_render_settings() {
    OfflineRenderSettings settings;
    settings.width = 65U;
    settings.height = 65U;
    settings.sample_count = SampleCount::Four;
    settings.clear_color = {0.125F, 0.25F, 0.375F};
    return settings;
}

void test_offline_mixed_source_alpha_visibility_preserves_output() {
    const ModelAsset opaque = unit_triangle_asset({0.1F, 0.3F, 0.8F});
    const ModelAsset visible_alpha = unit_triangle_asset({0.9F, 0.2F, 0.1F}, 0.5F);
    const ModelAsset off_frustum_alpha = unit_triangle_asset({0.1F, 0.9F, 0.2F}, 0.75F);

    OfflineSceneEntry opaque_entry;
    opaque_entry.asset = &opaque;
    opaque_entry.model = Mat4::translation({0.0F, 0.0F, -0.25F});
    opaque_entry.transparency_mode = OfflineSceneTransparencyMode::Opaque;

    OfflineSceneEntry visible_entry;
    visible_entry.asset = &visible_alpha;
    visible_entry.transparency_mode = OfflineSceneTransparencyMode::SourceAlpha;

    OfflineSceneEntry off_frustum_entry;
    off_frustum_entry.asset = &off_frustum_alpha;
    off_frustum_entry.model = Mat4::translation({20.0F, 0.0F, 0.0F});
    off_frustum_entry.transparency_mode = OfflineSceneTransparencyMode::SourceAlpha;

    const std::array<OfflineSceneEntry, 3> with_off_frustum{{
        opaque_entry,
        visible_entry,
        off_frustum_entry,
    }};
    const std::array<OfflineSceneEntry, 2> manually_visible{{
        opaque_entry,
        visible_entry,
    }};

    const OfflineRenderSettings settings = visibility_render_settings();
    const OfflineSceneCamera camera = explicit_visibility_camera();
    const Framebuffer filtered_scene = render_scene_preview(
        with_off_frustum,
        settings,
        OfflineSceneOrdering::MixedTransparency,
        camera);
    const Framebuffer manual_scene = render_scene_preview(
        manually_visible,
        settings,
        OfflineSceneOrdering::MixedTransparency,
        camera);

    check(
        filtered_scene.rgb8() == manual_scene.rgb8()
            && filtered_scene.fnv1a64() == manual_scene.fnv1a64(),
        "offline mixed-transparency visibility preserves resolved output when an off-frustum source-alpha draw is removed");
    check_framebuffers_equal(
        filtered_scene,
        manual_scene,
        "offline mixed-transparency source-alpha visibility integration");
}

void test_off_frustum_source_alpha_still_receives_full_target_preflight() {
    const ModelAsset visible_alpha = unit_triangle_asset({0.9F, 0.2F, 0.1F}, 0.5F);
    const ModelAsset invalid_off_frustum_alpha = unit_triangle_asset({0.2F, 0.8F, 0.3F}, 0.5F);

    OfflineSceneEntry visible_entry;
    visible_entry.asset = &visible_alpha;
    visible_entry.transparency_mode = OfflineSceneTransparencyMode::SourceAlpha;

    OfflineSceneEntry invalid_entry;
    invalid_entry.asset = &invalid_off_frustum_alpha;
    invalid_entry.model = Mat4::translation({20.0F, 0.0F, 0.0F});
    invalid_entry.transparency_mode = OfflineSceneTransparencyMode::SourceAlpha;
    // This definition is statically valid but cannot fit the actual render
    // target. M84 must continue to preflight the complete original plan before
    // executing only its visibility-filtered subset.
    invalid_entry.options.viewport_state.viewport = RasterRect{0U, 0U, 4096U, 4096U};

    const std::array<OfflineSceneEntry, 2> scene{{visible_entry, invalid_entry}};
    bool threw = false;
    try {
        (void)render_scene_preview(
            scene,
            visibility_render_settings(),
            OfflineSceneOrdering::MixedTransparency,
            explicit_visibility_camera());
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(
        threw,
        "off-frustum source-alpha draws remain subject to complete target-dependent fail-closed preflight");
}

}  // namespace

int main() {
    try {
        test_six_clip_planes_reject_provably_outside_draws();
        test_boundary_crossing_and_caller_order_are_conservative();
        test_perspective_filter_execution_is_attachment_equivalent();
        test_conservative_large_bounds_are_not_false_rejected();
        test_visibility_filter_fails_closed_on_unrepresentable_state();
        test_empty_filter_is_deterministic();
        test_offline_mixed_source_alpha_visibility_preserves_output();
        test_off_frustum_source_alpha_still_receives_full_target_preflight();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " prepared visibility test(s) failed\n";
        return 1;
    }
    std::cout << "all prepared visibility tests passed\n";
    return 0;
}
