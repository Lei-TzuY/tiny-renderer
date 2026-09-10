#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/image_loader.hpp"
#include "tiny_renderer/model_renderer.hpp"
#include "tiny_renderer/obj_loader.hpp"
#include "tiny_renderer/offline_render.hpp"

namespace {

std::size_t parse_positive_size(std::string_view text, const char* label) {
    std::size_t parsed_count = 0U;
    unsigned long long value = 0ULL;
    try {
        value = std::stoull(std::string(text), &parsed_count, 10);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " must be a positive integer");
    }
    if (parsed_count != text.size() || value == 0ULL
        || value > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(std::string(label) + " must be a positive integer");
    }
    return static_cast<std::size_t>(value);
}

float parse_finite_float(std::string_view text, const char* label) {
    std::size_t parsed_count = 0U;
    float value = 0.0F;
    try {
        value = std::stof(std::string(text), &parsed_count);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " must be a finite number");
    }
    if (parsed_count != text.size() || !std::isfinite(value)) {
        throw std::invalid_argument(std::string(label) + " must be a finite number");
    }
    return value;
}

tiny_renderer::SampleCount parse_sample_count(std::string_view text) {
    if (text == "1") {
        return tiny_renderer::SampleCount::One;
    }
    if (text == "4") {
        return tiny_renderer::SampleCount::Four;
    }
    throw std::invalid_argument("sample count must be 1 or 4");
}

tiny_renderer::MipFilterMode parse_mip_filter(std::string_view text, const char* label) {
    if (text == "base") {
        return tiny_renderer::MipFilterMode::Disabled;
    }
    if (text == "nearest") {
        return tiny_renderer::MipFilterMode::Nearest;
    }
    if (text == "linear") {
        return tiny_renderer::MipFilterMode::Linear;
    }
    throw std::invalid_argument(std::string(label) + " must be base, nearest, or linear");
}

std::size_t parse_anisotropy(std::string_view text, const char* label) {
    const std::size_t value = parse_positive_size(text, label);
    if (value != 1U && value != 2U && value != 4U) {
        throw std::invalid_argument(std::string(label) + " must be 1, 2, or 4");
    }
    return value;
}

tiny_renderer::OutputTransferFunction parse_output_transfer(std::string_view text) {
    if (text == "linear") {
        return tiny_renderer::OutputTransferFunction::Linear;
    }
    if (text == "srgb") {
        return tiny_renderer::OutputTransferFunction::Srgb;
    }
    throw std::invalid_argument("output transfer must be linear or srgb");
}

std::string lowercase_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

bool has_normal_texture(const tiny_renderer::ModelAsset& asset) {
    return std::any_of(
        asset.draws.begin(),
        asset.draws.end(),
        [](const tiny_renderer::MaterialDraw& draw) {
            return static_cast<bool>(draw.normal_texture);
        });
}

tiny_renderer::NormalBinding preview_normal_binding(const tiny_renderer::ModelAsset& asset) {
    if (asset.mesh.vertices.empty()) {
        throw std::invalid_argument("environment-lit preview requires a non-empty normal-bearing model");
    }
    const std::size_t varying_count = asset.mesh.vertices.front().varyings.count;
    if (varying_count == 3U) {
        return {0U, 1U, 2U};
    }
    if (varying_count == 5U) {
        return {2U, 3U, 4U};
    }
    throw std::invalid_argument(
        "environment-lit preview requires canonical OBJ normals (v//vn or v/vt/vn)");
}

tiny_renderer::ModelRenderOptions preview_options(const tiny_renderer::ModelAsset& asset) {
    tiny_renderer::ModelRenderOptions options{};
    if (!has_normal_texture(asset)) {
        return options;
    }
    if (asset.mesh.vertices.empty() || asset.mesh.vertices.front().varyings.count < 5U) {
        throw std::invalid_argument("normal-mapped preview asset lacks canonical UV+normal varyings");
    }
    options.directional_light = tiny_renderer::DirectionalLight{
        true,
        {2U, 3U, 4U},
        {0.4F, 0.7F, 1.0F},
        0.2F,
        0.8F,
        {0.0F, 0.0F, 3.0F},
        {1.0F, 1.0F, 1.0F},
    };
    return options;
}

