#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <system_error>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/gltf_loader.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/shadow_renderer.hpp"
#include "tiny_renderer/skinning.hpp"

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

std::filesystem::path fixture_path(const char* name) {
#ifndef TINY_RENDERER_SOURCE_DIR
#error TINY_RENDERER_SOURCE_DIR must be provided for glTF tests
#endif
    return std::filesystem::path(TINY_RENDERER_SOURCE_DIR)
        / "tests" / "fixtures" / name;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to read test text fixture");
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> read_bytes(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to read test binary fixture");
    }
    const std::string raw{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const auto* begin =
        reinterpret_cast<const std::uint8_t*>(raw.data());
    return std::vector<std::uint8_t>(
        begin,
        begin + raw.size());
}

void write_text(
    const std::filesystem::path& path,
    const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    if (!output || !output.write(
            text.data(),
            static_cast<std::streamsize>(text.size()))) {
        throw std::runtime_error("failed to write test glTF JSON");
    }
}

void write_bytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!output
        || (!bytes.empty()
            && !output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))) {
        throw std::runtime_error("failed to write test glTF buffer");
    }
}

std::filesystem::path case_root(const std::string& name) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path()
        / ("tiny_renderer_gltf_" + name);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    if (!std::filesystem::create_directories(root, error)
        || error) {
        throw std::runtime_error("failed to create glTF test directory");
    }
    return root;
}

std::filesystem::path write_case(
    const std::string& name,
    const std::string& json,
    const std::vector<std::uint8_t>& bytes) {
    const std::filesystem::path root = case_root(name);
    write_text(root / "case.gltf", json);
    write_bytes(root / "skinned_triangle.bin", bytes);
    return root / "case.gltf";
}

std::filesystem::path write_morph_case(
    const std::string& name,
    const std::string& json,
    const std::vector<std::uint8_t>& bytes) {
    const std::filesystem::path root = case_root(name);
    write_text(root / "case.gltf", json);
    write_bytes(root / "skinned_morph_triangle.bin", bytes);
    return root / "case.gltf";
}

void replace_once(
    std::string& text,
    const std::string& before,
    const std::string& after) {
    const std::size_t position = text.find(before);
    if (position == std::string::npos) {
        throw std::runtime_error(
            "test replacement anchor not found: " + before);
    }
    text.replace(position, before.size(), after);
}

void replace_animation_section(
    std::string& text,
    const std::string& replacement) {
    const std::string begin_marker = "  \"animations\": [";
    const std::string end_marker = "  \"scenes\":";
    const std::size_t begin = text.find(begin_marker);
    const std::size_t end = text.find(end_marker, begin);
    if (begin == std::string::npos || end == std::string::npos) {
        throw std::runtime_error(
            "test animation section anchor not found");
    }
    text.replace(begin, end - begin, replacement);
}



void replace_animations_array(
    std::string& text,
    const std::string& body) {
    const std::string begin_marker = "  \"animations\": [";
    const std::string end_marker = "  ],\n  \"scenes\"";
    const std::size_t begin = text.find(begin_marker);
    const std::size_t end = text.find(end_marker, begin);
    if (begin == std::string::npos || end == std::string::npos) {
        throw std::runtime_error(
            "test animation-array replacement anchor not found");
    }
    text.replace(
        begin,
        end + 5U - begin,
        "  \"animations\": [\n"
            + body
            + "\n  ],\n");
}

void set_f32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    float value) {
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        throw std::out_of_range("test float write exceeds fixture");
    }
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    bytes[offset + 0U] =
        static_cast<std::uint8_t>(bits & 0xFFU);
    bytes[offset + 1U] =
        static_cast<std::uint8_t>((bits >> 8U) & 0xFFU);
    bytes[offset + 2U] =
        static_cast<std::uint8_t>((bits >> 16U) & 0xFFU);
    bytes[offset + 3U] =
        static_cast<std::uint8_t>((bits >> 24U) & 0xFFU);
}

void append_f32(
    std::vector<std::uint8_t>& bytes,
    float value) {
    const std::uint32_t bits =
        std::bit_cast<std::uint32_t>(value);
    bytes.push_back(
        static_cast<std::uint8_t>(bits & 0xFFU));
    bytes.push_back(
        static_cast<std::uint8_t>((bits >> 8U) & 0xFFU));
    bytes.push_back(
        static_cast<std::uint8_t>((bits >> 16U) & 0xFFU));
    bytes.push_back(
        static_cast<std::uint8_t>((bits >> 24U) & 0xFFU));
}

