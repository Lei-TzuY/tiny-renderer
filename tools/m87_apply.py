from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one exact anchor, found {count}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=re.DOTALL)
    if count != 1:
        raise RuntimeError(f"{label}: expected one regex anchor, found {count}")
    return result


# Prepared draw execution gains a bounded camera-dependent override without
# changing the owned prepared model/spatial snapshot.
path = "include/tiny_renderer/prepared_spatial.hpp"
text = read(path)
text = replace_once(
    text,
    "#include <limits>\n#include <span>",
    "#include <limits>\n#include <optional>\n#include <span>",
    "prepared_spatial optional include")
text = replace_once(
    text,
    "struct PreparedDrawOrderEntry {\n"
    "    const PreparedSpatialSubmission* prepared{nullptr};\n"
    "    Mat4 model{Mat4::identity()};\n"
    "    std::size_t draw_index{};\n"
    "    double view_depth{};\n"
    "};\n",
    "struct PreparedDrawOrderEntry {\n"
    "    const PreparedSpatialSubmission* prepared{nullptr};\n"
    "    Mat4 model{Mat4::identity()};\n"
    "    std::size_t draw_index{};\n"
    "    double view_depth{};\n"
    "};\n\n"
    "// Camera-dependent execution values that may change between evaluations\n"
    "// without rebuilding the owned prepared model/spatial snapshot. The first\n"
    "// bounded override is the environment-reflection viewer position used by\n"
    "// reusable offline scene rendering.\n"
    "struct PreparedDrawExecutionOverrides {\n"
    "    std::optional<Vec3> environment_reflection_viewer_position{};\n"
    "};\n",
    "prepared_spatial override type")
text = replace_once(
    text,
    "void preflight_prepared_draw_order(\n"
    "    const Framebuffer& framebuffer,\n"
    "    std::span<const PreparedDrawOrderEntry> entries);\n\n"
    "// Executes the supplied draw sequence exactly in caller order. The executor\n"
    "// uses the canonical prepared-model material/raster mapping and selected\n"
    "// draw_mesh_range path; it does not re-sort or create a parallel raster path.\n"
    "// Complete plan preflight occurs before the first fragment submission.\n"
    "void draw_prepared_draw_order(\n"
    "    Framebuffer& framebuffer,\n"
    "    std::span<const PreparedDrawOrderEntry> entries,\n"
    "    const Mat4& view,\n"
    "    const Mat4& projection);\n",
    "void preflight_prepared_draw_order(\n"
    "    const Framebuffer& framebuffer,\n"
    "    std::span<const PreparedDrawOrderEntry> entries);\n\n"
    "void preflight_prepared_draw_order(\n"
    "    const Framebuffer& framebuffer,\n"
    "    std::span<const PreparedDrawOrderEntry> entries,\n"
    "    const PreparedDrawExecutionOverrides& overrides);\n\n"
    "// Executes the supplied draw sequence exactly in caller order. The executor\n"
    "// uses the canonical prepared-model material/raster mapping and selected\n"
    "// draw_mesh_range path; it does not re-sort or create a parallel raster path.\n"
    "// Complete plan preflight occurs before the first fragment submission.\n"
    "void draw_prepared_draw_order(\n"
    "    Framebuffer& framebuffer,\n"
    "    std::span<const PreparedDrawOrderEntry> entries,\n"
    "    const Mat4& view,\n"
    "    const Mat4& projection);\n\n"
    "void draw_prepared_draw_order(\n"
    "    Framebuffer& framebuffer,\n"
    "    std::span<const PreparedDrawOrderEntry> entries,\n"
    "    const Mat4& view,\n"
    "    const Mat4& projection,\n"
    "    const PreparedDrawExecutionOverrides& overrides);\n",
    "prepared_spatial execution overload declarations")
write(path, text)


