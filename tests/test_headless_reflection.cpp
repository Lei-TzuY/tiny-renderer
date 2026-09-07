#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

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

VaryingPack normal_varyings() {
    VaryingPack varyings;
    varyings.count = 3U;
    varyings.values[0] = 0.0F;
    varyings.values[1] = 0.0F;
    varyings.values[2] = 1.0F;
    return varyings;
}

ModelAsset normal_triangle_asset(
    Vec3 albedo = {0.35F, 0.2F, 0.1F},
    Vec3 specular = {0.8F, 0.5F, 0.25F}) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-1.0F, -1.0F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({1.0F, -1.0F, 0.0F}, normal_varyings()),
        Vertex::with_varyings({0.0F, 1.0F, 0.0F}, normal_varyings()),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "reflective";
    draw.material.albedo = albedo;
    draw.material.specular = specular;
    draw.material.shininess = 32.0F;
    asset.draws.push_back(draw);
    return asset;
}

Texture2D directional_environment_texture() {
    return Texture2D(
        4U,
        2U,
        {
            {7.0F, 0.2F, 0.1F},
            {0.2F, 6.0F, 0.1F},
            {0.1F, 0.2F, 5.0F},
            {3.5F, 1.0F, 0.25F},
            {0.5F, 3.0F, 0.25F},
            {2.0F, 0.5F, 4.0F},
            {5.0F, 2.0F, 0.5F},
            {0.25F, 4.0F, 2.0F},
        });
}

Texture2D constant_environment_texture() {
    return Texture2D(
        2U,
        2U,
        {
            {1.5F, 0.75F, 0.25F},
            {1.5F, 0.75F, 0.25F},
            {1.5F, 0.75F, 0.25F},
            {1.5F, 0.75F, 0.25F},
        });
}

EnvironmentReflectionState reflection_state(
    const Texture2D& texture,
    float intensity = 1.0F,
    float yaw = 0.0F) {
    EnvironmentReflectionState state;
    state.texture = &texture;
    state.sampler.address_u = AddressMode::Repeat;
    state.sampler.address_v = AddressMode::Clamp;
    state.sampler.filter = FilterMode::Nearest;
    state.sampler.mip_filter = MipFilterMode::Disabled;
    state.intensity = intensity;
    state.yaw_radians = yaw;
    return state;
}

EnvironmentBackgroundState background_state(const Texture2D& texture) {
    EnvironmentBackgroundState state;
    state.texture = &texture;
    state.sampler.address_u = AddressMode::Repeat;
    state.sampler.address_v = AddressMode::Clamp;
    state.sampler.filter = FilterMode::Bilinear;
    state.sampler.mip_filter = MipFilterMode::Disabled;
    state.intensity = 0.8F;
    state.yaw_radians = 0.15F;
    return state;
}

EnvironmentDiffuseLight diffuse_light(const Texture2D& texture) {
    EnvironmentDiffuseLight light;
    light.normal = {0U, 1U, 2U};
    light.environment.texture = &texture;
    light.environment.intensity = 0.35F;
    light.environment.yaw_radians = -0.1F;
    return light;
}

OfflineEnvironmentReflectionState offline_reflection(const Texture2D& texture) {
    OfflineEnvironmentReflectionState state;
    state.normal = {0U, 1U, 2U};
    state.environment = reflection_state(texture, 0.65F, 0.2F);
    return state;
}

void check_exact_samples(
    const Framebuffer& actual,
    const Framebuffer& expected,
    const std::string& message) {
    bool equal = actual.width() == expected.width()
        && actual.height() == expected.height()
        && actual.samples_per_pixel() == expected.samples_per_pixel();
    if (equal) {
        for (std::size_t y = 0U; y < actual.height(); ++y) {
            for (std::size_t x = 0U; x < actual.width(); ++x) {
                for (std::size_t sample = 0U; sample < actual.samples_per_pixel(); ++sample) {
                    const Vec3 a = actual.sample_color_at(x, y, sample);
                    const Vec3 b = expected.sample_color_at(x, y, sample);
                    equal = equal
                        && a.x == b.x && a.y == b.y && a.z == b.z
                        && actual.sample_depth_at(x, y, sample) == expected.sample_depth_at(x, y, sample)
                        && actual.sample_stencil_at(x, y, sample) == expected.sample_stencil_at(x, y, sample);
                }
            }
        }
    }
    check(equal, message);
}