void append_vec3(
    std::vector<std::uint8_t>& bytes,
    const Vec3& value) {
    append_f32(bytes, value.x);
    append_f32(bytes, value.y);
    append_f32(bytes, value.z);
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

VertexSkinBinding binding(
    std::initializer_list<SkinInfluence> influences) {
    return VertexSkinBinding(influences);
}

ModelAsset manual_model() {
    ModelAsset model;
    const VaryingPack normal{0.0F, 0.0F, 1.0F};
    model.mesh.vertices = {
        Vertex::with_varyings(
            {-0.5F, -0.5F, 0.0F}, normal),
        Vertex::with_varyings(
            {0.5F, -0.5F, 0.0F}, normal),
        Vertex::with_varyings(
            {0.0F, 0.5F, 0.0F}, normal),
    };
    model.mesh.triangles = {{0U, 1U, 2U}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    model.draws = {draw};
    return model;
}

SkeletalRigPtr manual_rig() {
    return std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{
            1U,
            std::nullopt,
        },
        std::vector<Mat4>{
            Mat4::translation({-0.35F, 0.0F, 0.0F}),
            Mat4::translation({-0.1F, 0.0F, 0.0F}),
        },
        std::vector<VertexSkinBinding>{
            binding({SkinInfluence{1U, 1.0F}}),
            binding({SkinInfluence{0U, 1.0F}}),
            binding({
                SkinInfluence{0U, 0.5F},
                SkinInfluence{1U, 0.5F},
            }),
        });
}

MorphTargetSetPtr manual_morph_targets() {
    MorphTarget first;
    first.position_deltas = {
        {0.0F, 0.0F, 0.0F},
        {0.25F, 0.0F, 0.0F},
        {0.0F, 0.25F, 0.0F},
    };
    first.normal_deltas = std::vector<Vec3>{
        {0.25F, 0.0F, 0.0F},
        {0.25F, 0.0F, 0.0F},
        {0.25F, 0.0F, 0.0F},
    };

    MorphTarget second;
    second.position_deltas = {
        {-0.25F, 0.0F, 0.0F},
        {0.0F, -0.25F, 0.0F},
        {0.0F, 0.0F, 0.0F},
    };
    second.normal_deltas = std::vector<Vec3>{
        {0.0F, 0.25F, 0.0F},
        {0.0F, 0.25F, 0.0F},
        {0.0F, 0.25F, 0.0F},
    };

    return std::make_shared<const MorphTargetSet>(
        std::vector<MorphTarget>{
            std::move(first),
            std::move(second),
        });
}

MorphStatePtr manual_default_morph_state() {
    return std::make_shared<const MorphState>(
        manual_morph_targets(),
        std::vector<float>{0.5F, -0.25F});
}

DirectionalLight test_light() {
    DirectionalLight light;
    light.enabled = true;
    light.normal = {0U, 1U, 2U};
    light.direction_to_light =
        normalize(Vec3{0.25F, 0.5F, 1.0F});
    light.ambient = 0.15F;
    light.diffuse = 0.85F;
    return light;
}

void test_valid_fixture_decodes_into_existing_ownership() {
    const GltfSkinnedAsset imported =
        load_gltf_skinned_asset_file(
            fixture_path("skinned_triangle.gltf"));

    check(imported.rig != nullptr,
          "glTF fixture produces an immutable M108 rig");
    check(imported.model.mesh.vertices.size() == 3U,
          "glTF fixture decodes three canonical vertices");
    check(imported.model.mesh.triangles.size() == 1U,
          "glTF fixture decodes one indexed triangle");
    check(imported.model.draws.size() == 1U,
          "glTF fixture projects one primitive onto one existing MaterialDraw");
    check(imported.normal_channels
              == std::optional<std::array<std::size_t, 3>>{
                  std::array<std::size_t, 3>{0U, 1U, 2U}},
          "glTF NORMAL is projected onto canonical smooth varying channels");
    check(imported.rest_local_transforms.size() == 2U,
          "glTF fixture owns one complete rest local transform per skin joint");

    if (imported.rig) {
        const auto parents = imported.rig->parents();
        check(
            parents.size() == 2U
                && parents[0]
                && *parents[0] == 1U
                && !parents[1],
            "glTF child-first skin.joints order maps node hierarchy into arbitrary-order M108 parents");
        const auto inverse = imported.rig->inverse_bind_matrices();
        check(
            inverse.size() == 2U
                && nearly_equal(inverse[0](0U, 3U), -0.35F)
                && nearly_equal(inverse[1](0U, 3U), -0.1F),
            "glTF column-major inverse-bind MAT4 data maps into renderer matrices in skin.joints order");
    }

    if (imported.rest_local_transforms.size() == 2U) {
        check(
            nearly_equal(
                imported.rest_local_transforms[0](0U, 3U),
                0.25F)
                && nearly_equal(
                    imported.rest_local_transforms[1](0U, 3U),
                    0.1F),
            "glTF node hierarchy becomes child-local and root-local M108 rest state");
    }

    if (imported.rig
        && imported.rest_local_transforms.size() == 2U) {
        const SkinningStatePtr rest =
            imported.rig->resolve_pose(
                imported.rest_local_transforms);
        const auto matrices = rest->skin_matrices();
        check(
            matrices.size() == 2U
                && nearly_equal(matrices[0](0U, 0U), 1.0F)
                && nearly_equal(matrices[0](0U, 3U), 0.0F)
                && nearly_equal(matrices[1](0U, 0U), 1.0F)
                && nearly_equal(matrices[1](0U, 3U), 0.0F),
            "fixture inverse binds cancel imported rest joint worlds");
    }
}

void test_imported_asset_matches_independent_programmatic_render_and_shadow() {
    GltfSkinnedAsset imported =
        load_gltf_skinned_asset_file(
            fixture_path("skinned_triangle.gltf"));
    const ModelAsset manual = manual_model();
    const SkeletalRigPtr manual_skin = manual_rig();

    const std::vector<Mat4> posed_locals{
        Mat4::translation({0.45F, 0.0F, 0.0F}),
        Mat4::translation({0.1F, 0.0F, 0.0F})
            * Mat4::scale({1.35F, 1.0F, 1.0F}),
    };

    ModelRenderOptions imported_options;
    imported_options.directional_light = test_light();
    imported_options.skeletal_pose_state =
        std::make_shared<const SkeletalPoseState>(
            imported.rig,
            posed_locals);

    ModelRenderOptions manual_options;
    manual_options.directional_light = test_light();
    manual_options.skeletal_pose_state =
        std::make_shared<const SkeletalPoseState>(
            manual_skin,
            posed_locals);

    Framebuffer imported_fb(57U, 57U, SampleCount::Four);
    Framebuffer manual_fb(57U, 57U, SampleCount::Four);
    imported_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 5U);
    manual_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 5U);
    draw_model_asset(
        imported_fb,
        imported.model,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        imported_options);
    draw_model_asset(
        manual_fb,
        manual,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        manual_options);
    check_same_framebuffer(
        imported_fb,
        manual_fb,
        "imported non-trivial skin pose is exact-equivalent to independent ModelAsset+M108 reference");

    const PreparedModelSubmission imported_prepared =
        prepare_model_asset(imported.model, imported_options);
    const PreparedModelSubmission manual_prepared =
        prepare_model_asset(manual, manual_options);
    const std::array<PreparedModelListEntry, 1> imported_entry{{
        {&imported_prepared, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> manual_entry{{
        {&manual_prepared, Mat4::identity()},
    }};
    const auto imported_shadow =
        render_directional_shadow_map(
            imported_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                47U,
                47U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    const auto manual_shadow =
        render_directional_shadow_map(
            manual_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                47U,
                47U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    for (std::size_t y = 0U;
         y < imported_shadow->height();
         ++y) {
        for (std::size_t x = 0U;
             x < imported_shadow->width();
             ++x) {
            check(
                imported_shadow->depth_at(x, y)
                    == manual_shadow->depth_at(x, y),
                "imported skinned shadow silhouette matches independent M108 reference");
        }
    }
}

void test_external_buffer_is_not_retained_after_import() {
    const std::string json =
        read_text(fixture_path("skinned_triangle.gltf"));
    const std::vector<std::uint8_t> bytes =
        read_bytes(fixture_path("skinned_triangle.bin"));
    const std::filesystem::path path =
        write_case("owned_lifetime", json, bytes);
    GltfSkinnedAsset imported =
        load_gltf_skinned_asset_file(path);

    std::error_code error;
    std::filesystem::remove_all(path.parent_path(), error);
    check(!error,
          "test can remove source glTF and external buffer after import");

    ModelRenderOptions options;
    options.directional_light = test_light();
    options.skeletal_pose_state =
        std::make_shared<const SkeletalPoseState>(
            imported.rig,
            imported.rest_local_transforms);
    Framebuffer framebuffer(37U, 37U, SampleCount::Four);
    draw_model_asset(
        framebuffer,
        imported.model,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        options);
    check(
        !framebuffer.rgb8().empty(),
        "imported model and rig retain no source-file lifetime dependency");
}

void test_json_schema_and_path_fail_closed() {
    const std::string valid =
        read_text(fixture_path("skinned_triangle.gltf"));
    const std::vector<std::uint8_t> bytes =
        read_bytes(fixture_path("skinned_triangle.bin"));

    check_throws<GltfLoadError>(
        [&] {
            (void)load_gltf_skinned_asset_file(
                write_case(
                    "malformed_json",
                    "{\"asset\": [",
                    bytes));
        },
        "malformed JSON is rejected");

    {
        std::string json = valid;
        replace_once(
            json,
            "\"skinned_triangle.bin\"",
            "\"../skinned_triangle.bin\"");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("unsafe_uri", json, bytes));
            },
            "external buffer path traversal is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "\"mode\": 4",
            "\"mode\": 5");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("primitive_mode", json, bytes));
            },
            "non-TRIANGLES primitive mode is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "            \"POSITION\": 0,\n",
            "");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("missing_position", json, bytes));
            },
            "missing POSITION attribute is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "            \"POSITION\": 0,\n",
            "            \"POSITION\": 0,\n"
            "            \"POSITION\": 0,\n");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("duplicate_position", json, bytes));
            },
            "duplicate JSON attribute key is rejected before semantic import");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"bufferView\": 3, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\"}",
            "{\"bufferView\": 3, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\", \"normalized\": true}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("normalized_weights", json, bytes));
            },
            "unsupported normalized accessor mode is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [-0.5, -0.5, 0.0], \"max\": [0.5, 0.5, 0.0]}",
            "{\"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\"}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("missing_position_bounds", json, bytes));
            },
            "POSITION accessor without required min/max is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "\"max\": [0.5, 0.5, 0.0]",
            "\"max\": [0.4, 0.5, 0.0]");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("position_bounds_mismatch", json, bytes));
            },
            "POSITION data outside declared accessor min/max is rejected");
    }

}

