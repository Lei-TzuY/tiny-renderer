#pragma once

#include <filesystem>

#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

// Load one bounded texture image selected by its filename extension.
// Supported formats are binary P6 PPM and uncompressed 24-bit true-color TGA.
[[nodiscard]] Texture2D load_texture_image_file(const std::filesystem::path& path);

}  // namespace tiny_renderer