void test_reflection_binds_exact_preview_camera_eye() {
    const ModelAsset asset = normal_triangle_asset({0.0F, 0.0F, 0.0F});
    const Texture2D environment = directional_environment_texture();

    for (const SampleCount sample_count : {SampleCount::One, SampleCount::Four}) {
        OfflineRenderSettings injected_settings{};
        injected_settings.width = 53U;
        injected_settings.height = 39U;
        injected_settings.sample_count = sample_count;
        injected_settings.environment_reflection = offline_reflection(environment);

        const Framebuffer injected_a = render_model_preview(asset, injected_settings);
        const Framebuffer injected_b = render_model_preview(asset, injected_settings);
        check_exact_samples(
            injected_a,
            injected_b,
            sample_count == SampleCount::One
                ? "1x headless reflection is exactly deterministic"
                : "4x headless reflection is exactly deterministic");

        OfflineRenderSettings explicit_settings = injected_settings;
        explicit_settings.environment_reflection.reset();
        ModelRenderOptions explicit_options{};
        EnvironmentReflectionLight explicit_light;
        explicit_light.normal = {0U, 1U, 2U};
        explicit_light.viewer_position = {0.0F, 0.0F, 3.0F};
        explicit_light.environment = offline_reflection(environment).environment;
        explicit_options.fixed_lights.environment_reflection = explicit_light;
        const Framebuffer explicit_camera = render_model_preview(
            asset, explicit_settings, explicit_options);
        check_exact_samples(
            injected_a,
            explicit_camera,
            "headless reflection injection uses the exact authoritative preview camera eye");

        explicit_options.fixed_lights.environment_reflection->viewer_position = {2.0F, 0.0F, 3.0F};
        const Framebuffer mismatched_camera = render_model_preview(
            asset, explicit_settings, explicit_options);
        check(
            injected_a.rgb8() != mismatched_camera.rgb8(),
            "a mismatched reflection viewer changes a directional environment result");
    }
}

void test_reflection_zero_specular_and_combined_environment() {
    const Texture2D environment = constant_environment_texture();

    for (const SampleCount sample_count : {SampleCount::One, SampleCount::Four}) {
        OfflineRenderSettings base{};
        base.width = 49U;
        base.height = 37U;
        base.sample_count = sample_count;
        base.clear_color = {0.02F, 0.03F, 0.04F};

        const ModelAsset zero_specular = normal_triangle_asset(
            {0.35F, 0.2F, 0.1F}, {0.0F, 0.0F, 0.0F});
        const Framebuffer zero_baseline = render_model_preview(zero_specular, base);
        OfflineRenderSettings zero_reflection_settings = base;
        zero_reflection_settings.environment_reflection = offline_reflection(environment);
        const Framebuffer zero_reflection = render_model_preview(
            zero_specular, zero_reflection_settings);
        check_exact_samples(
            zero_reflection,
            zero_baseline,
            "zero material specular preserves historical headless output exactly");

        const ModelAsset reflective = normal_triangle_asset();
        OfflineRenderSettings diffuse_background = base;
        diffuse_background.environment = background_state(environment);
        diffuse_background.environment_lighting = diffuse_light(environment);
        const Framebuffer without_reflection = render_model_preview(reflective, diffuse_background);

        OfflineRenderSettings combined = diffuse_background;
        combined.environment_reflection = offline_reflection(environment);
        const Framebuffer combined_a = render_model_preview(reflective, combined);
        const Framebuffer combined_b = render_model_preview(reflective, combined);
        check_exact_samples(
            combined_a,
            combined_b,
            "background + diffuse + reflection preview is exactly deterministic");

        const std::size_t center_x = base.width / 2U;
        const std::size_t center_y = base.height / 2U;
        check(
            combined_a.rgb8() != without_reflection.rgb8(),
            "reflection adds an observable specular environment contribution");
        check_vec3_near(
            combined_a.color_at(0U, 0U),
            without_reflection.color_at(0U, 0U),
            "reflection leaves uncovered environment background pixels unchanged");
        check(
            !near(combined_a.color_at(center_x, center_y).x,
                  without_reflection.color_at(center_x, center_y).x),
            "reflection changes covered model radiance without changing the background path");
    }
}

void test_invalid_or_duplicate_reflection_is_rejected() {
    const ModelAsset asset = normal_triangle_asset();
    OfflineRenderSettings settings{};
    settings.environment_reflection = OfflineEnvironmentReflectionState{};
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings); },
        "preview rejects null reflection environment texture");

    const Texture2D environment = constant_environment_texture();
    settings.environment_reflection = offline_reflection(environment);
    settings.environment_reflection->environment.intensity =
        std::numeric_limits<float>::infinity();
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings); },
        "preview rejects invalid reflection intensity");

    settings.environment_reflection = offline_reflection(environment);
    ModelRenderOptions options{};
    EnvironmentReflectionLight duplicate;
    duplicate.normal = {0U, 1U, 2U};
    duplicate.viewer_position = {0.0F, 0.0F, 3.0F};
    duplicate.environment = settings.environment_reflection->environment;
    options.fixed_lights.environment_reflection = duplicate;
    check_throws<std::invalid_argument>(
        [&] { (void)render_model_preview(asset, settings, options); },
        "preview rejects duplicate environment-reflection ownership");

    settings.environment_reflection = offline_reflection(environment);
    settings.environment_reflection->normal = {3U, 4U, 5U};
    check_throws<std::out_of_range>(
        [&] { (void)render_model_preview(asset, settings); },
        "preview rejects reflection normal bindings outside canonical model varyings");
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to create headless reflection CLI fixture");
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("failed to write headless reflection CLI fixture");
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
        throw std::runtime_error("failed to create reflection HDR fixture");
    }
    output << "PF\n2 2\n-1.0\n";
    for (std::size_t pixel = 0U; pixel < 4U; ++pixel) {
        write_float_le(output, 1.5F);
        write_float_le(output, 0.75F);
        write_float_le(output, 0.25F);
    }
    if (!output) {
        throw std::runtime_error("failed to write reflection HDR fixture");
    }
}