void test_binary_range_joint_weight_and_inverse_bind_fail_closed() {
    const std::string valid =
        read_text(fixture_path("skinned_triangle.gltf"));
    const std::vector<std::uint8_t> valid_bytes =
        read_bytes(fixture_path("skinned_triangle.bin"));

    {
        std::vector<std::uint8_t> bytes = valid_bytes;
        bytes.pop_back();
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("truncated_buffer", valid, bytes));
            },
            "truncated external buffer is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"buffer\": 0, \"byteOffset\": 140, \"byteLength\": 128}",
            "{\"buffer\": 0, \"byteOffset\": 140, \"byteLength\": 129}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("view_overflow", json, valid_bytes));
            },
            "bufferView range beyond declared buffer is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [-0.5, -0.5, 0.0], \"max\": [0.5, 0.5, 0.0]}",
            "{\"bufferView\": 0, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\", \"min\": [-0.5, -0.5, 0.0], \"max\": [0.5, 0.5, 0.0]}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("accessor_overflow", json, valid_bytes));
            },
            "accessor element range beyond its bufferView is rejected before typed reads");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [-0.5, -0.5, 0.0], \"max\": [0.5, 0.5, 0.0]}",
            "{\"bufferView\": 0, \"byteOffset\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [-0.5, -0.5, 0.0], \"max\": [0.5, 0.5, 0.0]}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("accessor_alignment", json, valid_bytes));
            },
            "misaligned float accessor is rejected before typed reads");
    }

    {
        std::vector<std::uint8_t> bytes = valid_bytes;
        bytes[72U] = 7U;
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("joint_range", valid, bytes));
            },
            "JOINTS_0 value outside skin.joints is rejected");
    }

    {
        std::vector<std::uint8_t> bytes = valid_bytes;
        set_f32(bytes, 84U, -0.25F);
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("negative_weight", valid, bytes));
            },
            "negative WEIGHTS_0 value is rejected");
    }

    {
        std::vector<std::uint8_t> bytes = valid_bytes;
        set_f32(bytes, 84U, 0.75F);
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("weight_sum", valid, bytes));
            },
            "WEIGHTS_0 whose float sum is not one is rejected");
    }

    {
        std::vector<std::uint8_t> bytes = valid_bytes;
        // First inverse-bind matrix, column 0 row 3.
        set_f32(bytes, 140U + 3U * 4U, 0.25F);
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("projective_inverse", valid, bytes));
            },
            "projective inverse-bind matrix is rejected through affine ownership");
    }
}


SkeletalRigPtr manual_animated_rig() {
    return std::make_shared<const SkeletalRig>(
        std::vector<std::optional<std::size_t>>{
            1U,
            std::nullopt,
        },
        std::vector<Mat4>{
            Mat4::translation({-0.4F, 0.0F, 0.0F}),
            Mat4::translation({-0.1F, 0.0F, 0.0F}),
        },
        std::vector<VertexSkinBinding>{
            binding({SkinInfluence{1U, 1.0F}}),
            binding({SkinInfluence{0U, 1.0F}}),
            binding({
                SkinInfluence{0U, 0.5F},
                SkinInfluence{1U, 0.5F},
            }),
        });
}

SkeletalTrsClip manual_animated_clip(
    const SkeletalRigPtr& rig) {
    return SkeletalTrsClip(
        rig,
        0.0F,
        1.0F,
        {
            SkeletalTrs{
                {0.25F, 0.0F, 0.0F},
                Quaternion{},
                {1.0F, 1.0F, 1.0F},
            },
            SkeletalTrs{
                {0.1F, 0.0F, 0.0F},
                Quaternion{},
                {1.0F, 1.0F, 1.0F},
            },
        },
        {
            Mat4::translation({0.05F, 0.0F, 0.0F}),
            Mat4::identity(),
        },
        {
            {
                0U,
                {
                    {0.25F, {0.25F, 0.0F, 0.0F}},
                    {0.75F, {0.45F, 0.0F, 0.0F}},
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
                    {0.5F, {1.0F, 1.0F, 1.0F}},
                    {1.0F, {1.5F, 1.0F, 1.0F}},
                },
            },
        });
}


SkeletalTrsClip manual_child_shift_clip(
    const SkeletalRigPtr& rig) {
    return SkeletalTrsClip(
        rig,
        0.25F,
        0.75F,
        {
            SkeletalTrs{
                {0.25F, 0.0F, 0.0F},
                Quaternion{},
                {1.0F, 1.0F, 1.0F},
            },
            SkeletalTrs{
                {0.1F, 0.0F, 0.0F},
                Quaternion{},
                {1.0F, 1.0F, 1.0F},
            },
        },
        {
            Mat4::translation({0.05F, 0.0F, 0.0F}),
            Mat4::identity(),
        },
        {
            {
                0U,
                {
                    {0.25F, {0.25F, 0.0F, 0.0F}},
                    {0.75F, {0.45F, 0.0F, 0.0F}},
                },
            },
        },
        {},
        {});
}


SkeletalTrsClip manual_mixed_step_clip(
    const SkeletalRigPtr& rig) {
    return SkeletalTrsClip(
        rig,
        0.0F,
        1.0F,
        {
            SkeletalTrs{
                {0.25F, 0.0F, 0.0F},
                Quaternion{},
                {1.0F, 1.0F, 1.0F},
            },
            SkeletalTrs{
                {0.1F, 0.0F, 0.0F},
                Quaternion{},
                {1.0F, 1.0F, 1.0F},
            },
        },
        {
            Mat4::translation({0.05F, 0.0F, 0.0F}),
            Mat4::identity(),
        },
        {
            {
                0U,
                {
                    {0.25F, {0.25F, 0.0F, 0.0F}},
                    {0.75F, {0.45F, 0.0F, 0.0F}},
                },
                SkeletalInterpolationMode::Linear,
            },
        },
        {
            {
                1U,
                {
                    {0.0F, Quaternion{}},
                    {1.0F, {0.0F, 0.0F, 1.0F, 0.0F}},
                },
                SkeletalInterpolationMode::Step,
            },
        },
        {
            {
                1U,
                {
                    {0.5F, {1.0F, 1.0F, 1.0F}},
                    {1.0F, {1.5F, 1.0F, 1.0F}},
                },
                SkeletalInterpolationMode::Step,
            },
        });
}

SkeletalTrsClip manual_child_step_clip(
    const SkeletalRigPtr& rig) {
    return SkeletalTrsClip(
        rig,
        0.25F,
        0.75F,
        {
            SkeletalTrs{
                {0.25F, 0.0F, 0.0F},
                Quaternion{},
                {1.0F, 1.0F, 1.0F},
            },
            SkeletalTrs{
                {0.1F, 0.0F, 0.0F},
                Quaternion{},
                {1.0F, 1.0F, 1.0F},
            },
        },
        {
            Mat4::translation({0.05F, 0.0F, 0.0F}),
            Mat4::identity(),
        },
        {
            {
                0U,
                {
                    {0.25F, {0.25F, 0.0F, 0.0F}},
                    {0.75F, {0.45F, 0.0F, 0.0F}},
                },
                SkeletalInterpolationMode::Step,
            },
        },
        {},
        {});
}

SkeletalTrsClip manual_cubic_pose_clip(
    const SkeletalRigPtr& rig) {
    return SkeletalTrsClip(
        rig,
        0.0F,
        1.0F,
        {
            SkeletalTrs{
                {0.25F, 0.0F, 0.0F},
                Quaternion{},
                {1.0F, 1.0F, 1.0F},
            },
            SkeletalTrs{
                {0.1F, 0.0F, 0.0F},
                Quaternion{},
                {1.0F, 1.0F, 1.0F},
            },
        },
        {
            Mat4::translation({0.05F, 0.0F, 0.0F}),
            Mat4::identity(),
        },
        {
            {
                0U,
                {},
                SkeletalInterpolationMode::CubicSpline,
                {
                    {
                        0.0F,
                        {0.0F, 0.0F, 0.0F},
                        {1.0F, 0.0F, 0.0F},
                        {1.0F, 0.0F, 0.25F},
                    },
                    {
                        1.0F,
                        {0.75F, 0.25F, 0.0F},
                        {0.0F, 0.45F, 0.0F},
                        {0.0F, 0.5F, 1.0F},
                    },
                },
            },
        },
        {
            {
                1U,
                {},
                SkeletalInterpolationMode::CubicSpline,
                {
                    {
                        0.0F,
                        {1.0F, 0.0F, 0.0F, 0.0F},
                        {0.0F, 1.0F, 0.0F, 0.0F},
                        {0.0F, 0.0F, 1.0F, 0.0F},
                    },
                    {
                        1.0F,
                        // Accessor 13 intentionally reuses the first
                        // inverse-bind matrix bytes; the animated fixture's
                        // child inverse bind is -0.4 after the intermediary
                        // node was introduced in M112.
                        {-0.4F, 0.0F, 0.0F, 1.0F},
                        {1.0F, 0.0F, 0.0F, 0.0F},
                        {0.0F, 1.0F, 0.0F, 0.0F},
                    },
                },
            },
        },
        {});
}

