#pragma once

#include <filesystem>
#include <istream>
#include <stdexcept>
#include <string>

#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

class PpmParseError : public std::runtime_error {
public:
    explicit PpmParseError(const std::string& message) : std::runtime_error(message) {}
};

[[nodiscard]] Texture2D load_ppm(
    std::istream& input,
    TextureTransferFunction transfer_function = TextureTransferFunction::Linear);
[[nodiscard]] Texture2D load_ppm_file(
    const std::filesystem::path& path,
    TextureTransferFunction transfer_function = TextureTransferFunction::Linear);

}  // namespace tiny_renderer