# Reuse canonical model->raster mapping while overlaying only the active
# reflection viewer at execution/preflight time.
path = "src/model_renderer.cpp"
text = read(path)
text = replace_once(
    text,
    "const MaterialDraw& prepared_draw_for(const PreparedDrawOrderEntry& entry) {",
    "ModelRenderOptions prepared_draw_execution_options(\n"
    "    const PreparedModelSubmission& prepared,\n"
    "    const PreparedDrawExecutionOverrides& overrides) {\n"
    "    ModelRenderOptions options = prepared.options();\n"
    "    if (overrides.environment_reflection_viewer_position) {\n"
    "        const Vec3 viewer = *overrides.environment_reflection_viewer_position;\n"
    "        if (!finite_vec3(viewer)) {\n"
    "            throw std::invalid_argument(\"prepared draw reflection viewer override must be finite\");\n"
    "        }\n"
    "        if (options.fixed_lights.environment_reflection) {\n"
    "            options.fixed_lights.environment_reflection->viewer_position = viewer;\n"
    "        }\n"
    "    }\n"
    "    return options;\n"
    "}\n\n"
    "const MaterialDraw& prepared_draw_for(const PreparedDrawOrderEntry& entry) {",
    "model_renderer execution override helper")
text = regex_once(
    text,
    r"void preflight_prepared_draw_entry\(\n    const Framebuffer& framebuffer,\n    const PreparedDrawOrderEntry& entry\) \{.*?\n\}\n\nvoid execute_prepared_draw_entry\(.*?\n\}\n\nvoid execute_prepared_model_transform",
    "void preflight_prepared_draw_entry(\n"
    "    const Framebuffer& framebuffer,\n"
    "    const PreparedDrawOrderEntry& entry,\n"
    "    const PreparedDrawExecutionOverrides& overrides) {\n"
    "    const MaterialDraw& draw = prepared_draw_for(entry);\n"
    "    const PreparedModelSubmission& prepared = entry.prepared->prepared();\n"
    "    const ModelAsset& asset = prepared.asset();\n"
    "    const ModelRenderOptions options = prepared_draw_execution_options(prepared, overrides);\n"
    "    detail::validate_alpha_test_state(options.alpha_test_state);\n"
    "    detail::preflight_mesh_range_submission(\n"
    "        framebuffer,\n"
    "        asset.mesh,\n"
    "        draw.range,\n"
    "        color_binding_for(asset),\n"
    "        texture_binding_for(draw, options),\n"
    "        options.directional_light,\n"
    "        options.point_light,\n"
    "        options.fixed_lights,\n"
    "        draw.material,\n"
    "        base_color_source_for(asset, draw),\n"
    "        options.cull_mode,\n"
    "        options.front_face,\n"
    "        options.depth_state,\n"
    "        options.viewport_state,\n"
    "        options.stencil_state,\n"
    "        options.blend_state,\n"
    "        options.alpha_to_coverage_state,\n"
    "        options.shadow_state,\n"
    "        options.point_shadow_state,\n"
    "        &entry.model,\n"
    "        false);\n"
    "}\n\n"
    "void preflight_prepared_draw_entry(\n"
    "    const Framebuffer& framebuffer,\n"
    "    const PreparedDrawOrderEntry& entry) {\n"
    "    preflight_prepared_draw_entry(framebuffer, entry, {});\n"
    "}\n\n"
    "void execute_prepared_draw_entry(\n"
    "    Framebuffer& framebuffer,\n"
    "    const PreparedDrawOrderEntry& entry,\n"
    "    const Mat4& view,\n"
    "    const Mat4& projection,\n"
    "    const PreparedDrawExecutionOverrides& overrides) {\n"
    "    const MaterialDraw& draw = prepared_draw_for(entry);\n"
    "    const PreparedModelSubmission& prepared = entry.prepared->prepared();\n"
    "    const ModelAsset& asset = prepared.asset();\n"
    "    const ModelRenderOptions options = prepared_draw_execution_options(prepared, overrides);\n"
    "    Rasterizer rasterizer = model_rasterizer(framebuffer, asset, draw, options);\n"
    "    rasterizer.draw_mesh_range(asset.mesh, draw.range, entry.model, view, projection);\n"
    "}\n\n"
    "void execute_prepared_draw_entry(\n"
    "    Framebuffer& framebuffer,\n"
    "    const PreparedDrawOrderEntry& entry,\n"
    "    const Mat4& view,\n"
    "    const Mat4& projection) {\n"
    "    execute_prepared_draw_entry(framebuffer, entry, view, projection, {});\n"
    "}\n\n"
    "void execute_prepared_model_transform",
    "model_renderer per-draw override implementation")