void test_animated_fixture_projects_to_programmatic_m111() {
    const std::filesystem::path path =
        fixture_path("animated/skinned_triangle.gltf");
    const GltfSkinnedAnimatedAsset imported =
        load_gltf_skinned_animated_asset_file(path);
    check(
        imported.animation != nullptr,
        "animated glTF produces one immutable M111 semantic clip");
    check(
        imported.asset.rig != nullptr,
        "animated glTF preserves M110 immutable skeletal ownership");
    check(
        imported.asset.model.mesh.vertices.size() == 3U
            && imported.asset.model.mesh.triangles.size() == 1U,
        "animated glTF preserves canonical M110 geometry projection");

    check_throws<GltfLoadError>(
        [&] {
            (void)load_gltf_skinned_asset_file(path);
        },
        "static M110 loader remains strict and rejects animation objects");
    check_throws<GltfLoadError>(
        [&] {
            (void)load_gltf_skinned_animated_asset_file(
                fixture_path("skinned_triangle.gltf"));
        },
        "animated loader requires exactly one animation object");

    if (!imported.animation || !imported.asset.rig) {
        return;
    }

    check(
        imported.animation->start_time() == 0.0F
            && imported.animation->end_time() == 1.0F,
        "animated glTF derives clip domain from accepted channel key domains");
    const auto prefixes = imported.animation->local_prefixes();
    check(
        prefixes.size() == 2U
            && prefixes[0](0U, 3U) == 0.05F
            && exact_matrix_equal(
                prefixes[1],
                Mat4::identity()),
        "non-joint intermediary transform becomes immutable child-joint local prefix");
    const auto defaults = imported.animation->default_pose();
    check(
        defaults.size() == 2U
            && defaults[0].translation.x == 0.25F
            && defaults[1].translation.x == 0.1F,
        "joint semantic defaults exclude the non-joint affine prefix");

    check(
        imported.asset.rest_local_transforms.size() == 2U
            && imported.asset.rest_local_transforms[0](0U, 3U)
                == 0.3F
            && imported.asset.rest_local_transforms[1](0U, 3U)
                == 0.1F,
        "static M110 rest locals remain prefix times joint semantic TRS");

    const SkeletalRigPtr reference_rig =
        manual_animated_rig();
    const SkeletalTrsClip reference =
        manual_animated_clip(reference_rig);
    const std::array<float, 6> times{
        1.0F,
        0.5F,
        0.0F,
        0.25F,
        0.75F,
        0.5F,
    };
    const auto imported_poses =
        imported.animation->sample(times);
    const auto reference_poses =
        reference.sample(times);
    check(
        imported_poses.size() == reference_poses.size(),
        "file-driven animation preserves requested sample cardinality");
    for (std::size_t sample = 0U;
         sample < imported_poses.size()
             && sample < reference_poses.size();
         ++sample) {
        const auto imported_locals =
            imported_poses[sample]->local_transforms();
        const auto reference_locals =
            reference_poses[sample]->local_transforms();
        check(
            imported_locals.size() == reference_locals.size(),
            "file-driven and programmatic M111 poses preserve joint cardinality");
        for (std::size_t joint = 0U;
             joint < imported_locals.size()
                 && joint < reference_locals.size();
             ++joint) {
            check(
                exact_matrix_equal(
                    imported_locals[joint],
                    reference_locals[joint]),
                "file-driven glTF channel projection is exact-equivalent to programmatic M111 semantic local state");
        }
    }

    const ModelAsset manual = manual_model();
    ModelRenderOptions imported_options;
    imported_options.directional_light = test_light();
    imported_options.skeletal_pose_state =
        imported_poses[1];

    ModelRenderOptions manual_options;
    manual_options.directional_light = test_light();
    manual_options.skeletal_pose_state =
        reference_poses[1];

    Framebuffer imported_fb(57U, 57U, SampleCount::Four);
    Framebuffer manual_fb(57U, 57U, SampleCount::Four);
    imported_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 6U);
    manual_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 6U);
    draw_model_asset(
        imported_fb,
        imported.asset.model,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        imported_options);
    draw_model_asset(
        manual_fb,
        manual,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        manual_options);
    check_same_framebuffer(
        imported_fb,
        manual_fb,
        "file-driven animated glTF midpoint matches independent programmatic M111 fixed-light execution");

    const PreparedModelSubmission imported_prepared =
        prepare_model_asset(
            imported.asset.model,
            imported_options);
    const PreparedModelSubmission manual_prepared =
        prepare_model_asset(
            manual,
            manual_options);
    const std::array<PreparedModelListEntry, 1> imported_entry{{
        {&imported_prepared, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> manual_entry{{
        {&manual_prepared, Mat4::identity()},
    }};
    const auto imported_shadow =
        render_directional_shadow_map(
            imported_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                47U,
                47U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    const auto manual_shadow =
        render_directional_shadow_map(
            manual_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                47U,
                47U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    for (std::size_t y = 0U;
         y < imported_shadow->height();
         ++y) {
        for (std::size_t x = 0U;
             x < imported_shadow->width();
             ++x) {
            check(
                imported_shadow->depth_at(x, y)
                    == manual_shadow->depth_at(x, y),
                "file-driven animated glTF shadow matches programmatic M111 reference");
        }
    }
}


void test_animation_collection_preserves_order_and_single_wrapper_compatibility() {
    const std::filesystem::path single_path =
        fixture_path("animated/skinned_triangle.gltf");
    const GltfSkinnedAnimationCollection single_collection =
        load_gltf_skinned_animation_collection_file(single_path);
    const GltfSkinnedAnimatedAsset legacy =
        load_gltf_skinned_animated_asset_file(single_path);

    check(
        single_collection.animations.size() == 1U
            && single_collection.animations[0].clip != nullptr,
        "one-animation glTF collection imports exactly one M111 clip");
    check(
        !single_collection.animations.empty()
            && !single_collection.animations[0].name,
        "animation name remains optional metadata");
    if (!single_collection.animations.empty()
        && single_collection.animations[0].clip
        && legacy.animation) {
        const std::array<float, 4> times{
            0.0F,
            0.25F,
            0.5F,
            1.0F,
        };
        const auto collection_poses =
            single_collection.animations[0].clip->sample(times);
        const auto legacy_poses =
            legacy.animation->sample(times);
        check(
            collection_poses.size() == legacy_poses.size(),
            "one-animation collection preserves legacy sample cardinality");
        for (std::size_t sample = 0U;
             sample < collection_poses.size()
                 && sample < legacy_poses.size();
             ++sample) {
            const auto collection_locals =
                collection_poses[sample]->local_transforms();
            const auto legacy_locals =
                legacy_poses[sample]->local_transforms();
            check(
                collection_locals.size() == legacy_locals.size(),
                "one-animation collection preserves legacy joint ownership");
            for (std::size_t joint = 0U;
                 joint < collection_locals.size()
                     && joint < legacy_locals.size();
                 ++joint) {
                check(
                    exact_matrix_equal(
                        collection_locals[joint],
                        legacy_locals[joint]),
                    "one-animation collection is exact-equivalent to the M112 compatibility wrapper");
            }
        }
    }

    const std::filesystem::path multi_path =
        fixture_path("animated/two_animations.gltf");
    const GltfSkinnedAnimationCollection collection =
        load_gltf_skinned_animation_collection_file(multi_path);
    check(
        collection.animations.size() == 2U,
        "multi-animation glTF preserves complete bounded collection cardinality");
    check(
        collection.animations.size() == 2U
            && collection.animations[0].name
            && *collection.animations[0].name == "FullPose"
            && collection.animations[1].name
            && *collection.animations[1].name == "ChildShift",
        "multi-animation glTF preserves animation array order and optional names");
    if (collection.animations.size() == 2U
        && collection.animations[0].clip
        && collection.animations[1].clip) {
        check(
            collection.animations[0].clip->start_time() == 0.0F
                && collection.animations[0].clip->end_time() == 1.0F
                && collection.animations[1].clip->start_time() == 0.25F
                && collection.animations[1].clip->end_time() == 0.75F,
            "multi-animation collection preserves independent source clip domains");
    }

    check_throws<GltfLoadError>(
        [&] {
            (void)load_gltf_skinned_animated_asset_file(multi_path);
        },
        "legacy exactly-one animated wrapper rejects a multi-animation asset");
    check_throws<GltfLoadError>(
        [&] {
            (void)load_gltf_skinned_asset_file(multi_path);
        },
        "static M110 loader remains strict and rejects a multi-animation asset");

    {
        std::string json = read_text(multi_path);
        replace_once(
            json,
            "\"name\": \"ChildShift\"",
            "\"name\": \"FullPose\"");
        const auto bytes = read_bytes(
            fixture_path("animated/skinned_triangle.bin"));
        const GltfSkinnedAnimationCollection duplicate_names =
            load_gltf_skinned_animation_collection_file(
                write_case(
                    "animation_duplicate_names",
                    json,
                    bytes));
        check(
            duplicate_names.animations.size() == 2U
                && duplicate_names.animations[0].name
                && duplicate_names.animations[1].name
                && *duplicate_names.animations[0].name
                    == *duplicate_names.animations[1].name,
            "animation names are retained metadata rather than an implicit uniqueness key");
    }
}

void test_file_driven_animation_collection_feeds_m113_blending() {
    const GltfSkinnedAnimationCollection imported =
        load_gltf_skinned_animation_collection_file(
            fixture_path("animated/two_animations.gltf"));
    if (imported.animations.size() != 2U
        || !imported.animations[0].clip
        || !imported.animations[1].clip) {
        check(false,
              "two-animation fixture must produce two blendable clips");
        return;
    }

    const SkeletalRigPtr reference_rig =
        manual_animated_rig();
    const SkeletalTrsClip reference_full =
        manual_animated_clip(reference_rig);
    const SkeletalTrsClip reference_shift =
        manual_child_shift_clip(reference_rig);

    const std::array<SkeletalTrsBlendRequest, 5> requests{{
        {0.0F, 0.25F, 0.0F},
        {1.0F, 0.75F, 1.0F},
        {0.5F, 0.5F, 0.5F},
        {0.75F, 0.25F, 0.25F},
        {0.5F, 0.5F, 0.5F},
    }};
    const auto imported_poses =
        blend_skeletal_trs_clips(
            *imported.animations[0].clip,
            *imported.animations[1].clip,
            requests);
    const auto reference_poses =
        blend_skeletal_trs_clips(
            reference_full,
            reference_shift,
            requests);

    check(
        imported_poses.size() == reference_poses.size(),
        "file-driven M113 blend preserves request cardinality");
    for (std::size_t sample = 0U;
         sample < imported_poses.size()
             && sample < reference_poses.size();
         ++sample) {
        const auto imported_locals =
            imported_poses[sample]->local_transforms();
        const auto reference_locals =
            reference_poses[sample]->local_transforms();
        check(
            imported_locals.size() == reference_locals.size(),
            "file-driven and programmatic M113 blend preserve joint cardinality");
        for (std::size_t joint = 0U;
             joint < imported_locals.size()
                 && joint < reference_locals.size();
             ++joint) {
            check(
                exact_matrix_equal(
                    imported_locals[joint],
                    reference_locals[joint]),
                "file-driven animation collection feeds M113 with exact programmatic semantic local state");
        }
    }

    if (imported_poses.size() < 3U
        || reference_poses.size() < 3U) {
        return;
    }
    const ModelAsset manual = manual_model();
    ModelRenderOptions imported_options;
    imported_options.directional_light = test_light();
    imported_options.skeletal_pose_state = imported_poses[2];
    ModelRenderOptions reference_options;
    reference_options.directional_light = test_light();
    reference_options.skeletal_pose_state = reference_poses[2];

    Framebuffer imported_fb(57U, 57U, SampleCount::Four);
    Framebuffer reference_fb(57U, 57U, SampleCount::Four);
    imported_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 8U);
    reference_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 8U);
    draw_model_asset(
        imported_fb,
        imported.asset.model,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        imported_options);
    draw_model_asset(
        reference_fb,
        manual,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        reference_options);
    check_same_framebuffer(
        imported_fb,
        reference_fb,
        "file-driven two-clip M113 interior blend matches independent fixed-light reference");

    const PreparedModelSubmission imported_prepared =
        prepare_model_asset(imported.asset.model, imported_options);
    const PreparedModelSubmission reference_prepared =
        prepare_model_asset(manual, reference_options);
    const std::array<PreparedModelListEntry, 1> imported_entry{{
        {&imported_prepared, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> reference_entry{{
        {&reference_prepared, Mat4::identity()},
    }};
    const auto imported_shadow =
        render_directional_shadow_map(
            imported_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                47U,
                47U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    const auto reference_shadow =
        render_directional_shadow_map(
            reference_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                47U,
                47U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    for (std::size_t y = 0U;
         y < imported_shadow->height();
         ++y) {
        for (std::size_t x = 0U;
             x < imported_shadow->width();
             ++x) {
            check(
                imported_shadow->depth_at(x, y)
                    == reference_shadow->depth_at(x, y),
                "file-driven M113 blend shadow matches independent programmatic reference");
        }
    }
}


void test_mixed_cubic_collection_matches_programmatic_m116_and_m113() {
    const GltfSkinnedAnimationCollection imported =
        load_gltf_skinned_animation_collection_file(
            fixture_path("animated/mixed_cubic.gltf"));
    check(
        imported.animations.size() == 3U
            && imported.animations[0].name
            && *imported.animations[0].name == "MixedPose"
            && imported.animations[1].name
            && *imported.animations[1].name == "CubicPose"
            && imported.animations[2].name
            && *imported.animations[2].name == "ChildStep",
        "mixed CUBICSPLINE collection preserves animation order and names");
    if (imported.animations.size() != 3U
        || !imported.animations[0].clip
        || !imported.animations[1].clip
        || !imported.animations[2].clip) {
        check(false,
              "mixed CUBICSPLINE fixture must produce three semantic clips");
        return;
    }

    const SkeletalRigPtr reference_rig =
        manual_animated_rig();
    const SkeletalTrsClip reference_mixed =
        manual_mixed_step_clip(reference_rig);
    const SkeletalTrsClip reference_cubic =
        manual_cubic_pose_clip(reference_rig);

    const std::array<float, 4> cubic_times{
        0.0F,
        0.5F,
        1.0F,
        0.5F,
    };
    const auto imported_cubic =
        imported.animations[1].clip->sample(cubic_times);
    const auto reference_cubic_poses =
        reference_cubic.sample(cubic_times);
    check(
        imported_cubic.size() == reference_cubic_poses.size(),
        "file-driven CUBICSPLINE clip preserves sample cardinality");
    for (std::size_t sample = 0U;
         sample < imported_cubic.size()
             && sample < reference_cubic_poses.size();
         ++sample) {
        const auto imported_locals =
            imported_cubic[sample]->local_transforms();
        const auto reference_locals =
            reference_cubic_poses[sample]->local_transforms();
        for (std::size_t joint = 0U;
             joint < imported_locals.size()
                 && joint < reference_locals.size();
             ++joint) {
            check(
                exact_matrix_equal(
                    imported_locals[joint],
                    reference_locals[joint]),
                "glTF CUBICSPLINE sampler projection is exact-equivalent to programmatic M116 local state");
        }
    }

    const std::array<SkeletalTrsBlendRequest, 4> requests{{
        {0.0F, 0.0F, 0.0F},
        {1.0F, 1.0F, 1.0F},
        {0.5F, 0.9F, 0.5F},
        {0.5F, 0.9F, 0.5F},
    }};
    const auto imported_blend =
        blend_skeletal_trs_clips(
            *imported.animations[1].clip,
            *imported.animations[0].clip,
            requests);
    const auto reference_blend =
        blend_skeletal_trs_clips(
            reference_cubic,
            reference_mixed,
            requests);
    check(
        imported_blend.size() == reference_blend.size(),
        "file-driven CUBICSPLINE plus STEP/LINEAR M113 blend preserves request cardinality");
    for (std::size_t sample = 0U;
         sample < imported_blend.size()
             && sample < reference_blend.size();
         ++sample) {
        const auto imported_locals =
            imported_blend[sample]->local_transforms();
        const auto reference_locals =
            reference_blend[sample]->local_transforms();
        for (std::size_t joint = 0U;
             joint < imported_locals.size()
                 && joint < reference_locals.size();
             ++joint) {
            check(
                exact_matrix_equal(
                    imported_locals[joint],
                    reference_locals[joint]),
                "M113 remains interpolation-mode agnostic for file-driven CUBICSPLINE blending");
        }
    }

    if (imported_blend.size() < 3U
        || reference_blend.size() < 3U) {
        return;
    }
    const ModelAsset manual = manual_model();
    ModelRenderOptions imported_options;
    imported_options.directional_light = test_light();
    imported_options.skeletal_pose_state =
        imported_blend[2];
    ModelRenderOptions reference_options;
    reference_options.directional_light = test_light();
    reference_options.skeletal_pose_state =
        reference_blend[2];

    Framebuffer imported_fb(57U, 57U, SampleCount::Four);
    Framebuffer reference_fb(57U, 57U, SampleCount::Four);
    imported_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 12U);
    reference_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 12U);
    draw_model_asset(
        imported_fb,
        imported.asset.model,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        imported_options);
    draw_model_asset(
        reference_fb,
        manual,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        reference_options);
    check_same_framebuffer(
        imported_fb,
        reference_fb,
        "file-driven CUBICSPLINE M113 blend matches independent fixed-light reference");

    const PreparedModelSubmission imported_prepared =
        prepare_model_asset(imported.asset.model, imported_options);
    const PreparedModelSubmission reference_prepared =
        prepare_model_asset(manual, reference_options);
    const std::array<PreparedModelListEntry, 1> imported_entry{{
        {&imported_prepared, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> reference_entry{{
        {&reference_prepared, Mat4::identity()},
    }};
    const auto imported_shadow =
        render_directional_shadow_map(
            imported_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                47U,
                47U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    const auto reference_shadow =
        render_directional_shadow_map(
            reference_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                47U,
                47U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    for (std::size_t y = 0U;
         y < imported_shadow->height();
         ++y) {
        for (std::size_t x = 0U;
             x < imported_shadow->width();
             ++x) {
            check(
                imported_shadow->depth_at(x, y)
                    == reference_shadow->depth_at(x, y),
                "file-driven CUBICSPLINE M113 blend shadow matches independent programmatic reference");
        }
    }
}

void test_cubic_file_validation_and_later_overflow() {
    const std::filesystem::path fixture =
        fixture_path("animated/mixed_cubic.gltf");
    const std::string valid = read_text(fixture);
    const std::vector<std::uint8_t> valid_bytes =
        read_bytes(fixture_path("animated/skinned_triangle.bin"));

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"bufferView\": 12, \"componentType\": 5126, \"count\": 6, \"type\": \"VEC3\"}",
            "{\"bufferView\": 12, \"componentType\": 5126, \"count\": 5, \"type\": \"VEC3\"}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animation_collection_file(
                    write_case(
                        "cubic_output_cardinality",
                        json,
                        valid_bytes));
            },
            "file-driven CUBICSPLINE rejects malformed three-times output cardinality");
    }

    {
        std::string json = valid;
        replace_animation_section(
            json,
            "  \"animations\": [\n"
            "    {\n"
            "      \"name\": \"CubicPose\",\n"
            "      \"samplers\": [\n"
            "        {\"input\": 6, \"output\": 12, \"interpolation\": \"CUBICSPLINE\"}\n"
            "      ],\n"
            "      \"channels\": [\n"
            "        {\"sampler\": 0, \"target\": {\"node\": 2, \"path\": \"translation\"}}\n"
            "      ]\n"
            "    }\n"
            "  ],\n");
        std::vector<std::uint8_t> bytes = valid_bytes;
        set_f32(
            bytes,
            276U,
            std::numeric_limits<float>::quiet_NaN());
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animation_collection_file(
                    write_case(
                        "cubic_nonfinite_tangent",
                        json,
                        bytes));
            },
            "file-driven CUBICSPLINE rejects non-finite tangent data");
    }

    {
        std::string json = valid;
        replace_animation_section(
            json,
            "  \"animations\": [\n"
            "    {\n"
            "      \"name\": \"CubicOverflow\",\n"
            "      \"samplers\": [\n"
            "        {\"input\": 6, \"output\": 12, \"interpolation\": \"CUBICSPLINE\"}\n"
            "      ],\n"
            "      \"channels\": [\n"
            "        {\"sampler\": 0, \"target\": {\"node\": 2, \"path\": \"scale\"}},\n"
            "        {\"sampler\": 0, \"target\": {\"node\": 1, \"path\": \"scale\"}}\n"
            "      ]\n"
            "    }\n"
            "  ],\n");
        std::vector<std::uint8_t> bytes = valid_bytes;
        constexpr float huge_tangent = 4.0e20F;
        // accessor 12: [in0,value0,out0,in1,value1,out1], each VEC3.
        for (std::size_t component = 0U; component < 3U; ++component) {
            set_f32(bytes, 276U + component * 4U, 0.0F);
            set_f32(bytes, 288U + component * 4U, 1.0F);
            set_f32(bytes, 312U + component * 4U, 0.0F);
            set_f32(bytes, 324U + component * 4U, 1.0F);
            set_f32(bytes, 336U + component * 4U, 0.0F);
        }
        set_f32(bytes, 300U, huge_tangent);
        set_f32(bytes, 312U, -huge_tangent);

        const GltfSkinnedAnimationCollection overflow =
            load_gltf_skinned_animation_collection_file(
                write_case(
                    "cubic_later_overflow",
                    json,
                    bytes));
        check(
            overflow.animations.size() == 1U
                && overflow.animations[0].clip,
            "file-driven cubic overflow fixture remains valid at endpoint construction");
        if (overflow.animations.empty()
            || !overflow.animations[0].clip) {
            return;
        }

        Framebuffer framebuffer(31U, 31U, SampleCount::Four);
        framebuffer.clear(
            {0.14F, 0.24F, 0.34F},
            0.65F,
            27U);
        const auto before = framebuffer.rgb8();
        const std::array<float, 2> times{
            0.0F,
            0.5F,
        };
        check_throws<std::invalid_argument>(
            [&] {
                const auto poses =
                    overflow.animations[0].clip->sample(times);
                for (const SkeletalPoseStatePtr& pose : poses) {
                    ModelRenderOptions options;
                    options.skeletal_pose_state = pose;
                    draw_model_asset(
                        framebuffer,
                        overflow.asset.model,
                        Mat4::identity(),
                        options);
                }
            },
            "later file-driven CUBICSPLINE hierarchy overflow rejects complete requested batch");
        check(
            framebuffer.rgb8() == before,
            "later file-driven CUBICSPLINE failure occurs before earlier endpoint owns framebuffer color");
        for (std::size_t sample = 0U;
             sample < framebuffer.samples_per_pixel();
             ++sample) {
            check(
                framebuffer.sample_depth_at(15U, 15U, sample)
                    == 0.65F,
                "later file-driven CUBICSPLINE failure occurs before depth ownership");
            check(
                framebuffer.sample_stencil_at(15U, 15U, sample)
                    == 27U,
                "later file-driven CUBICSPLINE failure occurs before stencil ownership");
        }
    }
}

void test_mixed_step_collection_matches_programmatic_m115_and_m113() {
    const GltfSkinnedAnimationCollection imported =
        load_gltf_skinned_animation_collection_file(
            fixture_path("animated/mixed_step.gltf"));
    check(
        imported.animations.size() == 2U
            && imported.animations[0].name
            && *imported.animations[0].name == "MixedPose"
            && imported.animations[1].name
            && *imported.animations[1].name == "ChildStep",
        "mixed LINEAR+STEP glTF preserves M114 collection order and names");
    if (imported.animations.size() != 2U
        || !imported.animations[0].clip
        || !imported.animations[1].clip) {
        check(false,
              "mixed LINEAR+STEP fixture must produce two semantic clips");
        return;
    }

    const SkeletalRigPtr reference_rig =
        manual_animated_rig();
    const SkeletalTrsClip reference_mixed =
        manual_mixed_step_clip(reference_rig);
    const SkeletalTrsClip reference_child =
        manual_child_step_clip(reference_rig);

    const std::array<float, 7> mixed_times{
        0.0F,
        0.5F,
        0.749F,
        0.75F,
        0.9F,
        1.0F,
        0.749F,
    };
    const auto imported_mixed =
        imported.animations[0].clip->sample(mixed_times);
    const auto reference_mixed_poses =
        reference_mixed.sample(mixed_times);
    check(
        imported_mixed.size() == reference_mixed_poses.size(),
        "mixed STEP file-driven clip preserves requested sample cardinality");
    for (std::size_t sample = 0U;
         sample < imported_mixed.size()
             && sample < reference_mixed_poses.size();
         ++sample) {
        const auto imported_locals =
            imported_mixed[sample]->local_transforms();
        const auto reference_locals =
            reference_mixed_poses[sample]->local_transforms();
        check(
            imported_locals.size() == reference_locals.size(),
            "mixed STEP imported/programmatic poses preserve joint cardinality");
        for (std::size_t joint = 0U;
             joint < imported_locals.size()
                 && joint < reference_locals.size();
             ++joint) {
            check(
                exact_matrix_equal(
                    imported_locals[joint],
                    reference_locals[joint]),
                "glTF STEP sampler projection is exact-equivalent to programmatic M115 semantic local state");
        }
    }

    const std::array<float, 5> child_times{
        0.25F,
        0.5F,
        0.749F,
        0.75F,
        0.5F,
    };
    const auto imported_child =
        imported.animations[1].clip->sample(child_times);
    const auto reference_child_poses =
        reference_child.sample(child_times);
    for (std::size_t sample = 0U;
         sample < imported_child.size()
             && sample < reference_child_poses.size();
         ++sample) {
        const auto imported_locals =
            imported_child[sample]->local_transforms();
        const auto reference_locals =
            reference_child_poses[sample]->local_transforms();
        for (std::size_t joint = 0U;
             joint < imported_locals.size()
                 && joint < reference_locals.size();
             ++joint) {
            check(
                exact_matrix_equal(
                    imported_locals[joint],
                    reference_locals[joint]),
                "file-driven child STEP track switches only at its exact key boundary");
        }
    }

    const std::array<SkeletalTrsBlendRequest, 5> requests{{
        {0.0F, 0.25F, 0.0F},
        {1.0F, 0.75F, 1.0F},
        {0.9F, 0.5F, 0.5F},
        {0.749F, 0.749F, 0.25F},
        {0.9F, 0.5F, 0.5F},
    }};
    const auto imported_blend =
        blend_skeletal_trs_clips(
            *imported.animations[0].clip,
            *imported.animations[1].clip,
            requests);
    const auto reference_blend =
        blend_skeletal_trs_clips(
            reference_mixed,
            reference_child,
            requests);
    check(
        imported_blend.size() == reference_blend.size(),
        "mixed STEP file-driven M113 blend preserves request cardinality");
    for (std::size_t sample = 0U;
         sample < imported_blend.size()
             && sample < reference_blend.size();
         ++sample) {
        const auto imported_locals =
            imported_blend[sample]->local_transforms();
        const auto reference_locals =
            reference_blend[sample]->local_transforms();
        for (std::size_t joint = 0U;
             joint < imported_locals.size()
                 && joint < reference_locals.size();
             ++joint) {
            check(
                exact_matrix_equal(
                    imported_locals[joint],
                    reference_locals[joint]),
                "mixed STEP collection feeds M113 with exact programmatic semantic state");
        }
    }

    if (imported_blend.size() < 3U
        || reference_blend.size() < 3U) {
        return;
    }
    const ModelAsset manual = manual_model();
    ModelRenderOptions imported_options;
    imported_options.directional_light = test_light();
    imported_options.skeletal_pose_state = imported_blend[2];
    ModelRenderOptions reference_options;
    reference_options.directional_light = test_light();
    reference_options.skeletal_pose_state = reference_blend[2];

    Framebuffer imported_fb(57U, 57U, SampleCount::Four);
    Framebuffer reference_fb(57U, 57U, SampleCount::Four);
    imported_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 10U);
    reference_fb.clear({0.02F, 0.03F, 0.04F}, 1.0F, 10U);
    draw_model_asset(
        imported_fb,
        imported.asset.model,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        imported_options);
    draw_model_asset(
        reference_fb,
        manual,
        Mat4::identity(),
        Mat4::identity(),
        Mat4::identity(),
        reference_options);
    check_same_framebuffer(
        imported_fb,
        reference_fb,
        "mixed STEP file-driven M113 blend matches independent fixed-light reference");

    const PreparedModelSubmission imported_prepared =
        prepare_model_asset(imported.asset.model, imported_options);
    const PreparedModelSubmission reference_prepared =
        prepare_model_asset(manual, reference_options);
    const std::array<PreparedModelListEntry, 1> imported_entry{{
        {&imported_prepared, Mat4::identity()},
    }};
    const std::array<PreparedModelListEntry, 1> reference_entry{{
        {&reference_prepared, Mat4::identity()},
    }};
    const auto imported_shadow =
        render_directional_shadow_map(
            imported_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                47U,
                47U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    const auto reference_shadow =
        render_directional_shadow_map(
            reference_entry,
            Mat4::identity(),
            DirectionalShadowMapOptions{
                47U,
                47U,
                CullMode::None,
                FrontFace::CounterClockwise,
            });
    for (std::size_t y = 0U;
         y < imported_shadow->height();
         ++y) {
        for (std::size_t x = 0U;
             x < imported_shadow->width();
             ++x) {
            check(
                imported_shadow->depth_at(x, y)
                    == reference_shadow->depth_at(x, y),
                "mixed STEP file-driven M113 shadow matches programmatic reference");
        }
    }
}

void test_animation_collection_fail_closed_contract() {
    const std::string multi =
        read_text(
            fixture_path("animated/two_animations.gltf"));
    const std::vector<std::uint8_t> bytes =
        read_bytes(
            fixture_path("animated/skinned_triangle.bin"));

    {
        std::string json = multi;
        std::string body;
        for (std::size_t index = 0U; index < 17U; ++index) {
            if (!body.empty()) {
                body += ",\n";
            }
            body +=
                "    {\"samplers\": [], \"channels\": []}";
        }
        replace_animations_array(json, body);
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animation_collection_file(
                    write_case(
                        "animation_collection_capacity",
                        json,
                        bytes));
            },
            "animation collection enforces bounded clip capacity before per-animation parsing");
    }

    {
        std::string json = multi;
        replace_once(
            json,
            "\"name\": \"ChildShift\",\n      \"samplers\": [\n        {\"input\": 8, \"output\": 9}",
            "\"name\": \"ChildShift\",\n      \"samplers\": [\n        {\"input\": 99, \"output\": 9}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animation_collection_file(
                    write_case(
                        "animation_collection_later_sampler",
                        json,
                        bytes));
            },
            "malformed later animation rejects the complete collection rather than returning the valid first clip");
    }

    {
        std::string json = multi;
        replace_once(
            json,
            "{\"sampler\": 0, \"target\": {\"node\": 2, \"path\": \"translation\"}}\n      ]\n    }\n  ],",
            "{\"sampler\": 0, \"target\": {\"node\": 3, \"path\": \"translation\"}}\n      ]\n    }\n  ],");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animation_collection_file(
                    write_case(
                        "animation_collection_later_channel",
                        json,
                        bytes));
            },
            "invalid later animation channel ownership rejects the entire collection");
    }
}

