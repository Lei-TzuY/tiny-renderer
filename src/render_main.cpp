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

tiny_renderer::MipFilterMode parse_environment_mip(std::string_view text) {
    if (text == "base") {
        return tiny_renderer::MipFilterMode::Disabled;
    }
    if (text == "nearest") {
        return tiny_renderer::MipFilterMode::Nearest;
    }
    if (text == "linear") {
        return tiny_renderer::MipFilterMode::Linear;
    }
    throw std::invalid_argument("environment mip mode must be base, nearest, or linear");
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

struct ParsedArguments {
    tiny_renderer::OfflineRenderSettings settings{};
    std::optional<std::filesystem::path> environment_path{};
    std::optional<float> environment_intensity{};
    std::optional<float> environment_yaw{};
    std::optional<tiny_renderer::MipFilterMode> environment_mip{};
};

ParsedArguments parse_arguments(int argc, char** argv) {
    ParsedArguments parsed;
    std::vector<std::string_view> positional;
    bool saw_environment = false;
    bool saw_intensity = false;
    bool saw_yaw = false;
    bool saw_mip = false;

    for (int index = 3; index < argc; ++index) {
        const std::string_view token = argv[index];
        const auto require_value = [&](const char* option) -> std::string_view {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string(option) + " requires a value");
            }
            ++index;
            return argv[index];
        };

        if (token == "--environment") {
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
            parsed.environment_mip = parse_environment_mip(require_value("--environment-mip"));
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
    return parsed;
}

void print_usage() {
    std::cerr
        << "usage: tiny_renderer_render INPUT.obj OUTPUT.(ppm|pfm) [WIDTH HEIGHT [SAMPLES]]"
           " [--environment IMAGE] [--environment-intensity VALUE] [--environment-yaw RADIANS]"
           " [--environment-mip base|nearest|linear]\n"
        << "  defaults: WIDTH=512 HEIGHT=512 SAMPLES=4 environment-intensity=1"
           " environment-yaw=0 environment-mip=base\n";
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
        const std::string extension = lowercase_extension(output_path);
        if (extension != ".ppm" && extension != ".pfm") {
            throw std::invalid_argument("output extension must be .ppm or .pfm");
        }

        ParsedArguments parsed = parse_arguments(argc, argv);
        std::optional<tiny_renderer::Texture2D> environment_texture;
        if (parsed.environment_path) {
            environment_texture.emplace(tiny_renderer::load_texture_image_file(
                *parsed.environment_path,
                tiny_renderer::TextureTransferFunction::Linear));
            tiny_renderer::EnvironmentBackgroundState environment;
            environment.texture = &*environment_texture;
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

        const tiny_renderer::ModelAsset asset = tiny_renderer::load_obj_model_asset_file(input_path);
        const tiny_renderer::Framebuffer framebuffer = tiny_renderer::render_model_preview(
            asset,
            parsed.settings,
            preview_options(asset));

        if (extension == ".ppm") {
            const tiny_renderer::DisplayMappingState display_mapping{};
            framebuffer.write_ppm(
                output_path.string(),
                display_mapping,
                tiny_renderer::OutputTransferFunction::Srgb);
            std::cout
                << "rendered format=ppm width=" << parsed.settings.width
                << " height=" << parsed.settings.height
                << " samples=" << framebuffer.samples_per_pixel()
                << " display_fnv1a64=0x" << std::hex
                << framebuffer.fnv1a64(
                    display_mapping,
                    tiny_renderer::OutputTransferFunction::Srgb)
                << '\n';
        } else {
            framebuffer.write_pfm(output_path.string());
            std::cout
                << "rendered format=pfm width=" << parsed.settings.width
                << " height=" << parsed.settings.height
                << " samples=" << framebuffer.samples_per_pixel()
                << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tiny_renderer_render: " << error.what() << '\n';
        return 1;
    }
}