std::vector<char> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to read headless reflection CLI output");
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
    const std::string& label,
    const std::string& extension,
    const std::string& samples,
    const std::vector<std::string>& mode_arguments) {
    const std::filesystem::path output_a = root / (label + "_a." + extension);
    const std::filesystem::path output_b = root / (label + "_b." + extension);
    std::vector<std::string> arguments{
        obj.string(), output_a.string(), "32", "24", samples,
    };
    arguments.insert(arguments.end(), mode_arguments.begin(), mode_arguments.end());
    const int first = run_renderer(executable, arguments);
    arguments[1] = output_b.string();
    const int second = run_renderer(executable, arguments);
    check(first == 0 && second == 0, label + " CLI renders succeed twice");
    if (first == 0 && second == 0) {
        check(
            read_file_bytes(output_a) == read_file_bytes(output_b),
            label + " repeated CLI output is byte-for-byte deterministic");
    }
}

void test_real_headless_reflection_cli(const std::filesystem::path& argv0) {
    std::filesystem::path executable = std::filesystem::absolute(argv0).parent_path()
        / "tiny_renderer_render";
    if (!std::filesystem::exists(executable)) {
        const std::filesystem::path exe_candidate = executable.string() + ".exe";
        if (std::filesystem::exists(exe_candidate)) {
            executable = exe_candidate;
        }
    }
    check(std::filesystem::exists(executable), "headless render CLI is available to reflection integration test");
    if (!std::filesystem::exists(executable)) {
        return;
    }

    const std::size_t path_hash = std::hash<std::string>{}(std::filesystem::absolute(argv0).string());
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("tiny_renderer_m59_" + std::to_string(path_hash));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const std::filesystem::path glossy_obj = root / "glossy.obj";
    const std::filesystem::path glossy_mtl = root / "glossy.mtl";
    const std::filesystem::path zero_obj = root / "zero.obj";
    const std::filesystem::path zero_mtl = root / "zero.mtl";
    const std::filesystem::path generated_obj = root / "generated.obj";
    const std::filesystem::path mixed_layout_obj = root / "mixed_layout.obj";
    const std::filesystem::path environment = root / "environment.pfm";

    write_text_file(
        glossy_obj,
        "mtllib glossy.mtl\n"
        "v -1 -1 0\n"
        "v 1 -1 0\n"
        "v 0 1 0\n"
        "vn 0 0 1\n"
        "usemtl glossy\n"
        "f 1//1 2//1 3//1\n");
    write_text_file(
        glossy_mtl,
        "newmtl glossy\n"
        "Kd 0.2 0.1 0.05\n"
        "Ks 0.8 0.4 0.2\n"
        "Ns 32\n");
    write_text_file(
        zero_obj,
        "mtllib zero.mtl\n"
        "v -1 -1 0\n"
        "v 1 -1 0\n"
        "v 0 1 0\n"
        "vn 0 0 1\n"
        "usemtl zero\n"
        "f 1//1 2//1 3//1\n");
    write_text_file(
        zero_mtl,
        "newmtl zero\n"
        "Kd 0.2 0.1 0.05\n"
        "Ks 0 0 0\n"
        "Ns 32\n");
    write_text_file(
        generated_obj,
        "mtllib glossy.mtl\n"
        "v -1 -1 0\n"
        "v 1 -1 0\n"
        "v 0 1 0\n"
        "usemtl glossy\n"
        "f 1 2 3\n");
    write_text_file(
        mixed_layout_obj,
        "mtllib glossy.mtl\n"
        "v -1 -1 0\n"
        "v 1 -1 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0.5 1\n"
        "vn 0 0 1\n"
        "usemtl glossy\n"
        "f 1/1/1 2/2 3/3/1\n");
    write_constant_pfm(environment);

    const std::vector<std::string> reflection_args{
        "--environment-reflection", environment.string(),
        "--environment-reflection-intensity", "0.65",
        "--environment-reflection-yaw", "0.2",
    };
    const std::vector<std::string> background_args{
        "--environment", environment.string(),
        "--environment-intensity", "0.8",
        "--environment-yaw", "0.15",
    };
    const std::vector<std::string> diffuse_args{
        "--environment-light", environment.string(),
        "--environment-light-intensity", "0.35",
        "--environment-light-yaw", "-0.1",
    };
    std::vector<std::string> combined_args = background_args;
    combined_args.insert(combined_args.end(), diffuse_args.begin(), diffuse_args.end());
    combined_args.insert(combined_args.end(), reflection_args.begin(), reflection_args.end());

    check_repeated_cli_render(
        executable, root, glossy_obj,
        "reflection_1x_ppm", "ppm", "1", reflection_args);
    check_repeated_cli_render(
        executable, root, glossy_obj,
        "reflection_4x_pfm", "pfm", "4", reflection_args);
    check_repeated_cli_render(
        executable, root, glossy_obj,
        "combined_1x_ppm", "ppm", "1", combined_args);
    check_repeated_cli_render(
        executable, root, glossy_obj,
        "combined_4x_pfm", "pfm", "4", combined_args);
    check_repeated_cli_render(
        executable, root, generated_obj,
        "generated_normals_reflection", "ppm", "1", reflection_args);

    const std::filesystem::path glossy_baseline = root / "glossy_baseline.pfm";
    const std::filesystem::path glossy_reflection = root / "glossy_reflection.pfm";
    check(
        run_renderer(executable, {glossy_obj.string(), glossy_baseline.string(), "32", "24", "1"}) == 0,
        "glossy baseline CLI render succeeds");
    std::vector<std::string> glossy_reflection_arguments{
        glossy_obj.string(), glossy_reflection.string(), "32", "24", "1",
    };
    glossy_reflection_arguments.insert(
        glossy_reflection_arguments.end(), reflection_args.begin(), reflection_args.end());
    check(
        run_renderer(executable, glossy_reflection_arguments) == 0,
        "glossy reflection CLI render succeeds");
    if (std::filesystem::exists(glossy_baseline) && std::filesystem::exists(glossy_reflection)) {
        check(
            read_file_bytes(glossy_baseline) != read_file_bytes(glossy_reflection),
            "CLI reflection observably shades non-zero Ks material");
    }

    const std::filesystem::path zero_baseline = root / "zero_baseline.pfm";
    const std::filesystem::path zero_reflection = root / "zero_reflection.pfm";
    check(
        run_renderer(executable, {zero_obj.string(), zero_baseline.string(), "32", "24", "4"}) == 0,
        "zero-specular baseline CLI render succeeds");
    std::vector<std::string> zero_reflection_arguments{
        zero_obj.string(), zero_reflection.string(), "32", "24", "4",
    };
    zero_reflection_arguments.insert(
        zero_reflection_arguments.end(), reflection_args.begin(), reflection_args.end());
    check(
        run_renderer(executable, zero_reflection_arguments) == 0,
        "zero-specular reflection CLI render succeeds");
    if (std::filesystem::exists(zero_baseline) && std::filesystem::exists(zero_reflection)) {
        check(
            read_file_bytes(zero_baseline) == read_file_bytes(zero_reflection),
            "zero Ks keeps reflection-enabled CLI output byte-identical");
    }

    const auto check_failed_without_output = [&](
                                                 const std::filesystem::path& obj,
                                                 const std::string& name,
                                                 std::vector<std::string> arguments) {
        const std::filesystem::path output = root / (name + ".ppm");
        std::filesystem::remove(output, ignored);
        arguments.insert(arguments.begin(), output.string());
        arguments.insert(arguments.begin(), obj.string());
        const int result = run_renderer(executable, arguments);
        check(result != 0, name + " malformed reflection CLI state is rejected");
        check(!std::filesystem::exists(output), name + " rejection occurs before output creation");
    };

    check_failed_without_output(
        glossy_obj,
        "orphan_reflection_intensity",
        {"--environment-reflection-intensity", "2.0"});
    check_failed_without_output(
        glossy_obj,
        "duplicate_reflection",
        {
            "--environment-reflection", environment.string(),
            "--environment-reflection", environment.string(),
        });
    check_failed_without_output(
        glossy_obj,
        "missing_reflection_file",
        {"--environment-reflection", (root / "missing.pfm").string()});
    check_failed_without_output(
        mixed_layout_obj,
        "unsupported_mixed_normal_layout",
        {"--environment-reflection", environment.string()});

    std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main(int argc, char** argv) {
    test_reflection_binds_exact_preview_camera_eye();
    test_reflection_zero_specular_and_combined_environment();
    test_invalid_or_duplicate_reflection_is_rejected();
    if (argc > 0) {
        test_real_headless_reflection_cli(argv[0]);
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "headless reflection tests passed\n";
    return 0;
}