text = regex_once(
    text,
    r"void preflight_prepared_draw_order\(\n    const Framebuffer& framebuffer,\n    std::span<const PreparedDrawOrderEntry> entries\) \{.*?\n\}\n\nvoid draw_prepared_draw_order\(.*?\n\}\n\nvoid draw_prepared_model\(",
    "void preflight_prepared_draw_order(\n"
    "    const Framebuffer& framebuffer,\n"
    "    std::span<const PreparedDrawOrderEntry> entries) {\n"
    "    preflight_prepared_draw_order(framebuffer, entries, {});\n"
    "}\n\n"
    "void preflight_prepared_draw_order(\n"
    "    const Framebuffer& framebuffer,\n"
    "    std::span<const PreparedDrawOrderEntry> entries,\n"
    "    const PreparedDrawExecutionOverrides& overrides) {\n"
    "    for (const PreparedDrawOrderEntry& entry : entries) {\n"
    "        preflight_prepared_draw_entry(framebuffer, entry, overrides);\n"
    "    }\n"
    "}\n\n"
    "void draw_prepared_draw_order(\n"
    "    Framebuffer& framebuffer,\n"
    "    std::span<const PreparedDrawOrderEntry> entries,\n"
    "    const Mat4& view,\n"
    "    const Mat4& projection) {\n"
    "    draw_prepared_draw_order(framebuffer, entries, view, projection, {});\n"
    "}\n\n"
    "void draw_prepared_draw_order(\n"
    "    Framebuffer& framebuffer,\n"
    "    std::span<const PreparedDrawOrderEntry> entries,\n"
    "    const Mat4& view,\n"
    "    const Mat4& projection,\n"
    "    const PreparedDrawExecutionOverrides& overrides) {\n"
    "    preflight_prepared_draw_order(framebuffer, entries, overrides);\n"
    "    for (const PreparedDrawOrderEntry& entry : entries) {\n"
    "        execute_prepared_draw_entry(framebuffer, entry, view, projection, overrides);\n"
    "    }\n"
    "}\n\n"
    "void draw_prepared_model(",
    "model_renderer public draw-order override overloads")
write(path, text)


# PreparedSceneEvaluation keeps camera matrices immutable while callers may
# supply a bounded execution overlay for camera-dependent shading state.
path = "include/tiny_renderer/prepared_scene.hpp"
text = read(path)
text = regex_once(
    text,
    r"inline void preflight_prepared_scene_evaluation\(\n    const Framebuffer& framebuffer,\n    const PreparedSceneEvaluation& evaluation\) \{.*?\n\}\n\ninline void draw_prepared_scene_evaluation\(.*?\n\}\n\n\}  // namespace tiny_renderer",
    "inline void preflight_prepared_scene_evaluation(\n"
    "    const Framebuffer& framebuffer,\n"
    "    const PreparedSceneEvaluation& evaluation,\n"
    "    const PreparedDrawExecutionOverrides& overrides) {\n"
    "    preflight_prepared_draw_order(\n"
    "        framebuffer, evaluation.caller_order_draws(), overrides);\n"
    "    preflight_prepared_draw_order(\n"
    "        framebuffer, evaluation.back_to_front_draws(), overrides);\n"
    "}\n\n"
    "inline void preflight_prepared_scene_evaluation(\n"
    "    const Framebuffer& framebuffer,\n"
    "    const PreparedSceneEvaluation& evaluation) {\n"
    "    preflight_prepared_scene_evaluation(framebuffer, evaluation, {});\n"
    "}\n\n"
    "inline void draw_prepared_scene_evaluation(\n"
    "    Framebuffer& framebuffer,\n"
    "    const PreparedSceneEvaluation& evaluation,\n"
    "    const PreparedDrawExecutionOverrides& overrides) {\n"
    "    preflight_prepared_scene_evaluation(framebuffer, evaluation, overrides);\n"
    "    draw_prepared_draw_order(\n"
    "        framebuffer,\n"
    "        evaluation.visible_caller_order_draws(),\n"
    "        evaluation.view(),\n"
    "        evaluation.projection(),\n"
    "        overrides);\n"
    "    draw_prepared_draw_order(\n"
    "        framebuffer,\n"
    "        evaluation.visible_back_to_front_draws(),\n"
    "        evaluation.view(),\n"
    "        evaluation.projection(),\n"
    "        overrides);\n"
    "}\n\n"
    "inline void draw_prepared_scene_evaluation(\n"
    "    Framebuffer& framebuffer,\n"
    "    const PreparedSceneEvaluation& evaluation) {\n"
    "    draw_prepared_scene_evaluation(framebuffer, evaluation, {});\n"
    "}\n\n"
    "}  // namespace tiny_renderer",
    "prepared_scene execution override overloads")
