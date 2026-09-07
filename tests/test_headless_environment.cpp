#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/environment.hpp"
#include "tiny_renderer/offline_render.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(float a, float b, float epsilon = 2.0e-4F) {
    return std::fabs(a - b) <= epsilon;
}

void check_vec3_near(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check(
        near(actual.x, expected.x)
            && near(actual.y, expected.y)
            && near(actual.z, expected.z),
        message);
}

template <typename Exception, typename Function>
void check_throws(Function&& function, const std::string& message) {
    bool threw_expected = false;
    try {
        function();
    } catch (const Exception&) {
        threw_expected = true;
    } catch (...) {
    }
    check(threw_expected, message);
}

ModelAsset triangle_asset() {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-1.0F, -1.0F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({1.0F, -1.0F, 0.0F}, VaryingPack{}),
        Vertex::with_varyings({0.0F, 1.0F, 0.0F}, VaryingPack{}),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "preview";
    draw.material.albedo = {0.8F, 0.2F, 0.1F};
    asset.draws.push_back(draw);
    return asset;
}

VaryingPack normal_varyings() {
    VaryingPack varyings;
    varyings.count = 3U;
    varyings.values[0] = 0.0F;
    varyings.values[1] = 0.0F;
    varyings.values[2] = 1.0F;
    return varyings;
}

ModelAsset normal_triangle_asset() {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-1.0F, -1.0F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({1.0F, -1.0F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.0F, 1.0F, 0.0F}, normal_varyings()),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "preview";
    draw.material.albedo = {0.8F, 0.4F, 0.2F};
    asset.draws.push_back(draw);
    return asset;
}

Texture2D environment_texture() {
    return Texture2D(
        4U,
        2U,
        {
            {4.0F, 0.25F, 0.5F},
            {2.0F, 0.5F, 0.25F},
            {0.25F, 3.0F, 0.5F},
            {0.5F, 0.25F, 5.0F},
            {6.0F, 0.5F, 0.25F},
            {0.25F, 7.0F, 0.5F},
            {0.5F, 0.25F, 8.0F},
            {9.0F, 1.0F, 0.25F},
        });
}

Texture2D constant_environment_texture() {
    return Texture2D(
        2U,
        2U,
        {
            {0.5F, 0.25F, 0.125F},
            {0.5F, 0.25F, 0.125F},
            {0.5F, 0.25F, 0.125F},
            {0.5F, 0.25F, 0.125F},
        });
}

EnvironmentBackgroundState environment_state(const Texture2D& texture) {
    EnvironmentBackgroundState environment;
    environment.texture = &texture;
    environment.sampler.address_u = AddressMode::Repeat;
    environment.sampler.address_v = AddressMode::Clamp;
    environment.sampler.filter = FilterMode::Nearest;
    environment.sampler.mip_filter = MipFilterMode::Disabled;
    environment.intensity = 1.5F;
    environment.yaw_radians = 0.2F;
    return environment;
}

EnvironmentDiffuseLight environment_light(const Texture2D& texture) {
    EnvironmentDiffuseLight light;
    light.normal = {0U, 1U, 2U};
    light.environment.texture = &texture;
    light.environment.intensity = 0.75F;
    light.environment.yaw_radians = -0.2F;
    return light;
}

