#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "tiny_renderer/framebuffer.hpp"
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

tiny_renderer::SampleCount parse_sample_count(std::string_view text) {
    if (text == "1") {
        return tiny_renderer::SampleCount::One;
    }
    if (text == "4") {
        return tiny_renderer::SampleCount::Four;
    }
    throw std::invalid_argument("sample count must be 1 or 4");
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

void print_usage() {
    std::cerr
        << "usage: tiny_renderer_render INPUT.obj OUTPUT.(ppm|pfm) [WIDTH HEIGHT [SAMPLES]]\n"
        << "  defaults: WIDTH=512 HEIGHT=512 SAMPLES=4\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 5 && argc != 6) {
            print_usage();
            return 2;
        }

        const std::filesystem::path input_path = argv[1];
        const std::filesystem::path output_path = argv[2];
        tiny_renderer::OfflineRenderSettings settings{};
        if (argc >= 5) {
            settings.width = parse_positive_size(argv[3], "width");
            settings.height = parse_positive_size(argv[4], "height");
        }
        if (argc == 6) {
            settings.sample_count = parse_sample_count(argv[5]);
        }

        const tiny_renderer::ModelAsset asset = tiny_renderer::load_obj_model_asset_file(input_path);
        const tiny_renderer::Framebuffer framebuffer = tiny_renderer::render_model_preview(
            asset,
            settings,
            preview_options(asset));

        const std::string extension = lowercase_extension(output_path);
        if (extension == ".ppm") {
            const tiny_renderer::DisplayMappingState display_mapping{};
            framebuffer.write_ppm(
                output_path.string(),
                display_mapping,
                tiny_renderer::OutputTransferFunction::Srgb);
            std::cout
                << "rendered format=ppm width=" << settings.width
                << " height=" << settings.height
                << " samples=" << framebuffer.samples_per_pixel()
                << " display_fnv1a64=0x" << std::hex
                << framebuffer.fnv1a64(
                    display_mapping,
                    tiny_renderer::OutputTransferFunction::Srgb)
                << '\n';
        } else if (extension == ".pfm") {
            framebuffer.write_pfm(output_path.string());
            std::cout
                << "rendered format=pfm width=" << settings.width
                << " height=" << settings.height
                << " samples=" << framebuffer.samples_per_pixel()
                << '\n';
        } else {
            throw std::invalid_argument("output extension must be .ppm or .pfm");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tiny_renderer_render: " << error.what() << '\n';
        return 1;
    }
}