write(path, text)


# Public offline API owns an address-stable PreparedScenePlan and settings once.
path = "include/tiny_renderer/offline_render.hpp"
text = read(path)
text = replace_once(
    text,
    "#include <fstream>\n#include <optional>",
    "#include <fstream>\n#include <memory>\n#include <optional>",
    "offline_render memory include")
text = replace_once(
    text,
    "#include <string_view>\n#include <vector>",
    "#include <string_view>\n#include <utility>\n#include <vector>",
    "offline_render utility include")
text = replace_once(
    text,
    "#include \"tiny_renderer/model_renderer.hpp\"",
    "#include \"tiny_renderer/model_renderer.hpp\"\n#include \"tiny_renderer/prepared_scene.hpp\"",
    "offline_render prepared scene include")
entry_anchor = (
    "struct OfflineSceneEntry {\n"
    "    const ModelAsset* asset{nullptr};\n"
    "    Mat4 model{Mat4::identity()};\n"
    "    ModelRenderOptions options{};\n"
    "    std::optional<OfflineSceneTransparencyMode> transparency_mode{};\n"
    "};\n")
entry_extension = entry_anchor + (
    "\n// Reusable explicit-camera mixed-transparency scene. Canonical model,\n"
    "// material, texture, and per-draw spatial ownership is prepared once; each\n"
    "// render only reevaluates camera ordering/visibility and camera-dependent\n"
    "// shading state. Borrowed resources referenced by OfflineRenderSettings\n"
    "// must outlive this prepared scene just as they must outlive one-shot calls.\n"
    "class PreparedOfflineMixedScene {\n"
    "public:\n"
    "    PreparedOfflineMixedScene(const PreparedOfflineMixedScene&) = delete;\n"
    "    PreparedOfflineMixedScene& operator=(const PreparedOfflineMixedScene&) = delete;\n"
    "    PreparedOfflineMixedScene(PreparedOfflineMixedScene&&) noexcept = default;\n"
    "    PreparedOfflineMixedScene& operator=(PreparedOfflineMixedScene&&) noexcept = default;\n\n"
    "    [[nodiscard]] const PreparedScenePlan& plan() const noexcept { return *plan_; }\n"
    "    [[nodiscard]] const OfflineRenderSettings& settings() const noexcept { return settings_; }\n\n"
    "private:\n"
    "    friend PreparedOfflineMixedScene prepare_offline_mixed_scene(\n"
    "        std::span<const OfflineSceneEntry> entries,\n"
    "        OfflineRenderSettings settings);\n\n"
    "    PreparedOfflineMixedScene(\n"
    "        std::unique_ptr<PreparedScenePlan> plan,\n"
    "        OfflineRenderSettings settings)\n"
    "        : plan_(std::move(plan)), settings_(std::move(settings)) {}\n\n"
    "    std::unique_ptr<PreparedScenePlan> plan_;\n"
    "    OfflineRenderSettings settings_;\n"
    "};\n\n"
    "[[nodiscard]] PreparedOfflineMixedScene prepare_offline_mixed_scene(\n"
    "    std::span<const OfflineSceneEntry> entries,\n"
    "    OfflineRenderSettings settings = {});\n\n"
    "// Reuses the prepared scene ownership with one explicit camera. The active\n"
    "// camera eye is rebound to offline environment reflection at execution time\n"
    "// without copying or rebuilding the owned canonical model snapshots.\n"
    "[[nodiscard]] Framebuffer render_prepared_scene_preview(\n"
    "    const PreparedOfflineMixedScene& scene,\n"
    "    const OfflineSceneCamera& camera);\n")
