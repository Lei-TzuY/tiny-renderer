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
    return {
        reinterpret_cast<const std::uint8_t*>(raw.data()),
        reinterpret_cast<const std::uint8_t*>(raw.data())
            + raw.size(),
    };
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
    test_node_hierarchy_fail_closed();

    if (failures != 0) {
        std::cerr << failures << " glTF test(s) failed\n";
        return 1;
    }
    std::cout << "glTF tests passed\n";
    return 0;
}