void test_animation_schema_and_data_fail_closed() {
    const std::string valid =
        read_text(
            fixture_path(
                "animated/skinned_triangle.gltf"));
    const std::vector<std::uint8_t> valid_bytes =
        read_bytes(
            fixture_path(
                "animated/skinned_triangle.bin"));

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"bufferView\": 6, \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\", \"min\": [0.0], \"max\": [1.0]}",
            "{\"bufferView\": 6, \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\"}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case(
                        "animation_input_missing_bounds",
                        json,
                        valid_bytes));
            },
            "animation input accessor without required min/max is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "\"max\": [1.0]",
            "\"max\": [2.0]");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case(
                        "animation_input_bad_bounds",
                        json,
                        valid_bytes));
            },
            "animation input min/max must match actual first and last key times");
    }

    {
        std::vector<std::uint8_t> bytes = valid_bytes;
        set_f32(bytes, 268U, -0.25F);
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case(
                        "animation_input_negative_time",
                        valid,
                        bytes));
            },
            "animation input time must be non-negative");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "\"interpolation\": \"LINEAR\"",
            "\"interpolation\": \"CATMULLROM\"");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case("animation_unknown_interpolation", json, valid_bytes));
            },
            "unknown animation interpolation mode is rejected rather than degraded");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "\"interpolation\": \"LINEAR\"",
            "\"interpolation\": \"CUBICSPLINE\"");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case(
                        "animation_cubic_bad_cardinality",
                        json,
                        valid_bytes));
            },
            "CUBICSPLINE sampler requires exactly three output elements per input key");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"sampler\": 1, \"target\": {\"node\": 2, \"path\": \"translation\"}}",
            "{\"sampler\": 99, \"target\": {\"node\": 2, \"path\": \"translation\"}}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case("animation_sampler_ref", json, valid_bytes));
            },
            "animation channel sampler reference is range checked");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"sampler\": 1, \"target\": {\"node\": 2, \"path\": \"translation\"}}",
            "{\"sampler\": 1, \"target\": {\"node\": 3, \"path\": \"translation\"}}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case("animation_non_joint", json, valid_bytes));
            },
            "animation channel targeting non-joint intermediary is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "\"path\": \"translation\"",
            "\"path\": \"weights\"");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case("animation_path", json, valid_bytes));
            },
            "unsupported animation target path is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"input\": 10, \"output\": 11, \"interpolation\": \"LINEAR\"}",
            "{\"input\": 10, \"output\": 11, \"interpolation\": \"STEP\"}");
        replace_once(
            json,
            "{\"sampler\": 2, \"target\": {\"node\": 1, \"path\": \"scale\"}}",
            "{\"sampler\": 2, \"target\": {\"node\": 2, \"path\": \"translation\"}}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case("animation_duplicate", json, valid_bytes));
            },
            "duplicate animation joint/property ownership is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"bufferView\": 9, \"componentType\": 5126, \"count\": 2, \"type\": \"VEC3\"}",
            "{\"bufferView\": 9, \"componentType\": 5126, \"count\": 1, \"type\": \"VEC3\"}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case("animation_count", json, valid_bytes));
            },
            "animation sampler input/output count mismatch is rejected");
    }

    {
        std::vector<std::uint8_t> bytes = valid_bytes;
        set_f32(bytes, 312U, 0.25F);
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case("animation_times", valid, bytes));
            },
            "non-increasing animation input time is rejected");
    }

    {
        std::vector<std::uint8_t> bytes = valid_bytes;
        set_f32(bytes, 300U, 2.0F);
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case("animation_quaternion", valid, bytes));
            },
            "non-unit animation rotation output is rejected by M111 semantic ownership");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "{\"translation\": [0.25, 0.0, 0.0]}",
            "{\"matrix\": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.25, 0, 0, 1]}");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_animated_asset_file(
                    write_case("animation_matrix_joint", json, valid_bytes));
            },
            "animation channel targeting matrix-backed joint is rejected");
    }
}