text = replace_once(text, entry_anchor, entry_extension, "offline reusable scene declarations")
write(path, text)


# Offline mixed rendering now consumes the M86 scene plan instead of maintaining
# parallel one-off spatial/order/filter vectors.
path = "src/offline_render.cpp"
text = read(path)
text = replace_once(
    text,
    "#include <limits>\n#include <optional>",
    "#include <limits>\n#include <memory>\n#include <optional>",
    "offline_render cpp memory include")
text = replace_once(
    text,
    "#include \"tiny_renderer/prepared_spatial.hpp\"",
    "#include \"tiny_renderer/prepared_scene.hpp\"",
    "offline_render cpp prepared scene include")
insert_marker = "Framebuffer render_scene_preview(\n    std::span<const OfflineSceneEntry> entries,"
insert_code = (
    "PreparedOfflineMixedScene prepare_offline_mixed_scene(\n"
    "    std::span<const OfflineSceneEntry> entries,\n"
    "    OfflineRenderSettings settings) {\n"
    "    validate_settings(settings);\n\n"
    "    std::vector<PreparedScenePlanEntry> plan_entries;\n"
    "    plan_entries.reserve(entries.size());\n"
    "    for (const OfflineSceneEntry& entry : entries) {\n"
    "        if (entry.asset == nullptr) {\n"
    "            throw std::invalid_argument(\"offline prepared scene entry requires a model asset\");\n"
    "        }\n"
    "        if (!entry.transparency_mode) {\n"
    "            throw std::invalid_argument(\n"
    "                \"offline prepared mixed scene requires an explicit transparency mode for every entry\");\n"
    "        }\n\n"
    "        ModelRenderOptions options = entry.options;\n"
    "        apply_offline_scene_transparency_mode(options, *entry.transparency_mode);\n"
    "        // Environment reflection is injected once for canonical ownership,\n"
    "        // then its viewer is rebound from the active camera for each render.\n"
    "        options = inject_offline_environment(\n"
    "            settings, std::move(options), Vec3{0.0F, 0.0F, 0.0F});\n"
    "        const PreparedScenePhase phase =\n"
    "            *entry.transparency_mode == OfflineSceneTransparencyMode::SourceAlpha\n"
    "            ? PreparedScenePhase::BackToFront\n"
    "            : PreparedScenePhase::CallerOrder;\n"
    "        plan_entries.push_back({\n"
    "            prepare_spatial_model(*entry.asset, std::move(options)),\n"
    "            entry.model,\n"
    "            phase,\n"
    "        });\n"
    "    }\n\n"
    "    return PreparedOfflineMixedScene{\n"
    "        std::make_unique<PreparedScenePlan>(std::move(plan_entries)),\n"
    "        std::move(settings)};\n"
    "}\n\n"
    "Framebuffer render_prepared_scene_preview(\n"
    "    const PreparedOfflineMixedScene& scene,\n"
    "    const OfflineSceneCamera& camera) {\n"
    "    const OfflineRenderSettings& settings = scene.settings();\n"
    "    validate_settings(settings);\n"
    "    validate_offline_scene_camera(camera);\n"
    "    const PreviewGeometryState geometry = explicit_scene_geometry_state(camera, settings);\n"
    "    const PreparedSceneEvaluation evaluation = evaluate_prepared_scene_plan(\n"
    "        scene.plan(), geometry.view, geometry.projection);\n\n"
    "    PreparedDrawExecutionOverrides overrides;\n"
    "    if (settings.environment_reflection) {\n"
    "        overrides.environment_reflection_viewer_position = camera.eye;\n"
    "    }\n\n"
    "    Framebuffer framebuffer(settings.width, settings.height, settings.sample_count);\n"
    "    // Complete unfiltered-plan target validation must happen before clear or\n"
    "    // environment mutation, exactly as in the one-shot mixed transaction.\n"
    "    preflight_prepared_scene_evaluation(framebuffer, evaluation, overrides);\n"
    "    framebuffer.clear(settings.clear_color);\n"
    "    draw_preview_environment(framebuffer, settings, geometry.camera);\n"
    "    draw_prepared_scene_evaluation(framebuffer, evaluation, overrides);\n"
    "    return framebuffer;\n"
    "}\n\n"
    + insert_marker)