void apply_scene_material_shading_override(
    tiny_renderer::ModelAsset& asset,
    const std::optional<tiny_renderer::MaterialShadingModel>& shading_model_override) {
    if (!shading_model_override) {
        return;
    }
    for (tiny_renderer::MaterialDraw& draw : asset.draws) {
        draw.material.shading_model = *shading_model_override;
    }
}

struct ParsedArguments {
    tiny_renderer::OfflineRenderSettings settings{};
    std::optional<tiny_renderer::MipFilterMode> texture_mip{};
    std::optional<std::size_t> texture_anisotropy{};
    std::optional<float> display_exposure{};
    std::optional<tiny_renderer::OutputTransferFunction> output_transfer{};
    std::optional<std::filesystem::path> environment_path{};
    std::optional<float> environment_intensity{};
    std::optional<float> environment_yaw{};
    std::optional<tiny_renderer::MipFilterMode> environment_mip{};
    std::optional<std::filesystem::path> environment_light_path{};
    std::optional<float> environment_light_intensity{};
    std::optional<float> environment_light_yaw{};
    std::optional<std::filesystem::path> environment_reflection_path{};
    std::optional<float> environment_reflection_intensity{};
    std::optional<float> environment_reflection_yaw{};
    std::optional<tiny_renderer::MipFilterMode> environment_reflection_mip{};
    std::optional<std::size_t> environment_reflection_anisotropy{};
    std::optional<float> environment_reflection_footprint{};
    bool environment_reflection_material_shininess{false};
};