void test_imported_animation_later_overflow_is_batch_fail_closed() {
    std::string json =
        read_text(
            fixture_path(
                "animated/skinned_triangle.gltf"));
    std::vector<std::uint8_t> bytes =
        read_bytes(
            fixture_path(
                "animated/skinned_triangle.bin"));

    replace_once(
        json,
        "{\"sampler\": 1, \"target\": {\"node\": 2, \"path\": \"translation\"}}",
        "{\"sampler\": 1, \"target\": {\"node\": 2, \"path\": \"scale\"}}");
    replace_once(
        json,
        "{\"input\": 8, \"output\": 9}",
        "{\"input\": 8, \"output\": 9, \"interpolation\": \"STEP\"}");
    replace_once(
        json,
        "{\"input\": 10, \"output\": 11, \"interpolation\": \"LINEAR\"}",
        "{\"input\": 10, \"output\": 11, \"interpolation\": \"STEP\"}");
    // Child scale output: identity -> huge X.
    set_f32(bytes, 316U + 0U, 1.0F);
    set_f32(bytes, 316U + 4U, 1.0F);
    set_f32(bytes, 316U + 8U, 1.0F);
    set_f32(bytes, 328U + 0U, 1.0e20F);
    set_f32(bytes, 328U + 4U, 1.0F);
    set_f32(bytes, 328U + 8U, 1.0F);
    // Parent scale output: identity -> huge X.
    set_f32(bytes, 360U + 0U, 1.0e20F);

    const GltfSkinnedAnimatedAsset imported =
        load_gltf_skinned_animated_asset_file(
            write_case(
                "animation_later_overflow",
                json,
                bytes));

    Framebuffer framebuffer(31U, 31U, SampleCount::Four);
    framebuffer.clear(
        {0.13F, 0.23F, 0.33F},
        0.71F,
        21U);
    const auto before = framebuffer.rgb8();
    const std::array<float, 2> sample_times{
        0.5F,
        1.0F,
    };
    check_throws<std::invalid_argument>(
        [&] {
            const auto poses =
                imported.animation->sample(sample_times);
            for (const SkeletalPoseStatePtr& pose : poses) {
                ModelRenderOptions options;
                options.skeletal_pose_state = pose;
                draw_model_asset(
                    framebuffer,
                    imported.asset.model,
                    Mat4::identity(),
                    options);
            }
        },
        "later exact file-driven STEP key hierarchy overflow rejects complete requested batch");
    check(
        framebuffer.rgb8() == before,
        "later file-driven STEP failure occurs before earlier held sample owns framebuffer color");
    for (std::size_t sample = 0U;
         sample < framebuffer.samples_per_pixel();
         ++sample) {
        check(
            framebuffer.sample_depth_at(15U, 15U, sample)
                == 0.71F,
            "later file-driven animation failure occurs before framebuffer depth ownership");
        check(
            framebuffer.sample_stencil_at(15U, 15U, sample)
                == 21U,
            "later file-driven animation failure occurs before framebuffer stencil ownership");
    }
}