PerspectiveCameraState preview_camera(const OfflineRenderSettings& settings) {
    return {
        {0.0F, 0.0F, 3.0F},
        {0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        settings.vertical_fov_radians,
        static_cast<float>(settings.width) / static_cast<float>(settings.height),
    };
}

Vec3 expected_environment_sample(
    const Texture2D& texture,
    const EnvironmentBackgroundState& environment,
    const OfflineRenderSettings& settings,
    std::size_t x,
    std::size_t y,
    std::size_t sample_index) {
    const Vec3 direction = perspective_sample_direction(
        preview_camera(settings),
        settings.width,
        settings.height,
        settings.sample_count,
        x,
        y,
        sample_index);
    const Vec2 uv = equirectangular_uv(direction, environment.yaw_radians);
    return texture.sample(uv, environment.sampler) * environment.intensity;
}

void check_depth_stencil_equal(
    const Framebuffer& a,
    const Framebuffer& b,
    const std::string& message) {
    bool equal = a.width() == b.width()
        && a.height() == b.height()
        && a.samples_per_pixel() == b.samples_per_pixel();
    if (equal) {
        for (std::size_t y = 0U; y < a.height(); ++y) {
            for (std::size_t x = 0U; x < a.width(); ++x) {
                for (std::size_t sample = 0U; sample < a.samples_per_pixel(); ++sample) {
                    equal = equal
                        && a.sample_depth_at(x, y, sample) == b.sample_depth_at(x, y, sample)
                        && a.sample_stencil_at(x, y, sample) == b.sample_stencil_at(x, y, sample);
                }
            }
        }
    }
    check(equal, message);
}

void test_default_preview_remains_compatible() {
    const ModelAsset asset = triangle_asset();
    OfflineRenderSettings settings{};
    settings.width = 47U;
    settings.height = 31U;
    settings.sample_count = SampleCount::One;

    const Framebuffer historical = render_model_preview(asset, settings);
    settings.environment.reset();
    settings.environment_lighting.reset();
    const Framebuffer explicit_no_environment = render_model_preview(asset, settings);
    check(
        historical.rgb8() == explicit_no_environment.rgb8(),
        "explicitly absent environment state preserves historical preview bytes");
}

void test_environment_uses_exact_preview_camera_and_preserves_attachments() {
    const ModelAsset asset = triangle_asset();
    const Texture2D texture = environment_texture();
    const EnvironmentBackgroundState environment = environment_state(texture);

    for (const SampleCount sample_count : {SampleCount::One, SampleCount::Four}) {
        OfflineRenderSettings settings{};
        settings.width = 41U;
        settings.height = 29U;
        settings.sample_count = sample_count;
        const Framebuffer without_environment = render_model_preview(asset, settings);
        settings.environment = environment;
        const Framebuffer with_environment_a = render_model_preview(asset, settings);
        const Framebuffer with_environment_b = render_model_preview(asset, settings);

        check(
            with_environment_a.rgb8() == with_environment_b.rgb8(),
            sample_count == SampleCount::One
                ? "1x environment preview is deterministic"
                : "4x environment preview is deterministic");
        check(
            with_environment_a.rgb8() != without_environment.rgb8(),
            "environment changes uncovered preview color");
        check_depth_stencil_equal(
            with_environment_a,
            without_environment,
            "environment preview preserves geometry depth/stencil ownership");

        const std::size_t x = 0U;
        const std::size_t y = 0U;
        check(
            std::isinf(with_environment_a.sample_depth_at(x, y, 0U)),
            "corner used for camera contract remains uncovered by geometry");
        for (std::size_t sample = 0U; sample < with_environment_a.samples_per_pixel(); ++sample) {
            check_vec3_near(
                with_environment_a.sample_color_at(x, y, sample),
                expected_environment_sample(texture, environment, settings, x, y, sample),
                "uncovered sample uses M53 ray reconstructed from exact preview camera");
        }
    }
}

void test_background_and_diffuse_lighting_are_independent() {
    const ModelAsset asset = normal_triangle_asset();
    const Texture2D texture = constant_environment_texture();
    EnvironmentBackgroundState background;
    background.texture = &texture;
    background.intensity = 1.25F;
    background.yaw_radians = 0.3F;
    const EnvironmentDiffuseLight lighting = environment_light(texture);

    for (const SampleCount sample_count : {SampleCount::One, SampleCount::Four}) {
        OfflineRenderSettings base{};
        base.width = 49U;
        base.height = 37U;
        base.sample_count = sample_count;
        base.clear_color = {0.02F, 0.03F, 0.04F};

        const Framebuffer baseline = render_model_preview(asset, base);

        OfflineRenderSettings lighting_only = base;
        lighting_only.environment_lighting = lighting;
        const Framebuffer lit_a = render_model_preview(asset, lighting_only);
        const Framebuffer lit_b = render_model_preview(asset, lighting_only);

        OfflineRenderSettings background_only = base;
        background_only.environment = background;
        const Framebuffer background_frame = render_model_preview(asset, background_only);

        OfflineRenderSettings combined = base;
        combined.environment = background;
        combined.environment_lighting = lighting;
        const Framebuffer combined_a = render_model_preview(asset, combined);
        const Framebuffer combined_b = render_model_preview(asset, combined);

        check(
            lit_a.rgb8() == lit_b.rgb8(),
            sample_count == SampleCount::One
                ? "1x lighting-only preview is deterministic"
                : "4x lighting-only preview is deterministic");
        check(
            combined_a.rgb8() == combined_b.rgb8(),
            sample_count == SampleCount::One
                ? "1x combined environment preview is deterministic"
                : "4x combined environment preview is deterministic");

        const std::size_t center_x = base.width / 2U;
        const std::size_t center_y = base.height / 2U;
        check_vec3_near(
            lit_a.color_at(0U, 0U),
            base.clear_color,
            "lighting-only preview leaves uncovered pixels at clear color");
        check_vec3_near(
            background_frame.color_at(center_x, center_y),
            baseline.color_at(center_x, center_y),
            "visible environment background does not implicitly enable diffuse lighting");
        check(
            !near(lit_a.color_at(center_x, center_y).x, baseline.color_at(center_x, center_y).x),
            "diffuse environment lighting shades the model without requiring a visible background");
        check_vec3_near(
            combined_a.color_at(0U, 0U),
            background_frame.color_at(0U, 0U),
            "combined preview preserves background-only uncovered pixels");
        check_vec3_near(
            combined_a.color_at(center_x, center_y),
            lit_a.color_at(center_x, center_y),
            "combined preview preserves lighting-only covered model pixels");

        check_vec3_near(
            lit_a.color_at(center_x, center_y),
            {
                0.8F * 0.5F * 0.75F,
                0.4F * 0.25F * 0.75F,
                0.2F * 0.125F * 0.75F,
            },
            "constant HDR environment reaches the existing Lambert material path analytically");
    }
}

void test_invalid_environment_is_rejected() {
    const ModelAsset asset = triangle_asset();
    OfflineRenderSettings settings{};
    EnvironmentBackgroundState invalid;
    settings.environment = invalid;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings); },
        "preview rejects null environment texture");

    const Texture2D texture = environment_texture();
    invalid = environment_state(texture);
    invalid.intensity = std::numeric_limits<float>::infinity();
    settings.environment = invalid;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings); },
        "preview rejects invalid environment intensity");
}

