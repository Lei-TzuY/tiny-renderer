#include "tiny_renderer/image_loader.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include "tiny_renderer/ppm_loader.hpp"
#include "tiny_renderer/tga_loader.hpp"

namespace tiny_renderer {
namespace {

std::string lowercase_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](char value) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
        });
    return extension;
}

}  // namespace

Texture2D load_texture_image_file(const std::filesystem::path& path) {
    const std::string extension = lowercase_extension(path);
    if (extension == ".ppm") {
        return load_ppm_file(path);
    }
    if (extension == ".tga") {
        return load_tga_file(path);
    }
    throw std::invalid_argument(
        "unsupported texture image extension '" + extension + "' for file: " + path.string());
}

}  // namespace tiny_renderer