ParsedArguments parse_arguments(int argc, char** argv) {
    ParsedArguments parsed;
    std::vector<std::string_view> positional;
    bool saw_texture_mip = false;
    bool saw_texture_anisotropy = false;
    bool saw_display_exposure = false;
    bool saw_output_transfer = false;
    bool saw_environment = false;
    bool saw_intensity = false;
    bool saw_yaw = false;
    bool saw_mip = false;
    bool saw_environment_light = false;
    bool saw_light_intensity = false;
    bool saw_light_yaw = false;
    bool saw_environment_reflection = false;
    bool saw_reflection_intensity = false;
    bool saw_reflection_yaw = false;
    bool saw_reflection_mip = false;
    bool saw_reflection_anisotropy = false;
    bool saw_reflection_footprint = false;
    bool saw_reflection_material_shininess = false;

    for (int index = 3; index < argc; ++index) {
        const std::string_view token = argv[index];
        const auto require_value = [&](const char* option) -> std::string_view {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string(option) + " requires a value");
            }
            ++index;
            return argv[index];
        };

        if (token == "--texture-mip") {
            if (saw_texture_mip) {
                throw std::invalid_argument("--texture-mip may be specified at most once");
            }
            saw_texture_mip = true;
            parsed.texture_mip = parse_mip_filter(require_value("--texture-mip"), "texture mip mode");
        } else if (token == "--texture-anisotropy") {
            if (saw_texture_anisotropy) {
                throw std::invalid_argument("--texture-anisotropy may be specified at most once");
            }
            saw_texture_anisotropy = true;
            parsed.texture_anisotropy = parse_anisotropy(
                require_value("--texture-anisotropy"),
                "texture anisotropy");
        } else if (token == "--display-exposure") {
            if (saw_display_exposure) {
                throw std::invalid_argument("--display-exposure may be specified at most once");
            }
            saw_display_exposure = true;
            parsed.display_exposure = parse_finite_float(
                require_value("--display-exposure"),
                "display exposure");
        } else if (token == "--output-transfer") {
            if (saw_output_transfer) {
                throw std::invalid_argument("--output-transfer may be specified at most once");
            }
            saw_output_transfer = true;
            parsed.output_transfer = parse_output_transfer(require_value("--output-transfer"));
        } else if (token == "--environment") {
            if (saw_environment) {
                throw std::invalid_argument("--environment may be specified at most once");
            }
            saw_environment = true;
            parsed.environment_path = std::filesystem::path(require_value("--environment"));
            if (parsed.environment_path->empty()) {
                throw std::invalid_argument("--environment requires a non-empty path");
            }
        } else if (token == "--environment-intensity") {
            if (saw_intensity) {
                throw std::invalid_argument("--environment-intensity may be specified at most once");
            }
            saw_intensity = true;
            parsed.environment_intensity = parse_finite_float(
                require_value("--environment-intensity"),
                "environment intensity");
        } else if (token == "--environment-yaw") {
            if (saw_yaw) {
                throw std::invalid_argument("--environment-yaw may be specified at most once");
            }
            saw_yaw = true;
            parsed.environment_yaw = parse_finite_float(
                require_value("--environment-yaw"),
                "environment yaw");
        } else if (token == "--environment-mip") {
            if (saw_mip) {
                throw std::invalid_argument("--environment-mip may be specified at most once");
            }
            saw_mip = true;
            parsed.environment_mip = parse_mip_filter(require_value("--environment-mip"), "environment mip mode");
        } else if (token == "--environment-light") {
            if (saw_environment_light) {
                throw std::invalid_argument("--environment-light may be specified at most once");
            }
            saw_environment_light = true;
            parsed.environment_light_path = std::filesystem::path(require_value("--environment-light"));
            if (parsed.environment_light_path->empty()) {
                throw std::invalid_argument("--environment-light requires a non-empty path");
            }
        } else if (token == "--environment-light-intensity") {
            if (saw_light_intensity) {
                throw std::invalid_argument("--environment-light-intensity may be specified at most once");
            }
            saw_light_intensity = true;
            parsed.environment_light_intensity = parse_finite_float(
                require_value("--environment-light-intensity"),
                "environment light intensity");
        } else if (token == "--environment-light-yaw") {
            if (saw_light_yaw) {
                throw std::invalid_argument("--environment-light-yaw may be specified at most once");
            }
            saw_light_yaw = true;
            parsed.environment_light_yaw = parse_finite_float(
                require_value("--environment-light-yaw"),
                "environment light yaw");
        } else if (token == "--environment-reflection") {
            if (saw_environment_reflection) {
                throw std::invalid_argument("--environment-reflection may be specified at most once");
            }
            saw_environment_reflection = true;
            parsed.environment_reflection_path =
                std::filesystem::path(require_value("--environment-reflection"));
            if (parsed.environment_reflection_path->empty()) {
                throw std::invalid_argument("--environment-reflection requires a non-empty path");
            }
        } else if (token == "--environment-reflection-intensity") {
            if (saw_reflection_intensity) {
                throw std::invalid_argument(
                    "--environment-reflection-intensity may be specified at most once");
            }
            saw_reflection_intensity = true;
            parsed.environment_reflection_intensity = parse_finite_float(
                require_value("--environment-reflection-intensity"),
                "environment reflection intensity");
        } else if (token == "--environment-reflection-yaw") {
            if (saw_reflection_yaw) {
                throw std::invalid_argument(
                    "--environment-reflection-yaw may be specified at most once");
            }
            saw_reflection_yaw = true;
            parsed.environment_reflection_yaw = parse_finite_float(
                require_value("--environment-reflection-yaw"),
                "environment reflection yaw");
        } else if (token == "--environment-reflection-mip") {
            if (saw_reflection_mip) {
                throw std::invalid_argument(
                    "--environment-reflection-mip may be specified at most once");
            }
            saw_reflection_mip = true;
            parsed.environment_reflection_mip = parse_mip_filter(
                require_value("--environment-reflection-mip"),
                "environment reflection mip mode");
        } else if (token == "--environment-reflection-anisotropy") {
            if (saw_reflection_anisotropy) {
                throw std::invalid_argument(
                    "--environment-reflection-anisotropy may be specified at most once");
            }
            saw_reflection_anisotropy = true;
            parsed.environment_reflection_anisotropy = parse_anisotropy(
                require_value("--environment-reflection-anisotropy"),
                "environment reflection anisotropy");
        } else if (token == "--environment-reflection-footprint") {
            if (saw_reflection_footprint) {
                throw std::invalid_argument(
                    "--environment-reflection-footprint may be specified at most once");
            }
            saw_reflection_footprint = true;
            parsed.environment_reflection_footprint = parse_finite_float(
                require_value("--environment-reflection-footprint"),
                "environment reflection angular footprint");
        } else if (token == "--environment-reflection-material-shininess") {
            if (saw_reflection_material_shininess) {
                throw std::invalid_argument(
                    "--environment-reflection-material-shininess may be specified at most once");
            }
            saw_reflection_material_shininess = true;
            parsed.environment_reflection_material_shininess = true;
        } else if (token.starts_with("--")) {
            throw std::invalid_argument("unknown option: " + std::string(token));
        } else {
            positional.push_back(token);
        }
    }

    if (positional.size() != 0U && positional.size() != 2U && positional.size() != 3U) {
        throw std::invalid_argument("optional positional arguments must be WIDTH HEIGHT [SAMPLES]");
    }
    if (positional.size() >= 2U) {
        parsed.settings.width = parse_positive_size(positional[0], "width");
        parsed.settings.height = parse_positive_size(positional[1], "height");
    }
    if (positional.size() == 3U) {
        parsed.settings.sample_count = parse_sample_count(positional[2]);
    }
    if ((parsed.environment_intensity || parsed.environment_yaw || parsed.environment_mip)
        && !parsed.environment_path) {
        throw std::invalid_argument("environment intensity/yaw/mip requires --environment");
    }
    if ((parsed.environment_light_intensity || parsed.environment_light_yaw)
        && !parsed.environment_light_path) {
        throw std::invalid_argument(
            "environment light intensity/yaw requires --environment-light");
    }
    if ((parsed.environment_reflection_intensity
         || parsed.environment_reflection_yaw
         || parsed.environment_reflection_mip
         || parsed.environment_reflection_anisotropy
         || parsed.environment_reflection_footprint
         || parsed.environment_reflection_material_shininess)
        && !parsed.environment_reflection_path) {
        throw std::invalid_argument(
            "environment reflection controls require --environment-reflection");
    }
    return parsed;
}