text = replace_once(text, insert_marker, insert_code, "offline reusable scene implementation")
text = replace_once(
    text,
    "    std::vector<PreparedModelListEntry> ordered_entries;\n"
    "    std::vector<PreparedSpatialSubmission> depth_writing_spatial;\n"
    "    std::vector<PreparedSpatialListEntry> depth_writing_spatial_entries;\n"
    "    std::vector<PreparedDrawOrderEntry> depth_writing_draws;\n"
    "    std::vector<PreparedDrawOrderEntry> visible_depth_writing_draws;\n"
    "    std::vector<PreparedSpatialSubmission> source_alpha_spatial;\n"
    "    std::vector<PreparedSpatialListEntry> source_alpha_spatial_entries;\n"
    "    std::vector<PreparedDrawOrderEntry> ordered_source_alpha_draws;\n"
    "    std::vector<PreparedDrawOrderEntry> visible_source_alpha_draws;\n",
    "    std::vector<PreparedModelListEntry> ordered_entries;\n"
    "    std::unique_ptr<PreparedScenePlan> mixed_plan;\n"
    "    std::optional<PreparedSceneEvaluation> mixed_evaluation;\n"
    "    PreparedDrawExecutionOverrides mixed_overrides;\n",
    "offline mixed temporary plan state")
text = regex_once(
    text,
    r"    switch \(ordering\) \{\n        case OfflineSceneOrdering::InputOrder:\n            break;\n        case OfflineSceneOrdering::BackToFront:.*?\n    \}\n\n    Framebuffer framebuffer",
    "    switch (ordering) {\n"
    "        case OfflineSceneOrdering::InputOrder:\n"
    "            break;\n"
    "        case OfflineSceneOrdering::BackToFront:\n"
    "            ordered_entries = order_prepared_model_list_back_to_front(\n"
    "                render_span, geometry.view);\n"
    "            break;\n"
    "        case OfflineSceneOrdering::MixedTransparency: {\n"
    "            std::vector<PreparedScenePlanEntry> plan_entries;\n"
    "            plan_entries.reserve(entries.size());\n"
    "            for (std::size_t i = 0U; i < entries.size(); ++i) {\n"
    "                const PreparedScenePhase phase =\n"
    "                    *entries[i].transparency_mode == OfflineSceneTransparencyMode::SourceAlpha\n"
    "                    ? PreparedScenePhase::BackToFront\n"
    "                    : PreparedScenePhase::CallerOrder;\n"
    "                plan_entries.push_back({\n"
    "                    prepare_spatial_submission(std::move(prepared[i])),\n"
    "                    render_entries[i].model,\n"
    "                    phase,\n"
    "                });\n"
    "            }\n"
    "            mixed_plan = std::make_unique<PreparedScenePlan>(std::move(plan_entries));\n"
    "            mixed_evaluation = evaluate_prepared_scene_plan(\n"
    "                *mixed_plan, geometry.view, geometry.projection);\n"
    "            if (settings.environment_reflection) {\n"
    "                mixed_overrides.environment_reflection_viewer_position = active_camera.eye;\n"
    "            }\n"
    "            break;\n"
    "        }\n"
    "    }\n\n"
    "    Framebuffer framebuffer",
    "offline mixed scene plan orchestration")
text = regex_once(
    text,
    r"    if \(ordering == OfflineSceneOrdering::MixedTransparency\) \{\n        preflight_prepared_draw_order\(.*?\n    \} else \{\n        preflight_prepared_model_list\(framebuffer, render_span\);\n    \}",
    "    if (ordering == OfflineSceneOrdering::MixedTransparency) {\n"
    "        if (!mixed_evaluation) {\n"
    "            throw std::logic_error(\"mixed offline scene evaluation was not prepared\");\n"
    "        }\n"
    "        preflight_prepared_scene_evaluation(\n"
    "            framebuffer, *mixed_evaluation, mixed_overrides);\n"
    "    } else {\n"
    "        preflight_prepared_model_list(framebuffer, render_span);\n"
    "    }",
    "offline mixed complete preflight")