void test_invalid_or_duplicate_environment_lighting_is_rejected() {
    const ModelAsset asset = normal_triangle_asset();
    OfflineRenderSettings settings{};
    settings.environment_lighting = EnvironmentDiffuseLight{};
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings); },
        "preview rejects null diffuse environment texture");

    const Texture2D texture = constant_environment_texture();
    const EnvironmentDiffuseLight lighting = environment_light(texture);
    settings.environment_lighting = lighting;
    settings.environment_lighting->environment.intensity = std::numeric_limits<float>::infinity();
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings); },
        "preview rejects invalid diffuse environment intensity");

    settings.environment_lighting = lighting;
    ModelRenderOptions options{};
    options.fixed_lights.environment_diffuse = lighting;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings, options); },
        "preview rejects duplicate environment-light ownership");
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to create headless CLI fixture");
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("failed to write headless CLI fixture");
    }
}

void write_float_le(std::ofstream& output, float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const char bytes[4]{
        static_cast<char>(bits & 0xFFU),
        static_cast<char>((bits >> 8U) & 0xFFU),
        static_cast<char>((bits >> 16U) & 0xFFU),
        static_cast<char>((bits >> 24U) & 0xFFU),
    };
    output.write(bytes, 4);
}

void write_constant_pfm(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to create headless HDR fixture");
    }
    output << "PF\n2 2\n-1.0\n";
    for (std::size_t pixel = 0U; pixel < 4U; ++pixel) {
        write_float_le(output, 0.5F);
        write_float_le(output, 0.25F);
        write_float_le(output, 0.125F);
    }
    if (!output) {
        throw std::runtime_error("failed to write headless HDR fixture");
    }
}

std::vector<char> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to read headless CLI output");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string quote(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

int run_renderer(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments) {
    std::string command = quote(executable);
    for (const std::string& argument : arguments) {
        command += " \"" + argument + "\"";
    }
    return std::system(command.c_str());
}