void apply_texture_sampler_options(
    tiny_renderer::ModelRenderOptions& options,
    const ParsedArguments& parsed) {
    if (parsed.texture_mip) {
        options.sampler.mip_filter = *parsed.texture_mip;
    }
    if (parsed.texture_anisotropy) {
        options.sampler.max_anisotropy = *parsed.texture_anisotropy;
    }
    tiny_renderer::validate_sampler_state(options.sampler);
}

bool same_normal_binding(
    const tiny_renderer::NormalBinding& a,
    const tiny_renderer::NormalBinding& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

tiny_renderer::NormalBinding common_scene_normal_binding(
    const std::vector<tiny_renderer::ModelAsset>& assets) {
    if (assets.empty()) {
        throw std::logic_error("cannot infer a normal binding for an empty scene");
    }
    const tiny_renderer::NormalBinding binding = preview_normal_binding(assets.front());
    for (std::size_t i = 1U; i < assets.size(); ++i) {
        const tiny_renderer::NormalBinding candidate = preview_normal_binding(assets[i]);
        if (!same_normal_binding(binding, candidate)) {
            throw std::invalid_argument(
                "scene environment lighting/reflection requires one shared canonical normal layout across all models");
        }
    }
    return binding;
}

const char* ordering_name(tiny_renderer::OfflineSceneOrdering ordering) {
    switch (ordering) {
        case tiny_renderer::OfflineSceneOrdering::InputOrder:
            return "input";
        case tiny_renderer::OfflineSceneOrdering::BackToFront:
            return "back-to-front";
    }
    throw std::logic_error("unknown offline scene ordering after manifest validation");
}

void print_usage() {
    std::cerr
        << "usage: tiny_renderer_render INPUT.(obj|trscene) OUTPUT.(ppm|pfm) [WIDTH HEIGHT [SAMPLES]]"
           " [--texture-mip base|nearest|linear] [--texture-anisotropy 1|2|4]"
           " [--display-exposure VALUE] [--output-transfer linear|srgb]"
           " [--environment IMAGE] [--environment-intensity VALUE] [--environment-yaw RADIANS]"
           " [--environment-mip base|nearest|linear]"
           " [--environment-light IMAGE] [--environment-light-intensity VALUE]"
           " [--environment-light-yaw RADIANS]"
           " [--environment-reflection IMAGE] [--environment-reflection-intensity VALUE]"
           " [--environment-reflection-yaw RADIANS]"
           " [--environment-reflection-mip base|nearest|linear]"
           " [--environment-reflection-anisotropy 1|2|4]"
           " [--environment-reflection-footprint RADIANS]"
           " [--environment-reflection-material-shininess]\n"
        << "  .trscene format: tiny-renderer-scene-v1; optional 'ordering input|back-to-front';"
           " optional 'camera EX EY EZ TX TY TZ UX UY UZ VFOV_RADIANS NEAR FAR';"
           " repeat 'model FILE.obj TX TY TZ SCALE ROTATION_Y_RADIANS [inherit|lambert|blinn-phong]"
           " [opaque|source-alpha|alpha-to-coverage]' (max 256 sibling OBJ files)\n"
        << "  defaults: WIDTH=512 HEIGHT=512 SAMPLES=4 texture-mip=base texture-anisotropy=1"
           " display-exposure=1 output-transfer=srgb"
           " environment-intensity=1 environment-yaw=0 environment-mip=base"
           " environment-light-intensity=1 environment-light-yaw=0"
           " environment-reflection-intensity=1 environment-reflection-yaw=0"
           " environment-reflection-mip=base environment-reflection-anisotropy=1"
           " environment-reflection-footprint=0 reflection-policy=base\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            print_usage();
            return 2;
        }

        const std::filesystem::path input_path = argv[1];
        const std::filesystem::path output_path = argv[2];
        const std::string input_extension = lowercase_extension(input_path);
        if (input_extension != ".obj" && input_extension != ".trscene") {
            throw std::invalid_argument("input extension must be .obj or .trscene");
        }
        const std::string extension = lowercase_extension(output_path);
        if (extension != ".ppm" && extension != ".pfm") {
            throw std::invalid_argument("output extension must be .ppm or .pfm");
        }

        ParsedArguments parsed = parse_arguments(argc, argv);
        if (extension == ".pfm" && (parsed.display_exposure || parsed.output_transfer)) {
            throw std::invalid_argument("display output controls require .ppm output");
        }
        tiny_renderer::DisplayMappingState display_mapping{};
        if (parsed.display_exposure) {
            display_mapping.exposure = *parsed.display_exposure;
        }
        const tiny_renderer::OutputTransferFunction output_transfer =
            parsed.output_transfer.value_or(tiny_renderer::OutputTransferFunction::Srgb);
        tiny_renderer::validate_display_mapping_state(display_mapping);
        tiny_renderer::validate_output_transfer_function(output_transfer);

        std::optional<tiny_renderer::Texture2D> environment_texture;
        std::optional<tiny_renderer::Texture2D> environment_light_texture;
        std::optional<tiny_renderer::Texture2D> environment_reflection_texture;
        const tiny_renderer::Texture2D* background_texture = nullptr;
        const tiny_renderer::Texture2D* light_texture = nullptr;
        const tiny_renderer::Texture2D* reflection_texture = nullptr;

        if (parsed.environment_path) {
            environment_texture.emplace(tiny_renderer::load_texture_image_file(
                *parsed.environment_path,
                tiny_renderer::TextureTransferFunction::Linear));
            background_texture = &*environment_texture;

            tiny_renderer::EnvironmentBackgroundState environment;
            environment.texture = background_texture;
            if (parsed.environment_intensity) {
                environment.intensity = *parsed.environment_intensity;
            }
            if (parsed.environment_yaw) {
                environment.yaw_radians = *parsed.environment_yaw;
            }
            if (parsed.environment_mip) {
                environment.sampler.mip_filter = *parsed.environment_mip;
                environment.mip_policy = *parsed.environment_mip == tiny_renderer::MipFilterMode::Disabled
                    ? tiny_renderer::EnvironmentMipPolicy::BaseLevel
                    : tiny_renderer::EnvironmentMipPolicy::RayFootprint;
            }
            tiny_renderer::validate_environment_background_state(environment);
            parsed.settings.environment = environment;
        }

        std::optional<tiny_renderer::EnvironmentDiffuseState> diffuse_environment;
        if (parsed.environment_light_path) {
            if (parsed.environment_path
                && parsed.environment_path->lexically_normal()
                    == parsed.environment_light_path->lexically_normal()) {
                light_texture = background_texture;
            } else {
                environment_light_texture.emplace(tiny_renderer::load_texture_image_file(
                    *parsed.environment_light_path,
                    tiny_renderer::TextureTransferFunction::Linear));
                light_texture = &*environment_light_texture;
            }

            tiny_renderer::EnvironmentDiffuseState environment_light;
            environment_light.texture = light_texture;
            if (parsed.environment_light_intensity) {
                environment_light.intensity = *parsed.environment_light_intensity;
            }
            if (parsed.environment_light_yaw) {
                environment_light.yaw_radians = *parsed.environment_light_yaw;
            }
            tiny_renderer::validate_environment_diffuse_state(environment_light);
            diffuse_environment = environment_light;
        }

        std::optional<tiny_renderer::EnvironmentReflectionState> reflection_environment;
        if (parsed.environment_reflection_path) {
            const std::filesystem::path normalized_reflection =
                parsed.environment_reflection_path->lexically_normal();
            if (parsed.environment_path
                && parsed.environment_path->lexically_normal() == normalized_reflection) {
                reflection_texture = background_texture;
            } else if (parsed.environment_light_path
                && parsed.environment_light_path->lexically_normal() == normalized_reflection) {
                reflection_texture = light_texture;
            } else {
                environment_reflection_texture.emplace(tiny_renderer::load_texture_image_file(
                    *parsed.environment_reflection_path,
                    tiny_renderer::TextureTransferFunction::Linear));
                reflection_texture = &*environment_reflection_texture;
            }

            tiny_renderer::EnvironmentReflectionState environment_reflection;
            environment_reflection.texture = reflection_texture;
            if (parsed.environment_reflection_intensity) {
                environment_reflection.intensity = *parsed.environment_reflection_intensity;
            }
            if (parsed.environment_reflection_yaw) {
                environment_reflection.yaw_radians = *parsed.environment_reflection_yaw;
            }
            if (parsed.environment_reflection_mip) {
                environment_reflection.sampler.mip_filter = *parsed.environment_reflection_mip;
            }
            if (parsed.environment_reflection_anisotropy) {
                environment_reflection.sampler.max_anisotropy =
                    *parsed.environment_reflection_anisotropy;
            }
            if (parsed.environment_reflection_footprint) {
                environment_reflection.angular_footprint_radians =
                    *parsed.environment_reflection_footprint;
            }

            if (parsed.environment_reflection_material_shininess) {
                environment_reflection.mip_policy =
                    tiny_renderer::EnvironmentReflectionMipPolicy::MaterialShininess;
            } else if (parsed.environment_reflection_footprint
                       || (parsed.environment_reflection_mip
                           && *parsed.environment_reflection_mip
                               != tiny_renderer::MipFilterMode::Disabled)
                       || (parsed.environment_reflection_anisotropy
                           && *parsed.environment_reflection_anisotropy > 1U)) {
                environment_reflection.mip_policy =
                    tiny_renderer::EnvironmentReflectionMipPolicy::AngularFootprint;
            }

            tiny_renderer::validate_environment_reflection_state(environment_reflection);
            reflection_environment = environment_reflection;
        }

        tiny_renderer::Framebuffer framebuffer(1U, 1U);
        std::size_t rendered_models = 0U;
        tiny_renderer::OfflineSceneOrdering rendered_ordering =
            tiny_renderer::OfflineSceneOrdering::InputOrder;
        bool rendered_scene = false;
        bool rendered_explicit_camera = false;

        if (input_extension == ".obj") {
            const tiny_renderer::ModelAsset asset = tiny_renderer::load_obj_model_asset_file(input_path);
            tiny_renderer::ModelRenderOptions options = preview_options(asset);
            apply_texture_sampler_options(options, parsed);
            if (diffuse_environment) {
                tiny_renderer::EnvironmentDiffuseLight light;
                light.normal = preview_normal_binding(asset);
                light.environment = *diffuse_environment;
                parsed.settings.environment_lighting = light;
            }
            if (reflection_environment) {
                tiny_renderer::OfflineEnvironmentReflectionState reflection;
                reflection.normal = preview_normal_binding(asset);
                reflection.environment = *reflection_environment;
                parsed.settings.environment_reflection = reflection;
            }
            framebuffer = tiny_renderer::render_model_preview(asset, parsed.settings, options);
            rendered_models = 1U;
        } else {
            rendered_scene = true;
            const tiny_renderer::OfflineSceneManifest manifest =
                tiny_renderer::load_offline_scene_manifest_file(input_path);
            rendered_ordering = manifest.ordering;
            rendered_explicit_camera = manifest.camera.has_value();

            std::vector<tiny_renderer::ModelAsset> assets;
            std::vector<tiny_renderer::ModelRenderOptions> options;
            assets.reserve(manifest.entries.size());
            options.reserve(manifest.entries.size());
            for (const tiny_renderer::OfflineSceneManifestEntry& entry : manifest.entries) {
                assets.push_back(tiny_renderer::load_obj_model_asset_file(entry.model_path));
                apply_scene_material_shading_override(
                    assets.back(), entry.shading_model_override);
                options.push_back(preview_options(assets.back()));
                apply_texture_sampler_options(options.back(), parsed);
                tiny_renderer::apply_offline_scene_transparency_mode(
                    options.back(), entry.transparency_mode);
            }

            if (!assets.empty() && (diffuse_environment || reflection_environment)) {
                const tiny_renderer::NormalBinding normal = common_scene_normal_binding(assets);
                if (diffuse_environment) {
                    tiny_renderer::EnvironmentDiffuseLight light;
                    light.normal = normal;
                    light.environment = *diffuse_environment;
                    parsed.settings.environment_lighting = light;
                }
                if (reflection_environment) {
                    tiny_renderer::OfflineEnvironmentReflectionState reflection;
                    reflection.normal = normal;
                    reflection.environment = *reflection_environment;
                    parsed.settings.environment_reflection = reflection;
                }
            }

            std::vector<tiny_renderer::OfflineSceneEntry> scene_entries;
            scene_entries.reserve(manifest.entries.size());
            for (std::size_t i = 0U; i < manifest.entries.size(); ++i) {
                scene_entries.push_back({
                    &assets[i],
                    manifest.entries[i].model,
                    options[i],
                });
            }
            framebuffer = tiny_renderer::render_scene_preview(
                scene_entries,
                parsed.settings,
                manifest.ordering,
                manifest.camera);
            rendered_models = scene_entries.size();
        }

        if (extension == ".ppm") {
            framebuffer.write_ppm(
                output_path.string(),
                display_mapping,
                output_transfer);
            std::cout
                << "rendered format=ppm width=" << parsed.settings.width
                << " height=" << parsed.settings.height
                << " samples=" << framebuffer.samples_per_pixel()
                << " source=" << (rendered_scene ? "scene" : "model")
                << " models=" << rendered_models;
            if (rendered_scene) {
                std::cout
                    << " ordering=" << ordering_name(rendered_ordering)
                    << " camera=" << (rendered_explicit_camera ? "explicit" : "auto-fit");
            }
            std::cout
                << " display_fnv1a64=0x" << std::hex
                << framebuffer.fnv1a64(display_mapping, output_transfer)
                << '\n';
        } else {
            framebuffer.write_pfm(output_path.string());
            std::cout
                << "rendered format=pfm width=" << parsed.settings.width
                << " height=" << parsed.settings.height
                << " samples=" << framebuffer.samples_per_pixel()
                << " source=" << (rendered_scene ? "scene" : "model")
                << " models=" << rendered_models;
            if (rendered_scene) {
                std::cout
                    << " ordering=" << ordering_name(rendered_ordering)
                    << " camera=" << (rendered_explicit_camera ? "explicit" : "auto-fit");
            }
            std::cout << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tiny_renderer_render: " << error.what() << '\n';
        return 1;
    }
}