text = regex_once(
    text,
    r"        case OfflineSceneOrdering::MixedTransparency:\n            draw_prepared_draw_order\(.*?\n            break;",
    "        case OfflineSceneOrdering::MixedTransparency:\n"
    "            draw_prepared_scene_evaluation(\n"
    "                framebuffer, *mixed_evaluation, mixed_overrides);\n"
    "            break;",
    "offline mixed scene execution")
write(path, text)


# Register focused M87 integration coverage.
path = "CMakeLists.txt"
text = read(path)
text = replace_once(
    text,
    "    add_executable(tiny_renderer_offline_render_tests tests/test_offline_render.cpp)\n"
    "    target_link_libraries(tiny_renderer_offline_render_tests PRIVATE tiny_renderer)\n"
    "    add_test(NAME tiny_renderer_offline_render_tests COMMAND tiny_renderer_offline_render_tests)\n",
    "    add_executable(tiny_renderer_offline_render_tests tests/test_offline_render.cpp)\n"
    "    target_link_libraries(tiny_renderer_offline_render_tests PRIVATE tiny_renderer)\n"
    "    add_test(NAME tiny_renderer_offline_render_tests COMMAND tiny_renderer_offline_render_tests)\n\n"
    "    add_executable(tiny_renderer_offline_prepared_scene_tests tests/test_offline_prepared_scene.cpp)\n"
    "    target_link_libraries(tiny_renderer_offline_prepared_scene_tests PRIVATE tiny_renderer)\n"
    "    add_test(\n"
    "        NAME tiny_renderer_offline_prepared_scene_tests\n"
    "        COMMAND tiny_renderer_offline_prepared_scene_tests)\n",
    "CMake M87 test registration")
write(path, text)


# Seal M86 and promote the live status layer to the M87 implementation slice.
path = "STATUS.md"
text = read(path)
text = replace_once(
    text,
    "## Integrated architecture through Milestone 85",
    "## Integrated architecture through Milestone 86",
    "STATUS integrated milestone")
text = replace_once(
    text,
    "Milestone 84 consumes conservative visibility in the real mixed-transparency offline source-alpha phase while retaining complete unfiltered-plan target preflight before mutation. Milestone 85 extends the same contract across the complete mixed-transparency transaction: opaque/A2C work is flattened in caller/canonical order, source-alpha work remains stable far-to-near, both complete plans are preflighted before clear/environment/geometry mutation, and only visibility-retained subsets execute through the existing prepared draw executor. A 4x mixed scene with off-frustum depth-writing work is regression-locked to exact resolved byte/hash and per-sample RGB/depth/stencil equivalence against manual omission, while target-invalid off-frustum work still rejects fail closed.\n\n"
    "The exact integrated `main` commit is `918c412f37f97bcf2a9822e0264783417f1fca21` (Milestone 85). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.",
    "Milestone 84 consumes conservative visibility in the real mixed-transparency offline source-alpha phase while retaining complete unfiltered-plan target preflight before mutation. Milestone 85 extends the same contract across the complete mixed-transparency transaction: opaque/A2C work is flattened in caller/canonical order, source-alpha work remains stable far-to-near, both complete plans are preflighted before clear/environment/geometry mutation, and only visibility-retained subsets execute through the existing prepared draw executor. A 4x mixed scene with off-frustum depth-writing work is regression-locked to exact resolved byte/hash and per-sample RGB/depth/stencil equivalence against manual omission, while target-invalid off-frustum work still rejects fail closed.\n\n"
    "Milestone 86 promotes those one-off draw vectors into an address-stable `PreparedScenePlan` that owns prepared spatial submissions once, reevaluates caller-order/back-to-front planning and conservative visibility for each camera, binds each evaluation to the exact view/projection matrices that produced it, and preserves complete unfiltered-plan preflight before visible execution.\n\n"
    "The exact integrated `main` commit is `7979bce89104773d732daee9cbac998a1db7b9c6` (Milestone 86). Its Linux, macOS, and ASan/UBSan post-merge CI gates are green.",
    "STATUS M86 integration seal")