void check_repeated_cli_render(
    const std::filesystem::path& executable,
    const std::filesystem::path& root,
    const std::filesystem::path& obj,
    const std::filesystem::path& environment,
    const std::string& label,
    const std::string& extension,
    const std::string& samples,
    const std::vector<std::string>& mode_arguments) {
    const std::filesystem::path output_a = root / (label + "_a." + extension);
    const std::filesystem::path output_b = root / (label + "_b." + extension);
    std::vector<std::string> base{
        obj.string(),
        output_a.string(),
        "32",
        "24",
        samples,
    };
    base.insert(base.end(), mode_arguments.begin(), mode_arguments.end());
    const int first_result = run_renderer(executable, base);

    base[1] = output_b.string();
    const int second_result = run_renderer(executable, base);
    check(first_result == 0 && second_result == 0, label + " CLI renders succeed twice");
    if (first_result == 0 && second_result == 0) {
        check(
            read_file_bytes(output_a) == read_file_bytes(output_b),
            label + " repeated CLI output is byte-for-byte deterministic");
    }
    (void)environment;
}

void test_real_headless_environment_lighting_cli(const std::filesystem::path& argv0) {
    std::filesystem::path executable = std::filesystem::absolute(argv0).parent_path()
        / "tiny_renderer_render";
    if (!std::filesystem::exists(executable)) {
        const std::filesystem::path exe_candidate = executable.string() + ".exe";
        if (std::filesystem::exists(exe_candidate)) {
            executable = exe_candidate;
        }
    }
    check(std::filesystem::exists(executable), "headless render CLI executable is available to integration test");
    if (!std::filesystem::exists(executable)) {
        return;
    }

    const std::size_t path_hash = std::hash<std::string>{}(std::filesystem::absolute(argv0).string());
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("tiny_renderer_m57_" + std::to_string(path_hash));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::filesystem::path obj = root / "preview.obj";
    const std::filesystem::path mtl = root / "preview.mtl";
    const std::filesystem::path environment = root / "environment.pfm";
    write_text_file(
        obj,
        "mtllib preview.mtl\n"
        "v -1 -1 0\n"
        "v 1 -1 0\n"
        "v 0 1 0\n"
        "vn 0 0 1\n"
        "usemtl preview\n"
        "f 1//1 2//1 3//1\n");
    write_text_file(mtl, "newmtl preview\nKd 0.8 0.4 0.2\n");
    write_constant_pfm(environment);

    const std::vector<std::string> background_args{
        "--environment", environment.string(),
        "--environment-intensity", "1.25",
        "--environment-yaw", "0.2",
    };
    const std::vector<std::string> lighting_args{
        "--environment-light", environment.string(),
        "--environment-light-intensity", "0.75",
        "--environment-light-yaw", "-0.2",
    };
    std::vector<std::string> combined_args = background_args;
    combined_args.insert(combined_args.end(), lighting_args.begin(), lighting_args.end());

    for (const auto& mode : std::vector<std::pair<std::string, std::vector<std::string>>>{
             {"background", background_args},
             {"lighting", lighting_args},
             {"combined", combined_args},
         }) {
        check_repeated_cli_render(
            executable, root, obj, environment,
            mode.first + "_1x_ppm", "ppm", "1", mode.second);
        check_repeated_cli_render(
            executable, root, obj, environment,
            mode.first + "_4x_pfm", "pfm", "4", mode.second);
    }

    const auto check_failed_without_output = [&](
                                                 const std::string& name,
                                                 std::vector<std::string> arguments) {
        const std::filesystem::path output = root / (name + ".ppm");
        arguments.insert(arguments.begin(), output.string());
        arguments.insert(arguments.begin(), obj.string());
        const int result = run_renderer(executable, arguments);
        check(result != 0, name + " malformed CLI state is rejected");
        check(!std::filesystem::exists(output), name + " rejection occurs before output creation");
    };

    check_failed_without_output(
        "orphan_light_intensity",
        {"--environment-light-intensity", "2.0"});
    check_failed_without_output(
        "duplicate_light",
        {
            "--environment-light", environment.string(),
            "--environment-light", environment.string(),
        });
    check_failed_without_output(
        "missing_light_file",
        {"--environment-light", (root / "missing.pfm").string()});

    std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main(int argc, char** argv) {
    test_default_preview_remains_compatible();
    test_environment_uses_exact_preview_camera_and_preserves_attachments();
    test_background_and_diffuse_lighting_are_independent();
    test_invalid_environment_is_rejected();
    test_invalid_or_duplicate_environment_lighting_is_rejected();
    if (argc > 0) {
        test_real_headless_environment_lighting_cli(argv[0]);
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "headless environment tests passed\n";
    return 0;
}