void test_node_hierarchy_fail_closed() {
    const std::string valid =
        read_text(fixture_path("skinned_triangle.gltf"));
    const std::vector<std::uint8_t> bytes =
        read_bytes(fixture_path("skinned_triangle.bin"));

    {
        std::string json = valid;
        replace_once(
            json,
            "    {\"translation\": [0.25, 0.0, 0.0]}\n",
            "    {\"children\": [1], \"translation\": [0.25, 0.0, 0.0]}\n");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("node_cycle", json, bytes));
            },
            "cyclic glTF node topology is rejected before M108 construction");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "\"joints\": [2, 1]",
            "\"joints\": [2, 99]");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("joint_node_range", json, bytes));
            },
            "skin joint node outside nodes array is rejected");
    }

    {
        std::string json = valid;
        replace_once(
            json,
            "    {\"children\": [2], \"translation\": [0.1, 0.0, 0.0]},\n",
            "    {\"translation\": [0.1, 0.0, 0.0]},\n");
        check_throws<GltfLoadError>(
            [&] {
                (void)load_gltf_skinned_asset_file(
                    write_case("joint_roots", json, bytes));
            },
            "skin joints without a common node-tree root are rejected");
    }
}

}  // namespace

int main() {
    test_valid_fixture_decodes_into_existing_ownership();
    test_imported_asset_matches_independent_programmatic_render_and_shadow();
    test_external_buffer_is_not_retained_after_import();
    test_json_schema_and_path_fail_closed();
    test_binary_range_joint_weight_and_inverse_bind_fail_closed();
    test_animated_fixture_projects_to_programmatic_m111();
    test_animation_collection_preserves_order_and_single_wrapper_compatibility();
    test_file_driven_animation_collection_feeds_m113_blending();
    test_mixed_cubic_collection_matches_programmatic_m116_and_m113();
    test_cubic_file_validation_and_later_overflow();
    test_mixed_step_collection_matches_programmatic_m115_and_m113();
    test_animation_collection_fail_closed_contract();
    test_animation_schema_and_data_fail_closed();
    test_imported_animation_later_overflow_is_batch_fail_closed();
    test_node_hierarchy_fail_closed();

    if (failures != 0) {
        std::cerr << failures << " glTF test(s) failed\n";
        return 1;
    }
    std::cout << "glTF tests passed\n";
    return 0;
}