text = replace_once(
    text,
    "Milestone 86 is the active implementation surface on branch `milestone-86-prepared-scene-plan`; no separate parallel M86 implementation should be opened while it is active.",
    "Milestone 87 is the active implementation surface on branch `milestone-87-offline-prepared-scene`; no separate parallel M87 implementation should be opened while it is active.",
    "STATUS active branch")
text = regex_once(
    text,
    r"## Milestone 86 candidate — reusable prepared scene plan\n.*?\n## Architectural invariants",
    "## Milestone 87 candidate — reusable offline prepared mixed scenes\n\n"
    "M87 gives the M86 reusable plan a real offline mixed-transparency consumer. Scene/model/spatial ownership is prepared once, while each explicit camera render reevaluates ordering/visibility and rebinds camera-dependent environment reflection without rebuilding canonical model snapshots.\n\n"
    "Acceptance surface:\n\n"
    "- `PreparedOfflineMixedScene` owns one address-stable `PreparedScenePlan` plus the validated offline settings snapshot; source model assets may be destroyed after preparation while imported/shared texture lifetime remains retained by prepared ownership;\n"
    "- preparation requires the same explicit per-entry mixed-transparency declaration as the one-shot transaction and maps opaque/A2C work to caller order and source-alpha work to stable back-to-front execution;\n"
    "- each explicit-camera render calls `evaluate_prepared_scene_plan` for fresh view-depth ordering and conservative visibility without reconstructing mesh/material/texture/spatial state;\n"
    "- a bounded prepared-draw execution override changes only the environment-reflection viewer position for the active camera; it copies render options for execution but never copies or rebuilds the owned `ModelAsset`;\n"
    "- both complete unfiltered execution phases are preflighted with the same active-camera override before clear/environment/geometry mutation; visibility remains execution selection only;\n"
    "- the existing one-shot `render_scene_preview(..., MixedTransparency)` delegates its mixed planning/filtering/execution to `PreparedScenePlan` rather than maintaining parallel depth-writing/source-alpha orchestration vectors;\n"
    "- reusable explicit-camera output is regression-locked against one-shot mixed rendering on 4x targets for resolved byte/hash plus exact per-sample RGB/depth/stencil attachments across multiple cameras;\n"
    "- camera B reflection must match one-shot camera B rendering after a prior camera A render, proving the viewer was rebound rather than frozen in the prepared snapshot;\n"
    "- target-dependent constraints such as alpha-to-coverage on a 1x framebuffer remain fail-closed at render preflight;\n"
    "- M87 adds no new raster/material/framebuffer path, auto-fit reusable camera policy, scene hierarchy, BVH/occlusion culling, or performance claim.\n\n"
    "## Architectural invariants",
    "STATUS M87 candidate section")
text = replace_once(
    text,
    "- Camera-dependent visibility state is bound to the exact matrices that produced it; reusable ownership does not imply reusable camera-derived subsets.\n",
    "- Camera-dependent visibility state is bound to the exact matrices that produced it; reusable ownership does not imply reusable camera-derived subsets.\n"
    "- Reusable offline execution may overlay camera-dependent reflection viewer state, but canonical prepared mesh/material/texture/spatial ownership remains immutable and is not rebuilt per camera.\n",
    "STATUS reusable camera invariant")
text = regex_once(
    text,
    r"## Promotion after Milestone 86\n\n.*?\n?$",
    "## Promotion after Milestone 87\n\n"
    "After M87 converges, re-read exact live `main`, open PRs/issues, active branches, and the reusable offline consumer. The next promotion should decide whether repeated-frame/camera-sequence orchestration needs a bounded reusable frame API and collect controlled measurements that distinguish preparation cost from per-camera evaluation/execution cost. Acceleration structures such as BVHs or occlusion culling remain premature until measured workloads show a justified bottleneck and the complete fail-closed transaction contract can be preserved.\n",
    "STATUS M87 promotion")
write(path, text)

print("M87 source integration patch applied")
